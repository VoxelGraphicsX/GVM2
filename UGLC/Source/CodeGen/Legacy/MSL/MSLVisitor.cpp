#include "MSLVisitor.hpp"
#include <CodeGen/PixelLocalFieldAnalysis.hpp>
#include <CodeGen/PixelLocalInputPlan.hpp>
#include <CodeGen/ShaderTypeClassificationUtils.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/ShaderDeclarationEmissionPlanner.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>

#include <clang/AST/DeclTemplate.h>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace UGLC::CodeGen::MSL
{
    namespace
    {
        ShaderStageKind getShaderStageKindForEntry(const clang::FunctionDecl *entryFunction)
        {
            if (entryFunction == nullptr)
            {
                return ShaderStageKind::Compute;
            }

            const std::string functionName = entryFunction->getNameAsString();
            if (functionName == mUGLVertexShaderFunctionName)
            {
                return ShaderStageKind::Vertex;
            }
            if (functionName == mUGLFragmentShaderFunctionName || functionName == mUGLPixelShaderFunctionName)
            {
                return ShaderStageKind::Fragment;
            }
            if (functionName == mUGLComputeShaderFunctionName)
            {
                return ShaderStageKind::Compute;
            }
            if (functionName == mUGLHullShaderFunctionName)
            {
                return ShaderStageKind::Hull;
            }
            if (functionName == mUGLDomainShaderFunctionName)
            {
                return ShaderStageKind::Domain;
            }

            return ShaderStageKind::Compute;
        }

        std::string getGeneratedEntryPointName(const clang::FunctionDecl *entryFunction)
        {
            if (entryFunction == nullptr)
            {
                return {};
            }

            const std::string functionName = entryFunction->getNameAsString();
            if (functionName == mUGLVertexShaderFunctionName || functionName == mUGLDomainShaderFunctionName)
            {
                return VertexShaderEntryName;
            }
            if (functionName == mUGLFragmentShaderFunctionName || functionName == mUGLPixelShaderFunctionName)
            {
                return FragmentShaderEntryName;
            }
            if (functionName == mUGLComputeShaderFunctionName)
            {
                return ComputeShaderEntryName;
            }
            return functionName;
        }

        /** Resolves the concrete MSL pointee type for a UGL::BindGroup<T> shader handle. */
        std::optional<std::string> makeBindGroupPointerPointeeTypeName(MSLVisitor &visitor, const clang::QualType &bindGroupHandleType, AbstractTypeConvertor *typeConvertor)
        {
            if (!visitor.isBindGroupHandleType(bindGroupHandleType))
            {
                return std::nullopt;
            }

            const std::vector<clang::TemplateArgument> args = visitor.getTemplateArgumentsFromType(bindGroupHandleType);
            if (args.empty() || args.front().getKind() != clang::TemplateArgument::Type)
            {
                throw std::runtime_error("UGL::BindGroup<T> requires a concrete bind-group type for MSL code generation.");
            }

            clang::QualType bindGroupType = args.front().getAsType();
            if (const auto resolvedType = visitor.tryResolveTemplateSubstitutionType(bindGroupType))
            {
                bindGroupType = *resolvedType;
            }
            return visitor.generateTypeCanonicalName(bindGroupType, typeConvertor);
        }

        std::string makeMSLUserVaryingAttribute(int location)
        {
            return " [[user(locn" + std::to_string(location) + ")]]";
        }

        /** Returns a normalized expression after removing wrappers that do not change the addressed atomic target. */
        const clang::Expr *stripAtomicTargetWrappers(const clang::Expr *expr)
        {
            if (expr == nullptr)
            {
                return nullptr;
            }

            const clang::Expr *current = expr->IgnoreParenImpCasts();
            while (current != nullptr)
            {
                if (const auto *materialize = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
                {
                    current = materialize->getSubExpr()->IgnoreParenImpCasts();
                    continue;
                }
                if (const auto *bindTemporary = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
                {
                    current = bindTemporary->getSubExpr()->IgnoreParenImpCasts();
                    continue;
                }
                if (const auto *defaultArg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(current))
                {
                    current = defaultArg->getExpr()->IgnoreParenImpCasts();
                    continue;
                }
                return current;
            }
            return nullptr;
        }

        /** Returns whether the function name belongs to the UGL atomic builtin surface. */
        bool isUGLAtomicBuiltinName(std::string_view name)
        {
            return name == "atomicAdd" ||
                   name == "atomicOr" ||
                   name == "atomicAnd" ||
                   name == "atomicLoad" ||
                   name == "atomicStore" ||
                   name == "atomicCompareExchange" ||
                   name == "atomicMax" ||
                   name == "atomicMin";
        }

        /** Returns whether a type name denotes a plain scalar that Metal supports in atomic<T>. */
        bool isSupportedMetalAtomicScalarTypeName(std::string_view typeName)
        {
            return typeName == "int" ||
                   typeName == "uint" ||
                   typeName == "unsigned int" ||
                   typeName == "uint32_t" ||
                   typeName == "std::uint32_t" ||
                   typeName == "UGL::uint";
        }

        /** Normalizes a generated scalar type name to the spelling used inside Metal atomic<T>. */
        std::optional<std::string> normalizeMetalAtomicScalarTypeName(std::string_view typeName)
        {
            if (typeName == "int")
            {
                return "int";
            }
            if (typeName == "uint" ||
                typeName == "unsigned int" ||
                typeName == "uint32_t" ||
                typeName == "std::uint32_t" ||
                typeName == "UGL::uint")
            {
                return "unsigned int";
            }
            return std::nullopt;
        }

        /** Returns the ordinary binary operation represented by a compound assignment opcode. */
        std::optional<std::string_view> getCompoundAssignmentOperationSpelling(clang::BinaryOperatorKind opcode)
        {
            switch (opcode)
            {
            case clang::BO_MulAssign:
                return "*";
            case clang::BO_DivAssign:
                return "/";
            case clang::BO_RemAssign:
                return "%";
            case clang::BO_AddAssign:
                return "+";
            case clang::BO_SubAssign:
                return "-";
            case clang::BO_ShlAssign:
                return "<<";
            case clang::BO_ShrAssign:
                return ">>";
            case clang::BO_AndAssign:
                return "&";
            case clang::BO_XorAssign:
                return "^";
            case clang::BO_OrAssign:
                return "|";
            default:
                return std::nullopt;
            }
        }

        /** Returns the ordinary binary operation represented by an overloaded compound assignment opcode. */
        std::optional<std::string_view> getCompoundAssignmentOperationSpelling(clang::OverloadedOperatorKind opcode)
        {
            switch (opcode)
            {
            case clang::OO_StarEqual:
                return "*";
            case clang::OO_SlashEqual:
                return "/";
            case clang::OO_PercentEqual:
                return "%";
            case clang::OO_PlusEqual:
                return "+";
            case clang::OO_MinusEqual:
                return "-";
            case clang::OO_LessLessEqual:
                return "<<";
            case clang::OO_GreaterGreaterEqual:
                return ">>";
            case clang::OO_AmpEqual:
                return "&";
            case clang::OO_CaretEqual:
                return "^";
            case clang::OO_PipeEqual:
                return "|";
            default:
                return std::nullopt;
            }
        }

        /** Builds a stable generated local name from a source location. */
        std::string makeGeneratedIdentifier(const std::string &prefix, const clang::SourceLocation location)
        {
            return "__uglc_" + prefix + "_" + std::to_string(location.getRawEncoding());
        }

        /** Adds the Metal device address space to a structured-buffer atomic element pointer type. */
        std::string makeDeviceAtomicPointerTypeName(std::string_view pointerTypeName)
        {
            static constexpr std::string_view kConstPrefix = "const ";
            if (pointerTypeName.starts_with(kConstPrefix))
            {
                return "const device " + std::string(pointerTypeName.substr(kConstPrefix.size()));
            }
            return "device " + std::string(pointerTypeName);
        }

    }
    MSLVisitor::MSLVisitor(clang::ASTContext *Context)
        : BaseASTVisitor(Context, &mTypeConvertor, &mAttriConv, nullptr),
          mShaderBuiltinTranslator(*this, mWaveBuiltinAnalyzer)
    {
    }

    void MSLVisitor::analyzeAtomicResourceUsage(const std::vector<const clang::Decl *> &shaderDefs, const clang::CXXRecordDecl *shaderClassDecl)
    {
        mAtomicPlainFields.clear();
        mAtomicStructuredBufferFields.clear();
        mAtomicStructuredBufferVariables.clear();
        mAtomicStructuredBufferParameterIDs.clear();
        mAtomicGroupSharedVariables.clear();

        bool changed = true;
        while (changed)
        {
            const size_t plainFieldCount = mAtomicPlainFields.size();
            const size_t structuredBufferFieldCount = mAtomicStructuredBufferFields.size();
            const size_t structuredBufferVariableCount = mAtomicStructuredBufferVariables.size();
            const size_t structuredBufferParameterIDCount = mAtomicStructuredBufferParameterIDs.size();
            const size_t groupSharedVariableCount = mAtomicGroupSharedVariables.size();

            for (const clang::Decl *decl : shaderDefs)
            {
                analyzeAtomicDecl(decl);
            }
            analyzeAtomicDecl(shaderClassDecl);

            changed = plainFieldCount != mAtomicPlainFields.size() ||
                      structuredBufferFieldCount != mAtomicStructuredBufferFields.size() ||
                      structuredBufferVariableCount != mAtomicStructuredBufferVariables.size() ||
                      structuredBufferParameterIDCount != mAtomicStructuredBufferParameterIDs.size() ||
                      groupSharedVariableCount != mAtomicGroupSharedVariables.size();
        }
    }

    void MSLVisitor::analyzeAtomicDecl(const clang::Decl *decl)
    {
        if (decl == nullptr)
        {
            return;
        }

        if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(decl))
        {
            if (functionDecl->hasBody())
            {
                analyzeAtomicStmt(functionDecl->getBody());
            }
            return;
        }

        if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        {
            for (const clang::Decl *innerDecl : recordDecl->decls())
            {
                analyzeAtomicDecl(innerDecl);
            }
            return;
        }

        if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
        {
            if (varDecl->hasInit())
            {
                analyzeAtomicStmt(varDecl->getInit());
            }
        }
    }

    void MSLVisitor::analyzeAtomicStmt(const clang::Stmt *stmt)
    {
        if (stmt == nullptr)
        {
            return;
        }

        if (const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(stmt))
        {
            const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl());
            if (calleeDecl != nullptr && isUGLAtomicBuiltinName(calleeDecl->getNameAsString()) && callExpr->getNumArgs() > 0)
            {
                recordAtomicTargetExpression(callExpr->getArg(0));
            }
            else
            {
                propagateAtomicStructuredBufferCallArguments(callExpr);
            }
        }

        for (const clang::Stmt *child : stmt->children())
        {
            analyzeAtomicStmt(child);
        }
    }

    std::optional<std::string> MSLVisitor::makeAtomicScalarTypeName(const clang::QualType &type)
    {
        if (type.isNull())
        {
            return std::nullopt;
        }

        clang::QualType resolvedType = getUnqualifiedType(type);
        while (const auto *arrayType = Context->getAsConstantArrayType(resolvedType))
        {
            resolvedType = getUnqualifiedType(arrayType->getElementType());
        }

        if (const auto *recordDecl = resolvedType->getAsCXXRecordDecl())
        {
            const std::string recordName = getClassCanonicalName(recordDecl, nullptr);
            if (recordName == mUGLShaderAtomicName)
            {
                const auto templateArgs = getTemplateArgumentsFromType(resolvedType);
                if (!templateArgs.empty() && templateArgs.front().getKind() == clang::TemplateArgument::Type)
                {
                    return makeAtomicScalarTypeName(templateArgs.front().getAsType());
                }
            }
        }

        const std::string typeName = generateTypeCanonicalName(resolvedType, &mTypeConvertor);
        if (!isSupportedMetalAtomicScalarTypeName(typeName))
        {
            return std::nullopt;
        }
        return normalizeMetalAtomicScalarTypeName(typeName);
    }

    bool MSLVisitor::isAtomicPlainField(const clang::FieldDecl *field) const
    {
        return field != nullptr && mAtomicPlainFields.contains(field->getCanonicalDecl());
    }

    bool MSLVisitor::isAtomicStructuredBufferField(const clang::FieldDecl *field) const
    {
        return field != nullptr && mAtomicStructuredBufferFields.contains(field->getCanonicalDecl());
    }

    bool MSLVisitor::isAtomicStructuredBufferVariable(const clang::VarDecl *decl) const
    {
        if (decl == nullptr)
        {
            return false;
        }
        if (mAtomicStructuredBufferVariables.contains(decl->getCanonicalDecl()))
        {
            return true;
        }
        const auto *param = llvm::dyn_cast<clang::ParmVarDecl>(decl);
        if (param == nullptr)
        {
            return false;
        }
        const auto parameterID = makeAtomicStructuredBufferParameterID(param);
        return parameterID.has_value() && mAtomicStructuredBufferParameterIDs.contains(*parameterID);
    }

    std::optional<std::string> MSLVisitor::makeAtomicStructuredBufferParameterID(const clang::ParmVarDecl *param) const
    {
        if (param == nullptr)
        {
            return std::nullopt;
        }
        const auto *functionDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(param->getDeclContext());
        if (functionDecl == nullptr)
        {
            return std::nullopt;
        }

        const auto *canonicalFunctionDecl = functionDecl->getCanonicalDecl();
        for (unsigned paramIndex = 0; paramIndex < canonicalFunctionDecl->getNumParams(); ++paramIndex)
        {
            const clang::ParmVarDecl *canonicalParam = canonicalFunctionDecl->getParamDecl(paramIndex);
            if (canonicalParam == param || canonicalParam == param->getCanonicalDecl())
            {
                return getFunctionUniqueID(canonicalFunctionDecl) + "#" + std::to_string(paramIndex);
            }
        }

        for (unsigned paramIndex = 0; paramIndex < functionDecl->getNumParams(); ++paramIndex)
        {
            if (functionDecl->getParamDecl(paramIndex) == param)
            {
                return getFunctionUniqueID(canonicalFunctionDecl) + "#" + std::to_string(paramIndex);
            }
        }
        return std::nullopt;
    }

    bool MSLVisitor::recordAtomicStructuredBufferVariableIfSupported(const clang::VarDecl *decl)
    {
        if (decl == nullptr)
        {
            return false;
        }
        if (!makeAtomicStructuredBufferPointerTypeName(decl->getType()).has_value())
        {
            return false;
        }

        mAtomicStructuredBufferVariables.insert(decl->getCanonicalDecl());
        if (const auto *param = llvm::dyn_cast<clang::ParmVarDecl>(decl))
        {
            if (const auto parameterID = makeAtomicStructuredBufferParameterID(param))
            {
                mAtomicStructuredBufferParameterIDs.insert(*parameterID);
            }
        }
        return true;
    }

    bool MSLVisitor::isAtomicGroupSharedVariable(const clang::VarDecl *decl) const
    {
        return decl != nullptr && mAtomicGroupSharedVariables.contains(decl->getCanonicalDecl());
    }

    bool MSLVisitor::recordAtomicGroupSharedVariableIfSupported(const clang::VarDecl *decl)
    {
        if (decl == nullptr)
        {
            return false;
        }

        clang::QualType resolvedType = getUnqualifiedType(decl->getType());
        while (const auto *arrayType = Context->getAsConstantArrayType(resolvedType))
        {
            resolvedType = getUnqualifiedType(arrayType->getElementType());
        }

        if (!checkTypeCanonicalName(resolvedType, "UGL::GroupShared"))
        {
            return false;
        }

        const auto templateArgs = getTemplateArgumentsFromType(resolvedType);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type)
        {
            return false;
        }

        if (!makeAtomicScalarTypeName(templateArgs.front().getAsType()).has_value())
        {
            return false;
        }

        mAtomicGroupSharedVariables.insert(decl->getCanonicalDecl());
        return true;
    }

    std::optional<std::string> MSLVisitor::makeAtomicStructuredBufferPointerTypeName(const clang::QualType &type)
    {
        clang::QualType resolvedType = getUnqualifiedType(type);
        if (!checkTypeCanonicalName(resolvedType, mUGLShaderStructuredBufferName) &&
            !checkTypeCanonicalName(resolvedType, mUGLShaderRWStructuredBufferName))
        {
            return std::nullopt;
        }

        const auto templateArgs = getTemplateArgumentsFromType(resolvedType);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type)
        {
            return std::nullopt;
        }

        const std::optional<std::string> scalarTypeName = makeAtomicScalarTypeName(templateArgs.front().getAsType());
        if (!scalarTypeName.has_value())
        {
            return std::nullopt;
        }

        const bool readOnly = checkTypeCanonicalName(resolvedType, mUGLShaderStructuredBufferName);
        return std::string(readOnly ? "const " : "") + "atomic<" + *scalarTypeName + ">*";
    }

    std::optional<std::string> MSLVisitor::makeAtomicStructuredBufferPointerTypeName(const clang::FieldDecl *field)
    {
        if (!isAtomicStructuredBufferField(field))
        {
            return std::nullopt;
        }

        return makeAtomicStructuredBufferPointerTypeName(field->getType());
    }

    std::optional<std::string> MSLVisitor::makeAtomicStructuredBufferPointerTypeName(const clang::VarDecl *decl)
    {
        if (!isAtomicStructuredBufferVariable(decl))
        {
            return std::nullopt;
        }

        return makeAtomicStructuredBufferPointerTypeName(decl->getType());
    }

    std::optional<std::string> MSLVisitor::makeAtomicGroupSharedVariableTypeName(const clang::VarDecl *decl)
    {
        if (!isAtomicGroupSharedVariable(decl))
        {
            return std::nullopt;
        }

        clang::QualType resolvedType = getUnqualifiedType(decl->getType());
        while (const auto *arrayType = Context->getAsConstantArrayType(resolvedType))
        {
            resolvedType = getUnqualifiedType(arrayType->getElementType());
        }

        if (!checkTypeCanonicalName(resolvedType, "UGL::GroupShared"))
        {
            return std::nullopt;
        }

        const auto templateArgs = getTemplateArgumentsFromType(resolvedType);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type)
        {
            return std::nullopt;
        }

        const std::optional<std::string> scalarTypeName = makeAtomicScalarTypeName(templateArgs.front().getAsType());
        if (!scalarTypeName.has_value())
        {
            return std::nullopt;
        }
        return "threadgroup atomic<" + *scalarTypeName + ">";
    }

    std::optional<std::string> MSLVisitor::makeAtomicPlainValueTypeName(const clang::QualType &type)
    {
        if (type.isNull())
        {
            return std::nullopt;
        }

        clang::QualType resolvedType = getUnqualifiedType(type);
        while (const auto *arrayType = Context->getAsConstantArrayType(resolvedType))
        {
            resolvedType = getUnqualifiedType(arrayType->getElementType());
        }

        if (const auto *recordDecl = resolvedType->getAsCXXRecordDecl())
        {
            const std::string recordName = getClassCanonicalName(recordDecl, nullptr);
            if (recordName == "UGL::GroupShared" || recordName == mUGLShaderAtomicName)
            {
                const auto templateArgs = getTemplateArgumentsFromType(resolvedType);
                if (!templateArgs.empty() && templateArgs.front().getKind() == clang::TemplateArgument::Type)
                {
                    return makeAtomicPlainValueTypeName(templateArgs.front().getAsType());
                }
            }
        }

        return makeAtomicScalarTypeName(resolvedType);
    }

    std::optional<std::string> MSLVisitor::makeAtomicBackedLValuePointerTypeName(const clang::Expr *expr)
    {
        const clang::Expr *targetExpr = stripAtomicTargetWrappers(expr);
        if (targetExpr == nullptr)
        {
            return std::nullopt;
        }

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(targetExpr))
        {
            if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
            {
                if (isAtomicGroupSharedVariable(varDecl))
                {
                    const auto valueTypeName = makeAtomicPlainValueTypeName(varDecl->getType());
                    if (valueTypeName.has_value())
                    {
                        return "threadgroup atomic<" + *valueTypeName + ">*";
                    }
                }
            }
        }

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(targetExpr))
        {
            if (const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl()))
            {
                if (isAtomicPlainField(fieldDecl))
                {
                    const auto valueTypeName = makeAtomicPlainValueTypeName(fieldDecl->getType());
                    if (valueTypeName.has_value())
                    {
                        return "device atomic<" + *valueTypeName + ">*";
                    }
                }
            }
        }

        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(targetExpr);
            operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Subscript && operatorCallExpr->getNumArgs() >= 1)
        {
            const clang::Expr *baseExpr = stripAtomicTargetWrappers(operatorCallExpr->getArg(0));
            if (const auto *declRefExpr = llvm::dyn_cast_or_null<clang::DeclRefExpr>(baseExpr))
            {
                if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
                {
                    if (isAtomicGroupSharedVariable(varDecl))
                    {
                        const auto valueTypeName = makeAtomicPlainValueTypeName(varDecl->getType());
                        if (valueTypeName.has_value())
                        {
                            return "threadgroup atomic<" + *valueTypeName + ">*";
                        }
                    }
                    if (const auto pointerTypeName = makeAtomicStructuredBufferPointerTypeName(varDecl))
                    {
                        return makeDeviceAtomicPointerTypeName(*pointerTypeName);
                    }
                }
            }
            if (const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(baseExpr))
            {
                if (const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl()))
                {
                    if (const auto pointerTypeName = makeAtomicStructuredBufferPointerTypeName(fieldDecl))
                    {
                        return makeDeviceAtomicPointerTypeName(*pointerTypeName);
                    }
                }
            }
        }

        if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(targetExpr))
        {
            const clang::Expr *baseExpr = stripAtomicTargetWrappers(arraySubscriptExpr->getBase());
            if (const auto *declRefExpr = llvm::dyn_cast_or_null<clang::DeclRefExpr>(baseExpr))
            {
                if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
                {
                    if (isAtomicGroupSharedVariable(varDecl))
                    {
                        const auto valueTypeName = makeAtomicPlainValueTypeName(varDecl->getType());
                        if (valueTypeName.has_value())
                        {
                            return "threadgroup atomic<" + *valueTypeName + ">*";
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }

    bool MSLVisitor::isAtomicBackedLValue(const clang::Expr *expr)
    {
        return makeAtomicBackedLValuePointerTypeName(expr).has_value();
    }

    std::string MSLVisitor::translateExprWithAtomicLValueMode(const clang::Expr *expr, const bool rawAtomicLValue)
    {
        const bool previousMode = mTranslateAtomicLValueRaw;
        mTranslateAtomicLValueRaw = rawAtomicLValue;
        try
        {
            std::string result = TranslateExpr(expr);
            mTranslateAtomicLValueRaw = previousMode;
            return result;
        }
        catch (...)
        {
            mTranslateAtomicLValueRaw = previousMode;
            throw;
        }
    }

    std::string MSLVisitor::translateAtomicBackedLValueRaw(const clang::Expr *expr)
    {
        return translateExprWithAtomicLValueMode(expr, true);
    }

    std::string MSLVisitor::translateAtomicBackedLValueLoad(const clang::Expr *expr)
    {
        return "atomicLoad(" + translateAtomicBackedLValueRaw(expr) + ")";
    }

    std::string MSLVisitor::translateAtomicBackedCompoundAssignment(const clang::Expr *lhsExpr, const clang::Expr *rhsExpr, const clang::SourceLocation location, const std::string_view operationSpelling)
    {
        const auto pointerTypeName = makeAtomicBackedLValuePointerTypeName(lhsExpr);
        const auto valueTypeName = makeAtomicPlainValueTypeName(lhsExpr->getType());
        if (!pointerTypeName.has_value() || !valueTypeName.has_value())
        {
            throwCodegenError(lhsExpr, "MSL atomic-backed ordinary compound assignment only supports int and uint scalar lvalues.");
        }

        const std::string targetName = makeGeneratedIdentifier("atomic_plain_target", location);
        const std::string valueName = makeGeneratedIdentifier("atomic_plain_value", location);
        const std::string targetExpr = translateAtomicBackedLValueRaw(lhsExpr);
        const std::string rhsValueExpr = TranslateExpr(rhsExpr);

        std::string result;
        result += "{ ";
        result += *pointerTypeName + " " + targetName + " = &" + targetExpr + EOS();
        result += *valueTypeName + " " + valueName + " = atomicLoad(*" + targetName + ")" + EOS();
        result += valueName + " = " + valueName + " " + std::string(operationSpelling) + " " + rhsValueExpr + EOS();
        result += "atomicStore(*" + targetName + ", " + valueName + ")" + EOS();
        result += "}";
        return result;
    }

    std::string MSLVisitor::translateAtomicBackedCompoundAssignment(const clang::BinaryOperator *expr, const std::string_view operationSpelling)
    {
        return translateAtomicBackedCompoundAssignment(expr->getLHS(), expr->getRHS(), expr->getExprLoc(), operationSpelling);
    }

    std::string MSLVisitor::translateAtomicBackedIncrement(const clang::UnaryOperator *expr, const std::string_view operationSpelling)
    {
        const auto pointerTypeName = makeAtomicBackedLValuePointerTypeName(expr->getSubExpr());
        const auto valueTypeName = makeAtomicPlainValueTypeName(expr->getSubExpr()->getType());
        if (!pointerTypeName.has_value() || !valueTypeName.has_value())
        {
            throwCodegenError(expr, "MSL atomic-backed ordinary increment and decrement only support int and uint scalar lvalues.");
        }

        const std::string targetName = makeGeneratedIdentifier("atomic_plain_target", expr->getExprLoc());
        const std::string valueName = makeGeneratedIdentifier("atomic_plain_value", expr->getExprLoc());
        const std::string targetExpr = translateAtomicBackedLValueRaw(expr->getSubExpr());

        std::string result;
        result += "{ ";
        result += *pointerTypeName + " " + targetName + " = &" + targetExpr + EOS();
        result += *valueTypeName + " " + valueName + " = atomicLoad(*" + targetName + ")" + EOS();
        result += valueName + " = " + valueName + " " + std::string(operationSpelling) + " 1" + EOS();
        result += "atomicStore(*" + targetName + ", " + valueName + ")" + EOS();
        result += "}";
        return result;
    }

    void MSLVisitor::recordAtomicStructuredBufferBaseExpression(const clang::Expr *expr)
    {
        const clang::Expr *normalizedBase = stripAtomicTargetWrappers(expr);
        if (normalizedBase == nullptr)
        {
            return;
        }

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(normalizedBase))
        {
            if (const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl()))
            {
                if (makeAtomicStructuredBufferPointerTypeName(fieldDecl->getType()).has_value())
                {
                    mAtomicStructuredBufferFields.insert(fieldDecl->getCanonicalDecl());
                }
            }
            return;
        }

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(normalizedBase))
        {
            if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
            {
                recordAtomicStructuredBufferVariableIfSupported(varDecl);
            }
            return;
        }

        for (const clang::Stmt *child : normalizedBase->children())
        {
            if (const auto *childExpr = llvm::dyn_cast_or_null<clang::Expr>(child))
            {
                recordAtomicStructuredBufferBaseExpression(childExpr);
            }
        }
    }

    void MSLVisitor::propagateAtomicStructuredBufferCallArguments(const clang::CallExpr *callExpr)
    {
        if (callExpr == nullptr)
        {
            return;
        }

        const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl());
        if (calleeDecl == nullptr)
        {
            if (const auto *memberCall = llvm::dyn_cast<clang::CXXMemberCallExpr>(callExpr))
            {
                calleeDecl = memberCall->getMethodDecl();
            }
        }
        if (calleeDecl == nullptr)
        {
            return;
        }

        const unsigned argumentCount = std::min(callExpr->getNumArgs(), calleeDecl->getNumParams());
        for (unsigned argIndex = 0; argIndex < argumentCount; ++argIndex)
        {
            const clang::ParmVarDecl *calleeParam = calleeDecl->getParamDecl(argIndex);
            if (isAtomicStructuredBufferVariable(calleeParam))
            {
                recordAtomicStructuredBufferBaseExpression(callExpr->getArg(argIndex));
            }
        }
    }

    void MSLVisitor::recordAtomicTargetExpression(const clang::Expr *expr)
    {
        const clang::Expr *targetExpr = stripAtomicTargetWrappers(expr);
        if (targetExpr == nullptr)
        {
            return;
        }

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(targetExpr))
        {
            if (const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl()))
            {
                if (makeAtomicScalarTypeName(fieldDecl->getType()).has_value())
                {
                    mAtomicPlainFields.insert(fieldDecl->getCanonicalDecl());
                }
            }
            return;
        }

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(targetExpr))
        {
            if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
            {
                // Scalar and array groupshared atomics must be discovered from the atomic target expression before
                // translateVarDecl() chooses the storage type.
                //
                // DSL:
                //   GroupShared<uint> counter;
                //   GroupShared<uint> scratch[1];
                //   atomicStore(counter, 0u);
                //   atomicOr(scratch[0], 1u);
                //
                // MSL:
                //   threadgroup atomic<unsigned int> counter;
                //   threadgroup atomic<unsigned int> scratch[1];
                recordAtomicGroupSharedVariableIfSupported(varDecl);
            }
            return;
        }

        auto recordSubscriptBase = [&](const clang::Expr *baseExpr) {
            recordAtomicStructuredBufferBaseExpression(baseExpr);
            const clang::Expr *normalizedBase = stripAtomicTargetWrappers(baseExpr);
            if (const auto *declRefExpr = llvm::dyn_cast_or_null<clang::DeclRefExpr>(normalizedBase))
            {
                if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()))
                {
                    recordAtomicGroupSharedVariableIfSupported(varDecl);
                }
            }
        };

        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(targetExpr); operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Subscript)
        {
            recordSubscriptBase(operatorCallExpr->getArg(0));
            return;
        }

        if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(targetExpr))
        {
            recordSubscriptBase(arraySubscriptExpr->getBase());
        }
    }

    // Preserve the existing visible function signature and splice hidden Metal-only
    // parameters at the end when transitive wave analysis says they are required.
    std::string MSLVisitor::generateFunctionSignatureWithWaveBuiltins(const clang::FunctionDecl *func, const std::string &funcName, const std::string &constSpecifierValue)
    {
        std::string signature = generateFunctionSignature(func, funcName, constSpecifierValue);
        const MSLWaveBuiltinAnalyzer::Requirements requirements = mWaveBuiltinAnalyzer.getRequirements(func);
        if (requirements.any() == false)
        {
            return signature;
        }

        std::vector<std::string> extraParams;
        mWaveBuiltinAnalyzer.appendInjectedParams(extraParams, requirements, false);
        if (extraParams.empty())
        {
            return signature;
        }

        const size_t rightParen = signature.rfind(')');
        if (rightParen == std::string::npos)
        {
            return signature;
        }

        std::string extraParamText = stringJoin(extraParams, ", ");
        if (rightParen > 0 && signature[rightParen - 1] != '(')
        {
            extraParamText = ", " + extraParamText;
        }
        signature.insert(rightParen, extraParamText);
        return signature;
    }

    std::string MSLVisitor::generateFunctionPrototypeWithWaveBuiltins(const clang::FunctionDecl *func)
    {
        if (func == nullptr || func->isImplicit())
        {
            return {};
        }
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
            method != nullptr && !method->isUserProvided() && !checkAttibuteByName(func, mUGLCTORName))
        {
            return {};
        }

        std::string result;
        const bool isUGLCtor = checkAttibuteByName(func, mUGLCTORName);
        const std::string funcName = func->getNameAsString();
        std::vector<std::string> constAddressSpaceOverrideList = {"const ", "const constant", "const device"};
        int constAddressSpaceOverrideListCount = 1;

        if (const auto *cxxMethod = llvm::dyn_cast<clang::CXXMethodDecl>(func); !isUGLCtor && cxxMethod != nullptr && cxxMethod->isConst())
        {
            constAddressSpaceOverrideListCount = 3;
        }

        for (int i = 0; i < constAddressSpaceOverrideListCount; ++i)
        {
            const std::string &addr = constAddressSpaceOverrideList[i];
            std::string signature = generateFunctionSignatureWithWaveBuiltins(func, funcName, addr);
            if (!signature.empty() && signature.back() == '\n')
            {
                signature.pop_back();
            }
            result += mSpaceManager.getSpace() + signature + EOS();
        }
        return result;
    }


    std::string MSLVisitor::generateRecordDefinitionDetailed(const clang::CXXRecordDecl *decl)
    {
        std::string result = "\n";
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string name = generateRecordDefinitionName(decl, &mTypeConvertor);

        result += mSpaceManager.getSpace() + keyword + " " + name + NewLine();
        result += enterScope();

        for (auto *innerDecl : decl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (shouldEmitNestedRecordDefinition(decl, nestedRecord))
                {
                    result += getLineDirective(nestedRecord->getBeginLoc());
                    result += generateRecordDefinition(nestedRecord);
                }
            }
        }


        result += generateRecordDataMembers(decl);

        // Emit member functions after fields so method bodies see the complete record layout.
        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();

        return result;
    }
    std::string MSLVisitor::generateUGLFramebufferClass(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string name = generateRecordDefinitionName(decl, &mTypeConvertor);

        if (auto *templateDecl = decl->getDescribedClassTemplate();
            templateDecl != nullptr && getCurrentTemplateSubstitutionContext() == nullptr)
        {
            result += getLineDirective(templateDecl->getBeginLoc());
            result += generateTemplateParameters(templateDecl->getTemplateParameters());
        }

        result += mSpaceManager.getSpace() + keyword + " " + name + NewLine();
        result += enterScope();

        int fieldCounter = 0;
        for (auto *field : decl->fields())
        {
            std::string attrs = "[[color(" + std::to_string(fieldCounter) + ")]]"; // generateAttributes(field, &mAttriConv);
            auto templateArgs = getTemplateArgumentsFromType(field->getType());

            if (checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName) ||
                checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName))
            {
                if (checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName))
                {
                    const size_t writePatternIndex = 5;
                    if (templateArgs.size() <= writePatternIndex)
                    {
                        continue;
                    }
                    auto writePattern = translateTemplateArgument(templateArgs.at(writePatternIndex));
                    if (writePattern == mUGLDepthStencilAttachmentNoWrite)
                    {
                        continue;
                    }
                    else if (writePattern == mUGLDepthStencilAttachmentWriteLess)
                    {
                        attrs = "[[depth(less)]]";
                    }
                    else if (writePattern == mUGLDepthStencilAttachmentWriteGreater)
                    {
                        attrs = "[[depth(greater)]]";
                    }
                    else
                    {
                        throw std::runtime_error("can not found depth attachment write pattern: " + writePattern);
                    }
                }
                else
                {
                    if (templateArgs.size() == 1)
                    {
                        continue;
                    }
                    const size_t writePatternIndex = 1;
                    if (templateArgs.size() <= writePatternIndex)
                    {
                        continue;
                    }
                    auto writePattern = translateTemplateArgument(templateArgs.at(writePatternIndex));
                    if (writePattern == mUGLDepthStencilAttachmentNoWrite)
                    {
                        continue;
                    }
                    else if (writePattern == mUGLDepthStencilAttachmentWriteLess)
                    {
                        attrs = "[[depth(less)]]";
                    }
                    else if (writePattern == mUGLDepthStencilAttachmentWriteGreater)
                    {
                        attrs = "[[depth(greater)]]";
                    }
                    else
                    {
                        throw std::runtime_error("can not found depth attachment write pattern: " + writePattern);
                    }
                }
            }


            std::string typeName = generateTypeCanonicalName(field->getType(), &mTypeConvertor);
            if (isAtomicPlainField(field))
            {
                if (const auto atomicScalarTypeName = makeAtomicScalarTypeName(field->getType()))
                {
                    typeName = "atomic<" + *atomicScalarTypeName + ">";
                }
            }
            std::string varName = field->getNameAsString();
            result += mSpaceManager.getSpace() + typeName + " " + varName + attrs; // + EOS();
            if (field->hasInClassInitializer() && (isImplicitNode(field->getInClassInitializer()) == false))
            {
                result += " = " + TranslateExpr(field->getInClassInitializer());
            }
            result += EOS();
            fieldCounter++;
        }

        // Emit framebuffer helper methods after attachment fields.
        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                // result += generateFunctionSignature(method, method->getNameAsString());
                // result += generateFunctionBody(method);
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();

        return result;
    }
    std::string MSLVisitor::generateRenderSetBufferComponentMethods(const std::string &varName,
                                                                    const std::string &returnTypeName,
                                                                    const std::string &accessBoundExpr,
                                                                    const std::string &addressQualifier)
    {
        const std::string addressSuffix = addressQualifier.empty() ? "" : " " + addressQualifier;
        std::string result;

        result += mSpaceManager.getSpace() + returnTypeName + " " + varName + "get(uint renderEntityID, uint renderEntityInstanceID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundExpr + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_component_count_nz = max(__uglc_bounds.x, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_element_count_nz = max(__uglc_bounds.y, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(renderEntityID, __uglc_component_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_raw_base = " + varName + "ComponentList[__uglc_safe_entity]" + EOS();
        result += mSpaceManager.getSpace() + "const bool __uglc_base_valid = renderEntityID < __uglc_bounds.x && __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_base = __uglc_base_valid ? __uglc_raw_base : 0u" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_base_clamped = min(__uglc_safe_base, __uglc_element_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_physical_remaining = (__uglc_element_count_nz - 1u) - __uglc_safe_base_clamped" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_offset = min(renderEntityInstanceID, __uglc_physical_remaining)" + EOS();
        result += mSpaceManager.getSpace() + "return " + varName + "[__uglc_safe_base_clamped + __uglc_safe_offset]" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + returnTypeName + " " + varName + "getRaw(uint index) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundExpr + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_element_count_nz = max(__uglc_bounds.y, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_index = min(index, __uglc_element_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "return " + varName + "[__uglc_safe_index]" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "bool " + varName + "checkValid(uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundExpr + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_component_count_nz = max(__uglc_bounds.x, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(renderEntityID, __uglc_component_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_raw_base = " + varName + "ComponentList[__uglc_safe_entity]" + EOS();
        result += mSpaceManager.getSpace() + "return renderEntityID < __uglc_bounds.x && __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y" + EOS();
        result += quitScope();
        return result;
    }

    std::string MSLVisitor::generateRenderSetTextureComponentMethods(const std::string &varName,
                                                                     const std::string &returnTypeName,
                                                                     const std::string &accessBoundExpr,
                                                                     const std::string &addressQualifier)
    {
        const std::string addressSuffix = addressQualifier.empty() ? "" : " " + addressQualifier;
        std::string result;

        result += mSpaceManager.getSpace() + returnTypeName + " " + varName + "get(uint renderEntityID, uint renderEntityInstanceID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundExpr + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_slot = min(renderEntityInstanceID, " + std::to_string(RenderTextureMaxTextureCountPerEntity - 1u) + "u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_raw_component_index = (renderEntityID*" + std::to_string(RenderTextureMaxTextureCountPerEntity) + "u) + __uglc_safe_slot" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_raw_descriptor = (__uglc_raw_component_index < __uglc_bounds.x) ? " + varName + "ComponentList[__uglc_raw_component_index] : 0u" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_descriptor = (__uglc_raw_descriptor != 4294967295u && __uglc_raw_descriptor < __uglc_bounds.y) ? __uglc_raw_descriptor : 0u" + EOS();
        result += mSpaceManager.getSpace() + "return " + varName + "[__uglc_safe_descriptor].texture" + EOS();
        result += quitScope();
        return result;
    }

    std::string MSLVisitor::generateRenderSetEntityMethods(const std::string &addressQualifier)
    {
        const std::string addressSuffix = addressQualifier.empty() ? "" : " " + addressQualifier;
        std::string result;

        result += mSpaceManager.getSpace() + "UGL_RenderEntityInfo_ UGLLoadRenderEntityInfoSafe(uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_header = " + mUGLRenderSetAccessBoundDataVariableName + "[0u]" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_entity_count_nz = max(__uglc_header.x, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(renderEntityID, __uglc_entity_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "return " + mUGLRenderSetRenderEntityInfoVariableName + "[__uglc_safe_entity]" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "uint2 UGLLoadRenderEntityCMDParamsSafe(uint cmdIndex) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_header = " + mUGLRenderSetAccessBoundDataVariableName + "[0u]" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_cmd_count_nz = max(__uglc_header.y, 1u)" + EOS();
        result += mSpaceManager.getSpace() + "const uint __uglc_safe_cmd_index = min(cmdIndex, __uglc_cmd_count_nz - 1u)" + EOS();
        result += mSpaceManager.getSpace() + "return " + mUGLRenderSetRenderEntityCMDParamsVariableName + "[__uglc_safe_cmd_index]" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "void getRenderEntityInfo(uint renderEntityID, thread uint &indexCount, thread uint& instanceCount, thread uint &firstIndex, thread int &vertexOffset, thread uint &globalInstanceBase) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ info = UGLLoadRenderEntityInfoSafe(renderEntityID)" + EOS();
        result += mSpaceManager.getSpace() + "indexCount = info.indexCount" + EOS();
        result += mSpaceManager.getSpace() + "instanceCount = info.instanceCount" + EOS();
        result += mSpaceManager.getSpace() + "firstIndex = info.firstIndex" + EOS();
        result += mSpaceManager.getSpace() + "vertexOffset = info.vertexOffset" + EOS();
        result += mSpaceManager.getSpace() + "globalInstanceBase = info.globalInstanceBase" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "uint getRenderEntityIndexCount(uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ info = UGLLoadRenderEntityInfoSafe(renderEntityID)" + EOS();
        result += mSpaceManager.getSpace() + "return info.indexCount" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "uint getRenderEntityVersion(uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ info = UGLLoadRenderEntityInfoSafe(renderEntityID)" + EOS();
        result += mSpaceManager.getSpace() + "return info.entityVersion" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "uint getRenderEntityGlobalInstanceBase(uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ info = UGLLoadRenderEntityInfoSafe(renderEntityID)" + EOS();
        result += mSpaceManager.getSpace() + "return info.globalInstanceBase" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "void getRenderEntityCMDParams(thread uint cmdIndex, thread uint& renderEntityID, thread uint& instanceID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "uint2 res = UGLLoadRenderEntityCMDParamsSafe(cmdIndex)" + EOS();
        result += mSpaceManager.getSpace() + "renderEntityID = res.x" + EOS();
        result += mSpaceManager.getSpace() + "instanceID = res.y" + EOS();
        result += quitScope();

        result += mSpaceManager.getSpace() + "bool checkValid(thread uint renderEntityID) const" + addressSuffix + NewLine();
        result += enterScope();
        result += mSpaceManager.getSpace() + "const uint2 __uglc_header = " + mUGLRenderSetAccessBoundDataVariableName + "[0u]" + EOS();
        result += mSpaceManager.getSpace() + "const bool __uglc_entity_in_range = renderEntityID < __uglc_header.x" + EOS();
        result += mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ info = UGLLoadRenderEntityInfoSafe(renderEntityID)" + EOS();
        result += mSpaceManager.getSpace() + "return __uglc_entity_in_range && info.instanceCount > 0u && info.indexCount > 0u && info.instanceCount != 0xffffffffu && info.indexCount != 0xffffffffu" + EOS();
        result += quitScope();

        return result;
    }

    std::string MSLVisitor::generateUGLRenderSetClass(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string name = generateRecordDefinitionName(decl, &mTypeConvertor);

        if (auto *templateDecl = decl->getDescribedClassTemplate())
        {
            result += getLineDirective(templateDecl->getBeginLoc());
            result += generateTemplateParameters(templateDecl->getTemplateParameters());
        }

        result += mSpaceManager.getSpace() + keyword + " " + name + NewLine();
        result += enterScope();

        int fieldCounter = 0;
        result += mSpaceManager.getSpace() + "device uint2* " + mUGLRenderSetAccessBoundDataVariableName + " [[id(" + std::to_string(fieldCounter) + ")]]" + EOS();
        fieldCounter++;
        for (auto *field : decl->fields())
        {

            std::string varName = field->getNameAsString();
            auto templateArgs = getTemplateArgumentsFromType(field->getType());
            if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
            {
                throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                         + "\" requires a concrete component element type for MSL code generation.");
            }
            std::string templateTypeName = translateTemplateArgument(templateArgs.front(), &mTypeConvertor);

            std::string typeName = generateTypeCanonicalName(field->getType());
            result += mSpaceManager.getSpace() + "device uint* " + varName + mUGLRenderSetComponentListVariableNamePostfix + " [[id(" + std::to_string(fieldCounter) + ")]]";
            result += EOS();
            fieldCounter++;
            if (typeName.starts_with("UGL::BufferC"))
            {
                typeName = "device " + templateTypeName + "*";
            }
            else if (typeName.starts_with("UGL::TextureC"))
            {
                if (templateArgs.size() < 2)
                {
                    throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                             + "\" requires TextureComponent<ElementType, MaxResourceCount> for MSL code generation.");
                }
                const int maxResourceCount = static_cast<int>(getIntValueFromTemplateArgument(templateArgs.at(1)));
                if (maxResourceCount <= 0)
                {
                    throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                             + "\" resolves an invalid MaxResourceCount = " + std::to_string(maxResourceCount)
                                             + " for MSL code generation. MaxResourceCount must be greater than 0.");
                }
                std::string UGLTextureWrapperName = getBindlessTextureWrapperFromTemplateType(false, templateTypeName);
                typeName = "device " + UGLTextureWrapperName + "*";
            }
            else
            {
                throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                         + "\" uses unsupported component type \"" + generateTypeCanonicalName(field->getType())
                                         + "\" for MSL code generation.");
            }
            result += mSpaceManager.getSpace() + typeName + " " + varName + "[[id(" + std::to_string(fieldCounter) + ")]]";
            result += EOS();
            fieldCounter++;
        }
        {
            result += mSpaceManager.getSpace() + "device UGL_RenderEntityInfo_ *" + mUGLRenderSetRenderEntityInfoVariableName + " [[id(" + std::to_string(fieldCounter) + ")]]" + EOS();
            fieldCounter++;
            result += mSpaceManager.getSpace() + "device uint2 *" + mUGLRenderSetRenderEntityCMDParamsVariableName + " [[id(" + std::to_string(fieldCounter) + ")]]" + EOS();
        }
        size_t renderSetFieldIndex = 0u;
        for (auto *field : decl->fields())
        {
            auto templateArgs = getTemplateArgumentsFromType(field->getType());
            if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
            {
                throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                         + "\" requires a concrete component element type for MSL code generation.");
            }
            std::string templateTypeName = translateTemplateArgument(templateArgs.front(), &mTypeConvertor);

            std::string typeName = generateTypeCanonicalName(field->getType());
            std::string returnTypeName;
            std::string varName = field->getNameAsString();

            if (typeName.starts_with("UGL::BufferC"))
            {
                returnTypeName = templateTypeName;
                const std::string accessBoundExpr = mUGLRenderSetAccessBoundDataVariableName + "[" + std::to_string(renderSetFieldIndex + 1u) + "u]";
                result += generateRenderSetBufferComponentMethods(varName, returnTypeName, accessBoundExpr, "constant");
                result += generateRenderSetBufferComponentMethods(varName, returnTypeName, accessBoundExpr, "device");
                result += generateRenderSetBufferComponentMethods(varName, returnTypeName, accessBoundExpr, "");
            }
            else if (typeName.starts_with("UGL::TextureC"))
            {
                if (templateArgs.size() < 2)
                {
                    throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                             + "\" requires TextureComponent<ElementType, MaxResourceCount> for MSL code generation.");
                }
                const int maxResourceCount = static_cast<int>(getIntValueFromTemplateArgument(templateArgs.at(1)));
                if (maxResourceCount <= 0)
                {
                    throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                             + "\" resolves an invalid MaxResourceCount = " + std::to_string(maxResourceCount)
                                             + " for MSL code generation. MaxResourceCount must be greater than 0.");
                }
                returnTypeName = MakeFormatToVectorTypeForTexture(templateTypeName);
                returnTypeName = "texture2d<" + returnTypeName + ">";
                const std::string accessBoundExpr = mUGLRenderSetAccessBoundDataVariableName + "[" + std::to_string(renderSetFieldIndex + 1u) + "u]";
                result += generateRenderSetTextureComponentMethods(varName, returnTypeName, accessBoundExpr, "");
                result += generateRenderSetTextureComponentMethods(varName, returnTypeName, accessBoundExpr, "constant");
                result += generateRenderSetTextureComponentMethods(varName, returnTypeName, accessBoundExpr, "device");
            }
            else
            {
                throw std::runtime_error("RenderSet \"" + name + "\" field \"" + field->getNameAsString()
                                         + "\" uses unsupported component type \"" + generateTypeCanonicalName(field->getType())
                                         + "\" for MSL code generation.");
            }
            renderSetFieldIndex++;
        }
        {
            result += generateRenderSetEntityMethods("constant");
            result += generateRenderSetEntityMethods("device");
            result += generateRenderSetEntityMethods("");
        }

        result += endClass();

        return result;
    }
    std::string MSLVisitor::generateUGLBindGroupClass(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string name = generateRecordDefinitionName(decl, &mTypeConvertor);
        const clang::FunctionDecl *createFunc = getMethodFromClass(decl, mUGLCTORFunctionName, makeCreateMethodLookupOptions());
        const auto baseBindings = resolveBaseShaderResourceBindings(decl, createFunc);


        if (auto *templateDecl = decl->getDescribedClassTemplate())
        {
            result += getLineDirective(templateDecl->getBeginLoc());
            result += generateTemplateParameters(templateDecl->getTemplateParameters());
        }

        result += mSpaceManager.getSpace() + keyword + " " + name + NewLine();
        result += enterScope();

        for (const auto &resourceBinding : baseBindings)
        {
            const auto *field = resourceBinding.fieldDecl;
            const std::string addrSpace = getShaderResourceAddressSpaceQualifier(field->getType());
            std::string realAddrSpace = addrSpace;
            std::string attrs = " [[id(" + std::to_string(resourceBinding.bindingIndex) + ")]]";
            std::string typeName = generateTypeCanonicalName(field->getType(), &mTypeConvertor);
            if (const auto atomicPointerTypeName = makeAtomicStructuredBufferPointerTypeName(field))
            {
                typeName = *atomicPointerTypeName;
            }


            /* if (allowConstant == false && addrSpace == "constant")
            {
                std::erase(typeName, '*');
            } */

            std::string varName = field->getNameAsString();


            result += mSpaceManager.getSpace() + realAddrSpace + " " + typeName + " " + varName + attrs;
            result += EOS();
        }


        result += endClass();

        return result;
    }
    void MSLVisitor::registerRenderVaryingRecordDecl(const clang::CXXRecordDecl *recordDecl)
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        mRenderVaryingRecordDecls.insert(recordDecl->getCanonicalDecl());
    }
    bool MSLVisitor::isRenderVaryingRecordDecl(const clang::CXXRecordDecl *recordDecl) const
    {
        if (recordDecl == nullptr)
        {
            return false;
        }

        return mRenderVaryingRecordDecls.find(recordDecl->getCanonicalDecl()) != mRenderVaryingRecordDecls.end();
    }
    void MSLVisitor::collectRenderVaryingRecordDecls(const clang::CXXRecordDecl *shaderClassDecl, const clang::FunctionDecl *entryFunction)
    {
        mRenderVaryingRecordDecls.clear();
        if (shaderClassDecl == nullptr || entryFunction == nullptr || (!checkDerivedClassByName(shaderClassDecl, mUGLRenderClassBaseName) && !checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName)))
        {
            return;
        }

        auto isRenderEntryRecordStageInputParam = [&](const clang::ParmVarDecl *param) -> bool
        {
            if (param == nullptr)
            {
                return false;
            }

            return !checkAttibuteByName(param, mUGLAttributeVertexIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeInstanceIDName) &&
                   !checkAttibuteByName(param, mUGLAttributePrimitiveIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeBarycentricsName) &&
                   !checkAttibuteByName(param, mUGLAttributePixelCoordName) &&
                   !checkAttibuteByName(param, mUGLAttributeDispatchThreadIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeGroupThreadIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeGroupIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeGroupIndexName) &&
                   !checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName) &&
                   !checkAttibuteByName(param, mUGLAttributeDomainLocationName);
        };

        const std::string entryFunctionName = entryFunction->getNameAsString();
        if (entryFunctionName == mUGLVertexShaderFunctionName || entryFunctionName == mUGLDomainShaderFunctionName)
        {
            const auto *outputRecordDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(entryFunction->getReturnType()));
            if (outputRecordDecl != nullptr && !checkDerivedClassByName(outputRecordDecl, mUGLFrameBufferBaseName))
            {
                registerRenderVaryingRecordDecl(outputRecordDecl);
            }
        }

        if (entryFunctionName == mUGLFragmentShaderFunctionName)
        {
            for (unsigned i = 0; i < entryFunction->getNumParams(); ++i)
            {
                const auto *param = entryFunction->getParamDecl(i);
                if (!isRenderEntryRecordStageInputParam(param))
                {
                    continue;
                }

                const auto *inputRecordDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(param->getType()));
                if (inputRecordDecl != nullptr && !checkDerivedClassByName(inputRecordDecl, mUGLFrameBufferBaseName))
                {
                    registerRenderVaryingRecordDecl(inputRecordDecl);
                }
            }
        }
    }
    std::string MSLVisitor::getRenderVaryingFieldAttributes(const clang::FieldDecl *field) const
    {
        if (field == nullptr)
        {
            return {};
        }

        std::string result;
        for (const auto *attr : getAllAttributes(field))
        {
            const std::string rawAttribute = const_cast<MSLVisitor *>(this)->generateRawAttribute(attr);
            if (isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                const int location = getIndexedAttributeNumber(rawAttribute, mUGLAttributeAttributeName);
                if (location >= 0)
                {
                    result += makeMSLUserVaryingAttribute(location);
                }
                continue;
            }

            result += const_cast<MSLVisitor *>(this)->generateAttribute(attr, &mAttriConv);
        }
        return result;
    }
    std::string MSLVisitor::translateBinaryOperator(const clang::BinaryOperator *E)
    {
        if (E->isAssignmentOp() && isAtomicBackedLValue(E->getLHS()))
        {
            if (E->getOpcode() == clang::BO_Assign)
            {
                return "atomicStore(" + translateAtomicBackedLValueRaw(E->getLHS()) + ", " + TranslateExpr(E->getRHS()) + ")";
            }

            const auto operationSpelling = getCompoundAssignmentOperationSpelling(E->getOpcode());
            if (!operationSpelling.has_value())
            {
                throwCodegenError(E, "MSL atomic-backed ordinary assignment uses an unsupported compound assignment operator.");
            }
            return translateAtomicBackedCompoundAssignment(E, *operationSpelling);
        }

        return BaseASTVisitor::translateBinaryOperator(E);
    }

    std::string MSLVisitor::translateUnaryOperator(const clang::UnaryOperator *E)
    {
        if (!mTranslateAtomicLValueRaw && isAtomicBackedLValue(E->getSubExpr()))
        {
            switch (E->getOpcode())
            {
            case clang::UO_PreInc:
            case clang::UO_PostInc:
                return translateAtomicBackedIncrement(E, "+");
            case clang::UO_PreDec:
            case clang::UO_PostDec:
                return translateAtomicBackedIncrement(E, "-");
            case clang::UO_AddrOf:
                throwCodegenError(E, "MSL atomic-backed ordinary address-of is not supported. Use an explicit atomic builtin or avoid taking the address of inferred atomic storage.");
            default:
                break;
            }
        }

        return BaseASTVisitor::translateUnaryOperator(E);
    }

    std::string MSLVisitor::translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E)
    {
        if (E->getNumArgs() >= 2 && isAtomicBackedLValue(E->getArg(0)))
        {
            const clang::OverloadedOperatorKind operationKind = E->getOperator();
            if (operationKind == clang::OO_Equal)
            {
                return "atomicStore(" + translateAtomicBackedLValueRaw(E->getArg(0)) + ", " + TranslateExpr(E->getArg(1)) + ")";
            }

            const auto operationSpelling = getCompoundAssignmentOperationSpelling(operationKind);
            if (operationSpelling.has_value())
            {
                return translateAtomicBackedCompoundAssignment(E->getArg(0), E->getArg(1), E->getExprLoc(), *operationSpelling);
            }
        }

        return BaseASTVisitor::translateCXXOperatorCallExprBinaryOp(E);
    }

    std::string MSLVisitor::translateMemberExpr(const clang::MemberExpr *E)
    {
        if (!mTranslateAtomicLValueRaw && isAtomicBackedLValue(E))
        {
            return translateAtomicBackedLValueLoad(E);
        }

        clang::Expr *baseExpr = E->getBase();
        std::string memStr = E->getMemberNameInfo().getName().getAsString();
        // Direct member access inside a method is emitted without an explicit this pointer.
        if (E->isImplicitAccess())
        {
            return memStr;
        }

        // User-written this-> access is rejected unless an explicit backend path enables it.
        if (const auto *thisExpr = tryGetCXXThisExpr(baseExpr))
        {
            if (thisExpr->isImplicit() || !isUserWrittenThisExpr(thisExpr))
            {
                return memStr;
            }
            if (!allowExplicitThisPointerAccess())
            {
                throwExplicitThisPointerAccessError(thisExpr);
            }
        }

        std::string baseStr = TranslateExpr(E->getBase());

        std::string accessOperator = E->isArrow() ? "->" : ".";
        auto baseDecl = getUnqualifiedType(E->getBase()->getType())->getAsCXXRecordDecl();
        auto baseDeclName = getClassCanonicalName(baseDecl, nullptr);

        if (baseDecl && isTextureAccessPackerType(baseDeclName))
        {
            accessOperator = ".";
            return baseStr + accessOperator + memStr;
        }
        if (baseDecl && (isRenderSetBufferComponentType(baseDeclName) || isRenderSetTextureComponentType(baseDeclName)))
        {
            return baseStr + memStr;
        }
        if (!baseStr.empty() && (baseStr.back() == '.' || (baseStr.length() > 1 && baseStr.substr(baseStr.length() - 2) == "->")))
        {

            return baseStr + memStr;
        }

        return baseStr + accessOperator + memStr;
    }

    std::string MSLVisitor::translateDeclRefExpr(const clang::DeclRefExpr *E)
    {
        if (const auto value = tryTranslateTemplateSubstitutedDeclRefExpr(E))
        {
            return *value;
        }

        if (!mTranslateAtomicLValueRaw && isAtomicBackedLValue(E))
        {
            return translateAtomicBackedLValueLoad(E);
        }

        return BaseASTVisitor::translateDeclRefExpr(E);
    }

    std::string MSLVisitor::getShaderResourceAddressSpaceQualifier(const clang::QualType &qt) const
    {
        const clang::CXXRecordDecl *recordDecl = getUnqualifiedType(qt)->getAsCXXRecordDecl();
        if (recordDecl == nullptr)
        {
            return "thread";
        }

        return std::string(getMSLAddressSpaceQualifierForCanonicalType(getClassCanonicalName(recordDecl)));
    }
    std::string MSLVisitor::generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor)
    {
        std::string constSpecificer;
        std::string refSpecificer;
        std::string arraySpecifiers = generateDeclArraySpecifier(param->getOriginalType());
        std::string pointerSpecifier = arraySpecifiers.empty() ? "" : "*";
        std::string addressSpace = getShaderResourceAddressSpaceQualifier(param->getType());
        std::string attrs = generateAttributes(param, nullptr);
        const bool isInParam = checkAttibuteByName(param, mUGLAttributeINName);
        const bool isOutParam = checkAttibuteByName(param, mUGLAttributeOUTName);
        const bool isInOutParam = checkAttibuteByName(param, mUGLAttributeINOUTName);
        const clang::QualType parameterType = getUnqualifiedType(param->getType());
        const clang::CXXRecordDecl *recordDecl = parameterType->getAsCXXRecordDecl();
        const std::string recordCanonicalName = getClassCanonicalName(recordDecl);
        if (isBindGroupHandleType(param->getType()))
        {
            if (isOutParam || isInOutParam)
            {
                throw std::runtime_error("MSL shader helper parameter \"" + param->getNameAsString()
                                         + "\" uses UGL::BindGroup<T> with [[OUT]] or [[INOUT]], but bind groups are read-only argument-buffer handles and must be passed by input only.");
            }

            const std::optional<std::string> pointeeTypeName = makeBindGroupPointerPointeeTypeName(*this, param->getType(), typeConvertor);
            if (!pointeeTypeName.has_value())
            {
                throw std::runtime_error("MSL shader helper parameter \"" + param->getNameAsString()
                                         + "\" uses UGL::BindGroup<T>, but the concrete bind-group type could not be resolved.");
            }
            return "const constant " + *pointeeTypeName + "* " + param->getNameAsString();
        }
        if (const auto *renderSetTypeDecl = tryGetRenderSetTypeDeclFromType(param->getType(), *this))
        {
            if (!isInParam || isOutParam || isInOutParam)
            {
                throw std::runtime_error("MSL shader helper parameter \"" + param->getNameAsString()
                                         + "\" uses UGL::RenderSet<T> without an explicit [[IN]] contract. RenderSet helper parameters must be declared as [[IN]] UGL::RenderSet<T> and cannot use [[OUT]] or [[INOUT]].");
            }

            return "const constant " + getClassCanonicalName(renderSetTypeDecl, &mTypeConvertor) + "* " + param->getNameAsString();
        }
        validateInputOnlyShaderHandleParameterOrThrow(param, "MSL");
        if (isInParam)
        {
            constSpecificer = "const ";
            attrs = "";
        }
        if (isInParam || isOutParam || isInOutParam)
        {
            refSpecificer = "&";
            attrs = "";
        }
        else if (pointerSpecifier.empty() && addressSpace == "thread")
        {
            addressSpace.clear();
        }

        if (pointerSpecifier.empty() == false)
        {
            refSpecificer.clear();
        }

        if (recordDecl != nullptr && isTextureOrSamplerCanonicalName(recordCanonicalName))
        {
            refSpecificer.clear();
            constSpecificer.clear();
        }

        std::string typeName = generateTypeCanonicalName(param->getType(), typeConvertor);
        if (const auto atomicPointerTypeName = makeAtomicStructuredBufferPointerTypeName(param))
        {
            typeName = *atomicPointerTypeName;
        }

        return constSpecificer + addressSpace + " " + typeName + pointerSpecifier + refSpecificer + " " + param->getNameAsString() + attrs;
    }
    std::string MSLVisitor::generateFunctionDefinition(const clang::FunctionDecl *func)
    {
        auto diagnosticScope = scopeDiagnosticLocation(func);
        try
        {
            std::string result;
            if (func == nullptr || func->isImplicit())
            {
                return result;
            }
            if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
                method != nullptr && !method->isUserProvided() && !checkAttibuteByName(func, mUGLCTORName))
            {
                return result;
            }

            std::string attrs = generateAttributes(func, &this->mAttriConv);

            result += NewLine() + mSpaceManager.getSpace() + attrs + NewLine();
            std::string funcName = func->getNameAsString();

            const bool IsUGLCtor = checkAttibuteByName(func, mUGLCTORName);

            std::vector<std::string> constAddressSpaceOverrideList = {"const ", "const constant", "const device"};
            int constAddressSpaceOverrideListCount = 1;

            if (auto *cxxMethod = llvm::dyn_cast<clang::CXXMethodDecl>(func); IsUGLCtor == false && cxxMethod)
            {
                if (cxxMethod->isConst())
                {
                    constAddressSpaceOverrideListCount = 3;
                }
            }

            for (int i = 0; i < constAddressSpaceOverrideListCount; i++)
            {
                const auto &addr = constAddressSpaceOverrideList[i];
                result += generateFunctionSignatureWithWaveBuiltins(func, funcName, addr);
                // Body lowering may rewrite global wave queries and helper calls, so it
                // needs to know exactly which function signature is active right now.
                const clang::FunctionDecl *previousCodegenFunction = mCurrentCodegenFunction;
                mCurrentCodegenFunction = func->getCanonicalDecl();
                if (IsUGLCtor)
                {
                    result += generateUGLCTORFunctionBody(func);
                }
                else
                {
                    result += generateFunctionBody(func);
                }
                mCurrentCodegenFunction = previousCodegenFunction;
            }
            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, func);
        }
    }
    std::string MSLVisitor::generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor)
    {
        (void)typeConvertor;
        std::string result;
        std::string attrs = generateAttributes(decl, &mAttriConv);
        const auto *parentRecordDecl = decl != nullptr ? llvm::dyn_cast<clang::CXXRecordDecl>(decl->getParent()) : nullptr;
        if (isRenderVaryingRecordDecl(parentRecordDecl))
        {
            attrs = getRenderVaryingFieldAttributes(decl);
        }
        std::string addressSpace; // = getShaderResourceAddressSpaceQualifier(decl->getType());

        std::string typeName;
        if (const std::optional<std::string> pointeeTypeName = makeBindGroupPointerPointeeTypeName(*this, decl->getType(), &mTypeConvertor))
        {
            typeName = "const constant " + *pointeeTypeName + "*";
        }
        else
        {
            typeName = generateTypeCanonicalName(decl->getType(), &mTypeConvertor);
        }
        if (isAtomicPlainField(decl))
        {
            if (const auto atomicScalarTypeName = makeAtomicScalarTypeName(decl->getType()))
            {
                typeName = "atomic<" + *atomicScalarTypeName + ">";
            }
        }

        std::string varName = decl->getNameAsString();
        result += getLineDirective(decl->getBeginLoc());
        result += mSpaceManager.getSpace() + addressSpace + " " + typeName + " " + varName + generateDeclArraySpecifier(decl->getType()) + attrs; // + EOS();
        if (decl->hasInClassInitializer() && (isImplicitNode(decl->getInClassInitializer()) == false))
        {
            result += " = " + TranslateExpr(decl->getInClassInitializer());
        }
        result += EOS();
        return result;
    }

    void MSLVisitor::validateBindGroupBufferBindings(const clang::CXXRecordDecl *shaderClassDecl) const
    {
        if (shaderClassDecl == nullptr)
        {
            return;
        }

        std::string ownerKind = "ShaderClass";
        if (checkDerivedClassByName(shaderClassDecl, mUGLComputeClassBaseName))
        {
            ownerKind = "ComputeClass";
        }
        else if (checkDerivedClassByName(shaderClassDecl, mUGLRenderClassBaseName) || checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName))
        {
            ownerKind = "RenderClass";
        }

        validateBindGroupInfoMapAgainstBackendOrThrow(getMSLShaderBackendCapabilities(),
                                                      ownerKind,
                                                      shaderClassDecl,
                                                      mBindGroupInfoMap,
                                                      mBindgroupOffset);
    }
    int MSLVisitor::getValidatedVertexInputAttributeLocation(const clang::FieldDecl *field,
                                                             const std::string &renderClassName,
                                                             const std::string &vertexInputTypeName) const
    {
        std::vector<std::string> matchedAttributes;
        for (const auto *attr : getAllAttributes(field))
        {
            const std::string rawAttribute = const_cast<MSLVisitor *>(this)->generateRawAttribute(attr);
            if (isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        if (matchedAttributes.empty())
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires every field of vertex input \""
                                     + vertexInputTypeName + "\" to declare an explicit [[AttributeN]]. Field \""
                                     + field->getNameAsString() + "\" is missing one.");
        }
        if (matchedAttributes.size() > 1)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString()
                                     + "\" in vertex input \"" + vertexInputTypeName
                                     + "\" declares multiple [[AttributeN]] annotations.");
        }

        const int location = getIndexedAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
        if (location < 0)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString()
                                     + "\" in vertex input \"" + vertexInputTypeName
                                     + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
        }

        return location;
    }
    void MSLVisitor::validateVertexInputRecordOrThrow(const clang::CXXRecordDecl *recordDecl,
                                                      const std::string &renderClassName) const
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string vertexInputTypeName = recordDecl->getQualifiedNameAsString().empty()
                                                    ? recordDecl->getNameAsString()
                                                    : recordDecl->getQualifiedNameAsString();
        std::unordered_map<int, const clang::FieldDecl *> locationToFieldMap;
        for (const auto *field : recordDecl->fields())
        {
            const int location = getValidatedVertexInputAttributeLocation(field, renderClassName, vertexInputTypeName);
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" vertex input \"" + vertexInputTypeName
                                         + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \""
                                         + iter->second->getNameAsString() + "\" and field \"" + field->getNameAsString() + "\".");
            }
            locationToFieldMap.emplace(location, field);
        }
    }
    void MSLVisitor::validateRenderVaryingRecordOrThrow(const clang::CXXRecordDecl *recordDecl,
                                                        const std::string &renderClassName,
                                                        const std::string &recordRole,
                                                        bool requirePosition) const
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string recordTypeName = recordDecl->getQualifiedNameAsString().empty()
                                               ? recordDecl->getNameAsString()
                                               : recordDecl->getQualifiedNameAsString();
        std::unordered_map<int, const clang::FieldDecl *> locationToFieldMap;
        const clang::FieldDecl *positionField = nullptr;
        const clang::FieldDecl *primitiveIDField = nullptr;
        const clang::FieldDecl *barycentricsField = nullptr;
        for (const auto *field : recordDecl->fields())
        {
            if (checkAttibuteByName(field, mUGLAttributePositionName))
            {
                if (positionField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName
                                             + "\" declares duplicate [[" + mUGLAttributePositionName + "]] fields \""
                                             + positionField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                positionField = field;
                continue;
            }
            if (checkAttibuteByName(field, mUGLAttributePrimitiveIDName))
            {
                if (primitiveIDField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName
                                             + "\" declares duplicate [[" + mUGLAttributePrimitiveIDName + "]] fields \""
                                             + primitiveIDField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                primitiveIDField = field;
                continue;
            }
            if (checkAttibuteByName(field, mUGLAttributeBarycentricsName))
            {
                if (barycentricsField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName
                                             + "\" declares duplicate [[" + mUGLAttributeBarycentricsName + "]] fields \""
                                             + barycentricsField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                barycentricsField = field;
                continue;
            }

            std::vector<std::string> matchedAttributes;
            for (const auto *attr : getAllAttributes(field))
            {
                const std::string rawAttribute = const_cast<MSLVisitor *>(this)->generateRawAttribute(attr);
                if (isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
                {
                    matchedAttributes.emplace_back(rawAttribute);
                }
            }

            if (matchedAttributes.empty())
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires every non-system field of "
                                         + recordRole + " \"" + recordTypeName
                                         + "\" to declare an explicit [[AttributeN]]. Field \"" + field->getNameAsString() + "\" is missing one.");
            }
            if (matchedAttributes.size() > 1)
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString()
                                         + "\" in " + recordRole + " \"" + recordTypeName + "\" declares multiple [[AttributeN]] annotations.");
            }

            const int location = getIndexedAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
            if (location < 0)
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString()
                                         + "\" in " + recordRole + " \"" + recordTypeName
                                         + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
            }
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName
                                         + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \""
                                         + iter->second->getNameAsString() + "\" and field \"" + field->getNameAsString() + "\".");
            }
            locationToFieldMap.emplace(location, field);
        }

        if (requirePosition && positionField == nullptr)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires " + recordRole + " \"" + recordTypeName
                                     + "\" to declare exactly one [[" + mUGLAttributePositionName + "]] field.");
        }
    }
    void MSLVisitor::validateShaderEntryBuiltinParametersOrThrow(const clang::FunctionDecl *shaderFunc) const
    {
        if (shaderFunc == nullptr)
        {
            return;
        }

        const std::string functionName = shaderFunc->getNameAsString();
        const bool isVertex = functionName == mUGLVertexShaderFunctionName;
        const bool isFragment = functionName == mUGLFragmentShaderFunctionName || functionName == mUGLPixelShaderFunctionName;
        const bool isCompute = functionName == mUGLComputeShaderFunctionName;
        const bool isDomain = functionName == mUGLDomainShaderFunctionName;
        const std::string functionLabel = shaderFunc->getQualifiedNameAsString();

        for (unsigned i = 0; i < shaderFunc->getNumParams(); ++i)
        {
            const auto *param = shaderFunc->getParamDecl(i);
            auto requireStage = [&](const std::string &attributeName, bool condition, const std::string &allowedStageDescription)
            {
                if (checkAttibuteByName(param, attributeName) && !condition)
                {
                    throw std::runtime_error("Shader entry \"" + functionLabel + "\" parameter \"" + param->getNameAsString()
                                             + "\" uses [[" + attributeName + "]], which is only valid in "
                                             + allowedStageDescription + " shaders.");
                }
            };

            requireStage(mUGLAttributeVertexIDName, isVertex, "vertex");
            requireStage(mUGLAttributeInstanceIDName, isVertex, "vertex");
            requireStage(mUGLAttributeRenderEntityIDName, isVertex, "vertex");
            requireStage(mUGLAttributeRenderEntityInstanceIDName, isVertex, "vertex");
            requireStage(mUGLAttributePrimitiveIDName, isFragment, "fragment");
            requireStage(mUGLAttributeBarycentricsName, isFragment, "fragment");
            requireStage(mUGLAttributePixelCoordName, isFragment, "fragment or pixel-local pixel");
            requireStage(mUGLAttributeSampleIndexName, isFragment, "fragment or pixel-local pixel");
            requireStage(mUGLAttributeDispatchThreadIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupThreadIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupIndexName, isCompute, "compute");
            requireStage(mUGLAttributeDomainLocationName, isDomain, "domain");
        }
    }
    void MSLVisitor::validateRenderEntityBuiltinContractOrThrow(const clang::FunctionDecl *shaderFunc) const
    {
        if (shaderFunc == nullptr)
        {
            return;
        }

        const auto *renderEntityIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityIDName);
        const auto *renderEntityInstanceIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityInstanceIDName);
        if (renderEntityIDVariable == nullptr && renderEntityInstanceIDVariable == nullptr)
        {
            return;
        }

        if (shaderFunc->getNameAsString() != mUGLVertexShaderFunctionName)
        {
            throw std::runtime_error("Shader entry \"" + shaderFunc->getQualifiedNameAsString()
                                     + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [["
                                     + mUGLAttributeRenderEntityInstanceIDName + "]], but these builtins are currently only supported in vertex shaders.");
        }

        const auto renderSetCount = static_cast<size_t>(std::count_if(mBindGroupInfoMap.begin(), mBindGroupInfoMap.end(),
                                                                      [](const auto &entry)
                                                                      {
                                                                          return entry.second.isRenderSet;
                                                                      }));
        if (renderSetCount == 0u)
        {
            throw std::runtime_error("Vertex entry \"" + shaderFunc->getQualifiedNameAsString()
                                     + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [["
                                     + mUGLAttributeRenderEntityInstanceIDName
                                     + "]], but its owning shader class does not bind any UGL::RenderSet<T>.");
        }
        if (renderSetCount != 1u)
        {
            throw std::runtime_error("Vertex entry \"" + shaderFunc->getQualifiedNameAsString()
                                     + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [["
                                     + mUGLAttributeRenderEntityInstanceIDName
                                     + "]], but RenderEntity builtins require exactly one UGL::RenderSet<T> binding because InstanceID must be decoded through that RenderSet's RenderEntityCMDParams.");
        }
    }

    EmittedShaderSource MSLVisitor::generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                                   const BindGroupInfoMap &bindGroupInfoMap,
                                                   const clang::CXXRecordDecl *shaderClassDecl,
                                                   const clang::FunctionDecl *entryFunction,
                                                   const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        auto diagnosticScope = scopeDiagnosticLocation(entryFunction != nullptr ? static_cast<const clang::Decl *>(entryFunction)
                                                                               : static_cast<const clang::Decl *>(shaderClassDecl));
        if (templateSpecialization != nullptr)
        {
            pushTemplateSubstitutionContext(templateSpecialization);
        }
        try
        {
        mBindGroupInfoMap = bindGroupInfoMap;
        mRenderVaryingRecordDecls.clear();
        mMainFunc = entryFunction;
        mBindgroupOffset = 0;
        auto vertexFunction = getMethodFromClass(shaderClassDecl, mUGLVertexShaderFunctionName, makeVertexShaderMethodLookupOptions());
        if (vertexFunction != nullptr)
        {
            if (getParamFromFunctionWithAttribute(vertexFunction, getUGLAttributeVertexInputNameByIndex(0)) != nullptr)
            {
                mBindgroupOffset = getMSLShaderBackendCapabilities().renderVertexInputReservedBufferSlots;
            }
        }
        // Reject unsupported SlotN usage before emitting any shader text so the
        // user gets a deterministic DSL diagnostic instead of a later runtime
        // Metal module compilation failure.
        validateBindGroupBufferBindings(shaderClassDecl);
        for (const auto *def : shaderDefs)
        {
            if (mMainFunc == nullptr)
            {
                if (const auto *funcDef = llvm::dyn_cast<clang::FunctionDecl>(def); funcDef && checkIsShaderFunction(funcDef))
                {
                    mMainFunc = funcDef;
                    break;
                }
            }
        }
        if (mMainFunc == nullptr)
        {
            throw std::runtime_error("Shader class \"" + shaderClassDecl->getQualifiedNameAsString() + "\" does not define a supported shader entry for MSL code generation. Expected one of: vertex, fragment, compute, domain, or hull.");
        }
        validateMSLShaderBufferLayoutsOrThrow(mBindGroupInfoMap);
        collectRenderVaryingRecordDecls(shaderClassDecl, mMainFunc);
        if (shaderClassDecl != nullptr && (checkDerivedClassByName(shaderClassDecl, mUGLRenderClassBaseName) || checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName)))
        {
            const std::string renderClassName = shaderClassDecl->getQualifiedNameAsString();
            const std::string entryFunctionName = mMainFunc->getNameAsString();
            if (entryFunctionName == mUGLVertexShaderFunctionName)
            {
                if (const auto *vertexInputParam = getParamFromFunctionWithAttribute(mMainFunc, getUGLAttributeVertexInputNameByIndex(0)))
                {
                    const auto *vertexInputRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(vertexInputParam->getType()));
                    if (vertexInputRecord == nullptr)
                    {
                        throw std::runtime_error("RenderClass \"" + renderClassName + "\" uses [[VertexInput0]] parameter \""
                                                 + vertexInputParam->getNameAsString() + "\" with non-record type \""
                                                 + const_cast<MSLVisitor *>(this)->generateTypeCanonicalName(vertexInputParam->getType(), &mTypeConvertor) + "\".");
                    }
                    validateVertexInputRecordOrThrow(vertexInputRecord, renderClassName);
                }

                const auto *vertexOutputRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(mMainFunc->getReturnType()));
                if (vertexOutputRecord != nullptr && !checkDerivedClassByName(vertexOutputRecord, mUGLFrameBufferBaseName))
                {
                    validateRenderVaryingRecordOrThrow(vertexOutputRecord, renderClassName, "vertex output", true);
                }
            }
            else if (entryFunctionName == mUGLFragmentShaderFunctionName)
            {
                auto hasExplicitEntrySemantic = [&](const clang::ParmVarDecl *param) -> bool
                {
                    return checkAttibuteByName(param, mUGLAttributeVertexIDName) ||
                           checkAttibuteByName(param, mUGLAttributeInstanceIDName) ||
                           checkAttibuteByName(param, mUGLAttributePrimitiveIDName) ||
                           checkAttibuteByName(param, mUGLAttributeBarycentricsName) ||
                           checkAttibuteByName(param, mUGLAttributePixelCoordName) ||
                           checkAttibuteByName(param, mUGLAttributeDispatchThreadIDName) ||
                           checkAttibuteByName(param, mUGLAttributeGroupThreadIDName) ||
                           checkAttibuteByName(param, mUGLAttributeGroupIDName) ||
                           checkAttibuteByName(param, mUGLAttributeGroupIndexName) ||
                           checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) ||
                           checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName) ||
                           checkAttibuteByName(param, mUGLAttributeDomainLocationName);
                };
                for (unsigned i = 0; i < mMainFunc->getNumParams(); ++i)
                {
                    const auto *param = mMainFunc->getParamDecl(i);
                    if (hasExplicitEntrySemantic(param))
                    {
                        continue;
                    }

                    const auto *fragmentInputRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(param->getType()));
                    if (fragmentInputRecord == nullptr || checkDerivedClassByName(fragmentInputRecord, mUGLFrameBufferBaseName))
                    {
                        continue;
                    }

                    validateRenderVaryingRecordOrThrow(fragmentInputRecord, renderClassName, "fragment input");
                }
            }
        }
        // Precompute wave builtin demand before any helper is emitted so every
        // generated definition sees a stable hidden-parameter contract.
        mCurrentCodegenFunction = nullptr;
        mWaveBuiltinAnalyzer.analyze(shaderDefs, shaderClassDecl, mMainFunc, *this);
        analyzeAtomicResourceUsage(shaderDefs, shaderClassDecl);
        ShaderDeclarationEmissionSections emissionSections;
        const ShaderDeclarationEmissionPlanner declarationPlanner;
        const ShaderDeclarationEmissionPlan emissionPlan = declarationPlanner.build(shaderDefs, mMainFunc);

        for (const clang::CXXRecordDecl *recordDef : emissionPlan.recordForwardDeclarations)
        {
            emissionSections.recordForwardDeclarations += wrapInLexicalNamespaceScopes(recordDef, generateRecordForwardDeclaration(recordDef));
        }
        for (const clang::TypedefNameDecl *typeDef : emissionPlan.typedefs)
        {
            emissionSections.typeAliasDefinitions += wrapInLexicalNamespaceScopes(typeDef, VisitTypedefNameDecl(typeDef));
        }
        for (const clang::Decl *typeOrValueDecl : emissionPlan.recordAndVariableDefinitions)
        {
            if (const auto *recordDef = llvm::dyn_cast<clang::CXXRecordDecl>(typeOrValueDecl))
            {
                if (recordDef->getLexicalDeclContext() == shaderClassDecl)
                {
                    continue;
                }
                emissionSections.recordDefinitions += wrapInLexicalNamespaceScopes(recordDef, generateRecordDefinition(recordDef) + NewLine());
                continue;
            }
            if (const auto *varDef = llvm::dyn_cast<clang::VarDecl>(typeOrValueDecl))
            {
                emissionSections.recordDefinitions += wrapInLexicalNamespaceScopes(varDef, mSpaceManager.getSpace() + translateVarDecl(varDef) + EOS());
            }
        }
        for (const clang::FunctionDecl *funcDef : emissionPlan.helperFunctions)
        {
            emissionSections.helperPrototypes += wrapInLexicalNamespaceScopes(funcDef, generateFunctionPrototypeWithWaveBuiltins(funcDef));
            emissionSections.helperDefinitions += wrapInLexicalNamespaceScopes(funcDef, generateFunctionDefinition(funcDef));
        }
        for (const clang::FunctionTemplateDecl *functionTemplateDecl : emissionPlan.functionTemplates)
        {
            emissionSections.helperDefinitions += wrapInLexicalNamespaceScopes(functionTemplateDecl, generateTemplateFunctionDecl(functionTemplateDecl));
        }
        std::string shaderClassScopeBody;
        {
            for (auto *innerDecl : shaderClassDecl->decls())
            {
                if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
                {
                    if (shouldEmitNestedRecordDefinition(shaderClassDecl, nestedRecord))
                    {
                        shaderClassScopeBody += generateRecordDefinition(nestedRecord);
                    }
                }
            }
        }
        emissionSections.recordDefinitions += wrapInNamespaceScopes(getShaderClassLexicalScopeParts(shaderClassDecl), shaderClassScopeBody);
        emissionSections.entryDefinition = generateMainFunction(mMainFunc);

        std::string result = emissionSections.render();
        EmittedShaderSource emittedSource;
        emittedSource.backend = getMSLShaderBackendCapabilities().kind;
        emittedSource.stage = getShaderStageKindForEntry(mMainFunc);
        emittedSource.backendName = getMSLShaderBackendCapabilities().backendName;
        emittedSource.stageName = getShaderStageDisplayName(emittedSource.stage);
        emittedSource.entryPoint = getGeneratedEntryPointName(mMainFunc);
        emittedSource.debugName = shaderClassDecl->getQualifiedNameAsString() + "::" + (mMainFunc != nullptr ? mMainFunc->getNameAsString() : std::string("unknown"));
        const clang::SourceLocation sourceLocation = mMainFunc != nullptr ? mMainFunc->getLocation() : shaderClassDecl->getLocation();
        emittedSource.sourceName = Context->getSourceManager().getFilename(sourceLocation).str();
        emittedSource.sourceText = std::move(result);
        emittedSource.enableLineDirectives = false;
        if (templateSpecialization != nullptr)
        {
            popTemplateSubstitutionContext();
        }
        return emittedSource;
        }
        catch (const std::exception &error)
        {
            if (templateSpecialization != nullptr)
            {
                popTemplateSubstitutionContext();
            }
            rethrowCodegenError(error, entryFunction != nullptr ? static_cast<const clang::Decl *>(entryFunction)
                                                                : static_cast<const clang::Decl *>(shaderClassDecl));
        }
    }
    bool MSLVisitor::checkIsShaderFunction(const clang::FunctionDecl *shaderFunc) const
    {
        if (mMainFunc != nullptr)
        {
            return shaderFunc == mMainFunc;
        }
        std::string funcName = shaderFunc->getNameAsString();
        if (funcName == VertexFuncNameInRenderClass || funcName == FragmentFuncNameInRenderClass || funcName == PixelFuncNameInPixelLocalRenderClass || funcName == ComputeFuncNameInRenderClass || funcName == mUGLDomainShaderFunctionName || funcName == mUGLHullShaderFunctionName)
        {
            return true;
        }
        return false;
    }

    std::string MSLVisitor::makePixelLocalInputStructName(const clang::CXXRecordDecl *recordDecl) const
    {
        return recordDecl->getNameAsString() + "_PixelLocalInput";
    }

    std::string MSLVisitor::generatePixelLocalInputStructDefinition(const clang::CXXRecordDecl *recordDecl)
    {
        std::string structText;
        structText += mSpaceManager.getSpace() + "struct " + makePixelLocalInputStructName(recordDecl) + NewLine();
        structText += enterScope();
        for (const auto *field : recordDecl->fields())
        {
            structText += mSpaceManager.getSpace() + generateTypeCanonicalName(field->getType(), &mTypeConvertor) + " " + field->getNameAsString() + EOS();
        }
        structText += endClass();
        return structText;
    }

    std::string MSLVisitor::generateMainFunction(const clang::FunctionDecl *shaderFunc)
    {
        if (shaderFunc == nullptr)
        {
            // Keep a second guard here so any future direct call path still fails
            // with a readable compiler diagnostic instead of dereferencing null.
            throw std::runtime_error("MSL code generation cannot emit a shader entry because no entry function was resolved.");
        }
        std::string funcName = shaderFunc->getNameAsString();
        const auto *shaderClassDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(shaderFunc->getDeclContext());
        if (funcName == PixelFuncNameInPixelLocalRenderClass && shaderClassDecl != nullptr && checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName))
        {
            const clang::CXXRecordDecl *renderTargetRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(shaderFunc->getReturnType());
            if (renderTargetRecord != nullptr && checkDerivedClassByName(renderTargetRecord, mUGLFrameBufferBaseName))
            {
                const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis writeAnalysis =
                    PixelLocalFieldAnalysis::collectRenderTargetWriteFieldAnalysis(shaderFunc, renderTargetRecord);
                for (const clang::FieldDecl *field : renderTargetRecord->fields())
                {
                    const clang::CXXRecordDecl *fieldRecord = field->getType()->getAsCXXRecordDecl();
                    const std::string fieldTypeName = getClassCanonicalName(fieldRecord, nullptr);
                    const bool isDepthAttachment = fieldTypeName == mUGLDepthAttachmentName || fieldTypeName == mUGLPixelLocalDepthAttachmentName;
                    const bool writesDepth = writeAnalysis.conservativeAllWrites || writeAnalysis.fields.contains(field->getNameAsString());
                    if (isDepthAttachment && writesDepth)
                    {
                        const bool isPixelLocalDepthAttachment = fieldTypeName == mUGLPixelLocalDepthAttachmentName;
                        throwCodegenError(
                            field,
                            "IPixelLocalRenderClass::pixel() cannot write " + std::string(isPixelLocalDepthAttachment ? "PixelLocalDepthAttachment" : "depth attachment") + " field \"" + field->getNameAsString() + "\". Write native depth from IRenderClass::fragment(), or store depth needed by later pixel-local passes in a PixelLocalColorAttachment field.");
                    }
                }
            }
        }

        std::string shaderSpecifier;
        bool isDomainShader = false;
        if (funcName == VertexFuncNameInRenderClass)
        {
            funcName = VertexShaderEntryName;
            shaderSpecifier = VertexFuncNameInRenderClass;
        }
        else if (funcName == FragmentFuncNameInRenderClass || funcName == PixelFuncNameInPixelLocalRenderClass)
        {
            shaderSpecifier = FragmentFuncNameInRenderClass;
            funcName = FragmentShaderEntryName;
        }
        else if (funcName == ComputeFuncNameInRenderClass)
        {
            shaderSpecifier = "kernel";
            funcName = ComputeShaderEntryName;
        }
        else if (funcName == mUGLDomainShaderFunctionName)
        {
            shaderSpecifier = VertexFuncNameInRenderClass;
            funcName = VertexShaderEntryName;
            isDomainShader = true;
        }
        std::string funcAttr;
        if (isDomainShader)
        {
            const auto attrStr = generateAttributes(shaderFunc);
            if (attrStr.find("TessDomainTriangle") != attrStr.npos)
            {
                funcAttr = "[[patch(triangle, 3)]] ";
            }
            else
            {
                funcAttr = "[[patch(quad, 4)]] ";
            }
        }
        std::string returnType = generateTypeCanonicalName(shaderFunc->getReturnType(), &mTypeConvertor);
        std::string result;
        std::string pixelLocalInputPrelude;
        std::string pixelLocalInputMaterialization;
        std::unordered_set<std::string> emittedPixelLocalInputStructs;
        validateShaderEntryBuiltinParametersOrThrow(shaderFunc);
        validateRenderEntityBuiltinContractOrThrow(shaderFunc);

        auto RenderEntityIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityIDName);
        auto RenderEntityInstanceIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityInstanceIDName);
        auto InstanceIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeInstanceIDName);
        std::string renderEntityRenderSetName;
        if (RenderEntityIDVariable != nullptr || RenderEntityInstanceIDVariable != nullptr)
        {
            for (const auto &[bindingIndex, bindGroupInfo] : mBindGroupInfoMap)
            {
                (void)bindingIndex;
                if (bindGroupInfo.isRenderSet)
                {
                    renderEntityRenderSetName = bindGroupInfo.name;
                    break;
                }
            }
        }
        const MSLWaveBuiltinAnalyzer::Requirements waveBuiltinRequirements = mWaveBuiltinAnalyzer.getRequirements(shaderFunc);
        std::string InstanceIDVariableName;
        std::vector<std::string> mainFunctionParams;
        for (unsigned i = 0; i < shaderFunc->getNumParams(); ++i)
        {

            const clang::ParmVarDecl *param = shaderFunc->getParamDecl(i);
            if (checkAttibuteByName(param, mUGLAttributePixelLocalInputName))
            {
                const PixelLocalInputPlan::ParameterPlan inputPlan = PixelLocalInputPlan::collectParameterPlan(*this, shaderFunc, param);

                const std::string inputStructName = makePixelLocalInputStructName(inputPlan.recordDecl);
                if (emittedPixelLocalInputStructs.insert(inputStructName).second)
                {
                    pixelLocalInputPrelude += generatePixelLocalInputStructDefinition(inputPlan.recordDecl) + NewLine();
                }

                pixelLocalInputMaterialization += mSpaceManager.getSpace() + inputStructName + " " + param->getNameAsString() + EOS();
                for (const PixelLocalInputPlan::AttachmentPlan &inputAttachment : inputPlan.attachments)
                {
                    const auto templateArgs = getTemplateArgumentsFromType(inputAttachment.fieldDecl->getType());
                    if (templateArgs.empty())
                    {
                        throw std::runtime_error("Pixel-local input field \"" + inputAttachment.fieldName + "\" is missing its texture format template argument.");
                    }
                    const std::string typeName = MakeFormatToVectorTypeForFrameBuffer(translateTemplateArgument(templateArgs.front()));
                    const std::string attribute = "[[color(" + std::to_string(inputAttachment.colorAttachmentIndex) + ")]]";
                    const std::string inputName = "_UGLC_PixelLocalInput_" + param->getNameAsString() + "_" + inputAttachment.fieldName;
                    mainFunctionParams.emplace_back(typeName + " " + inputName + " " + attribute);
                    pixelLocalInputMaterialization += mSpaceManager.getSpace() + param->getNameAsString() + "." + inputAttachment.fieldName + " = " + inputName + EOS();
                }
                continue;
            }

            std::string attrs = generateAttributes(param, &mAttriConv);
            if (attrs.empty())
            {
                if (isDomainShader && generateTypeCanonicalName(param->getType()).find("OutputPatch") == std::string::npos)
                {
                    continue;
                }
                if (shaderFunc->getNameAsString() == ComputeFuncNameInRenderClass)
                {
                    throw std::runtime_error("Compute entry \"" + shaderFunc->getQualifiedNameAsString()
                                             + "\" parameter \"" + param->getNameAsString()
                                             + "\" is missing an explicit compute builtin attribute. MSL compute entries do not support implicit [[stage_in]] parameters.");
                }
                const auto *paramRecordDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(param->getType()));
                if (paramRecordDecl == nullptr)
                {
                    throw std::runtime_error("Shader entry \"" + shaderFunc->getQualifiedNameAsString()
                                             + "\" parameter \"" + param->getNameAsString()
                                             + "\" is unannotated and not a record type. Add an explicit stage builtin attribute or pass a structured stage input record.");
                }
                attrs = "[[stage_in]]";
            }
            if (checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) || checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName))
            {
                continue;
            }
            if (checkAttibuteByName(param, mUGLAttributeWaveLaneIndexName) || checkAttibuteByName(param, mUGLAttributeWaveLaneCountName))
            {
                continue;
            }
            mainFunctionParams.emplace_back(generateTypeCanonicalName(param->getType(), &mTypeConvertor) + " " + param->getNameAsString() + attrs);
        }
        // The compute entrypoint is the only place where Metal can materialize the
        // true simdgroup values. Every helper receives them transitively from here.
        mWaveBuiltinAnalyzer.appendInjectedParams(mainFunctionParams, waveBuiltinRequirements, true);
        for (const auto &[index, bindGroupInfo] : mBindGroupInfoMap)
        {
            (void)index;
            mainFunctionParams.emplace_back("const constant " + getClassCanonicalName(bindGroupInfo.typeDecl, &mTypeConvertor) + "* " + bindGroupInfo.name + " [[buffer(" + std::to_string(bindGroupInfo.bindingIndex + mBindgroupOffset) + ")]]");
        }

        if (InstanceIDVariable == nullptr && shaderSpecifier == VertexFuncNameInRenderClass)
        {
            InstanceIDVariableName = "_Backup_InstanceID";
            mainFunctionParams.emplace_back("uint " + InstanceIDVariableName + " [[instance_id]]");
        }
        else if (InstanceIDVariable != nullptr)
        {
            InstanceIDVariableName = InstanceIDVariable->getNameAsString();
        }

        result += pixelLocalInputPrelude;
        result += mSpaceManager.getSpace() + funcAttr + shaderSpecifier + " " + returnType + " " + funcName + "(";
        result += stringJoin(mainFunctionParams, ", ");
        result += ")" + NewLine();
        result += enterScope();
        std::string shaderNamespace = getFullNamespace(shaderFunc);
        if (shaderNamespace.empty() == false)
        {
            result += mSpaceManager.getSpace() + "using namespace " + shaderNamespace + EOS();
        }
        if (RenderEntityIDVariable || RenderEntityInstanceIDVariable)
        {
            result += mSpaceManager.getSpace() + "const uint2 __uglc_render_entity_cmd = " + renderEntityRenderSetName + "->UGLLoadRenderEntityCMDParamsSafe(" + InstanceIDVariableName + ")" + EOS();
        }
        if (RenderEntityIDVariable)
        {
            result += mSpaceManager.getSpace() + "const uint " + RenderEntityIDVariable->getNameAsString() + " = __uglc_render_entity_cmd.x" + EOS();
        }
        if (RenderEntityInstanceIDVariable)
        {
            result += mSpaceManager.getSpace() + "const uint " + RenderEntityInstanceIDVariable->getNameAsString() + " = __uglc_render_entity_cmd.y" + EOS();
        }

        result += pixelLocalInputMaterialization;
        // The main body uses the same lowering path as helpers, so set the same
        // active function context before translating expressions.
        const clang::FunctionDecl *previousCodegenFunction = mCurrentCodegenFunction;
        mCurrentCodegenFunction = shaderFunc->getCanonicalDecl();
        result += generateFunctionBody(shaderFunc);
        mCurrentCodegenFunction = previousCodegenFunction;
        result += quitScope();
        return result;
    }
    std::string MSLVisitor::translateCallExpr(const clang::CallExpr *E)
    {
        if (const auto *funcDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(E->getCalleeDecl()))
        {
            if (const auto translatedBuiltin = mShaderBuiltinTranslator.tryTranslateCallExpr(E, funcDecl, mMainFunc, mCurrentCodegenFunction))
            {
                return *translatedBuiltin;
            }

            if (isUGLAtomicBuiltinName(funcDecl->getNameAsString()) && E->getNumArgs() > 0)
            {
                std::string callee = TranslateExpr(E->getCallee());
                std::string templateArgs = generateTemplateCallArguments(E->getCallee());

                std::vector<std::string> args;
                args.reserve(E->getNumArgs());
                args.emplace_back(translateExprWithAtomicLValueMode(E->getArg(0), true));
                for (unsigned i = 1; i < E->getNumArgs(); ++i)
                {
                    args.emplace_back(TranslateExpr(E->getArg(i)));
                }
                return callee + templateArgs + "(" + stringJoin(args, ", ") + ")";
            }
        }

        std::string callee = TranslateExpr(E->getCallee());
        std::string templateArgs = generateTemplateCallArguments(E->getCallee());

        std::vector<std::string> args;
        args.reserve(E->getNumArgs() + 2);
        for (unsigned i = 0; i < E->getNumArgs(); ++i)
        {
            args.emplace_back(TranslateExpr(E->getArg(i)));
        }
        // Helper calls inherit the caller's already-materialized wave values.
        mWaveBuiltinAnalyzer.appendInjectedArgs(llvm::dyn_cast_or_null<clang::FunctionDecl>(E->getCalleeDecl()), mCurrentCodegenFunction, args);

        return callee + templateArgs + "(" + stringJoin(args, ", ") + ")";
    }
    bool MSLVisitor::checkCXXRecord(const clang::CXXRecordDecl *decl)
    {
        return !recordContainsHostResourceHandles(decl);
    }
    std::string MSLVisitor::generateRecordForwardDeclaration(const clang::CXXRecordDecl *decl)
    {
        if (decl == nullptr || decl->isImplicit())
        {
            return "";
        }
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            pushTemplateSubstitutionContext(specializationDecl);
            const std::string result = generateRecordForwardDeclaration(specializationDecl->getSpecializedTemplate()->getTemplatedDecl());
            popTemplateSubstitutionContext();
            return result;
        }
        if (checkDerivedClassByName(decl, mUGLComputeClassBaseName) || checkDerivedClassByName(decl, mUGLRenderClassBaseName) || checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName))
        {
            return "";
        }
        if (checkCXXRecord(decl) == false)
        {
            return "";
        }

        const std::string name = generateRecordDefinitionName(decl, &mTypeConvertor);
        if (name.empty())
        {
            return "";
        }

        const std::string keyword = decl->isClass() ? "class" : "struct";
        return mSpaceManager.getSpace() + keyword + " " + name + EOS();
    }
    std::string MSLVisitor::generateRecordDefinition(const clang::CXXRecordDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (decl == nullptr || decl->isImplicit())
            {
                return "";
            }
            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
                specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
            {
                pushTemplateSubstitutionContext(specializationDecl);
                const std::string result = generateRecordDefinition(specializationDecl->getSpecializedTemplate()->getTemplatedDecl());
                popTemplateSubstitutionContext();
                return result;
            }
            if (checkDerivedClassByName(decl, mUGLComputeClassBaseName) || checkDerivedClassByName(decl, mUGLRenderClassBaseName) || checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName))
            {
                return "";
            }
            if (checkDerivedClassByName(decl, mUGLFrameBufferBaseName))
            {
                return generateUGLFramebufferClass(decl);
            }
            else if (checkDerivedClassByName(decl, mUGLRenderSetBaseName))
            {
                return generateUGLRenderSetClass(decl);
            }
            else if (checkDerivedClassByName(decl, mUGLBindGroupBaseName))
            {
                return NewLine() + generateUGLBindGroupClass(decl);
            }
            else if (checkCXXRecord(decl) == false)
            {
                return "";
            }
            return generateRecordDefinitionDetailed(decl);
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    std::optional<std::string> MSLVisitor::tryTranslateTexture2DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                           const std::string &textureExpr,
                                                                           const std::string &methodName)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return "{" + translateTextureArgument(expr, 0) + "=(" + textureExpr + ".get_width());" + translateTextureArgument(expr, 1) + "=(" + textureExpr + ".get_height());}";
        case TextureMemberCallKind::Write:
            return textureExpr + ".write(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 0) + ")";
        case TextureMemberCallKind::Read:
            return textureExpr + ".read( uint2(" + translateTextureArgument(expr, 0) + "), " + translateOptionalTextureArgument(expr, 1, "0") + ")";
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", gradient2d(" + translateTextureArgument(expr, 2) + "," + translateTextureArgument(expr, 3) + "))";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", level(" + translateTextureArgument(expr, 2) + "))";
        case TextureMemberCallKind::GatherCmp:
            return textureExpr + ".gather_compare(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", " + translateOptionalTextureArgument(expr, 3, "int2(0)") + ")";
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha:
            return textureExpr + ".gather(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateOptionalTextureArgument(expr, 2, "int2(0)") + ", " + std::string(getMSLGatherComponent(methodKind)) + ")";
        case TextureMemberCallKind::Sample:
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> MSLVisitor::tryTranslateTexture2DArrayMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                                 const std::string &textureExpr,
                                                                                 const std::string &methodName)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return "{" + translateTextureArgument(expr, 0) + "=(" + textureExpr + ".get_width());" + translateTextureArgument(expr, 1) + "=(" + textureExpr + ".get_height());" + translateTextureArgument(expr, 2) + "=(" + textureExpr + ".get_array_size());}";
        case TextureMemberCallKind::Write:
            return textureExpr + ".write(" + translateTextureArgument(expr, 2) + ", " + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")";
        case TextureMemberCallKind::Read:
            return textureExpr + ".read( uint2(" + translateTextureArgument(expr, 0) + "), " + translateOptionalTextureArgument(expr, 1, "0") + ", " + translateOptionalTextureArgument(expr, 2, "0") + ")";
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", gradient2d(" + translateTextureArgument(expr, 3) + "," + translateTextureArgument(expr, 4) + "))";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", level(" + translateTextureArgument(expr, 3) + "))";
        case TextureMemberCallKind::GatherCmp:
            return textureExpr + ".gather_compare(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 3) + ", " + translateTextureArgument(expr, 2) + ", " + translateOptionalTextureArgument(expr, 4, "int2(0)") + ")";
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha:
            return textureExpr + ".gather(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", " + translateOptionalTextureArgument(expr, 3, "int2(0)") + ", " + std::string(getMSLGatherComponent(methodKind)) + ")";
        case TextureMemberCallKind::Sample:
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> MSLVisitor::tryTranslateTexture3DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                           const std::string &textureExpr,
                                                                           const std::string &methodName,
                                                                           const bool isReadWriteTexture)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return "{" + translateTextureArgument(expr, 0) + "=(" + textureExpr + ".get_width());" + translateTextureArgument(expr, 1) + "=(" + textureExpr + ".get_height());" + translateTextureArgument(expr, 2) + "=(" + textureExpr + ".get_depth());}";
        case TextureMemberCallKind::Write:
            return textureExpr + ".write(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 0) + ")";
        case TextureMemberCallKind::Read:
            if (isReadWriteTexture)
            {
                return textureExpr + ".read(uint3(" + translateTextureArgument(expr, 0) + "))";
            }
            return textureExpr + ".read(uint3(" + translateTextureArgument(expr, 0) + "), " + translateOptionalTextureArgument(expr, 1, "0") + ")";
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", gradient3d(" + translateTextureArgument(expr, 2) + "," + translateTextureArgument(expr, 3) + "))";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", level(" + translateTextureArgument(expr, 2) + "))";
        case TextureMemberCallKind::Sample:
            return textureExpr + ".sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")";
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha:
        case TextureMemberCallKind::GatherCmp:
            throw std::runtime_error("Texture3D does not support gather lowering.");
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::string MSLVisitor::translateTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex)
    {
        return TranslateExpr(expr->getArg(argIndex));
    }

    std::string MSLVisitor::translateOptionalTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex, std::string_view fallbackValue)
    {
        if (expr->getNumArgs() <= argIndex || llvm::isa<clang::CXXDefaultArgExpr>(expr->getArg(argIndex)))
        {
            return std::string(fallbackValue);
        }
        return translateTextureArgument(expr, argIndex);
    }

    std::string MSLVisitor::translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E)
    {
        if (llvm::isa<clang::CXXConversionDecl>(E->getMethodDecl()))
        {
            // Implicit conversion calls are inserted by Clang; emit the original object expression instead of `.operator Type()`.
            return TranslateExpr(E->getImplicitObjectArgument());
        }
        // Translate the full callee expression before applying resource intrinsic lowering.
        std::string Callee = TranslateExpr(E->getCallee());
        clang::Expr *baseObjectExpr = E->getImplicitObjectArgument();
        const std::string objectExpr = TranslateExpr(baseObjectExpr);
        const std::string methodName = E->getMethodDecl()->getNameAsString();

        std::string calleeTypeName = generateTypeCanonicalName(baseObjectExpr->getType());
        const auto *baseRecordDecl = getUnqualifiedType(baseObjectExpr->getType())->getAsCXXRecordDecl();
        const std::string baseRecordName = getClassCanonicalName(baseRecordDecl, nullptr);
        if (baseRecordName == mUGLPixelLocalColorAttachmentName && methodName == "read")
        {
            if (E->getNumArgs() != 0)
            {
                throw std::runtime_error("Pixel-local attachment read() does not accept explicit coordinates; it reads the current pixel.");
            }
            const clang::FunctionDecl *currentFunction = mCurrentCodegenFunction != nullptr ? mCurrentCodegenFunction : mMainFunc;
            if (!PixelLocalInputPlan::isDirectPixelLocalInputFieldRead(*this, currentFunction, E))
            {
                throw std::runtime_error("PixelLocalColorAttachment::read() must be called directly on a PixelLocalColorAttachment field of a [[PixelLocalInput]] framebuffer parameter.");
            }
            return objectExpr;
        }
        if (calleeTypeName.starts_with(mUGLShaderUniformBufferDataPacker))
        {
            if (methodName == "read")
            {
                return objectExpr + "[0]";
            }
        }
        else if (isTexture2DAccessPackerType(calleeTypeName))
        {
            if (const auto translated = tryTranslateTexture2DMemberCall(E, objectExpr, methodName))
            {
                return *translated;
            }
        }
        else if (isTexture2DArrayAccessPackerType(calleeTypeName))
        {
            if (const auto translated = tryTranslateTexture2DArrayMemberCall(E, objectExpr, methodName))
            {
                return *translated;
            }
        }
        else if (isTexture3DAccessPackerType(calleeTypeName))
        {
            const bool isReadWriteTexture = isReadWriteTexture3DAccessPackerType(calleeTypeName) || isReadWriteTextureCanonicalName(baseRecordName);
            if (const auto translated = tryTranslateTexture3DMemberCall(E, objectExpr, methodName, isReadWriteTexture))
            {
                return *translated;
            }
        }

        std::vector<std::string> argStrs;
        argStrs.reserve(E->getNumArgs() + 2);
        for (unsigned i = 0; i < E->getNumArgs(); ++i)
        {
            std::string argStr = TranslateExpr(E->getArg(i));
            if (argStr.empty() == false)
            {
                argStrs.push_back(std::move(argStr));
            }
        }
        // Member helpers participate in the same propagation model as free functions.
        mWaveBuiltinAnalyzer.appendInjectedArgs(E->getMethodDecl(), mCurrentCodegenFunction, argStrs);

        return Callee + generateTemplateCallArguments(E->getCallee()) + "(" + stringJoin(argStrs, ", ") + ")";
    }
    std::string MSLVisitor::translateReturnStmt(const clang::ReturnStmt *S)
    {
        if (S->getRetValue() && mMainFunc != nullptr
            && mCurrentCodegenFunction == mMainFunc
            && mMainFunc->getNameAsString() == mUGLVertexShaderFunctionName)
        {
            auto retExpr = TranslateExpr(S->getRetValue());
            auto retTypeName = generateTypeCanonicalName(S->getRetValue()->getType());
            auto vertRetTypeName = generateTypeCanonicalName(mMainFunc->getReturnType());
            if (retTypeName == vertRetTypeName)
            {
                const auto vertRetType = mMainFunc->getReturnType();
                const auto *retRecord = getUnqualifiedType(vertRetType)->getAsCXXRecordDecl();
                if (retRecord == nullptr)
                {
                    throw std::runtime_error("MSL vertex entry \"" + mMainFunc->getQualifiedNameAsString() + "\" must return a record type with a [[" + mUGLAttributePositionName + "]] field so UGLC can apply Metal clip-space Y correction.");
                }

                const auto *posField = getFieldFromClassWithAttribute(retRecord, mUGLAttributePositionName);
                if (posField == nullptr)
                {
                    throw std::runtime_error("MSL vertex output record \"" + retRecord->getQualifiedNameAsString() + "\" is missing required [[" + mUGLAttributePositionName + "]] field for Metal clip-space Y correction.");
                }

                return vertRetTypeName + "  __REVERSED_VERTEX__OUTPUT__ = " + retExpr + "; __REVERSED_VERTEX__OUTPUT__." + posField->getNameAsString() + ".y=-__REVERSED_VERTEX__OUTPUT__." + posField->getNameAsString() + ".y;return __REVERSED_VERTEX__OUTPUT__" + EOS();
            }
        }
        return BaseASTVisitor::translateReturnStmt(S);
    }

    std::string MSLVisitor::translateCXXThrowExpr(const clang::CXXThrowExpr *E)
    {
        throwCodegenError(E, "UGL shader code does not support throw expressions. Move this control flow to host code or replace it with an explicit shader return path.");
        return {};
    }

    std::string MSLVisitor::translateArraySubscriptExpr(const clang::ArraySubscriptExpr *E)
    {
        if (!mTranslateAtomicLValueRaw && isAtomicBackedLValue(E))
        {
            return translateAtomicBackedLValueLoad(E);
        }

        return BaseASTVisitor::translateArraySubscriptExpr(E);
    }

    std::string MSLVisitor::translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E)
    {
        if (!mTranslateAtomicLValueRaw && isAtomicBackedLValue(E))
        {
            return translateAtomicBackedLValueLoad(E);
        }

        // First argument: the indexed object.
        std::string base = TranslateExpr(E->getArg(0));
        // Second argument: the index expression.
        std::string index = TranslateExpr(E->getArg(1));
        std::string postfix;

        std::string typeName = generateTypeCanonicalName(E->getArg(0)->getType());
        if (isTexture2DArrayCanonicalName(typeName))
        {
            // postfix = ".texture";
        }

        return base + "[" + index + "]" + postfix;
    }


    std::string MSLVisitor::translateVarDecl(const clang::VarDecl *VD)
    {
        if (VD->isLocalVarDecl() && typeContainsHostResourceHandle(VD->getType()))
        {
            throwCodegenError(VD, "MSL shader local variable \"" + VD->getNameAsString() + "\" uses a host-only resource handle type. Host-only resource handles such as UGL::Texture, UGL::TextureView, UGL::Buffer, UGL::BufferRange, UGL::Device, and UGL::Queue cannot be used in shader code.");
        }

        if (const auto atomicGroupSharedTypeName = makeAtomicGroupSharedVariableTypeName(VD))
        {
            std::string result = *atomicGroupSharedTypeName + " " + VD->getNameAsString() + generateDeclArraySpecifier(VD->getType());
            if (VD->hasInit() && !isImplicitNode(VD->getInit()))
            {
                result += " = " + TranslateExpr(VD->getInit());
            }
            return result;
        }

        const clang::DeclContext *DC = VD->getDeclContext();

        std::string addrSpecificer = getShaderResourceAddressSpaceQualifier(VD->getType());
        // Namespace-scope constants must be address-space qualified so Metal does not treat them as local temporaries.
        if (llvm::isa<clang::TranslationUnitDecl>(DC) || llvm::isa<clang::NamespaceDecl>(DC))
        {

            if (VD->getType().isConstQualified())
            {
                addrSpecificer = "constant ";
            }
        }
        if (addrSpecificer == "thread")
        {
            addrSpecificer.clear();
        }


        return addrSpecificer + " " + BaseASTVisitor::translateVarDecl(VD);
    }
} // namespace UGLC::CodeGen::MSL
