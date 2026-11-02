#include "CPPGraphEntryEmitter.hpp"
#include "CPPVisitor.hpp"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/StmtCXX.h>

namespace UGLC::CodeGen::CPP
{
    CPPGraphEntryEmitter::CPPGraphEntryEmitter(CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    bool CPPGraphEntryEmitter::isRenderRecord(const clang::CXXRecordDecl &record) const
    {
        if (record.getQualifiedNameAsString() == "UGL::IRenderNode")
        {
            return true;
        }
        if (const auto *definition = record.getDefinition())
        {
            for (const auto &base : definition->bases())
            {
                if (const auto *parent = base.getType()->getAsCXXRecordDecl(); parent != nullptr && isRenderRecord(*parent))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool CPPGraphEntryEmitter::isRenderMethod(const clang::FunctionDecl &function) const
    {
        const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        return method != nullptr && !method->isStatic() && !method->isImplicit() &&
            !llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(method) && isRenderRecord(*method->getParent());
    }

    void CPPGraphEntryEmitter::validateTarget(const clang::FunctionDecl &function)
    {
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
            method != nullptr && method->getParent()->getQualifiedNameAsString() == "UGL::ReadbackNode")
        {
            validateValue(function.getReturnType(), function.getLocation());
            return;
        }
        const bool previous = mRenderValidation;
        mRenderValidation = isRenderMethod(function);
        try
        {
            if (mRenderValidation && !llvm::cast<clang::CXXMethodDecl>(function).isConst())
            {
                mVisitor.throwCodegenError(&function, "RenderNode build must use const configuration and invocation-local effects.");
            }
            validateFunction(function, false);
        }
        catch (...)
        {
            mRenderValidation = previous;
            throw;
        }
        mRenderValidation = previous;
    }

    std::optional<std::string> CPPGraphEntryEmitter::emitRenderMethod(const clang::FunctionDecl &function)
    {
        if (!isRenderMethod(function) || mVisitor.isFromExcludedFile(function.getLocation()))
        {
            return std::nullopt;
        }
        validateTarget(function);
        const auto *previous = mRenderMethod;
        mRenderMethod = &function;
        try
        {
            auto signature = mVisitor.generateFunctionSignature(&function, function.getNameAsString());
            const auto position = signature.find(function.getNameAsString() + "(");
            if (position == std::string::npos)
            {
                mVisitor.throwCodegenError(&function, "Cannot lower the RenderNode method signature.");
            }
            signature.insert(position + function.getNameAsString().size() + 1,
                "GVM::Core::RenderNode &__gvm_node" + std::string(function.getNumParams() == 0 ? "" : ", "));
            auto result = signature + mVisitor.generateFunctionBody(&function);
            mRenderMethod = previous;
            return result;
        }
        catch (...)
        {
            mRenderMethod = previous;
            throw;
        }
    }

    std::optional<std::string> CPPGraphEntryEmitter::translateRenderCall(const clang::CXXMemberCallExpr &call)
    {
        const auto *method = call.getMethodDecl();
        if (method == nullptr || !isRenderRecord(*method->getParent()))
        {
            return std::nullopt;
        }
        if (mRenderMethod == nullptr)
        {
            mVisitor.throwCodegenError(&call, "RenderNode methods can only execute inside graph.add invocations.");
        }
        const bool intrinsic = method->getParent()->getQualifiedNameAsString() == "UGL::IRenderNode";
        const auto name = method->getNameAsString();
        const auto *templates = method->getTemplateSpecializationArgs();
        if (intrinsic && name == "createBuffer")
        {
            const auto *buffer = templates != nullptr && templates->size() != 0
                ? llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>((*templates)[0].getAsType()->getAsCXXRecordDecl()) : nullptr;
            if (buffer == nullptr || buffer->getQualifiedNameAsString() != "UGL::Buffer")
            {
                mVisitor.throwCodegenError(&call, "RenderNode createBuffer requires an existing Buffer<T, BufferUsage<...>> type.");
            }
            const auto element = buffer->getTemplateArgs()[0].getAsType();
            validateValue(element, call.getExprLoc());
            const auto *usage = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(buffer->getTemplateArgs()[1].getAsType()->getAsCXXRecordDecl());
            if (usage == nullptr || usage->getQualifiedNameAsString() != "UGL::BufferUsage")
            {
                mVisitor.throwCodegenError(&call, "RenderNode createBuffer requires BufferUsage<...> usage declarations.");
            }
            std::string flags;
            for (const auto &argument : usage->getTemplateArgs()[0].pack_elements())
            {
                if (!flags.empty())
                {
                    flags += " | ";
                }
                flags += convertBufferUsage(argument.getAsType()->getAsCXXRecordDecl()->getQualifiedNameAsString());
            }
            return "__gvm_node.createBuffer({.label = " + mVisitor.TranslateExpr(call.getArg(0)) +
                ", .usage = " + (flags.empty() ? "0" : flags) + ", .size = uint64_t(" +
                mVisitor.TranslateExpr(call.getArg(1)) + ") * sizeof(" + mVisitor.generateTypeCanonicalName(element) + ")})";
        }
        const auto *object = call.getImplicitObjectArgument()->IgnoreParenImpCasts();
        std::string result = intrinsic ? "__gvm_node." + name :
            llvm::isa<clang::CXXThisExpr>(object) ? "this->" + name : mVisitor.TranslateExpr(object) + "." + name;
        if (intrinsic && name == "dispatchThreads")
        {
            result += "<" + mVisitor.generateTypeCanonicalName((*templates)[0].getAsType()) + ">";
        }
        result += "(";
        if (!intrinsic)
        {
            result += "__gvm_node";
        }
        for (unsigned index = 0; index < call.getNumArgs(); ++index)
        {
            if (index != 0 || !intrinsic)
            {
                result += ", ";
            }
            result += mVisitor.TranslateExpr(call.getArg(index));
        }
        return result + ")";
    }

    bool CPPGraphEntryEmitter::isTask(clang::QualType type) const
    {
        const auto *record = type.getNonReferenceType()->getAsCXXRecordDecl();
        return record != nullptr && record->getQualifiedNameAsString() == "UGL::Task";
    }

    bool CPPGraphEntryEmitter::isOwningContainer(const clang::CXXRecordDecl &record) const
    {
        const auto name = record.getQualifiedNameAsString();
        return name == "eastl::vector" || name == "eastl::array" || name == "eastl::basic_string";
    }

    void CPPGraphEntryEmitter::validateValue(clang::QualType type, clang::SourceLocation location, bool graphConstruction)
    {
        if (type->isPointerType() || type->isReferenceType() || type->isMemberPointerType() || type->isBlockPointerType() || type.isVolatileQualified())
        {
            mVisitor.throwCodegenError(location, "Task graph values cannot contain pointers, references, or volatile storage.");
        }
        type = type.getCanonicalType().getUnqualifiedType();
        if (type->isBuiltinType() || type->isEnumeralType())
        {
            return;
        }
        if (const auto *array = mVisitor.Context->getAsConstantArrayType(type))
        {
            validateValue(array->getElementType(), location, graphConstruction);
            return;
        }
        const auto *record = type->getAsCXXRecordDecl();
        if (record == nullptr)
        {
            mVisitor.throwCodegenError(location, "Task graph value requires a complete owned scalar, record, or approved container.");
        }
        const auto name = record->getQualifiedNameAsString();
        if ((graphConstruction || mRenderValidation) && name == "UGL::IRenderNode")
        {
            return;
        }
        if (graphConstruction && name == "UGL::ReadbackNode")
        {
            return;
        }
        if (mRenderValidation && (name == "UGL::Buffer" || name == "UGL::BufferRange"))
        {
            const auto *specialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(record);
            const auto element = specialization->getTemplateArgs()[0].getAsType();
            mRenderValidation = false;
            try
            {
                validateValue(element, location);
                if (!element.isTriviallyCopyableType(*mVisitor.Context))
                {
                    mVisitor.throwCodegenError(location, "Graph buffer elements must be trivially copyable owned CPU values.");
                }
            }
            catch (...)
            {
                mRenderValidation = true;
                throw;
            }
            mRenderValidation = true;
            return;
        }
        if (mRenderValidation && (name == "UGL::ComputePassTaskDescriptor" || name == "UGL::RenderPassTaskDescriptor"))
        {
            return;
        }
        if (graphConstruction && (isTask(type) || name == "UGL::TaskGraph"))
        {
            return;
        }
        if (name.starts_with("UGL::") && name != "UGL::Vector" && name != "UGL::Matrix")
        {
            mVisitor.throwCodegenError(location, "CPU graph tasks cannot contain GPU resources or graph runtime objects: " + name + ".");
        }
        auto &values = mRenderValidation ? mValidatedRenderValues : graphConstruction ? mValidatedGraphValues : mValidatedValues;
        if (!values.insert(record->getCanonicalDecl()).second)
        {
            return;
        }
        if (isOwningContainer(*record) || name == "UGL::Vector" || name == "UGL::Matrix")
        {
            const auto *specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
            if (specialization == nullptr || specialization->getTemplateArgs().size() == 0 ||
                specialization->getTemplateArgs()[0].getKind() != clang::TemplateArgument::Type)
            {
                mVisitor.throwCodegenError(location, "Task container requires a concrete owned element type.");
            }
            validateValue(specialization->getTemplateArgs()[0].getAsType(), location);
            if (isOwningContainer(*record) && name != "eastl::array")
            {
                const auto &arguments = specialization->getTemplateArgs();
                const auto *allocator = arguments[arguments.size() - 1].getAsType()->getAsCXXRecordDecl();
                if (allocator == nullptr || allocator->getQualifiedNameAsString() != "eastl::allocator")
                {
                    mVisitor.throwCodegenError(location, "CPU graph containers require the owning default EASTL allocator.");
                }
            }
            return;
        }
        if (mVisitor.isFromExcludedFile(record->getLocation()) || !record->hasDefinition())
        {
            mVisitor.throwCodegenError(location, "CPU graph value has no registered ownership contract: " + name + ".");
        }
        record = record->getDefinition();
        for (const auto &base : record->bases())
        {
            validateValue(base.getType(), location, graphConstruction);
        }
        for (const auto *field : record->fields())
        {
            validateValue(field->getType(), field->getLocation(), graphConstruction);
            if (const auto *initializer = field->getInClassInitializer())
            {
                validateStatement(*initializer, false);
            }
        }
        if (const auto *destructor = record->getDestructor(); destructor != nullptr && !destructor->isDefaulted() && !destructor->isImplicit())
        {
            validateFunction(*destructor, false);
        }
    }

    const clang::FunctionDecl &CPPGraphEntryEmitter::resolveCallable(const clang::Expr &expression)
    {
        const auto *value = expression.IgnoreParenImpCasts();
        if (const auto *reference = llvm::dyn_cast<clang::DeclRefExpr>(value))
        {
            if (const auto *function = llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl()))
            {
                return *function;
            }
        }
        const auto *record = value->getType()->getAsCXXRecordDecl();
        if (record == nullptr)
        {
            mVisitor.throwCodegenError(value, "Graph callable must be a statically named function or an ordinary class value, not a function pointer.");
        }
        const bool render = isRenderRecord(*record);
        validateValue(value->getType(), value->getExprLoc(), render);
        const clang::CXXMethodDecl *selected = nullptr;
        for (const auto *method : record->methods())
        {
            if (render ? method->getNameAsString() == "build" : method->getOverloadedOperator() == clang::OO_Call)
            {
                if (selected != nullptr)
                {
                    mVisitor.throwCodegenError(value, "Graph callable requires one unambiguous concrete call operator.");
                }
                selected = method;
            }
        }
        if (selected == nullptr)
        {
            mVisitor.throwCodegenError(value, "Graph callable requires a concrete class call operator.");
        }
        return *selected;
    }

    void CPPGraphEntryEmitter::validateFunction(const clang::FunctionDecl &function, bool graphConstruction)
    {
        auto &visited = mRenderValidation ? mValidatedRenderFunctions : graphConstruction ? mValidatedBuilders : mValidatedFunctions;
        if (!visited.insert(function.getCanonicalDecl()).second)
        {
            return;
        }
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function))
        {
            if (method->isVirtual())
            {
                mVisitor.throwCodegenError(&function, "CPU graph effects cannot be inferred through virtual dispatch.");
            }
        }
        validateValue(function.getReturnType(), function.getLocation(), graphConstruction);
        for (const auto *parameter : function.parameters())
        {
            validateValue(parameter->getType(), parameter->getLocation(), graphConstruction);
        }
        const auto *body = function.getBody();
        if (body == nullptr || mVisitor.isFromExcludedFile(function.getLocation()))
        {
            mVisitor.throwCodegenError(&function, "CPU graph callable has no complete source effect contract: " + function.getQualifiedNameAsString() + ".");
        }
        if (const auto *constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(&function))
        {
            for (const auto *initializer : constructor->inits())
            {
                validateStatement(*initializer->getInit(), graphConstruction);
            }
        }
        validateStatement(*body, graphConstruction);
    }

