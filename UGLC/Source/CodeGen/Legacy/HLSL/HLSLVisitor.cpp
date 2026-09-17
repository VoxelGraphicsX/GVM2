#include "HLSLVisitor.hpp"

#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>
#include <CodeGen/HLSL/HLSLPrelude.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRecordNameUtils.hpp>
#include <CodeGen/IntegerConstantExpressionUtils.hpp>
#include <CodeGen/PixelLocalFieldAnalysis.hpp>
#include <CodeGen/PixelLocalInputPlan.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/ShaderDeclarationEmissionPlanner.hpp>
#include <CodeGen/ShaderTypeClassificationUtils.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>

#include <clang/AST/DeclTemplate.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace UGLC::CodeGen::HLSL
{
    namespace
    {
        bool isFunctionStyleAttributeName(const std::string &rawAttribute, const std::string &attributeName)
        {
            const std::string prefix = attributeName + "(";
            return rawAttribute.starts_with(prefix) && rawAttribute.back() == ')';
        }

        const clang::Expr *unwrapBindGroupArgumentExpr(const clang::Expr *expr)
        {
            const clang::Expr *current = expr;
            while (current != nullptr)
            {
                current = PixelLocalFieldAnalysis::stripTransparentExprWrappers(current);
                if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(current))
                {
                    current = constantExpr->getSubExpr();
                    continue;
                }
                if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(current); constructExpr != nullptr && constructExpr->getNumArgs() == 1)
                {
                    current = constructExpr->getArg(0);
                    continue;
                }
                if (const auto *functionalCastExpr = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(current))
                {
                    current = functionalCastExpr->getSubExpr();
                    continue;
                }
                if (const auto *explicitCastExpr = llvm::dyn_cast<clang::ExplicitCastExpr>(current))
                {
                    current = explicitCastExpr->getSubExpr();
                    continue;
                }
                if (const auto *opaqueValueExpr = llvm::dyn_cast<clang::OpaqueValueExpr>(current))
                {
                    current = opaqueValueExpr->getSourceExpr();
                    continue;
                }
                return current;
            }
            return expr;
        }

        bool isNamespaceOrTranslationUnitScope(const clang::DeclContext *declContext)
        {
            return declContext != nullptr && (llvm::isa<clang::TranslationUnitDecl>(declContext) || llvm::isa<clang::NamespaceDecl>(declContext));
        }

        bool shouldForceStaticConstGlobalForHLSL(const clang::VarDecl *decl)
        {
            if (decl == nullptr || decl->isLocalVarDecl())
            {
                return false;
            }

            if (!isNamespaceOrTranslationUnitScope(decl->getDeclContext()))
            {
                return false;
            }

            if (!decl->getType().isConstQualified() || !decl->hasInit())
            {
                return false;
            }

            if (decl->getStorageClass() == clang::SC_Extern)
            {
                return false;
            }

            return true;
        }

        /** Collects record types that must be fully defined before HLSL resource wrappers or declarations use them. */
        std::unordered_set<const clang::CXXRecordDecl *> collectResourceBindingElementRecordDecls(BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap)
        {
            std::unordered_set<const clang::CXXRecordDecl *> result;
            for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
            {
                (void)slotIndex;
                if (bindGroupInfo.isRenderSet)
                {
                    continue;
                }
                for (const BaseShaderResourceBinding &resourceBinding : bindGroupInfo.resourceBindings)
                {
                    if (resourceBinding.elementType.isNull())
                    {
                        continue;
                    }
                    const auto *elementRecord = visitor.getUnqualifiedType(resourceBinding.elementType)->getAsCXXRecordDecl();
                    if (elementRecord != nullptr)
                    {
                        result.insert(elementRecord->getCanonicalDecl());
                    }
                }
            }
            return result;
        }

        /** Returns true when a record is directly used by an HLSL resource wrapper, handle field, or resource declaration. */
        bool isResourceBindingElementRecord(const std::unordered_set<const clang::CXXRecordDecl *> &resourceBindingElementRecords, const clang::CXXRecordDecl *recordDecl)
        {
            return recordDecl != nullptr && resourceBindingElementRecords.contains(recordDecl->getCanonicalDecl());
        }

        /** Returns true when a type directly or indirectly references one of the records in the provided set. */
        bool typeReferencesRecordSet(BaseASTVisitor &visitor, const clang::QualType &type, const std::unordered_set<const clang::CXXRecordDecl *> &recordSet)
        {
            if (type.isNull())
            {
                return false;
            }
            if (isResourceBindingElementRecord(recordSet, visitor.getUnqualifiedType(type)->getAsCXXRecordDecl()))
            {
                return true;
            }
            for (const clang::TemplateArgument &templateArgument : visitor.getTemplateArgumentsFromType(type))
            {
                if (templateArgument.getKind() == clang::TemplateArgument::Type &&
                    typeReferencesRecordSet(visitor, templateArgument.getAsType(), recordSet))
                {
                    return true;
                }
            }
            return false;
        }

        /** Returns true when a function signature depends on any record in the provided set. */
        bool functionSignatureReferencesRecordSet(BaseASTVisitor &visitor, const clang::FunctionDecl *functionDecl, const std::unordered_set<const clang::CXXRecordDecl *> &recordSet)
        {
            if (functionDecl == nullptr)
            {
                return false;
            }
            if (typeReferencesRecordSet(visitor, functionDecl->getReturnType(), recordSet))
            {
                return true;
            }
            for (const clang::ParmVarDecl *param : functionDecl->parameters())
            {
                if (typeReferencesRecordSet(visitor, param->getType(), recordSet))
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> splitAttributeParameters(const std::string &rawAttribute)
        {
            const size_t leftParen = rawAttribute.find('(');
            const size_t rightParen = rawAttribute.rfind(')');
            if (leftParen == std::string::npos || rightParen == std::string::npos || rightParen <= leftParen)
            {
                return {};
            }

            const std::string rawParams = rawAttribute.substr(leftParen + 1, rightParen - leftParen - 1);
            std::vector<std::string> params;
            size_t start = 0;
            while (start <= rawParams.size())
            {
                const size_t separator = rawParams.find(',', start);
                const size_t end = separator == std::string::npos ? rawParams.size() : separator;
                std::string param = rawParams.substr(start, end - start);
                const size_t first = param.find_first_not_of(" \t");
                const size_t last = param.find_last_not_of(" \t");
                params.emplace_back(first == std::string::npos ? std::string() : param.substr(first, last - first + 1));
                if (separator == std::string::npos)
                {
                    break;
                }
                start = separator + 1;
            }

            return params;
        }

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

        const clang::Expr *unwrapOverloadedArrowBaseExpr(const clang::Expr *expr)
        {
            const clang::Expr *baseExpr = expr == nullptr ? nullptr : expr->IgnoreParenImpCasts();
            if (const auto *operatorCallExpr = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(baseExpr); operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OverloadedOperatorKind::OO_Arrow && operatorCallExpr->getNumArgs() > 0)
            {
                return operatorCallExpr->getArg(0)->IgnoreParenImpCasts();
            }
            return baseExpr;
        }

        bool shouldForceDotMemberAccess(std::string_view typeName)
        {
            return typeName.starts_with(mUGLShaderUniformBufferDataPacker) || isTextureAccessPackerType(typeName) || isTextureObjectCanonicalName(typeName) || isRenderSetBufferComponentType(typeName) || isRenderSetTextureComponentType(typeName) || isRenderSetDataPackType(typeName);
        }

        bool endsWithExplicitMemberAccessOperator(const std::string &baseExpr)
        {
            return !baseExpr.empty() && (baseExpr.back() == '.' || (baseExpr.size() > 1 && baseExpr.substr(baseExpr.size() - 2) == "->"));
        }

        struct HLSLVectorTypeInfo
        {
            std::string scalarType;
            int elementCount = 0;
        };

        std::optional<HLSLVectorTypeInfo> parseHLSLVectorType(const std::string &typeName)
        {
            const size_t qualifierPos = typeName.rfind("::");
            const std::string unqualifiedTypeName = qualifierPos == std::string::npos ? typeName : typeName.substr(qualifierPos + 2);

            const size_t digitPos = unqualifiedTypeName.find_last_not_of("0123456789");
            if (digitPos == std::string::npos || digitPos + 1 >= unqualifiedTypeName.size())
            {
                return std::nullopt;
            }
            if (unqualifiedTypeName.find('x', digitPos + 1) != std::string::npos)
            {
                return std::nullopt;
            }

            const std::string scalarType = unqualifiedTypeName.substr(0, digitPos + 1);
            const std::string suffix = unqualifiedTypeName.substr(digitPos + 1);
            if (suffix.size() != 1 || !std::isdigit(static_cast<unsigned char>(suffix.front())))
            {
                return std::nullopt;
            }

            const int elementCount = suffix.front() - '0';
            if (elementCount < 2 || elementCount > 4)
            {
                return std::nullopt;
            }

            static constexpr const char *kSupportedScalarPrefixes[] = {
                "bool",
                "half",
                "float",
                "double",
                "int",
                "uint",
            };
            for (const char *prefix : kSupportedScalarPrefixes)
            {
                if (scalarType == prefix)
                {
                    return HLSLVectorTypeInfo{scalarType, elementCount};
                }
            }
            return std::nullopt;
        }

        struct HLSLMatrixTypeInfo
        {
            std::string scalarType;
            int rowCount = 0;
            int columnCount = 0;
        };

        enum class AtomicBuiltinKind
        {
            None,
            Add,
            Or,
            And,
            Load,
            Store,
            CompareExchange,
            Max,
            Min,
        };

        std::optional<HLSLMatrixTypeInfo> parseHLSLMatrixType(const std::string &typeName)
        {
            const size_t xPos = typeName.find('x');
            if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= typeName.size())
            {
                return std::nullopt;
            }

            size_t leftDigitStart = xPos;
            while (leftDigitStart > 0 && std::isdigit(static_cast<unsigned char>(typeName[leftDigitStart - 1])))
            {
                --leftDigitStart;
            }
            if (leftDigitStart == xPos)
            {
                return std::nullopt;
            }

            const std::string scalarType = typeName.substr(0, leftDigitStart);
            const std::string rowDigits = typeName.substr(leftDigitStart, xPos - leftDigitStart);
            const std::string colDigits = typeName.substr(xPos + 1);
            if (rowDigits.empty() || colDigits.empty())
            {
                return std::nullopt;
            }

            static constexpr const char *kSupportedScalarPrefixes[] = {
                "half",
                "float",
                "double",
                "int",
                "uint",
            };
            bool supportedScalar = false;
            for (const char *prefix : kSupportedScalarPrefixes)
            {
                if (scalarType == prefix)
                {
                    supportedScalar = true;
                    break;
                }
            }
            if (!supportedScalar)
            {
                return std::nullopt;
            }

            return HLSLMatrixTypeInfo{
                scalarType,
                std::stoi(rowDigits),
                std::stoi(colDigits),
            };
        }

        std::string makeRepeatedArgumentList(const std::string &argExpr, int count)
        {
            std::vector<std::string> args(static_cast<size_t>(count), argExpr);
            return stringJoin(args, ", ");
        }

        bool isHLSLScalarLikeTypeName(const std::string &typeName)
        {
            const size_t qualifierPos = typeName.rfind("::");
            const std::string unqualifiedTypeName = qualifierPos == std::string::npos ? typeName : typeName.substr(qualifierPos + 2);
            return unqualifiedTypeName == "bool" || unqualifiedTypeName == "half" || unqualifiedTypeName == "float" || unqualifiedTypeName == "double" || unqualifiedTypeName == "int" || unqualifiedTypeName == "uint";
        }

        /** Resolves a UGL atomic builtin from either qualified or unqualified source spelling. */
        AtomicBuiltinKind getAtomicBuiltinKindFromName(const std::string_view functionName)
        {
            if (functionName == "UGL::atomicAdd" || functionName == "atomicAdd")
            {
                return AtomicBuiltinKind::Add;
            }
            if (functionName == "UGL::atomicOr" || functionName == "atomicOr")
            {
                return AtomicBuiltinKind::Or;
            }
            if (functionName == "UGL::atomicAnd" || functionName == "atomicAnd")
            {
                return AtomicBuiltinKind::And;
            }
            if (functionName == "UGL::atomicLoad" || functionName == "atomicLoad")
            {
                return AtomicBuiltinKind::Load;
            }
            if (functionName == "UGL::atomicStore" || functionName == "atomicStore")
            {
                return AtomicBuiltinKind::Store;
            }
            if (functionName == "UGL::atomicCompareExchange" || functionName == "atomicCompareExchange")
            {
                return AtomicBuiltinKind::CompareExchange;
            }
            if (functionName == "UGL::atomicMax" || functionName == "atomicMax")
            {
                return AtomicBuiltinKind::Max;
            }
            if (functionName == "UGL::atomicMin" || functionName == "atomicMin")
            {
                return AtomicBuiltinKind::Min;
            }
            return AtomicBuiltinKind::None;
        }

        AtomicBuiltinKind getAtomicBuiltinKind(const clang::FunctionDecl *callee)
        {
            if (callee == nullptr)
            {
                return AtomicBuiltinKind::None;
            }

            return getAtomicBuiltinKindFromName(callee->getCanonicalDecl()->getQualifiedNameAsString());
        }

        std::string getAtomicInterlockedFunctionName(const AtomicBuiltinKind kind)
        {
            switch (kind)
            {
            case AtomicBuiltinKind::Add:
                return "InterlockedAdd";
            case AtomicBuiltinKind::Or:
                return "InterlockedOr";
            case AtomicBuiltinKind::And:
                return "InterlockedAnd";
            case AtomicBuiltinKind::Store:
                return "InterlockedExchange";
            case AtomicBuiltinKind::CompareExchange:
                return "InterlockedCompareExchange";
            case AtomicBuiltinKind::Max:
                return "InterlockedMax";
            case AtomicBuiltinKind::Min:
                return "InterlockedMin";
            default:
                return {};
            }
        }

        std::string makeGeneratedIdentifier(const std::string &prefix, const clang::SourceLocation location)
        {
            return "__uglc_" + prefix + "_" + std::to_string(location.getRawEncoding());
        }

        /** Returns true for atomic builtins that produce the original memory value and can initialize an expression result. */
        bool atomicBuiltinReturnsOriginalValue(const AtomicBuiltinKind kind)
        {
            return kind == AtomicBuiltinKind::Add || kind == AtomicBuiltinKind::Or || kind == AtomicBuiltinKind::And;
        }

        std::string stripIllegalLocalVariableQualifiers(const std::string &typeName)
        {
            static constexpr std::string_view kGroupSharedQualifier = "groupshared ";
            if (typeName.starts_with(kGroupSharedQualifier))
            {
                return typeName.substr(kGroupSharedQualifier.size());
            }
            return typeName;
        }

        /** Restores a visitor state slot to its previous value when a scoped generation path exits. */
        template <typename T>
        class ScopedValueRestore final
        {
        public:
            /** Captures the current slot value without changing it. */
            explicit ScopedValueRestore(T &inSlot)
                : slot(inSlot)
                , previous(inSlot)
            {
            }

            /** Restores the captured value. */
            ~ScopedValueRestore()
            {
                slot = previous;
            }

            /** Assigns a temporary value for the active scope. */
            void set(const T &value)
            {
                slot = value;
            }

        private:
            T &slot;
            T previous;
        };

        bool shouldDropFunctionQualifierForHLSL(const clang::NamedDecl *namedDecl)
        {
            const auto *functionDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(namedDecl);
            if (functionDecl == nullptr)
            {
                return false;
            }

            const std::string qualifiedName = functionDecl->getQualifiedNameAsString();
            return qualifiedName.starts_with("UGL::") || qualifiedName.starts_with("std::");
        }

        /** Returns true when a C++ method is emitted inside an HLSL record body. */
        bool isRecordMethodEmittedInHLSL(const clang::CXXMethodDecl *method)
        {
            return method != nullptr &&
                   method->hasBody() &&
                   !method->isCopyAssignmentOperator() &&
                   !method->isMoveAssignmentOperator() &&
                   !llvm::isa<clang::CXXConstructorDecl>(method) &&
                   !llvm::isa<clang::CXXDestructorDecl>(method);
        }

        /** Returns true when a record definition emits user-authored method bodies that may need helper prototypes. */
        bool recordHasUserProvidedMethodBodies(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return false;
            }
            for (const clang::CXXMethodDecl *method : recordDecl->methods())
            {
                if (isRecordMethodEmittedInHLSL(method) && method->isUserProvided())
                {
                    return true;
                }
            }
            return false;
        }

        /** Counts fields emitted by generateRecordDataMembers for a record definition. */
        unsigned countEmittedHLSLRecordFields(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return 0;
            }

            unsigned count = 0;
            for (const clang::FieldDecl *field : recordDecl->fields())
            {
                if (field != nullptr)
                {
                    ++count;
                }
            }
            return count;
        }

        /** Counts methods emitted inside an HLSL record body. */
        unsigned countEmittedHLSLRecordMethods(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return 0;
            }

            unsigned count = 0;
            for (const clang::CXXMethodDecl *method : recordDecl->methods())
            {
                if (isRecordMethodEmittedInHLSL(method))
                {
                    ++count;
                }
            }
            return count;
        }

        /** Returns the complete Clang specialization definition only when HLSL can emit it directly. */
        const clang::ClassTemplateSpecializationDecl *getDirectlyEmittableClassTemplateSpecializationDefinition(const clang::ClassTemplateSpecializationDecl *specializationDecl)
        {
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                return nullptr;
            }

            const auto *specializationDefinition = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(specializationDecl->getDefinition());
            if (specializationDefinition == nullptr || !specializationDefinition->isCompleteDefinition())
            {
                return nullptr;
            }

            const clang::CXXRecordDecl *primaryDecl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
            if (primaryDecl == nullptr || !primaryDecl->isCompleteDefinition())
            {
                return nullptr;
            }

            if (countEmittedHLSLRecordFields(primaryDecl) != countEmittedHLSLRecordFields(specializationDefinition))
            {
                return nullptr;
            }

            if (countEmittedHLSLRecordMethods(primaryDecl) != countEmittedHLSLRecordMethods(specializationDefinition))
            {
                return nullptr;
            }

            return specializationDefinition;
        }

    } // namespace

    HLSLVisitor::HLSLVisitor(clang::ASTContext *context)
        : BaseASTVisitor(context, &mTypeConvertor, nullptr, nullptr)
        , mLocalStorage(*this, &mTypeConvertor)
        , mAggregateInitializer(*this, &mTypeConvertor, mLocalStorage)
        , mResourceBindingEmitter(*this, &mTypeConvertor)
        , mShaderBuiltinTranslator(*this)
        , mTextureMemberCallLowering(*this)
        , mRenderSetEmitter(*this, &mTypeConvertor)
        , mRenderInterfaceValidator(*this, &mTypeConvertor)
        , mRecordEmitter(*this, &mTypeConvertor, mRenderInterfaceValidator)
    {
        mEnableLineDirectiveInsertion = true;
    }

    std::string HLSLVisitor::translateNestedNameSpecifierToHLSL(const clang::NestedNameSpecifier *nestedNameSpecifier)
    {
        if (nestedNameSpecifier == nullptr)
        {
            return {};
        }

        const std::string prefix = translateNestedNameSpecifierToHLSL(nestedNameSpecifier->getPrefix());
        switch (nestedNameSpecifier->getKind())
        {
        case clang::NestedNameSpecifier::Identifier: {
            const std::string identifier = sanitizeHLSLIdentifier(nestedNameSpecifier->getAsIdentifier()->getName().str());
            return identifier.empty() ? prefix : prefix + identifier + "::";
        }
        case clang::NestedNameSpecifier::Namespace: {
            const std::string namespaceName = sanitizeHLSLIdentifier(nestedNameSpecifier->getAsNamespace()->getNameAsString());
            return namespaceName.empty() ? prefix : prefix + namespaceName + "::";
        }
        case clang::NestedNameSpecifier::NamespaceAlias: {
            const std::string namespaceAlias = sanitizeHLSLIdentifier(nestedNameSpecifier->getAsNamespaceAlias()->getNameAsString());
            return namespaceAlias.empty() ? prefix : prefix + namespaceAlias + "::";
        }
        case clang::NestedNameSpecifier::Global:
            return "::";
        case clang::NestedNameSpecifier::TypeSpec:
        case clang::NestedNameSpecifier::TypeSpecWithTemplate: {
            const clang::Type *type = nestedNameSpecifier->getAsType();
            if (type == nullptr)
            {
                return prefix;
            }
            return prefix + generateTypeCanonicalName(clang::QualType(type, 0), &mTypeConvertor) + "::";
        }
        default:
            return prefix;
        }
    }

    std::string HLSLVisitor::extractBindGroupInstanceName(const clang::Expr *expr) const
    {
        if (expr == nullptr)
        {
            return {};
        }

        expr = unwrapBindGroupArgumentExpr(expr);
        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(expr))
        {
            const auto *valueDecl = llvm::dyn_cast<clang::ValueDecl>(declRefExpr->getDecl()->getCanonicalDecl());
            if (valueDecl != nullptr)
            {
                if (const auto aliasIt = mRenderSetParameterGlobalAliases.find(valueDecl); aliasIt != mRenderSetParameterGlobalAliases.end())
                {
                    return aliasIt->second;
                }
            }
            return declRefExpr->getDecl()->getNameAsString();
        }
        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(expr))
        {
            return memberExpr->getMemberNameInfo().getAsString();
        }
        if (const auto *dependentMemberExpr = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(expr))
        {
            if (dependentMemberExpr->isImplicitAccess())
            {
                return dependentMemberExpr->getMemberNameInfo().getAsString();
            }
        }
        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr))
        {
            if (operatorCallExpr->getOperator() == clang::OverloadedOperatorKind::OO_Arrow && operatorCallExpr->getNumArgs() > 0)
            {
                return extractBindGroupInstanceName(operatorCallExpr->getArg(0));
            }
        }
        return {};
    }

    std::string HLSLVisitor::extractRenderSetGlobalAliasName(const clang::Expr *expr) const
    {
        const std::string aliasName = extractBindGroupInstanceName(expr);
        if (aliasName.empty())
        {
            return {};
        }
        return sanitizeHLSLIdentifier(aliasName);
    }

    std::optional<std::string> HLSLVisitor::tryTranslateBindGroupResourceAccess(const clang::MemberExpr *E)
    {
        const auto access = tryResolveResourceFieldAccess(E, mUGLBindGroupName);
        if (!access.has_value())
        {
            return std::nullopt;
        }

        return TranslateExpr(access->ownerExpr) + "." + sanitizeHLSLIdentifier(access->fieldDecl->getNameAsString());
    }

    bool HLSLVisitor::isShaderResourceBehaviorRecordDefinition(const clang::CXXRecordDecl *recordDecl) const
    {
        if (recordDecl == nullptr || recordDecl->isImplicit())
        {
            return false;
        }
        if (checkDerivedClassByName(recordDecl, mUGLComputeClassBaseName) ||
            checkDerivedClassByName(recordDecl, mUGLRenderClassBaseName) ||
            checkDerivedClassByName(recordDecl, mUGLPixelLocalRenderClassBaseName) ||
            checkDerivedClassByName(recordDecl, mUGLFrameBufferBaseName) ||
            checkDerivedClassByName(recordDecl, mUGLBindGroupBaseName) ||
            checkDerivedClassByName(recordDecl, mUGLRenderSetBaseName))
        {
            return false;
        }
        if (Context != nullptr && isAnyShaderResourceHandleType(Context->getRecordType(recordDecl)))
        {
            return false;
        }

        const clang::CXXRecordDecl *classificationRecord = recordDecl;
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordDecl);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            classificationRecord = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
        }
        if (isFromExcludedFile(classificationRecord->getLocation()))
        {
            return false;
        }

        return recordUsesShaderResourceHandles(classificationRecord);
    }

    std::optional<std::string> HLSLVisitor::tryTranslateRenderSetResourceAccess(const clang::MemberExpr *E) const
    {
        const auto access = tryResolveResourceFieldAccess(E, mUGLRenderSetName);
        if (!access.has_value())
        {
            return std::nullopt;
        }

        const std::string renderSetName = extractBindGroupInstanceName(access->ownerExpr);
        if (renderSetName.empty())
        {
            return std::nullopt;
        }
        return mRenderSetEmitter.getResourceGlobalName(renderSetName, access->fieldDecl->getNameAsString());
    }

    std::optional<HLSLVisitor::ResourceFieldAccess> HLSLVisitor::tryResolveResourceFieldAccess(const clang::MemberExpr *expr, std::string_view expectedOwnerTypeName) const
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(expr->getMemberDecl());
        if (fieldDecl == nullptr)
        {
            return std::nullopt;
        }

        const clang::Expr *ownerExpr = unwrapOverloadedArrowBaseExpr(expr->getBase());
        if (ownerExpr == nullptr)
        {
            return std::nullopt;
        }

        if (!checkTypeCanonicalName(ownerExpr->getType(), std::string(expectedOwnerTypeName)))
        {
            return std::nullopt;
        }

        return ResourceFieldAccess{fieldDecl, ownerExpr};
    }

    std::optional<std::string> HLSLVisitor::tryTranslateDependentBindGroupResourceAccess(const clang::CXXDependentScopeMemberExpr *expr)
    {
        if (expr == nullptr || expr->isImplicitAccess())
        {
            return std::nullopt;
        }

        const clang::Expr *ownerExpr = unwrapOverloadedArrowBaseExpr(expr->getBase());
        if (ownerExpr == nullptr || !checkTypeCanonicalName(ownerExpr->getType(), mUGLBindGroupName))
        {
            return std::nullopt;
        }

        return TranslateExpr(ownerExpr) + "." + sanitizeHLSLIdentifier(expr->getMemberNameInfo().getAsString());
    }

    std::optional<std::string> HLSLVisitor::tryTranslateDependentTextureMemberCall(const clang::CallExpr *expr, const clang::CXXDependentScopeMemberExpr *calleeExpr)
    {
        if (expr == nullptr || calleeExpr == nullptr)
        {
            return std::nullopt;
        }

        const auto *baseResourceExpr = llvm::dyn_cast_or_null<clang::CXXDependentScopeMemberExpr>(calleeExpr->getBase());
        const std::optional<std::string> textureExpr = tryTranslateDependentBindGroupResourceAccess(baseResourceExpr);
        if (!textureExpr.has_value())
        {
            return std::nullopt;
        }

        const clang::Expr *ownerExpr = unwrapOverloadedArrowBaseExpr(baseResourceExpr->getBase());
        if (ownerExpr == nullptr)
        {
            return std::nullopt;
        }

        const std::string resourceFieldName = baseResourceExpr->getMemberNameInfo().getAsString();
        std::optional<BaseShaderResourceBinding> matchedResourceBinding;
        for (const auto &candidateBinding : mResourceBindingEmitter.resolveParameterBindings(ownerExpr->getType()))
        {
            if (candidateBinding.fieldDecl == nullptr || candidateBinding.fieldDecl->getNameAsString() != resourceFieldName)
            {
                continue;
            }
            matchedResourceBinding = candidateBinding;
            break;
        }

        if (!matchedResourceBinding.has_value() ||
            (matchedResourceBinding->kind != BaseShaderResourceKind::SampledTexture &&
             matchedResourceBinding->kind != BaseShaderResourceKind::StorageTexture))
        {
            return std::nullopt;
        }

        const bool isReadWriteTexture = matchedResourceBinding->kind == BaseShaderResourceKind::StorageTexture;
        const std::string methodName = calleeExpr->getMemberNameInfo().getAsString();
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        if (methodKind == TextureMemberCallKind::Unknown)
        {
            return std::nullopt;
        }

        switch (matchedResourceBinding->dimension)
        {
        case BaseShaderTextureDimension::Texture2D:
            switch (methodKind)
            {
            case TextureMemberCallKind::GetDimensions:
                return *textureExpr + ".GetDimensions(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ")";
            case TextureMemberCallKind::Write:
                return *textureExpr + "[" + translateDependentTextureArgument(expr, 0) + "] = " + translateDependentTextureArgument(expr, 1);
            case TextureMemberCallKind::Read:
                if (isReadWriteTexture)
                {
                    return *textureExpr + "[" + translateDependentTextureArgument(expr, 0) + "]";
                }
                return *textureExpr + ".Load(int3(" + translateDependentTextureArgument(expr, 0) + ", " + translateOptionalDependentTextureArgument(expr, 1, "0") + "))";
            case TextureMemberCallKind::SampleGrad:
                return *textureExpr + ".SampleGrad(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ", " + translateDependentTextureArgument(expr, 3) + ")";
            case TextureMemberCallKind::SampleLevel:
                return *textureExpr + ".SampleLevel(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ")";
            case TextureMemberCallKind::Sample:
                return *textureExpr + ".Sample(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ")";
            case TextureMemberCallKind::GatherCmp: {
                std::string result = *textureExpr + ".GatherCmp(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2);
                appendOptionalDependentTextureArgument(result, expr, 3);
                result += ")";
                return result;
            }
            case TextureMemberCallKind::Gather:
            case TextureMemberCallKind::GatherRed:
            case TextureMemberCallKind::GatherGreen:
            case TextureMemberCallKind::GatherBlue:
            case TextureMemberCallKind::GatherAlpha: {
                std::string result = *textureExpr + "." + std::string(getHLSLGatherIntrinsic(methodKind)) + "(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1);
                appendOptionalDependentTextureArgument(result, expr, 2);
                result += ")";
                return result;
            }
            case TextureMemberCallKind::Unknown:
                return std::nullopt;
            }
            break;
        case BaseShaderTextureDimension::Texture2DArray:
            switch (methodKind)
            {
            case TextureMemberCallKind::GetDimensions:
                return *textureExpr + ".GetDimensions(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ")";
            case TextureMemberCallKind::Write:
                return *textureExpr + "[uint3(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ")] = " + translateDependentTextureArgument(expr, 2);
            case TextureMemberCallKind::Read: {
                const std::string layerExpr = translateOptionalDependentTextureArgument(expr, 1, "0");
                if (isReadWriteTexture)
                {
                    return *textureExpr + "[uint3(" + translateDependentTextureArgument(expr, 0) + ", " + layerExpr + ")]";
                }
                return *textureExpr + ".Load(int4(" + translateDependentTextureArgument(expr, 0) + ", " + layerExpr + ", " + translateOptionalDependentTextureArgument(expr, 2, "0") + "))";
            }
            case TextureMemberCallKind::SampleGrad:
                return *textureExpr + ".SampleGrad(" + translateDependentTextureArgument(expr, 0) + ", float3(" + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + "), " + translateDependentTextureArgument(expr, 3) + ", " + translateDependentTextureArgument(expr, 4) + ")";
            case TextureMemberCallKind::SampleLevel:
                return *textureExpr + ".SampleLevel(" + translateDependentTextureArgument(expr, 0) + ", float3(" + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + "), " + translateDependentTextureArgument(expr, 3) + ")";
            case TextureMemberCallKind::Sample:
                return *textureExpr + ".Sample(" + translateDependentTextureArgument(expr, 0) + ", float3(" + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + "))";
            case TextureMemberCallKind::GatherCmp: {
                std::string result = *textureExpr + ".GatherCmp(" + translateDependentTextureArgument(expr, 0) + ", float3(" + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 3) + "), " + translateDependentTextureArgument(expr, 2);
                appendOptionalDependentTextureArgument(result, expr, 4);
                result += ")";
                return result;
            }
            case TextureMemberCallKind::Gather:
            case TextureMemberCallKind::GatherRed:
            case TextureMemberCallKind::GatherGreen:
            case TextureMemberCallKind::GatherBlue:
            case TextureMemberCallKind::GatherAlpha: {
                std::string result = *textureExpr + "." + std::string(getHLSLGatherIntrinsic(methodKind)) + "(" + translateDependentTextureArgument(expr, 0) + ", float3(" + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ")";
                appendOptionalDependentTextureArgument(result, expr, 3);
                result += ")";
                return result;
            }
            case TextureMemberCallKind::Unknown:
                return std::nullopt;
            }
            break;
        case BaseShaderTextureDimension::Texture3D:
            switch (methodKind)
            {
            case TextureMemberCallKind::GetDimensions:
                return *textureExpr + ".GetDimensions(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ")";
            case TextureMemberCallKind::Write:
                return *textureExpr + "[" + translateDependentTextureArgument(expr, 0) + "] = " + translateDependentTextureArgument(expr, 1);
            case TextureMemberCallKind::Read:
                if (isReadWriteTexture)
                {
                    return *textureExpr + "[" + translateDependentTextureArgument(expr, 0) + "]";
                }
                return *textureExpr + ".Load(int4(" + translateDependentTextureArgument(expr, 0) + ", " + translateOptionalDependentTextureArgument(expr, 1, "0") + "))";
            case TextureMemberCallKind::SampleGrad:
                return *textureExpr + ".SampleGrad(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ", " + translateDependentTextureArgument(expr, 3) + ")";
            case TextureMemberCallKind::SampleLevel:
                return *textureExpr + ".SampleLevel(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ", " + translateDependentTextureArgument(expr, 2) + ")";
            case TextureMemberCallKind::Sample:
                return *textureExpr + ".Sample(" + translateDependentTextureArgument(expr, 0) + ", " + translateDependentTextureArgument(expr, 1) + ")";
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
            break;
        case BaseShaderTextureDimension::None:
            break;
        }
        return std::nullopt;
    }

    std::string HLSLVisitor::translateDependentTextureArgument(const clang::CallExpr *expr, unsigned argIndex)
    {
        return TranslateExpr(expr->getArg(argIndex));
    }

    std::string HLSLVisitor::translateOptionalDependentTextureArgument(const clang::CallExpr *expr, unsigned argIndex, std::string_view fallbackValue)
    {
        if (expr->getNumArgs() <= argIndex || isDefaultArgumentExpr(expr->getArg(argIndex)))
        {
            return std::string(fallbackValue);
        }
        return translateDependentTextureArgument(expr, argIndex);
    }

    void HLSLVisitor::appendOptionalDependentTextureArgument(std::string &result, const clang::CallExpr *expr, unsigned argIndex)
    {
        if (expr->getNumArgs() > argIndex && !isDefaultArgumentExpr(expr->getArg(argIndex)))
        {
            result += ", " + translateDependentTextureArgument(expr, argIndex);
        }
    }

    std::optional<std::string> HLSLVisitor::tryTranslateDependentUniformBufferMemberCall(const clang::CallExpr *expr, const clang::CXXDependentScopeMemberExpr *calleeExpr)
    {
        if (expr == nullptr || calleeExpr == nullptr)
        {
            return std::nullopt;
        }

        const auto *baseResourceExpr = llvm::dyn_cast_or_null<clang::CXXDependentScopeMemberExpr>(calleeExpr->getBase());
        const std::optional<std::string> uniformBufferExpr = tryTranslateDependentBindGroupResourceAccess(baseResourceExpr);
        if (!uniformBufferExpr.has_value())
        {
            return std::nullopt;
        }

        const clang::Expr *ownerExpr = unwrapOverloadedArrowBaseExpr(baseResourceExpr->getBase());
        if (ownerExpr == nullptr)
        {
            return std::nullopt;
        }

        const std::string resourceFieldName = baseResourceExpr->getMemberNameInfo().getAsString();
        std::optional<BaseShaderResourceBinding> matchedResourceBinding;
        for (const auto &candidateBinding : mResourceBindingEmitter.resolveParameterBindings(ownerExpr->getType()))
        {
            if (candidateBinding.fieldDecl == nullptr || candidateBinding.fieldDecl->getNameAsString() != resourceFieldName)
            {
                continue;
            }
            matchedResourceBinding = candidateBinding;
            break;
        }

        if (!matchedResourceBinding.has_value() || matchedResourceBinding->kind != BaseShaderResourceKind::UniformBuffer)
        {
            return std::nullopt;
        }

        const std::string methodName = calleeExpr->getMemberNameInfo().getAsString();
        if (methodName == "read")
        {
            return *uniformBufferExpr + ".value";
        }

        const std::vector<std::string> argStrs = collectTranslatedCallArguments(expr);
        return *uniformBufferExpr + ".value." + sanitizeHLSLIdentifier(methodName) + "(" + stringJoin(argStrs, ", ") + ")";
    }

    bool HLSLVisitor::checkIsShaderFunction(const clang::FunctionDecl *shaderFunc) const
    {
        if (mMainFunc != nullptr)
        {
            return shaderFunc == mMainFunc;
        }

        const std::string funcName = shaderFunc->getNameAsString();
        return funcName == VertexFuncNameInRenderClass || funcName == FragmentFuncNameInRenderClass || funcName == PixelFuncNameInPixelLocalRenderClass || funcName == ComputeFuncNameInRenderClass || funcName == mUGLDomainShaderFunctionName || funcName == mUGLHullShaderFunctionName;
    }

    int HLSLVisitor::resolvePixelLocalInputDescriptorSetIndex() const
    {
        int descriptorSetIndex = 0;
        for (const auto &[slotIndex, bindGroupInfo] : mBindGroupInfoMap)
        {
            (void)slotIndex;
            descriptorSetIndex = std::max(descriptorSetIndex, bindGroupInfo.bindingIndex + 1);
        }
        return descriptorSetIndex;
    }

    bool HLSLVisitor::checkCXXRecord(const clang::CXXRecordDecl *decl)
    {
        return !recordContainsHostResourceHandles(decl);
    }

    std::array<std::string, 3> HLSLVisitor::getLocalWorkgroupSize(const clang::CXXRecordDecl *shaderClassDecl) const
    {
        constexpr const char *LocalWorkGroupSizeAttributeName = "LocalWorkGroupSize";

        if (shaderClassDecl == nullptr)
        {
            throw std::runtime_error("HLSL compute entry is missing its owning compute class while resolving [[LocalWorkGroupSize(x, y, z)]].");
        }

        std::vector<std::string> matchedAttributes;
        for (const auto *attr : getAllAttributes(shaderClassDecl))
        {
            const std::string rawAttribute = attr->getAnnotation().str();
            if (isFunctionStyleAttributeName(rawAttribute, LocalWorkGroupSizeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        const std::string computeClassName = shaderClassDecl->getQualifiedNameAsString();
        if (matchedAttributes.empty())
        {
            throw std::runtime_error("ComputeClass \"" + computeClassName + "\" is missing required [[LocalWorkGroupSize(x, y, z)]].");
        }

        if (matchedAttributes.size() > 1)
        {
            throw std::runtime_error("ComputeClass \"" + computeClassName + "\" has more than one [[LocalWorkGroupSize(...)]] attribute.");
        }

        std::vector<std::string> params = splitAttributeParameters(matchedAttributes.front());
        if (params.size() != 3)
        {
            throw std::runtime_error("ComputeClass \"" + computeClassName + "\" requires [[LocalWorkGroupSize(x, y, z)]] with exactly 3 arguments, but got " + std::to_string(params.size()) + ".");
        }

        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[i].empty())
            {
                throw std::runtime_error("ComputeClass \"" + computeClassName + "\" has an empty argument in [[LocalWorkGroupSize(x, y, z)]].");
            }

            const clang::DeclContext *evaluationContext = getCurrentTemplateSubstitutionContext() != nullptr
                                                               ? static_cast<const clang::DeclContext *>(getCurrentTemplateSubstitutionContext())
                                                               : static_cast<const clang::DeclContext *>(shaderClassDecl);
            const auto evaluated = evaluateCompileTimeIntegerExpression(params[i], evaluationContext, *Context);
            if (!evaluated.success)
            {
                if (const auto substitutedValue = tryResolveTemplateSubstitutionValueByName(params[i]); substitutedValue.has_value())
                {
                    params[i] = *substitutedValue;
                    continue;
                }
                throw std::runtime_error("ComputeClass \"" + computeClassName + "\" argument " + std::string(1, static_cast<char>('x' + static_cast<int>(i))) + " in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer, but expression \"" + params[i] + "\" " + evaluated.error + ".");
            }
            if (evaluated.value <= 0)
            {
                throw std::runtime_error("ComputeClass \"" + computeClassName + "\" argument " + std::string(1, static_cast<char>('x' + static_cast<int>(i))) + " in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer, but expression \"" + params[i] + "\" evaluates to " + std::to_string(evaluated.value) + ".");
            }
            params[i] = evaluated.resolvedExpression;
        }

        return {params[0], params[1], params[2]};
    }

    std::string HLSLVisitor::generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor)
    {
        if (decl != nullptr && isBindGroupHandleType(decl->getType()))
        {
            return mResourceBindingEmitter.generateHandleFieldDecl(decl);
        }
        return mRecordEmitter.generateRecordFieldDecl(decl, typeConvertor);
    }

    std::string HLSLVisitor::generateRecordDefinition(const clang::CXXRecordDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (decl == nullptr || decl->isImplicit())
            {
                return {};
            }
            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
                specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
            {
                const clang::ClassTemplateSpecializationDecl *directDefinition = getDirectlyEmittableClassTemplateSpecializationDefinition(specializationDecl);
                if (directDefinition == nullptr)
                {
                    pushTemplateSubstitutionContext(specializationDecl);
                    const std::string result = generateRecordDefinition(specializationDecl->getSpecializedTemplate()->getTemplatedDecl());
                    popTemplateSubstitutionContext();
                    return result;
                }
                if (directDefinition != specializationDecl)
                {
                    return generateRecordDefinition(directDefinition);
                }
            }
            if (const auto *templateDecl = decl->getDescribedClassTemplate();
                templateDecl != nullptr &&
                getCurrentTemplateSubstitutionContext() == nullptr &&
                mTypeConvertor.shouldMaterializeTemplateSpecializationName(getClassCanonicalName(decl)))
            {
                std::string result;
                for (const clang::ClassTemplateSpecializationDecl *specializationDecl : templateDecl->specializations())
                {
                    const clang::CXXRecordDecl *specializationDefinition = specializationDecl->getDefinition();
                    if (specializationDefinition == nullptr || isFromExcludedFile(specializationDefinition->getLocation()))
                    {
                        continue;
                    }
                    result += getLineDirective(specializationDefinition->getBeginLoc());
                    result += generateRecordDefinition(specializationDefinition);
                }
                return result;
            }
            if (checkDerivedClassByName(decl, mUGLComputeClassBaseName) || checkDerivedClassByName(decl, mUGLRenderClassBaseName) || checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName))
            {
                return {};
            }
            if (checkDerivedClassByName(decl, mUGLFrameBufferBaseName))
            {
                const std::string recordName = generateGlobalHLSLRecordTypeName(*this, decl, &mTypeConvertor);
                if (!recordName.empty() && !mEmittedRecordDefinitionNames.insert(recordName).second)
                {
                    return {};
                }
                return mRecordEmitter.generateFramebufferClass(decl, mUsePixelLocalFramebufferOutputFilter ? &mPixelLocalFramebufferOutputFields : nullptr);
            }
            if (checkDerivedClassByName(decl, mUGLBindGroupBaseName))
            {
                // HLSL consumes bind-group resources as globally-declared descriptors.
                return {};
            }
            if (checkDerivedClassByName(decl, mUGLRenderSetBaseName))
            {
                return {};
            }
            if (!checkCXXRecord(decl))
            {
                return {};
            }
            const std::string recordName = generateGlobalHLSLRecordTypeName(*this, decl, &mTypeConvertor);
            if (!recordName.empty() && !mEmittedRecordDefinitionNames.insert(recordName).second)
            {
                return {};
            }
            return mRecordEmitter.generateRecordDefinitionDetailed(decl);
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }

    std::string HLSLVisitor::generateRecordForwardDeclaration(const clang::CXXRecordDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (decl == nullptr || decl->isImplicit())
            {
                return {};
            }
            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
                specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
            {
                const clang::ClassTemplateSpecializationDecl *directDefinition = getDirectlyEmittableClassTemplateSpecializationDefinition(specializationDecl);
                if (directDefinition == nullptr)
                {
                    pushTemplateSubstitutionContext(specializationDecl);
                    const std::string result = generateRecordForwardDeclaration(specializationDecl->getSpecializedTemplate()->getTemplatedDecl());
                    popTemplateSubstitutionContext();
                    return result;
                }
                if (directDefinition != specializationDecl)
                {
                    return generateRecordForwardDeclaration(directDefinition);
                }
            }
            if (const auto *templateDecl = decl->getDescribedClassTemplate();
                templateDecl != nullptr &&
                getCurrentTemplateSubstitutionContext() == nullptr &&
                mTypeConvertor.shouldMaterializeTemplateSpecializationName(getClassCanonicalName(decl)))
            {
                std::string result;
                for (const clang::ClassTemplateSpecializationDecl *specializationDecl : templateDecl->specializations())
                {
                    const clang::CXXRecordDecl *specializationDefinition = specializationDecl->getDefinition();
                    if (specializationDefinition == nullptr || isFromExcludedFile(specializationDefinition->getLocation()))
                    {
                        continue;
                    }
                    result += generateRecordForwardDeclaration(specializationDefinition);
                }
                return result;
            }
            if (checkDerivedClassByName(decl, mUGLComputeClassBaseName) ||
                checkDerivedClassByName(decl, mUGLRenderClassBaseName) ||
                checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName) ||
                checkDerivedClassByName(decl, mUGLBindGroupBaseName) ||
                checkDerivedClassByName(decl, mUGLRenderSetBaseName))
            {
                return {};
            }
            if (!checkCXXRecord(decl))
            {
                return {};
            }

            const std::string name = generateGlobalHLSLRecordTypeName(*this, decl, &mTypeConvertor);
            if (name.empty())
            {
                return {};
            }
            if (!mEmittedRecordForwardDeclarationNames.insert(name).second)
            {
                return {};
            }

            return mSpaceManager.getSpace() + "struct " + name + EOS();
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }

    std::string HLSLVisitor::generateStandaloneFunctionSignature(const clang::FunctionDecl *func, const std::string &funcName, AbstractTypeConvertor *typeConvertor)
    {
        if (typeConvertor == nullptr)
        {
            typeConvertor = &mTypeConvertor;
        }

        std::string result;
        result += getLineDirective(func->getBeginLoc());
        result += mSpaceManager.getSpace() + generateTypeCanonicalName(func->getReturnType(), &mTypeConvertor) + " " + sanitizeHLSLIdentifier(funcName) + "(";
        std::vector<std::string> parameterDecls;
        for (unsigned i = 0; i < func->getNumParams(); ++i)
        {
            const auto *param = func->getParamDecl(i);
            if (const auto bindGroupHandleParameter = tryGenerateBindGroupHandleParameter(param))
            {
                parameterDecls.push_back(*bindGroupHandleParameter);
                continue;
            }

            if (isRenderSetParameterType(param->getType(), *this))
            {
                validateRenderSetHelperParameterContract(param);
                continue;
            }
            validateInputOnlyShaderHandleParameterOrThrow(param, "HLSL");

            parameterDecls.push_back(generateFunctionSignatureParam(param, typeConvertor));
        }
        result += stringJoin(parameterDecls, ", ");
        result += ")" + NewLine();
        return result;
    }

    std::optional<std::string> HLSLVisitor::tryGenerateBindGroupHandleParameter(const clang::ParmVarDecl *param)
    {
        if (param == nullptr || !mResourceBindingEmitter.isBindGroupParameterType(param->getType()))
        {
            return std::nullopt;
        }
        if (checkAttibuteByName(param, mUGLAttributeOUTName) || checkAttibuteByName(param, mUGLAttributeINOUTName))
        {
            throw std::runtime_error("HLSL shader helper parameter \"" + param->getNameAsString() + "\" uses UGL::BindGroup<T> with [[OUT]] or [[INOUT]], but bind groups must be passed by input only as handle values.");
        }
        return mResourceBindingEmitter.generateHandleParameter(param);
    }

    std::string HLSLVisitor::generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor)
    {
        std::string parameterQualifier;
        if (const auto bindGroupHandleParameter = tryGenerateBindGroupHandleParameter(param))
        {
            return *bindGroupHandleParameter;
        }
        validateInputOnlyShaderHandleParameterOrThrow(param, "HLSL");

        if (checkAttibuteByName(param, mUGLAttributeOUTName))
        {
            parameterQualifier = "out ";
        }
        else if (checkAttibuteByName(param, mUGLAttributeINOUTName))
        {
            parameterQualifier = "inout ";
        }
        else if (checkAttibuteByName(param, mUGLAttributeINName))
        {
            parameterQualifier = "in ";
        }

        return parameterQualifier + generateTypeCanonicalName(param->getType(), typeConvertor == nullptr ? &mTypeConvertor : typeConvertor) + " " + sanitizeHLSLIdentifier(param->getNameAsString()) + generateDeclArraySpecifier(param->getOriginalType());
    }

    bool HLSLVisitor::functionHasRenderSetParameter(const clang::FunctionDecl *func) const
    {
        return UGLC::CodeGen::functionHasRenderSetParameter(func, const_cast<HLSLVisitor &>(*this));
    }

    bool HLSLVisitor::functionHasBindGroupHandleParameter(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }

        for (const clang::ParmVarDecl *param : func->parameters())
        {
            try
            {
                if (mResourceBindingEmitter.isBindGroupParameterType(param->getType()))
                {
                    return true;
                }
            }
            catch (const std::exception &)
            {
                return true;
            }
        }
        return false;
    }

    void HLSLVisitor::validateRenderSetHelperParameterContract(const clang::ParmVarDecl *param) const
    {
        if (param == nullptr || !isRenderSetParameterType(param->getType(), const_cast<HLSLVisitor &>(*this)))
        {
            return;
        }

        const bool isInParam = checkAttibuteByName(param, mUGLAttributeINName);
        if (!isInParam || checkAttibuteByName(param, mUGLAttributeOUTName) || checkAttibuteByName(param, mUGLAttributeINOUTName))
        {
            throw std::runtime_error("HLSL shader helper parameter \"" + param->getNameAsString() + "\" uses UGL::RenderSet<T> without an explicit [[IN]] contract. RenderSet helper parameters must be declared as [[IN]] UGL::RenderSet<T> and cannot use [[OUT]] or [[INOUT]].");
        }
    }

    void HLSLVisitor::addBindGroupHandleRecord(std::vector<const clang::CXXRecordDecl *> &records,
                                               std::unordered_set<const clang::Decl *> &seenRecords,
                                               const clang::CXXRecordDecl *recordDecl)
    {
        if (recordDecl == nullptr)
        {
            return;
        }
        recordDecl = recordDecl->getCanonicalDecl();
        if (seenRecords.insert(recordDecl).second)
        {
            records.push_back(recordDecl);
        }
    }

    void HLSLVisitor::addBindGroupHandleType(std::vector<const clang::CXXRecordDecl *> &records,
                                             std::unordered_set<const clang::Decl *> &seenRecords,
                                             const clang::QualType &type)
    {
        if (!isBindGroupHandleType(type))
        {
            return;
        }
        try
        {
            addBindGroupHandleRecord(records, seenRecords, mResourceBindingEmitter.tryGetBindGroupTypeDeclFromType(type));
        }
        catch (const std::exception &)
        {
            // Dependent helper template declarations are diagnosed when a concrete specialization is emitted.
        }
    }

    void HLSLVisitor::collectBindGroupHandleRecordsFromRecord(std::vector<const clang::CXXRecordDecl *> &records,
                                                              std::unordered_set<const clang::Decl *> &seenRecords,
                                                              const clang::CXXRecordDecl *recordDecl)
    {
        if (recordDecl == nullptr || recordDecl->isImplicit() || recordDecl->getDescribedClassTemplate() != nullptr)
        {
            return;
        }
        if (checkDerivedClassByName(recordDecl, mUGLBindGroupBaseName))
        {
            addBindGroupHandleRecord(records, seenRecords, recordDecl);
        }
        for (const clang::FieldDecl *field : recordDecl->fields())
        {
            addBindGroupHandleType(records, seenRecords, field->getType());
        }
        for (const clang::CXXMethodDecl *method : recordDecl->methods())
        {
            for (const clang::ParmVarDecl *param : method->parameters())
            {
                addBindGroupHandleType(records, seenRecords, param->getType());
            }
        }
    }

    std::vector<const clang::CXXRecordDecl *> HLSLVisitor::collectBindGroupHandleRecords(const std::vector<const clang::Decl *> &shaderDefs)
    {
        std::vector<const clang::CXXRecordDecl *> records;
        std::unordered_set<const clang::Decl *> seenRecords;

        for (const clang::Decl *def : shaderDefs)
        {
            if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(def))
            {
                collectBindGroupHandleRecordsFromRecord(records, seenRecords, recordDecl);
            }
            else if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(def))
            {
                for (const clang::ParmVarDecl *param : functionDecl->parameters())
                {
                    addBindGroupHandleType(records, seenRecords, param->getType());
                }
            }
            else if (const auto *functionTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(def))
            {
                for (const clang::ParmVarDecl *param : functionTemplateDecl->getTemplatedDecl()->parameters())
                {
                    addBindGroupHandleType(records, seenRecords, param->getType());
                }
            }
        }

        return records;
    }

    std::string HLSLVisitor::generateBindGroupUniformWrapperDefinitions(const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls)
    {
        return mResourceBindingEmitter.generateUniformWrapperDefinitions(mBindGroupInfoMap, extraBindGroupDecls);
    }

    std::string HLSLVisitor::generateBindGroupHandleStructDefinitions(const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls)
    {
        return mResourceBindingEmitter.generateHandleStructDefinitions(mBindGroupInfoMap, extraBindGroupDecls);
    }

    std::string HLSLVisitor::generateFunctionDefinition(const clang::FunctionDecl *func)
    {
        auto diagnosticScope = scopeDiagnosticLocation(func);
        try
        {
            mLocalStorage.resetFunctionScope();
            mLocalGroupSharedGlobals.clear();
            std::string result;
            if (func == nullptr || func->isImplicit())
            {
                return result;
            }
            if (functionHasRenderSetParameter(func))
            {
                for (unsigned i = 0; i < func->getNumParams(); ++i)
                {
                    validateRenderSetHelperParameterContract(func->getParamDecl(i));
                }
                return result;
            }

            ScopedValueRestore<const clang::FunctionDecl *> currentFunctionScope(mCurrentFunctionDecl);
            currentFunctionScope.set(func);

            if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(func); method != nullptr && !method->isUserProvided() && !checkAttibuteByName(func, mUGLCTORName))
            {
                return result;
            }
            const std::string funcName = sanitizeHLSLIdentifier(func->getNameAsString());
            result += NewLine();
            result += generateStandaloneFunctionSignature(func, funcName, &mTypeConvertor);
            if (checkAttibuteByName(func, mUGLCTORName))
            {
                result += generateUGLCTORFunctionBody(func);
            }
            else
            {
                result += generateFunctionBody(func);
            }

            mLocalStorage.captureFunctionHelperDefinitions();
            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, func);
        }
    }

    std::string HLSLVisitor::translateDeclRefExpr(const clang::DeclRefExpr *E)
    {
        if (const auto value = tryTranslateTemplateSubstitutedDeclRefExpr(E))
        {
            return *value;
        }

        const clang::Decl *canonicalDecl = E->getDecl()->getCanonicalDecl();
        if (const auto *canonicalValueDecl = llvm::dyn_cast<clang::ValueDecl>(canonicalDecl); canonicalValueDecl != nullptr)
        {
            if (const auto groupSharedIt = mLocalGroupSharedGlobals.find(canonicalValueDecl); groupSharedIt != mLocalGroupSharedGlobals.end())
            {
                return groupSharedIt->second;
            }
            if (const auto aliasSelectorName = mLocalStorage.getAliasSelectorName(canonicalValueDecl); aliasSelectorName.has_value())
            {
                return *aliasSelectorName;
            }
            if (const auto renderSetAliasIt = mRenderSetParameterGlobalAliases.find(canonicalValueDecl); renderSetAliasIt != mRenderSetParameterGlobalAliases.end())
            {
                return renderSetAliasIt->second;
            }
        }

        if (const auto *canonicalNamedDecl = llvm::dyn_cast<clang::NamedDecl>(canonicalDecl); canonicalNamedDecl != nullptr && shouldDropFunctionQualifierForHLSL(canonicalNamedDecl))
        {
            return sanitizeHLSLIdentifier(E->getNameInfo().getName().getAsString());
        }

        if (E->getQualifier() == nullptr)
        {
            if (const auto *canonicalNamedDecl = llvm::dyn_cast<clang::NamedDecl>(canonicalDecl))
            {
                if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(canonicalNamedDecl); functionDecl != nullptr && !shouldDropFunctionQualifierForHLSL(functionDecl))
                {
                    const std::string calleeNamespace = getFullNamespace(functionDecl);
                    const std::string currentNamespace = mCurrentFunctionDecl == nullptr ? std::string() : getFullNamespace(mCurrentFunctionDecl);
                    const bool currentFunctionIsRecordMethod = mCurrentFunctionDecl != nullptr && llvm::isa<clang::CXXMethodDecl>(mCurrentFunctionDecl);
                    if (calleeNamespace.empty() || (!currentFunctionIsRecordMethod && calleeNamespace == currentNamespace))
                    {
                        return sanitizeHLSLIdentifier(E->getNameInfo().getName().getAsString());
                    }
                }

                const std::string qualifiedName = canonicalNamedDecl->getQualifiedNameAsString();
                if (!qualifiedName.empty() && qualifiedName.find("::") != std::string::npos && !shouldDropFunctionQualifierForHLSL(canonicalNamedDecl))
                {
                    return makeQualifiedHLSLIdentifier(qualifiedName);
                }
            }
        }

        return translateNestedNameSpecifierToHLSL(E->getQualifier()) + sanitizeHLSLIdentifier(E->getNameInfo().getName().getAsString());
    }

    std::string HLSLVisitor::translateBinaryOperator(const clang::BinaryOperator *E)
    {
        if (E->isAssignmentOp())
        {
            const clang::Expr *lhsExpr = PixelLocalFieldAnalysis::stripTransparentExprWrappers(E->getLHS());
            if (const auto matrixLValue = tryTranslateMatrixElementSubscriptLValue(lhsExpr))
            {
                return *matrixLValue + " " + E->getOpcodeStr().str() + " " + translateExprAsGroupedInfixOperand(E->getRHS());
            }

            if (E->getOpcode() != clang::BO_Assign)
            {
                return BaseASTVisitor::translateBinaryOperator(E);
            }

            if (const auto *subscriptCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(lhsExpr); subscriptCallExpr != nullptr && subscriptCallExpr->getOperator() == clang::OO_Subscript)
            {
                if (const auto translated = mLocalStorage.tryTranslateAliasWrite(subscriptCallExpr->getArg(0), subscriptCallExpr->getArg(1), E->getRHS()))
                {
                    return *translated;
                }
            }
            if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(lhsExpr))
            {
                if (const auto translated = mLocalStorage.tryTranslateAliasWrite(arraySubscriptExpr->getBase(), arraySubscriptExpr->getIdx(), E->getRHS()))
                {
                    return *translated;
                }
            }

            if (const auto *atomicCall = llvm::dyn_cast<clang::CallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(E->getRHS())))
            {
                const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(atomicCall->getCalleeDecl());
                const AtomicBuiltinKind atomicKind = getAtomicBuiltinKind(calleeDecl);
                if (atomicBuiltinReturnsOriginalValue(atomicKind))
                {
                    const std::string resultName = makeGeneratedIdentifier("atomic_assign_result", atomicCall->getExprLoc());
                    const std::string resultTypeName = generateTypeCanonicalName(atomicCall->getType(), &mTypeConvertor);
                    const std::string interlockedFunctionName = getAtomicInterlockedFunctionName(atomicKind);

                    // HLSL `Interlocked*` operations return results through an out parameter and DXC requires the
                    // first argument to be the real groupshared/UAV lvalue, not a wrapper function parameter.
                    //
                    // DSL:
                    //   assignedBase = atomicAdd(counter, 1u);
                    //
                    // HLSL:
                    //   uint __uglc_atomic_assign_result_N;
                    //   InterlockedAdd(counter, 1u, __uglc_atomic_assign_result_N);
                    //   assignedBase = __uglc_atomic_assign_result_N;
                    //
                    // More general expression rewrites, such as `x = atomicAdd(counter, 1u) + 5u`, need a future
                    // expression-lowering result that carries `{preludeStatements, expression}` through TranslateExpr().
                    std::string result;
                    result += resultTypeName + " " + resultName + EOS();
                    if (mLocalStorage.hasAtomicTarget(atomicCall->getArg(0)))
                    {
                        if (const auto translated = mLocalStorage.tryBuildAtomicDispatchStatements(atomicCall->getArg(0), interlockedFunctionName, resultName, {TranslateExpr(atomicCall->getArg(1))}, false, mSpaceManager.getSpace()))
                        {
                            result += *translated;
                        }
                    }
                    else
                    {
                        result += mSpaceManager.getSpace() + interlockedFunctionName + "(" + TranslateExpr(atomicCall->getArg(0)) + ", " + TranslateExpr(atomicCall->getArg(1)) + ", " + resultName + ")" + EOS();
                    }
                    result += mSpaceManager.getSpace() + TranslateExpr(E->getLHS()) + " = " + resultName;
                    return result;
                }
            }
        }

        return BaseASTVisitor::translateBinaryOperator(E);
    }

    std::optional<std::string> HLSLVisitor::tryTranslateMatrixElementSubscriptLValue(const clang::Expr *expr)
    {
        const auto *outerSubscript = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(expr));
        if (outerSubscript == nullptr || outerSubscript->getOperator() != clang::OO_Subscript || outerSubscript->getNumArgs() < 2)
        {
            return std::nullopt;
        }

        const auto *innerSubscript = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(outerSubscript->getArg(0)));
        if (innerSubscript == nullptr || innerSubscript->getOperator() != clang::OO_Subscript || innerSubscript->getNumArgs() < 2)
        {
            return std::nullopt;
        }

        const clang::Expr *matrixExpr = innerSubscript->getArg(0);
        const std::string matrixTypeName = generateTypeCanonicalName(matrixExpr->getType(), &mTypeConvertor);
        if (!parseHLSLMatrixType(matrixTypeName).has_value())
        {
            return std::nullopt;
        }

        const std::string matrix = TranslateExpr(matrixExpr);
        const std::string column = TranslateExpr(innerSubscript->getArg(1));
        const std::string row = TranslateExpr(outerSubscript->getArg(1));
        return matrix + "[" + row + "][" + column + "]";
    }

    std::string HLSLVisitor::translateReturnStmt(const clang::ReturnStmt *S)
    {
        const clang::Expr *returnExpr = S->getRetValue();
        if (returnExpr != nullptr)
        {
            returnExpr = returnExpr->IgnoreParenImpCasts();
            if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(returnExpr); constructExpr != nullptr && constructExpr->getNumArgs() == 1)
            {
                returnExpr = constructExpr->getArg(0)->IgnoreParenImpCasts();
            }
        }

        const auto *conditionalExpr = llvm::dyn_cast_or_null<clang::ConditionalOperator>(returnExpr);
        if (conditionalExpr != nullptr)
        {
            const clang::QualType resultType = getUnqualifiedType(conditionalExpr->getType());
            if (resultType->getAsCXXRecordDecl() != nullptr || resultType->isArrayType())
            {
                std::string result;
                result += "if (" + TranslateExpr(conditionalExpr->getCond()) + ")" + NewLine();
                result += enterScope();
                result += mSpaceManager.getSpace() + "return " + TranslateExpr(conditionalExpr->getTrueExpr()) + EOS();
                result += quitScope();
                result += mSpaceManager.getSpace() + "else" + NewLine();
                result += enterScope();
                result += mSpaceManager.getSpace() + "return " + TranslateExpr(conditionalExpr->getFalseExpr()) + EOS();
                result += quitScope();
                return result;
            }
        }
        return BaseASTVisitor::translateReturnStmt(S);
    }

    std::string HLSLVisitor::translateCXXThrowExpr(const clang::CXXThrowExpr *E)
    {
        throwCodegenError(E, "UGL shader code does not support throw expressions. Move this control flow to host code or replace it with an explicit shader return path.");
        return {};
    }

    std::string HLSLVisitor::translateMemberExpr(const clang::MemberExpr *E)
    {
        if (const auto bindGroupResource = tryTranslateBindGroupResourceAccess(E))
        {
            return *bindGroupResource;
        }
        if (const auto renderSetResource = tryTranslateRenderSetResourceAccess(E))
        {
            return *renderSetResource;
        }

        clang::Expr *baseExpr = E->getBase();
        const clang::Expr *strippedBaseExpr = PixelLocalFieldAnalysis::stripTransparentExprWrappers(baseExpr);
        std::string memStr = sanitizeHLSLIdentifier(E->getMemberNameInfo().getAsString());
        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(strippedBaseExpr); operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OverloadedOperatorKind::OO_Arrow && operatorCallExpr->getNumArgs() > 0)
        {
            const auto *operatorBaseDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(operatorCallExpr->getArg(0)->getType());
            const std::string operatorBaseName = getClassCanonicalName(operatorBaseDecl, nullptr);
            const auto *operatorResultDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(operatorCallExpr->getType());
            const std::string operatorResultName = getClassCanonicalName(operatorResultDecl, nullptr);
            if ((operatorResultDecl != nullptr && operatorResultName.starts_with(mUGLShaderUniformBufferDataPacker)) || (operatorBaseDecl != nullptr && operatorBaseName.starts_with(mUGLShaderUniformBufferName)))
            {
                return TranslateExpr(operatorCallExpr->getArg(0)) + ".value." + memStr;
            }
        }
        if (E->isImplicitAccess())
        {
            return memStr;
        }
        if (const auto *thisExpr = llvm::dyn_cast<clang::CXXThisExpr>(strippedBaseExpr))
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
        const auto *baseDecl = getUnqualifiedType(E->getBase()->getType())->getAsCXXRecordDecl();
        const std::string baseDeclName = getClassCanonicalName(baseDecl, nullptr);
        if (baseDecl != nullptr && baseDeclName.starts_with(mUGLShaderUniformBufferDataPacker) && llvm::isa<clang::FieldDecl>(E->getMemberDecl()))
        {
            return baseStr + ".value." + memStr;
        }

        if (endsWithExplicitMemberAccessOperator(baseStr))
        {
            return baseStr + memStr;
        }

        const std::string accessOperator = (baseDecl != nullptr && shouldForceDotMemberAccess(baseDeclName)) ? "." : (E->isArrow() ? "->" : ".");
        return baseStr + accessOperator + memStr;
    }

    std::string HLSLVisitor::translateCXXDependentScopeMemberExpr(const clang::CXXDependentScopeMemberExpr *E)
    {
        if (const auto bindGroupResource = tryTranslateDependentBindGroupResourceAccess(E))
        {
            return *bindGroupResource;
        }
        return BaseASTVisitor::translateCXXDependentScopeMemberExpr(E);
    }

    std::string HLSLVisitor::translateCallExpr(const clang::CallExpr *E)
    {
        const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(E->getCalleeDecl());
        if (const auto translatedBuiltin = mShaderBuiltinTranslator.tryTranslateCallExpr(E, calleeDecl, mMainFunc))
        {
            return *translatedBuiltin;
        }

        AtomicBuiltinKind atomicKind = getAtomicBuiltinKind(calleeDecl);
        if (atomicKind == AtomicBuiltinKind::None)
        {
            const clang::Expr *calleeExpr = E->getCallee()->IgnoreParenImpCasts();
            if (const auto *unresolvedLookupExpr = llvm::dyn_cast<clang::UnresolvedLookupExpr>(calleeExpr))
            {
                atomicKind = getAtomicBuiltinKindFromName(unresolvedLookupExpr->getName().getAsString());
            }
            else if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(calleeExpr))
            {
                atomicKind = getAtomicBuiltinKindFromName(declRefExpr->getNameInfo().getAsString());
            }
        }
        if (atomicKind == AtomicBuiltinKind::Load)
        {
            return TranslateExpr(E->getArg(0));
        }
        if (atomicKind == AtomicBuiltinKind::Store || atomicKind == AtomicBuiltinKind::Max || atomicKind == AtomicBuiltinKind::Min || atomicKind == AtomicBuiltinKind::Add || atomicKind == AtomicBuiltinKind::Or || atomicKind == AtomicBuiltinKind::And)
        {
            const std::string interlockedFunctionName = getAtomicInterlockedFunctionName(atomicKind);
            if (mLocalStorage.hasAtomicTarget(E->getArg(0)))
            {
                if (const auto translated = mLocalStorage.tryBuildAtomicDispatchBlock(E->getArg(0), interlockedFunctionName, makeGeneratedIdentifier("atomic_ignored", E->getExprLoc()), stripIllegalLocalVariableQualifiers(generateTypeCanonicalName(E->getArg(0)->getType(), &mTypeConvertor)), {TranslateExpr(E->getArg(1))}, false))
                {
                    return *translated;
                }
            }

            const std::string atomExpr = TranslateExpr(E->getArg(0));
            const std::string valueExpr = TranslateExpr(E->getArg(1));
            std::string atomTypeName = stripIllegalLocalVariableQualifiers(generateTypeCanonicalName(E->getArg(0)->getType(), &mTypeConvertor));
            if (atomTypeName.find("dependent") != std::string::npos)
            {
                atomTypeName = stripIllegalLocalVariableQualifiers(generateTypeCanonicalName(E->getArg(1)->getType(), &mTypeConvertor));
            }
            const std::string ignoredValueName = makeGeneratedIdentifier("atomic_ignored", E->getExprLoc());
            return "{ " + atomTypeName + " " + ignoredValueName + "; " + interlockedFunctionName + "(" + atomExpr + ", " + valueExpr + ", " + ignoredValueName + "); }";
        }
        if (atomicKind == AtomicBuiltinKind::CompareExchange)
        {
            const std::string interlockedFunctionName = getAtomicInterlockedFunctionName(atomicKind);
            if (mLocalStorage.hasAtomicTarget(E->getArg(0)))
            {
                if (const auto translated = mLocalStorage.tryBuildAtomicDispatchBlock(E->getArg(0), interlockedFunctionName, TranslateExpr(E->getArg(3)), stripIllegalLocalVariableQualifiers(generateTypeCanonicalName(E->getArg(0)->getType(), &mTypeConvertor)), {TranslateExpr(E->getArg(1)), TranslateExpr(E->getArg(2))}, true))
                {
                    return *translated;
                }
            }
            return interlockedFunctionName + "(" + TranslateExpr(E->getArg(0)) + ", " + TranslateExpr(E->getArg(1)) + ", " + TranslateExpr(E->getArg(2)) + ", " + TranslateExpr(E->getArg(3)) + ")";
        }

        if (const auto specializedCall = tryTranslateRenderSetSpecializedCall(E))
        {
            return *specializedCall;
        }

        if (const auto *dependentCallee = llvm::dyn_cast_or_null<clang::CXXDependentScopeMemberExpr>(E->getCallee()->IgnoreParenImpCasts()))
        {
            if (const auto translatedUniformBufferCall = tryTranslateDependentUniformBufferMemberCall(E, dependentCallee))
            {
                return *translatedUniformBufferCall;
            }
            if (const auto translatedTextureCall = tryTranslateDependentTextureMemberCall(E, dependentCallee))
            {
                return *translatedTextureCall;
            }
        }

        const std::string callee = TranslateExpr(E->getCallee());
        const std::string targetTypeName = generateTypeCanonicalName(E->getType(), &mTypeConvertor);
        if (const auto vectorType = parseHLSLVectorType(targetTypeName))
        {
            if (E->getNumArgs() == 1)
            {
                const clang::QualType argType = getUnqualifiedType(E->getArg(0)->getType());
                const bool looksLikeTypeConstructor = parseHLSLVectorType(callee).has_value() || callee == targetTypeName || E->getDirectCallee() == nullptr;
                const bool isScalarLikeArgument = argType->isBuiltinType() || argType->isEnumeralType() || isHLSLScalarLikeTypeName(generateTypeCanonicalName(argType, &mTypeConvertor));
                if (looksLikeTypeConstructor && isScalarLikeArgument)
                {
                    const std::string argExpr = TranslateExpr(E->getArg(0));
                    return targetTypeName + "(" + makeRepeatedArgumentList(argExpr, vectorType->elementCount) + ")";
                }
            }
        }

        const std::vector<std::string> argStrs = collectTranslatedCallArguments(E);
        return callee + generateTemplateCallArguments(E->getCallee()) + "(" + stringJoin(argStrs, ", ") + ")";
    }

    std::string HLSLVisitor::translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E)
    {
        if (const auto translated = mLocalStorage.tryTranslateOperatorAssignment(E); translated.has_value())
        {
            return *translated;
        }

        return BaseASTVisitor::translateCXXOperatorCallExprBinaryOp(E);
    }

    std::string HLSLVisitor::translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E)
    {
        if (const auto translated = mLocalStorage.tryTranslateArraySubscript(E); translated.has_value())
        {
            return *translated;
        }

        const std::string baseTypeName = generateTypeCanonicalName(E->getArg(0)->getType(), &mTypeConvertor);
        if (parseHLSLMatrixType(baseTypeName).has_value())
        {
            const std::string base = TranslateExpr(E->getArg(0));
            const std::string index = TranslateExpr(E->getArg(1));
            return "transpose(" + base + ")[" + index + "]";
        }

        return BaseASTVisitor::translateCXXOperatorCallExprArraySubScript(E);
    }

    bool HLSLVisitor::isDefaultArgumentExpr(const clang::Expr *expr)
    {
        return llvm::isa<clang::CXXDefaultArgExpr>(expr);
    }

    std::string HLSLVisitor::stripMemberCallSuffix(const std::string &callee, const std::string &suffix)
    {
        if (const auto pos = callee.find(suffix); pos != std::string::npos)
        {
            return callee.substr(0, pos);
        }
        if (!suffix.empty() && suffix.front() == '.')
        {
            const std::string arrowSuffix = "->" + suffix.substr(1);
            if (const auto pos = callee.find(arrowSuffix); pos != std::string::npos)
            {
                return callee.substr(0, pos);
            }
        }
        return callee;
    }

    std::optional<clang::QualType> HLSLVisitor::tryGetCallParameterType(const clang::CallExpr *expr, unsigned argIndex) const
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        const auto *calleeDecl = expr->getDirectCallee();
        if (calleeDecl == nullptr || argIndex >= calleeDecl->getNumParams())
        {
            return std::nullopt;
        }

        return calleeDecl->getParamDecl(argIndex)->getType();
    }

    std::vector<std::string> HLSLVisitor::translateCallArgument(const clang::CallExpr *expr, unsigned argIndex)
    {
        if (expr == nullptr || argIndex >= expr->getNumArgs())
        {
            return {};
        }

        const clang::Expr *argumentExpr = expr->getArg(argIndex);
        const auto targetType = tryGetCallParameterType(expr, argIndex);
        const clang::QualType effectiveTargetType = targetType.has_value() ? *targetType : argumentExpr->getType();

        if (typeContainsHostResourceHandle(effectiveTargetType))
        {
            const auto *calleeDecl = expr->getDirectCallee();
            const std::string calleeName = calleeDecl == nullptr ? std::string("<unknown>") : calleeDecl->getQualifiedNameAsString();
            throwCodegenError(argumentExpr, "HLSL shader helper call to \"" + calleeName + "\" uses a host-only resource handle argument. Host-only resource handles such as UGL::Texture, UGL::TextureView, UGL::Buffer, UGL::BufferRange, UGL::Device, and UGL::Queue cannot be used in shader code.");
        }

        if (mResourceBindingEmitter.isBindGroupParameterType(effectiveTargetType))
        {
            return {TranslateExpr(argumentExpr)};
        }

        if (!targetType.has_value())
        {
            if (isSampledTextureOrSamplerHandleType(effectiveTargetType))
            {
                return {TranslateExpr(argumentExpr)};
            }
            return {TranslateExpr(argumentExpr)};
        }

        if (isRenderSetParameterType(*targetType, *this))
        {
            const auto *calleeDecl = expr->getDirectCallee();
            const std::string calleeName = calleeDecl == nullptr ? std::string("<unknown>") : calleeDecl->getQualifiedNameAsString();
            throw std::runtime_error("HLSL helper call to \"" + calleeName +
                                     "\" passes a UGL::RenderSet<T> argument without RenderSet global-resource rebinding. "
                                     "Pass RenderSet arguments only to direct helper calls so UGLC can bind them to global RenderSet resources.");
        }
        if (isSampledTextureOrSamplerHandleType(*targetType))
        {
            return {TranslateExpr(argumentExpr)};
        }
        return {mAggregateInitializer.emitTypedInitializerExpression(*targetType, argumentExpr)};
    }

    std::vector<std::string> HLSLVisitor::collectTranslatedCallArguments(const clang::CallExpr *expr)
    {
        std::vector<std::string> argStrs;
        argStrs.reserve(expr->getNumArgs());
        for (unsigned i = 0; i < expr->getNumArgs(); ++i)
        {
            const std::vector<std::string> translatedArgs = translateCallArgument(expr, i);
            for (const auto &argStr : translatedArgs)
            {
                if (!argStr.empty())
                {
                    argStrs.push_back(argStr);
                }
            }
        }
        return argStrs;
    }

    std::optional<std::string> HLSLVisitor::tryTranslateRenderSetSpecializedCall(const clang::CallExpr *expr)
    {
        const auto *calleeDecl = expr == nullptr ? nullptr : expr->getDirectCallee();
        if (calleeDecl == nullptr || !functionHasRenderSetParameter(calleeDecl))
        {
            return std::nullopt;
        }

        if (!calleeDecl->hasBody())
        {
            throw std::runtime_error("HLSL helper call to \"" + calleeDecl->getQualifiedNameAsString() + "\" uses UGL::RenderSet<T> but the helper has no body to rebind to global RenderSet resources.");
        }

        const RenderSetSpecializedFunctionRequest request = makeRenderSetSpecializedFunctionRequest(expr, calleeDecl);
        queueRenderSetSpecializedFunction(request);

        std::vector<std::string> argStrs;
        argStrs.reserve(expr->getNumArgs());
        for (unsigned i = 0; i < expr->getNumArgs(); ++i)
        {
            const auto targetType = tryGetCallParameterType(expr, i);
            if (targetType.has_value() && isRenderSetParameterType(*targetType, *this))
            {
                continue;
            }

            const std::vector<std::string> translatedArgs = translateCallArgument(expr, i);
            for (const auto &argStr : translatedArgs)
            {
                if (!argStr.empty())
                {
                    argStrs.push_back(argStr);
                }
            }
        }

        return request.qualifiedCallName + "(" + stringJoin(argStrs, ", ") + ")";
    }

    HLSLVisitor::RenderSetSpecializedFunctionRequest HLSLVisitor::makeRenderSetSpecializedFunctionRequest(const clang::CallExpr *expr, const clang::FunctionDecl *calleeDecl)
    {
        RenderSetSpecializedFunctionRequest request;
        request.functionDecl = calleeDecl;

        std::string aliasSuffix;
        for (unsigned i = 0; i < calleeDecl->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = calleeDecl->getParamDecl(i);
            if (!isRenderSetParameterType(param->getType(), *this))
            {
                continue;
            }

            validateRenderSetHelperParameterContract(param);
            if (i >= expr->getNumArgs())
            {
                throw std::runtime_error("HLSL helper call to \"" + calleeDecl->getQualifiedNameAsString() + "\" is missing an argument for RenderSet parameter \"" + param->getNameAsString() + "\".");
            }

            const std::string aliasName = extractRenderSetGlobalAliasName(expr->getArg(i));
            if (aliasName.empty())
            {
                throw std::runtime_error("HLSL helper call to \"" + calleeDecl->getQualifiedNameAsString() + "\" passes UGL::RenderSet<T> parameter \"" + param->getNameAsString() + "\" through an unsupported expression. Pass a direct render-set variable, shader-class render-set field, or previously-forwarded RenderSet parameter.");
            }

            request.renderSetAliases.push_back(aliasName);
            aliasSuffix += "_" + sanitizeHLSLIdentifier(aliasName);
        }

        const std::string qualifiedName = calleeDecl->getQualifiedNameAsString();
        request.functionName = flattenQualifiedHLSLIdentifierForHelperName(qualifiedName) + "_UGLRenderSetSpecialized_" + std::to_string(calleeDecl->getLocation().getRawEncoding()) + aliasSuffix;

        const std::string namespaceName = getFullNamespace(calleeDecl);
        request.qualifiedCallName = namespaceName.empty() ? request.functionName : makeQualifiedHLSLIdentifier(namespaceName) + "::" + request.functionName;
        request.key = getFunctionUniqueID(calleeDecl) + ":" + stringJoin(request.renderSetAliases, ",");
        return request;
    }

    void HLSLVisitor::queueRenderSetSpecializedFunction(const RenderSetSpecializedFunctionRequest &request)
    {
        if (request.key.empty() || mEmittedRenderSetSpecializedFunctionKeys.contains(request.key))
        {
            return;
        }
        if (mQueuedRenderSetSpecializedFunctionKeys.insert(request.key).second)
        {
            mPendingRenderSetSpecializedFunctionRequests.push_back(request);
        }
    }

    std::string HLSLVisitor::generateRenderSetSpecializedFunctionSignature(const RenderSetSpecializedFunctionRequest &request, bool emitSemicolon)
    {
        const clang::FunctionDecl *func = request.functionDecl;
        std::string result;
        result += getLineDirective(func->getBeginLoc());
        result += mSpaceManager.getSpace() + generateTypeCanonicalName(func->getReturnType(), &mTypeConvertor) + " " + request.functionName + "(";

        std::vector<std::string> parameterDecls;
        for (unsigned i = 0; i < func->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = func->getParamDecl(i);
            if (const auto bindGroupHandleParameter = tryGenerateBindGroupHandleParameter(param))
            {
                parameterDecls.push_back(*bindGroupHandleParameter);
                continue;
            }

            if (isRenderSetParameterType(param->getType(), *this))
            {
                validateRenderSetHelperParameterContract(param);
                continue;
            }
            validateInputOnlyShaderHandleParameterOrThrow(param, "HLSL");
            parameterDecls.push_back(generateFunctionSignatureParam(param, &mTypeConvertor));
        }

        result += stringJoin(parameterDecls, ", ");
        result += ")";
        result += emitSemicolon ? ";" + NewLine() : NewLine();
        return result;
    }

    void HLSLVisitor::processPendingRenderSetSpecializedFunctions()
    {
        size_t requestIndex = 0;
        while (requestIndex < mPendingRenderSetSpecializedFunctionRequests.size())
        {
            const RenderSetSpecializedFunctionRequest request = mPendingRenderSetSpecializedFunctionRequests[requestIndex++];
            if (!mEmittedRenderSetSpecializedFunctionKeys.insert(request.key).second)
            {
                continue;
            }

            const std::unordered_map<const clang::ValueDecl *, std::string> previousAliases = mRenderSetParameterGlobalAliases;
            const clang::FunctionDecl *previousFunctionDecl = mCurrentFunctionDecl;
            mRenderSetParameterGlobalAliases = previousAliases;
            mCurrentFunctionDecl = request.functionDecl;
            mLocalStorage.resetFunctionScope();
            mLocalGroupSharedGlobals.clear();

            size_t renderSetAliasIndex = 0;
            for (unsigned i = 0; i < request.functionDecl->getNumParams(); ++i)
            {
                const clang::ParmVarDecl *param = request.functionDecl->getParamDecl(i);
                if (!isRenderSetParameterType(param->getType(), *this))
                {
                    continue;
                }
                if (renderSetAliasIndex >= request.renderSetAliases.size())
                {
                    throw std::runtime_error("Internal error: HLSL RenderSet helper global-resource rebinding lost an alias for parameter \"" + param->getNameAsString() + "\".");
                }
                const auto *paramValueDecl = llvm::dyn_cast<clang::ValueDecl>(param->getCanonicalDecl());
                if (paramValueDecl != nullptr)
                {
                    mRenderSetParameterGlobalAliases[paramValueDecl] = request.renderSetAliases[renderSetAliasIndex];
                }
                ++renderSetAliasIndex;
            }

            std::string prototype = generateRenderSetSpecializedFunctionSignature(request, true);
            std::string definition = generateRenderSetSpecializedFunctionSignature(request, false);
            if (checkAttibuteByName(request.functionDecl, mUGLCTORName))
            {
                definition += generateUGLCTORFunctionBody(request.functionDecl);
            }
            else
            {
                definition += generateFunctionBody(request.functionDecl);
            }
            mLocalStorage.captureFunctionHelperDefinitions();

            const std::string namespaceName = getFullNamespace(request.functionDecl);
            if (!namespaceName.empty())
            {
                std::vector<std::string> namespaceParts;
                size_t cursor = 0;
                while (cursor < namespaceName.size())
                {
                    const size_t separator = namespaceName.find("::", cursor);
                    const std::string rawPart = separator == std::string::npos ? namespaceName.substr(cursor) : namespaceName.substr(cursor, separator - cursor);
                    const std::string namespacePart = sanitizeHLSLIdentifier(rawPart);
                    if (!namespacePart.empty())
                    {
                        namespaceParts.push_back(namespacePart);
                    }
                    if (separator == std::string::npos)
                    {
                        break;
                    }
                    cursor = separator + 2;
                }

                std::string namespacePrefix;
                for (const std::string &namespacePart : namespaceParts)
                {
                    namespacePrefix += mSpaceManager.getSpace() + "namespace " + namespacePart + NewLine();
                    namespacePrefix += mSpaceManager.getSpace() + "{" + NewLine();
                }

                std::string namespaceSuffix;
                for (auto partIt = namespaceParts.rbegin(); partIt != namespaceParts.rend(); ++partIt)
                {
                    namespaceSuffix += mSpaceManager.getSpace() + "} // namespace " + *partIt + NewLine();
                }
                namespaceSuffix += NewLine();

                prototype = namespacePrefix + prototype + namespaceSuffix;
                definition = namespacePrefix + definition + namespaceSuffix;
            }

            mRenderSetSpecializedFunctionPrototypes.push_back(std::move(prototype));
            mRenderSetSpecializedFunctionDefinitions.push_back(std::move(definition));

            mRenderSetParameterGlobalAliases = previousAliases;
            mCurrentFunctionDecl = previousFunctionDecl;
        }
    }

    std::optional<std::string> HLSLVisitor::tryTranslateUniformBufferArrowCall(const clang::CXXMemberCallExpr *expr, const clang::Expr *strippedBaseObjectExpr)
    {
        const auto *directBaseDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(strippedBaseObjectExpr == nullptr ? clang::QualType() : strippedBaseObjectExpr->getType());
        const std::string directBaseName = getClassCanonicalName(directBaseDecl, nullptr);
        if (directBaseDecl != nullptr && directBaseName.starts_with(mUGLShaderUniformBufferName))
        {
            const std::string objectExpr = TranslateExpr(strippedBaseObjectExpr);
            if (expr->getMethodDecl()->getNameAsString() == "read")
            {
                return objectExpr + ".value";
            }

            const std::vector<std::string> argStrs = collectTranslatedCallArguments(expr);
            return objectExpr + ".value." + sanitizeHLSLIdentifier(expr->getMethodDecl()->getNameAsString()) + "(" + stringJoin(argStrs, ", ") + ")";
        }

        const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(strippedBaseObjectExpr);
        if (operatorCallExpr == nullptr || operatorCallExpr->getOperator() != clang::OverloadedOperatorKind::OO_Arrow || operatorCallExpr->getNumArgs() == 0)
        {
            return std::nullopt;
        }

        const auto *operatorBaseDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(operatorCallExpr->getArg(0)->getType());
        const std::string operatorBaseName = getClassCanonicalName(operatorBaseDecl, nullptr);
        const auto *operatorResultDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(operatorCallExpr->getType());
        const std::string operatorResultName = getClassCanonicalName(operatorResultDecl, nullptr);
        if ((operatorResultDecl == nullptr || !operatorResultName.starts_with(mUGLShaderUniformBufferDataPacker)) && (operatorBaseDecl == nullptr || !operatorBaseName.starts_with(mUGLShaderUniformBufferName)))
        {
            return std::nullopt;
        }

        const std::string objectExpr = TranslateExpr(operatorCallExpr->getArg(0));
        if (expr->getMethodDecl()->getNameAsString() == "read")
        {
            return objectExpr + ".value";
        }

        const std::vector<std::string> argStrs = collectTranslatedCallArguments(expr);
        return objectExpr + ".value." + sanitizeHLSLIdentifier(expr->getMethodDecl()->getNameAsString()) + "(" + stringJoin(argStrs, ", ") + ")";
    }

    std::optional<std::string> HLSLVisitor::tryTranslateUniformBufferDataPackerCall(const clang::CXXMemberCallExpr *expr, const std::string &currentObjectExpr, const std::string &calleeTypeName, const std::string &methodName)
    {
        if (!calleeTypeName.starts_with(mUGLShaderUniformBufferDataPacker))
        {
            return std::nullopt;
        }

        const std::string uniformValueExpr = currentObjectExpr + ".value";
        if (methodName == "read")
        {
            return uniformValueExpr;
        }

        const std::vector<std::string> argStrs = collectTranslatedCallArguments(expr);
        return uniformValueExpr + "." + sanitizeHLSLIdentifier(methodName) + "(" + stringJoin(argStrs, ", ") + ")";
    }

    clang::QualType HLSLVisitor::unwrapArrayElementType(clang::QualType type) const
    {
        clang::QualType currentType = getUnqualifiedType(type);
        while (currentType->isArrayType())
        {
            const clang::ArrayType *arrayType = Context->getAsArrayType(currentType);
            if (arrayType == nullptr)
            {
                break;
            }
            currentType = getUnqualifiedType(arrayType->getElementType());
        }
        return currentType;
    }

    std::string HLSLVisitor::translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E)
    {
        if (llvm::isa<clang::CXXConversionDecl>(E->getMethodDecl()))
        {
            return TranslateExpr(E->getImplicitObjectArgument());
        }

        clang::Expr *baseObjectExpr = E->getImplicitObjectArgument();
        const clang::Expr *strippedBaseObjectExpr = PixelLocalFieldAnalysis::stripTransparentExprWrappers(baseObjectExpr);
        if (const auto translated = tryTranslateUniformBufferArrowCall(E, strippedBaseObjectExpr); translated.has_value())
        {
            return *translated;
        }

        if (const auto specializedCall = tryTranslateRenderSetSpecializedCall(E))
        {
            return *specializedCall;
        }

        std::string callee = TranslateExpr(E->getCallee());
        const std::string calleeTypeName = generateTypeCanonicalName(baseObjectExpr->getType());
        const auto *baseRecordDecl = getUnqualifiedType(baseObjectExpr->getType())->getAsCXXRecordDecl();
        const std::string baseRecordName = getClassCanonicalName(baseRecordDecl, nullptr);
        const std::string methodName = E->getMethodDecl()->getNameAsString();
        const std::string currentMethodSuffix = "." + methodName;
        const std::string currentObjectExpr = stripMemberCallSuffix(callee, currentMethodSuffix);

        if (baseRecordName == mUGLPixelLocalColorAttachmentName && methodName == "read")
        {
            if (E->getNumArgs() != 0)
            {
                throw std::runtime_error("Pixel-local attachment read() does not accept explicit coordinates; it reads the current pixel.");
            }
            const clang::FunctionDecl *currentFunction = mCurrentFunctionDecl != nullptr ? mCurrentFunctionDecl : mMainFunc;
            if (!PixelLocalInputPlan::isDirectPixelLocalInputFieldRead(*this, currentFunction, E))
            {
                throw std::runtime_error("PixelLocalColorAttachment::read() must be called directly on a PixelLocalColorAttachment field of a [[PixelLocalInput]] framebuffer parameter.");
            }
            return currentObjectExpr;
        }

        if (const auto translated = tryTranslateUniformBufferDataPackerCall(E, currentObjectExpr, calleeTypeName, methodName); translated.has_value())
        {
            return *translated;
        }

        if (isTexture2DAccessPackerType(calleeTypeName) || isTexture2DCanonicalName(baseRecordName))
        {
            const bool isReadWriteTexture = isReadWriteTexture2DAccessPackerType(calleeTypeName) || isReadWriteTextureCanonicalName(baseRecordName);
            if (const auto translated = mTextureMemberCallLowering.tryTranslateTexture2DMemberCall(E, currentObjectExpr, methodName, isReadWriteTexture))
            {
                return *translated;
            }
        }

        if (isTexture2DArrayAccessPackerType(calleeTypeName) || isTexture2DArrayCanonicalName(baseRecordName))
        {
            const bool isReadWriteTexture = isReadWriteTexture2DArrayAccessPackerType(calleeTypeName) || isReadWriteTextureCanonicalName(baseRecordName);
            if (const auto translated = mTextureMemberCallLowering.tryTranslateTexture2DArrayMemberCall(E, currentObjectExpr, methodName, isReadWriteTexture))
            {
                return *translated;
            }
        }

        if (isTexture3DAccessPackerType(calleeTypeName) || isTexture3DCanonicalName(baseRecordName))
        {
            const bool isReadWriteTexture = isReadWriteTexture3DAccessPackerType(calleeTypeName) || isReadWriteTextureCanonicalName(baseRecordName);
            if (const auto translated = mTextureMemberCallLowering.tryTranslateTexture3DMemberCall(E, currentObjectExpr, methodName, isReadWriteTexture))
            {
                return *translated;
            }
        }

        if (isRenderSetBufferComponentType(baseRecordName))
        {
            if (const auto translated = mRenderSetEmitter.tryTranslateBufferComponentCall(E, currentObjectExpr, methodName))
            {
                return *translated;
            }
        }

        if (isRenderSetTextureComponentType(baseRecordName))
        {
            if (const auto translated = mRenderSetEmitter.tryTranslateTextureComponentCall(E, currentObjectExpr, methodName))
            {
                return *translated;
            }
        }

        if (isRenderSetDataPackType(baseRecordName))
        {
            if (const auto translated = mRenderSetEmitter.tryTranslateDataPackCall(E, currentObjectExpr, methodName))
            {
                return *translated;
            }
        }

        const std::vector<std::string> argStrs = collectTranslatedCallArguments(E);
        return callee + generateTemplateCallArguments(E->getCallee()) + "(" + stringJoin(argStrs, ", ") + ")";
    }

    std::string HLSLVisitor::translateCXXConstructExprFunction(const clang::CXXConstructExpr *E)
    {
        const std::string targetTypeName = generateTypeCanonicalName(E->getType(), &mTypeConvertor);
        if (const auto vectorType = parseHLSLVectorType(targetTypeName))
        {
            if (E->getNumArgs() == 1)
            {
                const clang::QualType argType = getUnqualifiedType(E->getArg(0)->getType());
                const bool isScalarLikeArgument = argType->isBuiltinType() || argType->isEnumeralType() || isHLSLScalarLikeTypeName(generateTypeCanonicalName(argType, &mTypeConvertor));
                if (isScalarLikeArgument)
                {
                    const std::string argExpr = TranslateExpr(E->getArg(0));
                    return targetTypeName + "(" + makeRepeatedArgumentList(argExpr, vectorType->elementCount) + ")";
                }
            }
        }
        return BaseASTVisitor::translateCXXConstructExprFunction(E);
    }

    std::string HLSLVisitor::translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *E)
    {
        const std::string targetTypeName = generateTypeCanonicalName(E->getType(), &mTypeConvertor);
        if (const auto vectorType = parseHLSLVectorType(targetTypeName))
        {
            const clang::QualType subExprType = getUnqualifiedType(E->getSubExpr()->getType());
            const bool isScalarLikeArgument = subExprType->isBuiltinType() || subExprType->isEnumeralType() || isHLSLScalarLikeTypeName(generateTypeCanonicalName(subExprType, &mTypeConvertor));
            if (isScalarLikeArgument)
            {
                const std::string argExpr = TranslateExpr(E->getSubExpr());
                return targetTypeName + "(" + makeRepeatedArgumentList(argExpr, vectorType->elementCount) + ")";
            }
        }
        return BaseASTVisitor::translateCXXFunctionalCastExpr(E);
    }

    std::string HLSLVisitor::translateVarDecl(const clang::VarDecl *VD)
    {
        if (VD->isLocalVarDecl() && typeContainsHostResourceHandle(VD->getType()))
        {
            throwCodegenError(VD, "HLSL shader local variable \"" + VD->getNameAsString() + "\" uses a host-only resource handle type. Host-only resource handles such as UGL::Texture, UGL::TextureView, UGL::Buffer, UGL::BufferRange, UGL::Device, and UGL::Queue cannot be used in shader code.");
        }

        if (VD->isLocalVarDecl() && mResourceBindingEmitter.isBindGroupParameterType(VD->getType()))
        {
            std::string result = mResourceBindingEmitter.getBindGroupHandleTypeNameFromType(VD->getType()) + " " + sanitizeHLSLIdentifier(VD->getNameAsString()) + generateDeclArraySpecifier(VD->getType());
            if (VD->hasInit() && !isImplicitNode(VD->getInit()))
            {
                result += " = " + TranslateExpr(VD->getInit());
            }
            return result;
        }

        if (VD->isLocalVarDecl() && isLegacyStorageBufferType(VD->getType()))
        {
            throwLegacyStorageBufferMigrationError("Local variable \"" + VD->getNameAsString() + "\"");
        }
        if (VD->isLocalVarDecl() && (checkTypeCanonicalName(VD->getType(), mUGLShaderStructuredBufferName) || checkTypeCanonicalName(VD->getType(), mUGLShaderRWStructuredBufferName)))
        {
            if (const auto translated = mLocalStorage.tryTranslateAliasVarDecl(VD); translated.has_value())
            {
                return *translated;
            }
        }

        const clang::QualType groupSharedBaseType = unwrapArrayElementType(VD->getType());
        if (VD->isLocalVarDecl() && checkTypeCanonicalName(groupSharedBaseType, "UGL::GroupShared"))
        {
            const auto *canonicalDecl = VD->getCanonicalDecl();
            const std::string globalName = makeGeneratedIdentifier(sanitizeHLSLIdentifier(VD->getNameAsString()) + "_groupshared", VD->getLocation());
            mLocalGroupSharedGlobals[canonicalDecl] = globalName;

            if (mPendingGroupSharedGlobalNames.insert(globalName).second)
            {
                const auto templateArgs = getTemplateArgumentsFromType(groupSharedBaseType);
                const clang::QualType elementType = templateArgs.empty() ? clang::QualType() : getUnqualifiedType(templateArgs.front().getAsType());
                const std::string elementTypeName = generateTypeCanonicalName(elementType, &mTypeConvertor);
                mPendingGroupSharedGlobalDeclarations.push_back("groupshared " + elementTypeName + " " + globalName + generateDeclArraySpecifier(VD->getType()) + EOS());
            }
            return {};
        }

        std::vector<std::string> qualifiers;
        // Vulkan/SPIR-V must not see namespace-scope immutable shader globals
        // as hidden `$Globals` descriptor bindings. Preserve internal-linkage
        // semantics explicitly so DXC keeps these arrays/records in static
        // constant storage instead of materializing a uniform resource.
        if (VD->getStorageClass() == clang::SC_Static || shouldForceStaticConstGlobalForHLSL(VD))
        {
            qualifiers.emplace_back("static");
        }
        if (VD->isConstexpr())
        {
            qualifiers.emplace_back("constexpr");
        }
        if (VD->getType().isConstQualified())
        {
            qualifiers.emplace_back("const");
        }

        std::string result;
        if (!qualifiers.empty())
        {
            result += stringJoin(qualifiers, " ") + " ";
        }
        std::string variableName = sanitizeHLSLIdentifier(VD->getNameAsString());
        const std::string variableTypeName = generateTypeCanonicalName(VD->getType(), &mTypeConvertor);
        result += variableTypeName + " " + variableName + generateDeclArraySpecifier(VD->getType());

        if (VD->getType()->isArrayType() && VD->hasInit())
        {
            const auto *construction = llvm::dyn_cast<clang::CXXConstructExpr>(VD->getInit()->IgnoreParenImpCasts());
            if (construction != nullptr && construction->getConstructor()->isImplicit() &&
                construction->getConstructor()->isDefaultConstructor() && !construction->getConstructor()->isTrivial())
            {
                return result + " = " + mAggregateInitializer.emitDefaultInitializer(VD->getType());
            }
        }

        if (VD->hasInit() && !isImplicitNode(VD->getInit()))
        {
            if (const auto *atomicCall = mAggregateInitializer.unwrapAtomicReturningCall(VD->getInit()))
            {
                return mAggregateInitializer.buildAtomicReturningVarDecl(VD, qualifiers, variableTypeName, variableName, atomicCall);
            }

            if (const auto *conditionalExpr = mAggregateInitializer.unwrapUnsupportedConditionalExpr(VD->getInit()))
            {
                if (!VD->isLocalVarDecl())
                {
                    throwCodegenError(VD, "HLSL/SPIR-V does not support namespace-scope struct or array conditional initialization. Move the conditional selection into a shader helper body or initialize the value explicitly.");
                }

                std::vector<std::string> mutableQualifiers;
                for (const std::string &qualifier : qualifiers)
                {
                    if (qualifier != "const" && qualifier != "constexpr")
                    {
                        mutableQualifiers.push_back(qualifier);
                    }
                }

                result.clear();
                if (!mutableQualifiers.empty())
                {
                    result += stringJoin(mutableQualifiers, " ") + " ";
                }
                result += variableTypeName + " " + variableName + generateDeclArraySpecifier(VD->getType());
                result += EOS();
                result += mSpaceManager.getSpace() + "if (" + TranslateExpr(conditionalExpr->getCond()) + ")" + NewLine();
                result += enterScope();
                result += mSpaceManager.getSpace() + variableName + " = " + TranslateExpr(conditionalExpr->getTrueExpr()) + EOS();
                result += quitScope();
                result += mSpaceManager.getSpace() + "else" + NewLine();
                result += enterScope();
                result += mSpaceManager.getSpace() + variableName + " = " + TranslateExpr(conditionalExpr->getFalseExpr()) + EOS();
                result += quitScope();
                return result;
            }

            result += " = " + mAggregateInitializer.emitTypedInitializer(VD->getType(), VD->getInit());
        }
        return result;
    }

    std::string HLSLVisitor::generateBindGroupResourceDeclarations()
    {
        std::string result;
        for (const auto &[slotIndex, bindGroupInfo] : mBindGroupInfoMap)
        {
            (void)slotIndex;
            if (bindGroupInfo.isRenderSet)
            {
                result += mRenderSetEmitter.generateResourceDeclarations(bindGroupInfo);
                continue;
            }
            result += mResourceBindingEmitter.generateResourceDeclarations(bindGroupInfo);
        }
        return result;
    }

    std::string HLSLVisitor::generateBindGroupHandleMaterializations()
    {
        std::string result;
        for (const auto &[slotIndex, bindGroupInfo] : mBindGroupInfoMap)
        {
            (void)slotIndex;
            result += mResourceBindingEmitter.generateHandleMaterialization(bindGroupInfo);
        }
        return result;
    }

    std::string HLSLVisitor::generateStandaloneFunctionPrototype(const clang::FunctionDecl *func, const std::string &funcName)
    {
        if (func == nullptr)
        {
            return {};
        }
        std::string signature = generateStandaloneFunctionSignature(func, funcName, &mTypeConvertor);
        if (!signature.empty() && signature.back() == '\n')
        {
            signature.pop_back();
        }
        return signature + ";" + NewLine();
    }

    std::string HLSLVisitor::makePixelLocalInputStructName(const clang::CXXRecordDecl *recordDecl) const
    {
        return sanitizeHLSLIdentifier(recordDecl->getNameAsString()) + "_PixelLocalInput";
    }

    std::string HLSLVisitor::generatePixelLocalInputStructDefinition(const clang::CXXRecordDecl *recordDecl)
    {
        std::string structText;
        structText += mSpaceManager.getSpace() + "struct " + makePixelLocalInputStructName(recordDecl) + NewLine();
        structText += enterScope();
        for (const auto *field : recordDecl->fields())
        {
            structText += mSpaceManager.getSpace() + generateTypeCanonicalName(field->getType(), &mTypeConvertor) + " " + sanitizeHLSLIdentifier(field->getNameAsString()) + EOS();
        }
        structText += endClass();
        return structText;
    }

    void HLSLVisitor::configurePixelLocalFramebufferOutputFilter(const clang::FunctionDecl *shaderFunc)
    {
        mUsePixelLocalFramebufferOutputFilter = false;
        mPixelLocalFramebufferOutputFields.clear();
        if (mShaderClassDecl == nullptr || shaderFunc == nullptr || (!checkDerivedClassByName(mShaderClassDecl, mUGLRenderClassBaseName) && !checkDerivedClassByName(mShaderClassDecl, mUGLPixelLocalRenderClassBaseName)))
        {
            return;
        }

        const std::string functionName = shaderFunc->getNameAsString();
        if (functionName != mUGLFragmentShaderFunctionName && functionName != mUGLPixelShaderFunctionName)
        {
            return;
        }

        const clang::CXXRecordDecl *returnRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(getUnqualifiedType(shaderFunc->getReturnType()));
        if (returnRecord == nullptr || !checkDerivedClassByName(returnRecord, mUGLFrameBufferBaseName))
        {
            return;
        }

        bool hasPixelLocalAttachment = false;
        for (const clang::FieldDecl *field : returnRecord->fields())
        {
            const clang::CXXRecordDecl *fieldRecord = field->getType()->getAsCXXRecordDecl();
            const std::string fieldTypeName = getClassCanonicalName(fieldRecord, nullptr);
            if (fieldTypeName == mUGLPixelLocalColorAttachmentName || fieldTypeName == mUGLPixelLocalDepthAttachmentName)
            {
                hasPixelLocalAttachment = true;
                break;
            }
        }
        if (!hasPixelLocalAttachment)
        {
            return;
        }

        const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis writeAnalysis = PixelLocalFieldAnalysis::collectRenderTargetWriteFieldAnalysis(shaderFunc, returnRecord);
        if (checkDerivedClassByName(mShaderClassDecl, mUGLPixelLocalRenderClassBaseName) && functionName == mUGLPixelShaderFunctionName)
        {
            for (const clang::FieldDecl *field : returnRecord->fields())
            {
                const clang::CXXRecordDecl *fieldRecord = field->getType()->getAsCXXRecordDecl();
                const std::string fieldTypeName = getClassCanonicalName(fieldRecord, nullptr);
                const bool isDepthAttachment = fieldTypeName == mUGLDepthAttachmentName || fieldTypeName == mUGLPixelLocalDepthAttachmentName;
                const bool writesDepth = writeAnalysis.conservativeAllWrites || writeAnalysis.fields.contains(field->getNameAsString());
                if (isDepthAttachment && writesDepth)
                {
                    const bool isPixelLocalDepthAttachment = fieldTypeName == mUGLPixelLocalDepthAttachmentName;
                    throwCodegenError(field, "IPixelLocalRenderClass::pixel() cannot write " + std::string(isPixelLocalDepthAttachment ? "PixelLocalDepthAttachment" : "depth attachment") + " field \"" + field->getNameAsString() + "\". Write native depth from IRenderClass::fragment(), or store depth needed by later pixel-local passes in a PixelLocalColorAttachment field.");
                }
            }
        }
        mPixelLocalFramebufferOutputFields = writeAnalysis.fields;
        mUsePixelLocalFramebufferOutputFilter = !writeAnalysis.conservativeAllWrites;
    }

    std::string HLSLVisitor::generateMainFunction(const clang::FunctionDecl *shaderFunc)
    {
        if (shaderFunc == nullptr)
        {
            throw std::runtime_error("HLSL code generation cannot emit a shader entry because no entry function was resolved.");
        }

        mLocalStorage.resetFunctionScope();
        mLocalGroupSharedGlobals.clear();

        std::string funcName = shaderFunc->getNameAsString();
        std::string entryAttributes;
        if (funcName == ComputeFuncNameInRenderClass)
        {
            funcName = ComputeShaderEntryName;
            const auto *shaderClassDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(shaderFunc->getDeclContext());
            const auto workgroupSize = getLocalWorkgroupSize(shaderClassDecl);
            entryAttributes = "[numthreads(" + workgroupSize[0] + ", " + workgroupSize[1] + ", " + workgroupSize[2] + ")]" + NewLine();
        }
        else if (funcName == VertexFuncNameInRenderClass)
        {
            funcName = VertexShaderEntryName;
        }
        else if (funcName == FragmentFuncNameInRenderClass || funcName == PixelFuncNameInPixelLocalRenderClass)
        {
            funcName = FragmentShaderEntryName;
        }

        std::string result;
        std::string pixelLocalInputPrelude;
        std::string pixelLocalInputMaterialization;
        std::unordered_set<std::string> emittedPixelLocalInputStructs;
        mRenderInterfaceValidator.validateShaderEntryBuiltinParametersOrThrow(shaderFunc);
        mRenderInterfaceValidator.validateRenderEntityBuiltinContractOrThrow(shaderFunc, mBindGroupInfoMap);

        auto renderEntityIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityIDName);
        auto renderEntityInstanceIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityInstanceIDName);
        auto instanceIDVariable = getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeInstanceIDName);
        std::string renderEntityRenderSetName;
        if (renderEntityIDVariable != nullptr || renderEntityInstanceIDVariable != nullptr)
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
        std::string instanceIDVariableName;
        std::vector<std::string> mainFunctionParams;

        for (unsigned i = 0; i < shaderFunc->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = shaderFunc->getParamDecl(i);
            if (checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) || checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName))
            {
                continue;
            }

            if (checkAttibuteByName(param, mUGLAttributePixelLocalInputName))
            {
                const PixelLocalInputPlan::ParameterPlan inputPlan = PixelLocalInputPlan::collectParameterPlan(*this, shaderFunc, param);

                const std::string inputStructName = makePixelLocalInputStructName(inputPlan.recordDecl);
                if (emittedPixelLocalInputStructs.insert(inputStructName).second)
                {
                    pixelLocalInputPrelude += generatePixelLocalInputStructDefinition(inputPlan.recordDecl) + NewLine();
                }

                const std::string paramName = sanitizeHLSLIdentifier(param->getNameAsString());
                for (const PixelLocalInputPlan::AttachmentPlan &inputAttachment : inputPlan.attachments)
                {
                    const std::string fieldName = sanitizeHLSLIdentifier(inputAttachment.fieldName);
                    const auto templateArgs = getTemplateArgumentsFromType(inputAttachment.fieldDecl->getType());
                    if (templateArgs.empty())
                    {
                        throw std::runtime_error("Pixel-local input field \"" + inputAttachment.fieldName + "\" is missing its texture format template argument.");
                    }
                    const std::string typeName = MakeFormatToVectorTypeForFrameBuffer(translateTemplateArgument(templateArgs.front(), nullptr));
                    const std::string resourceName = "_UGLC_PixelLocalInput_" + paramName + "_" + fieldName;
                    pixelLocalInputPrelude += mSpaceManager.getSpace() + "[[vk::binding(" + std::to_string(inputAttachment.inputAttachmentIndex) + ", " + std::to_string(mPixelLocalInputDescriptorSetIndex) + ")]] [[vk::input_attachment_index(" + std::to_string(inputAttachment.inputAttachmentIndex) + ")]] SubpassInput<" + typeName + "> " + resourceName + EOS();
                }
                pixelLocalInputMaterialization += mSpaceManager.getSpace() + inputStructName + " " + paramName + EOS();
                for (const PixelLocalInputPlan::AttachmentPlan &inputAttachment : inputPlan.attachments)
                {
                    const std::string fieldName = sanitizeHLSLIdentifier(inputAttachment.fieldName);
                    const std::string resourceName = "_UGLC_PixelLocalInput_" + paramName + "_" + fieldName;
                    pixelLocalInputMaterialization += mSpaceManager.getSpace() + paramName + "." + fieldName + " = " + resourceName + ".SubpassLoad()" + EOS();
                }
                continue;
            }

            std::string paramText = generateTypeCanonicalName(param->getType(), &mTypeConvertor) + " " + sanitizeHLSLIdentifier(param->getNameAsString());
            paramText += mRenderInterfaceValidator.getParameterAttributeOrSemantic(param);
            mainFunctionParams.emplace_back(std::move(paramText));
        }

        if (instanceIDVariable == nullptr && (shaderFunc->getNameAsString() == mUGLVertexShaderFunctionName) && (renderEntityIDVariable != nullptr || renderEntityInstanceIDVariable != nullptr))
        {
            instanceIDVariableName = "_UGLC_InstanceID";
            mainFunctionParams.emplace_back("uint " + instanceIDVariableName + " : SV_InstanceID");
        }
        else if (instanceIDVariable != nullptr)
        {
            instanceIDVariableName = sanitizeHLSLIdentifier(instanceIDVariable->getNameAsString());
        }

        result += pixelLocalInputPrelude;
        result += mSpaceManager.getSpace() + entryAttributes;
        result += mSpaceManager.getSpace() + generateTypeCanonicalName(shaderFunc->getReturnType(), &mTypeConvertor) + " " + funcName + "(";
        result += stringJoin(mainFunctionParams, ", ");
        result += ")" + NewLine();
        result += enterScope();

        if (renderEntityIDVariable != nullptr || renderEntityInstanceIDVariable != nullptr)
        {
            result += mSpaceManager.getSpace() + "const uint2 __uglc_render_entity_cmd = " + renderEntityRenderSetName + "_UGLLoadRenderEntityCMDParamsSafe(" + instanceIDVariableName + ")" + EOS();
        }
        if (renderEntityIDVariable != nullptr)
        {
            result += mSpaceManager.getSpace() + "const uint " + sanitizeHLSLIdentifier(renderEntityIDVariable->getNameAsString()) + " = __uglc_render_entity_cmd.x" + EOS();
        }
        if (renderEntityInstanceIDVariable != nullptr)
        {
            result += mSpaceManager.getSpace() + "const uint " + sanitizeHLSLIdentifier(renderEntityInstanceIDVariable->getNameAsString()) + " = __uglc_render_entity_cmd.y" + EOS();
        }

        result += pixelLocalInputMaterialization;
        result += generateBindGroupHandleMaterializations();
        result += generateFunctionBody(shaderFunc);
        result += quitScope();
        return result;
    }

    EmittedShaderSource HLSLVisitor::generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                                    const BindGroupInfoMap &bindGroupInfoMap,
                                                    const clang::CXXRecordDecl *shaderClassDecl,
                                                    const clang::FunctionDecl *entryFunction,
                                                    const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        auto diagnosticScope = scopeDiagnosticLocation(entryFunction != nullptr ? static_cast<const clang::Decl *>(entryFunction) : static_cast<const clang::Decl *>(shaderClassDecl));
        if (templateSpecialization != nullptr)
        {
            pushTemplateSubstitutionContext(templateSpecialization);
        }
        try
        {
            mPixelLocalInputDescriptorSetIndex = 0;
            mShaderClassDecl = shaderClassDecl;
            mUsePixelLocalFramebufferOutputFilter = false;
            mPixelLocalFramebufferOutputFields.clear();
            mBindGroupInfoMap = bindGroupInfoMap;
            mLocalStorage.resetShaderScope();
            std::unordered_set<std::string> flatBindGroupNames;
            for (const auto &[slotIndex, bindGroupInfo] : mBindGroupInfoMap)
            {
                (void)slotIndex;
                if (!bindGroupInfo.isRenderSet)
                {
                    flatBindGroupNames.insert(sanitizeHLSLIdentifier(bindGroupInfo.name));
                }
            }
            mLocalStorage.setFlatBindGroupNames(std::move(flatBindGroupNames));
            mAggregateInitializer.resetShaderScope();
            mPendingGroupSharedGlobalDeclarations.clear();
            mPendingGroupSharedGlobalNames.clear();
            mEmittedRecordForwardDeclarationNames.clear();
            mEmittedRecordDefinitionNames.clear();
            mRenderSetParameterGlobalAliases.clear();
            mPendingRenderSetSpecializedFunctionRequests.clear();
            mQueuedRenderSetSpecializedFunctionKeys.clear();
            mEmittedRenderSetSpecializedFunctionKeys.clear();
            mRenderSetSpecializedFunctionPrototypes.clear();
            mRenderSetSpecializedFunctionDefinitions.clear();
            mMainFunc = entryFunction;
            if (mMainFunc == nullptr)
            {
                for (const auto *def : shaderDefs)
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
                throw std::runtime_error("Shader class \"" + shaderClassDecl->getQualifiedNameAsString() + "\" does not define a supported shader entry for HLSL code generation. Expected one of: vertex, fragment, or compute.");
            }
            mPixelLocalInputDescriptorSetIndex = PixelLocalInputPlan::functionHasPixelLocalInputParameter(*this, mMainFunc) ? resolvePixelLocalInputDescriptorSetIndex() : 0;
            configurePixelLocalFramebufferOutputFilter(mMainFunc);

            validateBindGroupInfoMapAgainstBackendOrThrow(getHLSLSPIRVShaderBackendCapabilities(), checkDerivedClassByName(shaderClassDecl, mUGLComputeClassBaseName) ? "ComputeClass" : "RenderClass", shaderClassDecl, mBindGroupInfoMap, 0);
            validateHLSLShaderBufferLayoutsOrThrow(mBindGroupInfoMap);

            if (checkDerivedClassByName(shaderClassDecl, mUGLRenderClassBaseName) || checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName))
            {
                const auto *hullShaderFunction = getMethodFromClass(shaderClassDecl, mUGLHullShaderFunctionName, makeHullShaderMethodLookupOptions());
                const auto *domainShaderFunction = getMethodFromClass(shaderClassDecl, mUGLDomainShaderFunctionName, makeDomainShaderMethodLookupOptions());
                validateRenderStageSupportOrThrow(getHLSLSPIRVShaderBackendCapabilities(), shaderClassDecl, hullShaderFunction, domainShaderFunction);
            }
            mRenderInterfaceValidator.validateEntryRenderInterfaceOrThrow(shaderClassDecl, mMainFunc);

            const std::vector<const clang::CXXRecordDecl *> bindGroupHandleRecords = collectBindGroupHandleRecords(shaderDefs);
            const std::unordered_set<const clang::CXXRecordDecl *> resourceBindingElementRecords = collectResourceBindingElementRecordDecls(*this, mBindGroupInfoMap);
            std::unordered_set<const clang::CXXRecordDecl *> resourceBindingMethodElementRecords;
            for (const clang::CXXRecordDecl *recordDecl : resourceBindingElementRecords)
            {
                if (recordHasUserProvidedMethodBodies(recordDecl))
                {
                    resourceBindingMethodElementRecords.insert(recordDecl);
                }
            }
            ShaderDeclarationEmissionSections emissionSections;
            const ShaderDeclarationEmissionPlanner declarationPlanner;
            const ShaderDeclarationEmissionPlan emissionPlan = declarationPlanner.build(shaderDefs, mMainFunc);

            for (const clang::CXXRecordDecl *recordDef : emissionPlan.recordForwardDeclarations)
            {
                emissionSections.recordForwardDeclarations += generateRecordForwardDeclaration(recordDef);
            }

            for (const clang::Decl *typeOrValueDecl : emissionPlan.recordAndVariableDefinitions)
            {
                if (const auto *recordDef = llvm::dyn_cast<clang::CXXRecordDecl>(typeOrValueDecl))
                {
                    const std::string recordDefinition = generateRecordDefinition(recordDef) + NewLine();
                    if (isShaderResourceBehaviorRecordDefinition(recordDef))
                    {
                        emissionSections.lateRecordDefinitions += recordDefinition;
                    }
                    else
                    {
                        if (recordHasUserProvidedMethodBodies(recordDef) && !isResourceBindingElementRecord(resourceBindingElementRecords, recordDef))
                        {
                            emissionSections.recordDefinitions += recordDefinition;
                        }
                        else if (recordHasUserProvidedMethodBodies(recordDef))
                        {
                            emissionSections.earlyRecordDefinitions += recordDefinition;
                        }
                        else
                        {
                            emissionSections.typeAndValueDefinitions += recordDefinition;
                        }
                    }
                    continue;
                }
                if (const auto *varDef = llvm::dyn_cast<clang::VarDecl>(typeOrValueDecl))
                {
                    emissionSections.typeAndValueDefinitions += wrapInLexicalNamespaceScopes(varDef, mSpaceManager.getSpace() + translateVarDecl(varDef) + EOS());
                }
            }
            for (const clang::FunctionDecl *funcDef : emissionPlan.helperFunctions)
            {
                if (functionHasRenderSetParameter(funcDef))
                {
                    for (unsigned i = 0; i < funcDef->getNumParams(); ++i)
                    {
                        validateRenderSetHelperParameterContract(funcDef->getParamDecl(i));
                    }
                    continue;
                }
                if (funcDef->hasBody())
                {
                    const std::string helperPrototype = wrapInLexicalNamespaceScopes(funcDef, generateStandaloneFunctionPrototype(funcDef, funcDef->getNameAsString()));
                    if (functionHasBindGroupHandleParameter(funcDef) ||
                        functionSignatureReferencesRecordSet(*this, funcDef, resourceBindingMethodElementRecords))
                    {
                        emissionSections.helperPrototypes += helperPrototype;
                    }
                    else
                    {
                        emissionSections.earlyHelperPrototypes += helperPrototype;
                    }
                    emissionSections.helperDefinitions += wrapInLexicalNamespaceScopes(funcDef, generateFunctionDefinition(funcDef));
                }
                else
                {
                    const std::string helperPrototype = wrapInLexicalNamespaceScopes(funcDef, generateStandaloneFunctionPrototype(funcDef, funcDef->getNameAsString()));
                    if (functionHasBindGroupHandleParameter(funcDef) ||
                        functionSignatureReferencesRecordSet(*this, funcDef, resourceBindingMethodElementRecords))
                    {
                        emissionSections.helperPrototypes += helperPrototype;
                    }
                    else
                    {
                        emissionSections.earlyHelperPrototypes += helperPrototype;
                    }
                }
            }
            for (const clang::TypedefNameDecl *typedefDecl : emissionPlan.typedefs)
            {
                emissionSections.typeAndValueDefinitions += wrapInLexicalNamespaceScopes(typedefDecl, VisitTypedefNameDecl(typedefDecl));
            }
            for (const clang::ClassTemplateDecl *templateClassDecl : emissionPlan.classTemplates)
            {
                emissionSections.typeAndValueDefinitions += wrapInLexicalNamespaceScopes(templateClassDecl, generateTemplateClassDecl(templateClassDecl));
            }
            for (const clang::FunctionTemplateDecl *functionTemplateDecl : emissionPlan.functionTemplates)
            {
                if (functionHasRenderSetParameter(functionTemplateDecl->getTemplatedDecl()))
                {
                    continue;
                }
                emissionSections.helperDefinitions += wrapInLexicalNamespaceScopes(functionTemplateDecl, generateTemplateFunctionDecl(functionTemplateDecl));
            }
            std::vector<std::string> shaderClassScopeParts = getShaderClassLexicalScopeParts(shaderClassDecl);
            for (std::string &namespacePart : shaderClassScopeParts)
            {
                namespacePart = sanitizeHLSLIdentifier(namespacePart);
            }
            emissionSections.shaderClassScopeDefinitions = wrapInNamespaceScopes(shaderClassScopeParts, mRecordEmitter.generateShaderClassNestedDeclarations(shaderClassDecl));

            for (const clang::CXXMethodDecl *methodDef : emissionPlan.helperMethods)
            {
                if (functionHasRenderSetParameter(methodDef))
                {
                    for (unsigned i = 0; i < methodDef->getNumParams(); ++i)
                    {
                        validateRenderSetHelperParameterContract(methodDef->getParamDecl(i));
                    }
                    continue;
                }
                emissionSections.helperPrototypes += wrapInLexicalNamespaceScopes(methodDef, generateStandaloneFunctionPrototype(methodDef, methodDef->getNameAsString()));
                emissionSections.methodDefinitions += generateFunctionDefinition(methodDef);
            }

            emissionSections.entryDefinition = generateMainFunction(mMainFunc);
            mLocalStorage.captureFunctionHelperDefinitions();
            processPendingRenderSetSpecializedFunctions();

            emissionSections.backendTypeDefinitions += generateBindGroupUniformWrapperDefinitions(bindGroupHandleRecords);
            emissionSections.backendTypeDefinitions += generateBindGroupHandleStructDefinitions(bindGroupHandleRecords);
            emissionSections.backendResourceDeclarations += generateBindGroupResourceDeclarations();
            for (const auto &groupSharedDeclaration : mPendingGroupSharedGlobalDeclarations)
            {
                emissionSections.groupSharedDeclarations += groupSharedDeclaration;
            }
            for (const auto &prototype : mRenderSetSpecializedFunctionPrototypes)
            {
                emissionSections.backendSpecializedHelperPrototypes += prototype;
            }
            for (const auto &helperDefinition : mLocalStorage.pendingHelperDefinitions())
            {
                emissionSections.backendGeneratedHelperDefinitions += helperDefinition + NewLine();
            }
            for (const auto &helperDefinition : mAggregateInitializer.pendingHelperDefinitions())
            {
                emissionSections.backendGeneratedHelperDefinitions += helperDefinition + NewLine();
            }
            for (const auto &definition : mRenderSetSpecializedFunctionDefinitions)
            {
                emissionSections.backendSpecializedHelperDefinitions += definition;
            }
            std::string result = emissionSections.render();

            EmittedShaderSource emittedSource;
            emittedSource.backend = getHLSLSPIRVShaderBackendCapabilities().kind;
            emittedSource.stage = getShaderStageKindForEntry(mMainFunc);
            emittedSource.backendName = getHLSLSPIRVShaderBackendCapabilities().backendName;
            emittedSource.stageName = getShaderStageDisplayName(emittedSource.stage);
            emittedSource.entryPoint = getGeneratedEntryPointName(mMainFunc);
            emittedSource.debugName = shaderClassDecl->getQualifiedNameAsString() + "::" + (mMainFunc != nullptr ? mMainFunc->getNameAsString() : std::string("unknown"));
            const clang::SourceLocation sourceLocation = mMainFunc != nullptr ? mMainFunc->getLocation() : shaderClassDecl->getLocation();
            emittedSource.sourceName = Context->getSourceManager().getFilename(sourceLocation).str();
            emittedSource.preludeText = MakeHLSLPreludeSource();
            emittedSource.sourceText = std::move(result);
            emittedSource.enableLineDirectives = true;
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
            rethrowCodegenError(error, entryFunction != nullptr ? static_cast<const clang::Decl *>(entryFunction) : static_cast<const clang::Decl *>(shaderClassDecl));
        }
    }
} // namespace UGLC::CodeGen::HLSL
