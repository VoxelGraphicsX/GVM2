#include "CPPGraphEntryEmitter.hpp"
#include "CPPVisitor.hpp"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Lex/Lexer.h>

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

    bool CPPGraphEntryEmitter::isRenderNodeType(clang::QualType type) const
    {
        clang::QualType base = type;
        while (base->isPointerType() || base->isReferenceType())
        {
            base = base->isPointerType() ? base->getPointeeType() : base.getNonReferenceType();
        }
        base = base.getCanonicalType().getUnqualifiedType();
        const auto *record = base->getAsCXXRecordDecl();
        return record != nullptr && record->getQualifiedNameAsString() == "UGL::RenderNode";
    }

    bool CPPGraphEntryEmitter::isTaskGraphType(clang::QualType type) const
    {
        clang::QualType base = type;
        while (base->isPointerType() || base->isReferenceType())
        {
            base = base->isPointerType() ? base->getPointeeType() : base.getNonReferenceType();
        }
        base = base.getCanonicalType().getUnqualifiedType();
        const auto *record = base->getAsCXXRecordDecl();
        return record != nullptr && record->getQualifiedNameAsString() == "UGL::TaskGraph";
    }

    std::optional<std::string> CPPGraphEntryEmitter::validateExplicitRenderNodeParam(
        const clang::FunctionDecl &function,
        bool allowExplicitRenderNode) const
    {
        std::optional<std::string> explicitParamName;
        for (unsigned index = 0; index < function.getNumParams(); ++index)
        {
            const auto *parameter = function.getParamDecl(index);
            const auto paramType = parameter->getType();
            if (!isRenderNodeType(paramType))
            {
                continue;
            }
            if (!allowExplicitRenderNode)
            {
                mVisitor.throwCodegenError(parameter, "UGL::RenderNode can only be passed by mutable reference to RenderNode methods.");
            }
            if (!paramType->isLValueReferenceType() ||
                paramType.getNonReferenceType().isConstQualified() ||
                paramType.getNonReferenceType().isVolatileQualified())
            {
                mVisitor.throwCodegenError(parameter, "Explicit RenderNode parameter must have non-const reference type RenderNode &.");
            }
            if (function.getNameAsString() == "build" && index != 0)
            {
                mVisitor.throwCodegenError(parameter, "RenderNode build explicit RenderNode & parameter must be the first parameter.");
            }
            if (explicitParamName.has_value())
            {
                mVisitor.throwCodegenError(parameter, "RenderNode methods can accept at most one explicit RenderNode & parameter.");
            }
            if (parameter->getNameAsString().empty())
            {
                mVisitor.throwCodegenError(parameter, "Explicit RenderNode & parameter must be named.");
            }
            explicitParamName = parameter->getNameAsString();
        }
        return explicitParamName;
    }

    std::optional<std::string> CPPGraphEntryEmitter::validateExplicitTaskGraphParam(
        const clang::FunctionDecl &function,
        bool allowExplicitTaskGraph) const
    {
        std::optional<std::string> explicitParamName;
        for (unsigned index = 0; index < function.getNumParams(); ++index)
        {
            const auto *parameter = function.getParamDecl(index);
            const auto paramType = parameter->getType();
            if (!isTaskGraphType(paramType))
            {
                continue;
            }
            if (!allowExplicitTaskGraph)
            {
                mVisitor.throwCodegenError(
                    parameter,
                    "UGL::TaskGraph can only be passed by mutable reference as the first parameter of a subgraph builder.");
            }
            if (!paramType->isLValueReferenceType() ||
                paramType.getNonReferenceType().isConstQualified() ||
                paramType.getNonReferenceType().isVolatileQualified())
            {
                mVisitor.throwCodegenError(parameter, "Explicit TaskGraph parameter must have non-const reference type TaskGraph &.");
            }
            if (index != 0)
            {
                mVisitor.throwCodegenError(parameter, "Subgraph builder explicit TaskGraph & parameter must be the first parameter.");
            }
            if (explicitParamName.has_value())
            {
                mVisitor.throwCodegenError(parameter, "Subgraph builders can accept at most one explicit TaskGraph & parameter.");
            }
            if (parameter->getNameAsString().empty())
            {
                mVisitor.throwCodegenError(parameter, "Explicit TaskGraph & parameter must be named.");
            }
            explicitParamName = parameter->getNameAsString();
        }
        return explicitParamName;
    }

    bool CPPGraphEntryEmitter::isSubgraphMethod(const clang::CXXMethodDecl &method) const
    {
        return !isRenderMethod(method) && isTask(method.getReturnType());
    }

    void CPPGraphEntryEmitter::validateTarget(const clang::FunctionDecl &function)
    {
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
            method != nullptr &&
            (method->getParent()->getQualifiedNameAsString() == "UGL::ReadbackNode" ||
             method->getParent()->getQualifiedNameAsString() == "UGL::ReadbackTextureNode"))
        {
            validateValue(function.getReturnType(), function.getLocation());
            return;
        }
        if (!isRenderMethod(function) && isTask(function.getReturnType()))
        {
            const bool previous = mRenderValidation;
            mRenderValidation = false;
            try
            {
                validateResult(function);
                validateFunction(function, true);
            }
            catch (...)
            {
                mRenderValidation = previous;
                throw;
            }
            mRenderValidation = previous;
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
        const auto explicitNodeParam = validateExplicitRenderNodeParam(function, true);
        const auto *previous = mRenderMethod;
        const auto previousNodeVar = mRenderNodeVarName;
        mRenderMethod = &function;
        mRenderNodeVarName = explicitNodeParam.value_or("__gvm_node");
        try
        {
            auto signature = mVisitor.generateFunctionSignature(&function, function.getNameAsString());
            if (!explicitNodeParam.has_value())
            {
                const auto position = signature.find(function.getNameAsString() + "(");
                if (position == std::string::npos)
                {
                    mVisitor.throwCodegenError(&function, "Cannot lower the RenderNode method signature.");
                }
                signature.insert(position + function.getNameAsString().size() + 1,
                    "GVM::Core::RenderNode &__gvm_node" + std::string(function.getNumParams() == 0 ? "" : ", "));
            }
            auto result = signature + mVisitor.generateFunctionBody(&function);
            mRenderMethod = previous;
            mRenderNodeVarName = previousNodeVar;
            return result;
        }
        catch (...)
        {
            mRenderMethod = previous;
            mRenderNodeVarName = previousNodeVar;
            throw;
        }
    }

    std::optional<std::string> CPPGraphEntryEmitter::translateRenderCall(const clang::CXXMemberCallExpr &call)
    {
        const auto *method = call.getMethodDecl();
        if (method == nullptr)
        {
            return std::nullopt;
        }
        const auto parentName = method->getParent()->getQualifiedNameAsString();
        const bool isExplicitNode = parentName == "UGL::RenderNode";
        if (!isExplicitNode && !isRenderRecord(*method->getParent()))
        {
            return std::nullopt;
        }
        if (mRenderMethod == nullptr)
        {
            mVisitor.throwCodegenError(&call, "RenderNode methods can only execute inside graph.add invocations.");
        }
        const bool intrinsic = isExplicitNode || parentName == "UGL::IRenderNode";
        const auto *object = call.getImplicitObjectArgument()->IgnoreParenImpCasts();
        const std::string receiver = isExplicitNode ? mVisitor.TranslateExpr(object) : mRenderNodeVarName;
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
            return receiver + ".createBuffer({.label = " + mVisitor.TranslateExpr(call.getArg(0)) +
                ", .usage = " + (flags.empty() ? "0" : flags) + ", .size = uint64_t(" +
                mVisitor.TranslateExpr(call.getArg(1)) + ") * sizeof(" + mVisitor.generateTypeCanonicalName(element) + ")})";
        }
        if (intrinsic && name == "createTexture")
        {
            const auto *texture = templates != nullptr && templates->size() != 0
                ? llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>((*templates)[0].getAsType()->getAsCXXRecordDecl()) : nullptr;
            if (texture == nullptr || texture->getQualifiedNameAsString() != "UGL::Texture")
            {
                mVisitor.throwCodegenError(&call, "RenderNode createTexture requires an existing Texture<Format, TextureUsage<...>, Dimension> type.");
            }
            const auto &arguments = texture->getTemplateArgs();
            const auto *usage = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(arguments[1].getAsType()->getAsCXXRecordDecl());
            const auto *dimensionDecl = arguments[2].getAsType()->getAsCXXRecordDecl();
            const std::string dimensionName = dimensionDecl != nullptr ? dimensionDecl->getQualifiedNameAsString() : "";
            if (usage == nullptr || usage->getQualifiedNameAsString() != "UGL::TextureUsage")
            {
                mVisitor.throwCodegenError(&call, "RenderNode createTexture requires TextureUsage<...> and a 1D, 2D, or 3D texture dimension.");
            }
            std::string flags;
            bool usesPixelLocalAttachment = false;
            bool usesPersistentTextureAccess = false;
            for (const auto &argument : usage->getTemplateArgs()[0].pack_elements())
            {
                const std::string usageName = argument.getAsType()->getAsCXXRecordDecl()->getQualifiedNameAsString();
                if (usageName == "UGL::PixelLocalAttachment")
                {
                    usesPixelLocalAttachment = true;
                }
                else if (usageName == "UGL::TextureBinding" || usageName == "UGL::StorageBinding" ||
                         usageName == "UGL::CopySrc" || usageName == "UGL::CopyDst")
                {
                    usesPersistentTextureAccess = true;
                }
                if (!flags.empty())
                {
                    flags += " | ";
                }
                flags += convertTextureUsage(usageName);
            }
            const auto extent = "(" + mVisitor.TranslateExpr(call.getArg(1)) + ")";
            std::string dimensionExpr;
            std::string sizeExpr;
            if (dimensionName == "UGL::TextureDimension::e1D")
            {
                dimensionExpr = "GVM::RHI::TextureDimension::e1D";
                sizeExpr = "{uint32_t(" + extent + "), 1u, 1u}";
            }
            else if (dimensionName == "UGL::TextureDimension::e2D")
            {
                dimensionExpr = "GVM::RHI::TextureDimension::e2D";
                sizeExpr = "GVM::Core::Private::makeTextureExtent2D" + extent;
            }
            else if (dimensionName == "UGL::TextureDimension::e3D")
            {
                dimensionExpr = "GVM::RHI::TextureDimension::e3D";
                sizeExpr = "GVM::Core::Private::makeTextureExtent3D" + extent;
            }
            else
            {
                mVisitor.throwCodegenError(&call, "RenderNode createTexture requires TextureUsage<...> and a 1D, 2D, or 3D texture dimension.");
            }
            std::string storageModeField;
            if (usesPixelLocalAttachment && !usesPersistentTextureAccess)
            {
                storageModeField = ", .storageMode = GVM::RHI::TextureStorageMode::TransientAttachment";
            }
            return receiver + ".createTexture({.label = " + mVisitor.TranslateExpr(call.getArg(0)) +
                ", .usage = " + (flags.empty() ? "0" : flags) + ", .dimension = " + dimensionExpr +
                ", .size = " + sizeExpr + ", .format = " + convertTextureFormat(arguments[0].getAsType()->getAsCXXRecordDecl()->getQualifiedNameAsString()) +
                storageModeField + "})";
        }
        std::string result = intrinsic ? receiver + "." + name :
            llvm::isa<clang::CXXThisExpr>(object) ? "this->" + name : mVisitor.TranslateExpr(object) + "." + name;
        if (intrinsic && (name == "dispatchThreads" || name == "draw"))
        {
            result += "<" + mVisitor.generateTypeCanonicalName((*templates)[0].getAsType()) + ">";
        }
        result += "(";
        const bool prependNodeArg = !intrinsic && !validateExplicitRenderNodeParam(*method, true).has_value();
        bool hasArgument = false;
        if (prependNodeArg)
        {
            result += mRenderNodeVarName;
            hasArgument = true;
        }
        for (unsigned index = 0; index < call.getNumArgs(); ++index)
        {
            if (llvm::isa<clang::CXXDefaultArgExpr>(call.getArg(index)))
            {
                continue;
            }
            if (hasArgument)
            {
                result += ", ";
            }
            result += mVisitor.TranslateExpr(call.getArg(index));
            hasArgument = true;
        }
        return result + ")";
    }

    bool CPPGraphEntryEmitter::isTask(clang::QualType type) const
    {
        const auto *record = type.getNonReferenceType()->getAsCXXRecordDecl();
        return record != nullptr && record->getQualifiedNameAsString() == "UGL::Task";
    }

    bool CPPGraphEntryEmitter::isTaskVector(clang::QualType type) const
    {
        const auto *record = type.getNonReferenceType()->getAsCXXRecordDecl();
        const auto *specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(record);
        return specialization != nullptr && specialization->getQualifiedNameAsString() == "eastl::vector" &&
            specialization->getTemplateArgs().size() != 0 &&
            specialization->getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type &&
            isTask(specialization->getTemplateArgs()[0].getAsType());
    }

    std::optional<std::string> CPPGraphEntryEmitter::translateProjection(const clang::MemberExpr &member)
    {
        const auto *base = member.getBase()->IgnoreParenImpCasts();
        if (!isTask(base->getType()) || !llvm::isa<clang::FieldDecl>(member.getMemberDecl()))
        {
            return std::nullopt;
        }
        return "GVM::Core::Private::projectTask(" + mVisitor.TranslateExpr(base) +
            ", [](const auto &value)\n{\n    return value." + member.getMemberNameInfo().getAsString() + ";\n})";
    }

    bool CPPGraphEntryEmitter::isOwningContainer(const clang::CXXRecordDecl &record) const
    {
        const auto name = record.getQualifiedNameAsString();
        return name == "eastl::vector" || name == "eastl::array" || name == "eastl::basic_string";
    }

    bool CPPGraphEntryEmitter::isMathValue(const clang::CXXRecordDecl &record) const
    {
        const auto name = record.getQualifiedNameAsString();
        for (const char *scalar : {"bool", "int", "uint", "half", "float", "double"})
        {
            const auto prefix = std::string("UGL::") + scalar;
            if (name.size() == prefix.size() + 1 && name.starts_with(prefix) && name.back() >= '2' && name.back() <= '4')
            {
                return true;
            }
        }
        return false;
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
        if (mExportValidation && (name == "UGL::TextureView" || name == "UGL::IFrameBuffer"))
        {
            mVisitor.throwCodegenError(location, "GPU results must export the parent Texture allocation, not a view or framebuffer alone.");
        }
        if (isMathValue(*record))
        {
            return;
        }
        if (mExportValidation && name.starts_with("UGL::") && name != "UGL::Buffer" &&
            name != "UGL::BufferRange" && name != "UGL::Texture" && name != "UGL::Vector" && name != "UGL::Matrix")
        {
            mVisitor.throwCodegenError(location, "Graph results require CPU values or Buffer/Texture allocation handles; pass descriptors and graph builders cannot be exported.");
        }
        if ((graphConstruction || mRenderValidation) && name == "UGL::IRenderNode")
        {
            return;
        }
        if (graphConstruction && (name == "UGL::ReadbackNode" || name == "UGL::ReadbackTextureNode"))
        {
            return;
        }
        if ((mRenderValidation || graphConstruction) && (name == "UGL::Buffer" || name == "UGL::BufferRange"))
        {
            const auto *specialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(record);
            const auto element = specialization->getTemplateArgs()[0].getAsType();
            const bool previousRender = mRenderValidation;
            const bool exporting = mExportValidation;
            mRenderValidation = false;
            mExportValidation = false;
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
                mRenderValidation = previousRender;
                mExportValidation = exporting;
                throw;
            }
            mRenderValidation = previousRender;
            mExportValidation = exporting;
            return;
        }
        if ((graphConstruction || mRenderValidation) && !mExportValidation &&
            (name == "UGL::SwapchainQueryResult" || name == "UGL::RenderToSwapchainDescriptor"))
        {
            return;
        }
        if ((mRenderValidation || graphConstruction) && name == "UGL::Texture")
        {
            return;
        }
        if (mRenderValidation && (name == "UGL::ComputePassTaskDescriptor" || name == "UGL::RenderPassTaskDescriptor" ||
            name == "UGL::PixelLocalPassTaskDescriptor" || name == "UGL::PixelLocalNextPassDescriptor" ||
            name == "UGL::TextureView" || name == "UGL::TextureReadbackDescriptor" ||
            name == "UGL::IFrameBuffer" || name == "UGL::ColorAttachment" || name == "UGL::DepthStencilAttachment" ||
            name == "UGL::PixelLocalColorAttachment" || name == "UGL::PixelLocalDepthAttachment" ||
            name == "UGL::Color"))
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
        auto &values = mExportValidation ? mValidatedExportValues : mRenderValidation ? mValidatedRenderValues : graphConstruction ? mValidatedGraphValues : mValidatedValues;
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
            validateValue(specialization->getTemplateArgs()[0].getAsType(), location, graphConstruction);
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
        for (const auto *ctor : record->ctors())
        {
            if (!ctor->isDefaulted() && !ctor->isImplicit() && !ctor->isDeleted() &&
                (ctor->isCopyConstructor() || ctor->isMoveConstructor()))
            {
                validateFunction(*ctor, graphConstruction);
            }
        }
    }

    void CPPGraphEntryEmitter::validateContainerElementOperations(clang::QualType containerOrElementType, bool graphConstruction)
    {
        if (const auto *array = mVisitor.Context->getAsArrayType(containerOrElementType))
        {
            validateContainerElementOperations(array->getElementType(), graphConstruction);
            return;
        }
        const auto *record = containerOrElementType->getAsCXXRecordDecl();
        if (record == nullptr)
        {
            return;
        }
        if (isOwningContainer(*record))
        {
            const auto *specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
            if (specialization != nullptr && specialization->getTemplateArgs().size() > 0 &&
                specialization->getTemplateArgs()[0].getKind() == clang::TemplateArgument::Type)
            {
                validateContainerElementOperations(specialization->getTemplateArgs()[0].getAsType(), graphConstruction);
            }
            return;
        }
        if (isMathValue(*record) || isTask(containerOrElementType) || !record->hasDefinition() ||
            mVisitor.isFromExcludedFile(record->getLocation()))
        {
            return;
        }
        record = record->getDefinition();
        for (const auto &base : record->bases())
        {
            validateContainerElementOperations(base.getType(), graphConstruction);
        }
        for (const auto *field : record->fields())
        {
            validateContainerElementOperations(field->getType(), graphConstruction);
        }
        for (const auto *ctor : record->ctors())
        {
            if (!ctor->isDefaulted() && !ctor->isImplicit() && !ctor->isDeleted())
            {
                validateFunction(*ctor, graphConstruction);
            }
        }
        for (const auto *method : record->methods())
        {
            if (!method->isDefaulted() && !method->isImplicit() && !method->isDeleted() &&
                (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()))
            {
                validateFunction(*method, graphConstruction);
            }
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
            if ((method->isImplicit() || method->isDefaulted()) &&
                (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()))
            {
                validateValue(mVisitor.Context->getRecordType(method->getParent()), function.getLocation(), graphConstruction);
                return;
            }
            if (method->isVirtual())
            {
                mVisitor.throwCodegenError(&function, "CPU graph effects cannot be inferred through virtual dispatch.");
            }
        }
        const bool allowExplicitNode = mRenderValidation && isRenderMethod(function);
        const auto explicitNodeParam = validateExplicitRenderNodeParam(function, allowExplicitNode);
        const bool allowExplicitSubgraph = graphConstruction && !mRenderValidation && isTask(function.getReturnType());
        const auto explicitSubgraphParam = validateExplicitTaskGraphParam(function, allowExplicitSubgraph);
        validateValue(function.getReturnType(), function.getLocation(), graphConstruction);
        for (const auto *parameter : function.parameters())
        {
            if (explicitNodeParam.has_value() && isRenderNodeType(parameter->getType()))
            {
                continue;
            }
            if (explicitSubgraphParam.has_value() && isTaskGraphType(parameter->getType()))
            {
                continue;
            }
            validateValue(parameter->getType(), parameter->getLocation(), graphConstruction);
            if (parameter->hasDefaultArg() && !parameter->hasUninstantiatedDefaultArg())
            {
                if (const auto *defaultArg = parameter->getDefaultArg())
                {
                    validateStatement(*defaultArg, graphConstruction);
                }
            }
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
        if (const auto *assignment = llvm::dyn_cast<clang::CXXOperatorCallExpr>(&statement);
            assignment != nullptr && assignment->getOperator() == clang::OO_Equal && assignment->getNumArgs() != 0)
        {
            const auto *member = llvm::dyn_cast<clang::MemberExpr>(assignment->getArg(0)->IgnoreParenImpCasts());
            if (member != nullptr && isTask(member->getBase()->IgnoreParenImpCasts()->getType()))
            {
                mVisitor.throwCodegenError(member, "Deferred task field projections cannot be assigned; connect them as graph inputs.");
            }
        }
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
            else if (container)
            {
                if (const auto *memberCall = llvm::dyn_cast<clang::CXXMemberCallExpr>(call))
                {
                    if (const auto *obj = memberCall->getImplicitObjectArgument())
                    {
                        validateContainerElementOperations(obj->getType(), graphConstruction);
                    }
                }
                else
                {
                    validateContainerElementOperations(
                        mVisitor.Context->getRecordType(method->getParent()), graphConstruction);
                }
            }
            else if (!(method != nullptr && isMathValue(*method->getParent())))
            {
                const std::string parentName = method != nullptr ? method->getParent()->getQualifiedNameAsString() : "";
                const bool memberIntrinsic = method != nullptr &&
                    (parentName == "UGL::IRenderNode" ||
                     parentName == "UGL::RenderNode" ||
                     parentName == "UGL::Buffer" ||
                     parentName == "UGL::BufferRange" ||
                     parentName == "UGL::Texture" ||
                     parentName == "UGL::TextureView" ||
                     parentName == "UGL::ColorAttachment" ||
                     parentName == "UGL::DepthStencilAttachment" ||
                     parentName == "UGL::PixelLocalColorAttachment" ||
                     parentName == "UGL::PixelLocalDepthAttachment" ||
                     parentName == "UGL::PixelLocalPassTaskDescriptor" ||
                     parentName == "UGL::PixelLocalNextPassDescriptor" ||
                     parentName == "UGL::SwapchainQueryResult" ||
                     parentName == "UGL::RenderToSwapchainDescriptor" ||
                     parentName == "UGL::TextureReadbackDescriptor");
                const std::string calleeName = callee->getQualifiedNameAsString();
                const bool freeIntrinsic = calleeName == "UGL::pixelLocalPass" ||
                    calleeName == "UGL::nextPixelLocalPass" ||
                    calleeName == "UGL::RenderImGui";
                if (!(mRenderValidation && (memberIntrinsic || freeIntrinsic)))
                {
                    validateFunction(*callee, graphConstruction);
                }
            }
        }
        if (const auto *construction = llvm::dyn_cast<clang::CXXConstructExpr>(&statement))
        {
            validateValue(construction->getType(), construction->getExprLoc(), graphConstruction);
            const auto *constructor = construction->getConstructor();
            const std::string parentName = constructor->getParent()->getQualifiedNameAsString();
            const bool graph = parentName == "UGL::TaskGraph" || isTask(construction->getType()) ||
                parentName == "UGL::BufferRange" ||
                parentName == "UGL::TextureReadbackDescriptor" ||
                parentName == "UGL::SwapchainQueryResult";
            const bool renderIntrinsic = mRenderValidation &&
                (parentName == "UGL::BufferRange" ||
                 parentName == "UGL::TextureReadbackDescriptor" ||
                 parentName == "UGL::RenderToSwapchainDescriptor" ||
                 parentName == "UGL::PixelLocalPassTaskDescriptor" ||
                 parentName == "UGL::PixelLocalNextPassDescriptor" ||
                 parentName == "UGL::PixelLocalColorAttachment" ||
                 parentName == "UGL::PixelLocalDepthAttachment" ||
                 parentName == "UGL::SwapchainQueryResult");
            if (isOwningContainer(*constructor->getParent()))
            {
                validateContainerElementOperations(construction->getType(), graphConstruction);
            }
            else if (!constructor->isDefaulted() && !constructor->isImplicit() &&
                !isMathValue(*constructor->getParent()) && !(graphConstruction && graph) && !renderIntrinsic)
            {
                validateFunction(*constructor, graphConstruction);
            }
        }
        if (const auto *defaultArg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(&statement))
        {
            if (const auto *param = defaultArg->getParam();
                param != nullptr && !mVisitor.isFromExcludedFile(param->getLocation()))
            {
                if (const auto *expr = defaultArg->getExpr())
                {
                    validateStatement(*expr, graphConstruction);
                }
            }
        }
        if (const auto *defaultInit = llvm::dyn_cast<clang::CXXDefaultInitExpr>(&statement))
        {
            if (const auto *field = defaultInit->getField();
                field != nullptr && !mVisitor.isFromExcludedFile(field->getLocation()))
            {
                if (const auto *expr = defaultInit->getExpr())
                {
                    validateStatement(*expr, graphConstruction);
                }
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
        if (isTask(function.getReturnType()) && validateExplicitTaskGraphParam(function, true).has_value())
        {
            const std::string qualified = function.getQualifiedNameAsString();
            return "/* Invokes a statically resolved DSL subgraph builder. */ [] (GVM::Core::TaskGraph &__gvm_subgraph, auto... arguments) -> decltype(::" +
                qualified + "(__gvm_subgraph, arguments...))\n{\n    return ::" +
                qualified + "(__gvm_subgraph, arguments...);\n}";
        }
        const std::string qualified = function.getQualifiedNameAsString();
        return "/* Invokes a statically resolved DSL declaration. */ [] (auto... arguments) -> decltype(::" +
            qualified + "(arguments...))\n{\n    return ::" +
            qualified + "(arguments...);\n}";
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
            if (validateExplicitTaskGraphParam(target, true).has_value())
            {
                mVisitor.throwCodegenError(&call, "TaskRuntime entry must accept one owned input value without an explicit TaskGraph & parameter.");
            }
            validateResult(target);
            validateFunction(target, true);
        }
        else
        {
            validateTarget(target);
        }
        const bool renderTarget = llvm::isa<clang::CXXMethodDecl>(&target) &&
            isRenderRecord(*llvm::cast<clang::CXXMethodDecl>(&target)->getParent());
        const bool subgraphTarget = !renderTarget && isTask(target.getReturnType());
        for (unsigned index = 1; index < call.getNumArgs(); ++index)
        {
            const auto argType = call.getArg(index)->getType();
            const bool allowGraphArg = submit || (add && (isTask(argType) || isTaskVector(argType) || renderTarget || subgraphTarget));
            validateValue(argType, call.getArg(index)->getExprLoc(), allowGraphArg);
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
        const bool previous = mRenderValidation;
        mRenderValidation = true;
        mExportValidation = true;
        try
        {
            validateValue(task->getTemplateArgs()[0].getAsType(), function.getLocation());
        }
        catch (...)
        {
            mRenderValidation = previous;
            mExportValidation = false;
            throw;
        }
        mRenderValidation = previous;
        mExportValidation = false;
    }

    bool CPPGraphEntryEmitter::containsResources(clang::QualType type, std::unordered_set<const clang::CXXRecordDecl *> &visited) const
    {
        if (const auto *array = mVisitor.Context->getAsConstantArrayType(type))
        {
            return containsResources(array->getElementType(), visited);
        }
        const auto *record = type->getAsCXXRecordDecl();
        if (record == nullptr || !visited.insert(record->getCanonicalDecl()).second)
        {
            return false;
        }
        const auto name = record->getQualifiedNameAsString();
        if (name == "UGL::Buffer" || name == "UGL::BufferRange" || name == "UGL::Texture" ||
            name == "UGL::TextureView" || name == "UGL::TextureReadbackDescriptor")
        {
            return true;
        }
        if (isOwningContainer(*record))
        {
            const auto *specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
            return specialization != nullptr && containsResources(specialization->getTemplateArgs()[0].getAsType(), visited);
        }
        if (mVisitor.isFromExcludedFile(record->getLocation()) || !record->hasDefinition())
        {
            return false;
        }
        for (const auto &base : record->bases())
        {
            if (containsResources(base.getType(), visited))
            {
                return true;
            }
        }
        for (const auto *field : record->getDefinition()->fields())
        {
            if (containsResources(field->getType(), visited))
            {
                return true;
            }
        }
        return false;
    }

    std::string CPPGraphEntryEmitter::emitResourceVisitor(const clang::CXXRecordDecl &record) const
    {
        if (!record.isThisDeclarationADefinition() || isRenderRecord(record) ||
            mVisitor.checkDerivedClassByName(&record, mUGLAbstractRendererClassName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLRenderSetBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLBindGroupBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLFrameBufferBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLRenderClassBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLComputeClassBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLPixelLocalRenderClassBaseName))
        {
            return {};
        }
        std::unordered_set<const clang::CXXRecordDecl *> visited;
        if (!containsResources(mVisitor.Context->getRecordType(&record), visited))
        {
            return {};
        }
        std::string result = "\npublic:\n    static constexpr bool __gvm_containsResources = true;\n"
            "    /// Visits output allocations so successful retrieval can preserve their explicit destruction contract.\n"
            "    template <class Visitor>\n    void __gvm_visitResources(const Visitor &visitor) const\n    {\n";
        for (const auto &base : record.bases())
        {
            result += "        GVM::Core::Private::visitGraphResources(static_cast<const " +
                mVisitor.generateTypeCanonicalName(base.getType()) + " &>(*this), visitor);\n";
        }
        for (const auto *field : record.fields())
        {
            result += "        GVM::Core::Private::visitGraphResources(this->" + field->getNameAsString() + ", visitor);\n";
        }
        return result + "    }\n";
    }

    std::string CPPGraphEntryEmitter::emitTemplateResourceVisitor(const clang::CXXRecordDecl &record) const
    {
        if (!record.isThisDeclarationADefinition() || isRenderRecord(record) ||
            mVisitor.checkDerivedClassByName(&record, mUGLAbstractRendererClassName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLRenderSetBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLBindGroupBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLFrameBufferBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLRenderClassBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLComputeClassBaseName) ||
            mVisitor.checkDerivedClassByName(&record, mUGLPixelLocalRenderClassBaseName))
        {
            return {};
        }

        const clang::ClassTemplateDecl *ownerTemplate = record.getDescribedClassTemplate();
        if (ownerTemplate == nullptr)
        {
            if (const auto *partialSpec = llvm::dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(&record))
            {
                ownerTemplate = partialSpec->getSpecializedTemplate();
            }
        }
        if (ownerTemplate == nullptr)
        {
            return {};
        }

        bool usedWithResources = false;
        for (const auto *spec : ownerTemplate->specializations())
        {
            if (spec == nullptr)
            {
                continue;
            }
            std::unordered_set<const clang::CXXRecordDecl *> visited;
            if (containsResources(mVisitor.Context->getRecordType(spec), visited))
            {
                usedWithResources = true;
                break;
            }
        }
        if (!usedWithResources)
        {
            for (const auto *field : record.fields())
            {
                if (!field->getType()->isDependentType())
                {
                    std::unordered_set<const clang::CXXRecordDecl *> visited;
                    if (containsResources(field->getType(), visited))
                    {
                        usedWithResources = true;
                        break;
                    }
                }
            }
        }
        if (!usedWithResources)
        {
            return {};
        }

        const auto &sourceMgr = mVisitor.Context->getSourceManager();
        const auto &langOpts = mVisitor.Context->getLangOpts();
        std::string hasExpr = "false";
        std::string visitBody;
        for (const auto &base : record.bases())
        {
            std::string baseSpell;
            if (base.getType()->isDependentType() && base.getTypeSourceInfo() != nullptr)
            {
                baseSpell = clang::Lexer::getSourceText(
                    clang::CharSourceRange::getTokenRange(base.getTypeSourceInfo()->getTypeLoc().getSourceRange()),
                    sourceMgr,
                    langOpts).str();
            }
            else
            {
                baseSpell = mVisitor.generateTypeCanonicalName(base.getType());
            }
            if (!baseSpell.empty())
            {
                hasExpr += " || GVM::Core::Private::containsGraphResources<" + baseSpell + ">";
                visitBody += "        GVM::Core::Private::visitGraphResources(static_cast<const " + baseSpell + " &>(*this), visitor);\n";
            }
        }
        for (const auto *field : record.fields())
        {
            if (field->isBitField())
            {
                continue;
            }
            const std::string fieldName = field->getNameAsString();
            if (fieldName.empty())
            {
                continue;
            }
            hasExpr += " || GVM::Core::Private::containsGraphResources<decltype(" + fieldName + ")>";
            visitBody += "        GVM::Core::Private::visitGraphResources(this->" + fieldName + ", visitor);\n";
        }

        return "\npublic:\n"
            "    /// Reports at instantiation time whether this template specialization holds GPU allocations.\n"
            "    template <int = 0>\n"
            "    static constexpr bool __gvm_hasResources()\n"
            "    {\n"
            "        return " + hasExpr + ";\n"
            "    }\n"
            "    /// Visits output allocations so successful retrieval preserves their explicit destruction contract.\n"
            "    template <class Visitor>\n"
            "    void __gvm_visitResources(const Visitor &visitor) const\n"
            "    {\n" +
            visitBody +
            "    }\n";
    }

    std::string CPPGraphEntryEmitter::emitEntry(const clang::FunctionDecl &function)
    {
        if (!function.doesThisDeclarationHaveABody() || !isTask(function.getReturnType()) ||
            !mVisitor.checkAttibuteByName(&function, mUGLAttributeExportName))
        {
            return {};
        }
        if (function.getNumParams() != 1 || function.isTemplated() || isTaskGraphType(function.getParamDecl(0)->getType()))
        {
            mVisitor.throwCodegenError(&function, "Exported graph entry requires one concrete owned input parameter.");
        }
        validateResult(function);
        validateFunction(function, true);
        const std::string qualified = function.getQualifiedNameAsString();
        return "\nnamespace Entries\n{\n    /// Invokes the statically exported graph with an owned input snapshot.\n    inline constexpr auto " +
            function.getNameAsString() + " = [](auto input) -> decltype(::" + qualified + "(input))\n    {\n        return ::" + qualified + "(input);\n    };\n}\n";
    }
}