    bool CPPGraphEntryEmitter::hasMutableSubobject(clang::QualType type) const
    {
        if (const auto *array = mVisitor.Context->getAsArrayType(type))
        {
            return hasMutableSubobject(array->getElementType());
        }
        const auto *record = type->getAsCXXRecordDecl();
        if (record == nullptr || !record->hasDefinition())
        {
            return false;
        }
        if (isOwningContainer(*record))
        {
            const auto *specialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(record);
            return hasMutableSubobject(specialization->getTemplateArgs()[0].getAsType());
        }
        record = record->getDefinition();
        for (const auto &base : record->bases())
        {
            if (hasMutableSubobject(base.getType()))
            {
                return true;
            }
        }
        for (const auto *field : record->fields())
        {
            if (field->isMutable() || hasMutableSubobject(field->getType()))
            {
                return true;
            }
        }
        return false;
    }

    void CPPGraphEntryEmitter::validateGlobal(const clang::VarDecl &variable, clang::SourceLocation location)
    {
        if (!variable.isConstexpr())
        {
            mVisitor.throwCodegenError(location, "CPU graph tasks require owned inputs instead of shared global or static storage.");
        }
        validateValue(variable.getType(), location);
        if (hasMutableSubobject(variable.getType()))
        {
            mVisitor.throwCodegenError(location, "CPU graph constants cannot contain mutable subobjects.");
        }
    }

    void CPPGraphEntryEmitter::validateStatement(const clang::Stmt &statement, bool graphConstruction)
    {
        if (const auto *loop = llvm::dyn_cast<clang::CXXForRangeStmt>(&statement))
        {
            validateValue(loop->getLoopVariable()->getType(), loop->getLoopVariable()->getLocation(), graphConstruction);
            if (const auto *initializer = loop->getInit())
            {
                validateStatement(*initializer, graphConstruction);
            }
            validateStatement(*loop->getRangeInit(), graphConstruction);
            validateStatement(*loop->getBody(), graphConstruction);
            return;
        }
        if (const auto *declarations = llvm::dyn_cast<clang::DeclStmt>(&statement))
        {
            for (const auto *declaration : declarations->decls())
            {
                if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(declaration))
                {
                    validateValue(variable->getType(), variable->getLocation(), graphConstruction);
                    if (variable->isStaticLocal() && !variable->isConstexpr())
                    {
                        mVisitor.throwCodegenError(variable, "CPU graph tasks cannot access mutable static storage.");
                    }
                }
                if (const auto *alias = llvm::dyn_cast<clang::TypedefNameDecl>(declaration))
                {
                    validateValue(alias->getUnderlyingType(), alias->getLocation(), graphConstruction);
                }
            }
        }
        if (const auto *reference = llvm::dyn_cast<clang::DeclRefExpr>(&statement))
        {
            if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(reference->getDecl()); variable != nullptr && variable->hasGlobalStorage())
            {
                validateGlobal(*variable, reference->getExprLoc());
            }
        }
        if (const auto *member = llvm::dyn_cast<clang::MemberExpr>(&statement))
        {
            if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(member->getMemberDecl()); variable != nullptr && variable->hasGlobalStorage())
            {
                validateGlobal(*variable, member->getExprLoc());
            }
        }
        if (const auto *unary = llvm::dyn_cast<clang::UnaryOperator>(&statement))
        {
            if (unary->getOpcode() == clang::UO_AddrOf || unary->getOpcode() == clang::UO_Deref)
            {
                mVisitor.throwCodegenError(unary, "Task graph DSL does not support address-of or pointer dereference.");
            }
        }
        if (const auto *cast = llvm::dyn_cast<clang::ExplicitCastExpr>(&statement))
        {
            validateValue(cast->getTypeAsWritten(), cast->getExprLoc(), graphConstruction);
        }
        if (llvm::isa<clang::CXXNewExpr, clang::CXXDeleteExpr, clang::CXXNullPtrLiteralExpr>(&statement))
        {
            mVisitor.throwCodegenError(&statement, "Task graph DSL does not support explicit pointer allocation or pointer flow.");
        }
        if (const auto *handler = llvm::dyn_cast<clang::CXXCatchStmt>(&statement); handler != nullptr && handler->getExceptionDecl() != nullptr)
        {
            validateValue(handler->getCaughtType(), handler->getCatchLoc(), graphConstruction);
        }
        if (llvm::isa<clang::AsmStmt>(&statement))
        {
            mVisitor.throwCodegenError(&statement, "CPU graph effects cannot be inferred from inline assembly.");
        }
        if (const auto *self = llvm::dyn_cast<clang::CXXThisExpr>(&statement); self != nullptr && !self->isImplicit())
        {
            mVisitor.throwCodegenError(self, "Task graph DSL cannot expose the current object's pointer.");
        }
        if (const auto *call = llvm::dyn_cast<clang::CallExpr>(&statement))
        {
            if (call->getType()->isPointerType() || call->getType()->isMemberPointerType())
            {
                mVisitor.throwCodegenError(call, "Task graph DSL does not support pointer-valued calls.");
            }
            const auto *callee = call->getDirectCallee();
            if (callee == nullptr)
            {
                mVisitor.throwCodegenError(call, "CPU graph effects require statically resolved calls.");
            }
            const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
            const bool graphAdd = method != nullptr && method->getParent()->getQualifiedNameAsString() == "UGL::TaskGraph" && method->getNameAsString() == "add";
            const bool container = method != nullptr && isOwningContainer(*method->getParent());
            if (graphAdd && graphConstruction)
            {
                const auto &target = resolveCallable(*call->getArg(0));
                validateTarget(target);
            }
            else if (!container)
            {
                const bool intrinsic = method != nullptr &&
                    (method->getParent()->getQualifiedNameAsString() == "UGL::IRenderNode" ||
                     method->getParent()->getQualifiedNameAsString() == "UGL::BufferRange");
                if (!(mRenderValidation && intrinsic))
                {
                    validateFunction(*callee, graphConstruction);
                }
            }
        }
        if (const auto *construction = llvm::dyn_cast<clang::CXXConstructExpr>(&statement))
        {
            validateValue(construction->getType(), construction->getExprLoc(), graphConstruction);
            const auto *constructor = construction->getConstructor();
            const bool graph = constructor->getParent()->getQualifiedNameAsString() == "UGL::TaskGraph" || isTask(construction->getType());
            const bool bufferRange = mRenderValidation && constructor->getParent()->getQualifiedNameAsString() == "UGL::BufferRange";
            if (!constructor->isDefaulted() && !constructor->isImplicit() && !isOwningContainer(*constructor->getParent()) && !(graphConstruction && graph) && !bufferRange)
            {
                validateFunction(*constructor, graphConstruction);
            }
        }
        for (const auto *child : statement.children())
        {
            if (child != nullptr)
            {
                validateStatement(*child, graphConstruction);
            }
        }
    }

    std::string CPPGraphEntryEmitter::callableExpression(const clang::Expr &expression, const clang::FunctionDecl &function)
    {
        if (llvm::isa<clang::CXXMethodDecl>(&function))
        {
            return mVisitor.TranslateExpr(&expression);
        }
        return "/* Invokes a statically resolved DSL declaration. */ [] (auto... arguments)\n{\n    return ::" + function.getQualifiedNameAsString() + "(arguments...);\n}";
    }

    std::optional<std::string> CPPGraphEntryEmitter::translateCall(const clang::CXXMemberCallExpr &call)
    {
        if (const auto renderCall = translateRenderCall(call))
        {
            return renderCall;
        }
        const auto *method = call.getMethodDecl();
        const auto owner = method->getParent()->getQualifiedNameAsString();
        const bool add = owner == "UGL::TaskGraph" && method->getNameAsString() == "add";
        const bool submit = owner == "UGL::TaskRuntime" && method->getNameAsString() == "submit";
        if (!add && !submit)
        {
            return std::nullopt;
        }
        const auto &target = resolveCallable(*call.getArg(0));
        if (submit && !isTask(target.getReturnType()))
        {
            mVisitor.throwCodegenError(&call, "TaskRuntime entry must return a deferred Task value.");
        }
        if (submit)
        {
            validateResult(target);
            validateFunction(target, true);
        }
        else
        {
            validateTarget(target);
        }
        for (unsigned index = 1; index < call.getNumArgs(); ++index)
        {
            validateValue(call.getArg(index)->getType(), call.getArg(index)->getExprLoc(), add && isTask(call.getArg(index)->getType()));
        }
        std::string result = mVisitor.TranslateExpr(call.getImplicitObjectArgument()) + (add ? ".add(" : ".submit(");
        result += callableExpression(*call.getArg(0), target);
        for (unsigned index = 1; index < call.getNumArgs(); ++index)
        {
            result += ", " + mVisitor.TranslateExpr(call.getArg(index));
        }
        return result + ")";
    }

    void CPPGraphEntryEmitter::validateResult(const clang::FunctionDecl &function)
    {
        const auto *task = llvm::cast<clang::ClassTemplateSpecializationDecl>(function.getReturnType()->getAsCXXRecordDecl());
        validateValue(task->getTemplateArgs()[0].getAsType(), function.getLocation());
    }

    std::string CPPGraphEntryEmitter::emitEntry(const clang::FunctionDecl &function)
    {
        if (!function.doesThisDeclarationHaveABody() || !isTask(function.getReturnType()) ||
            !mVisitor.checkAttibuteByName(&function, mUGLAttributeExportName))
        {
            return {};
        }
        if (function.getNumParams() != 1 || function.isTemplated())
        {
            mVisitor.throwCodegenError(&function, "Exported graph entry requires one concrete owned input parameter.");
        }
        validateResult(function);
        validateFunction(function, true);
        return "\nnamespace Entries\n{\n    /// Invokes the statically exported graph with an owned input snapshot.\n    inline constexpr auto " +
            function.getNameAsString() + " = [](auto input)\n    {\n        return ::" + function.getQualifiedNameAsString() + "(input);\n    };\n}\n";
    }
}
