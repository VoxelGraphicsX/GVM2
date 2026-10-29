#include "CPPPixelLocalAnalysis.hpp"

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/PixelLocalInputPlan.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TemplateBase.h"

#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        constexpr size_t PixelLocalAccessPolicyIndex = 1;
        constexpr size_t PixelLocalStoragePolicyIndex = 2;
        constexpr size_t PixelLocalLoadPolicyIndex = 3;
        constexpr size_t PixelLocalStorePolicyIndex = 4;

        enum class PixelLocalAccessPolicy
        {
            ReadOnly,
            WriteOnly,
            ReadWrite
        };

        enum class PixelLocalStoragePolicy
        {
            Transient,
            Persistent
        };

        enum class PixelLocalLoadPolicy
        {
            DontCare,
            Clear,
            Load
        };

        enum class PixelLocalStorePolicy
        {
            Discard,
            Store
        };

        template <typename Policy>
        struct PixelLocalEnumPolicyCase
        {
            const char *enumeratorName = nullptr;
            Policy policy;
        };

        template <typename Policy>
        Policy resolvePixelLocalEnumPolicy(
            BaseASTVisitor &visitor,
            const clang::FieldDecl *fieldDecl,
            size_t policyIndex,
            const char *policyName,
            const char *expectedEnumName,
            std::initializer_list<PixelLocalEnumPolicyCase<Policy>> cases)
        {
            const std::vector<clang::TemplateArgument> templateArgs = visitor.getTemplateArgumentsFromType(fieldDecl->getType());
            if (templateArgs.size() <= policyIndex)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" is missing its " + std::string(policyName) + " policy.");
            }

            const clang::TemplateArgument &policyArgument = templateArgs.at(policyIndex);
            if (policyArgument.getKind() != clang::TemplateArgument::Integral)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" must use a " + std::string(expectedEnumName) + " enum value for its " + std::string(policyName) + " policy.");
            }

            const clang::QualType policyType = visitor.getUnqualifiedType(policyArgument.getIntegralType());
            const auto *enumType = policyType->getAs<clang::EnumType>();
            const clang::EnumDecl *enumDecl = enumType == nullptr ? nullptr : enumType->getDecl()->getCanonicalDecl();
            const std::string actualEnumName = enumDecl == nullptr ? visitor.generateTypeCanonicalName(policyType) : enumDecl->getQualifiedNameAsString();
            if (enumDecl == nullptr || actualEnumName != expectedEnumName)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" uses " + std::string(policyName) + " policy enum type \"" + actualEnumName + "\", expected \"" + std::string(expectedEnumName) + "\".");
            }

            const int64_t policyValue = policyArgument.getAsIntegral().getExtValue();
            for (const PixelLocalEnumPolicyCase<Policy> &policyCase : cases)
            {
                for (const clang::EnumConstantDecl *enumerator : enumDecl->enumerators())
                {
                    if (enumerator->getNameAsString() == policyCase.enumeratorName &&
                        enumerator->getInitVal().getExtValue() == policyValue)
                    {
                        return policyCase.policy;
                    }
                }
            }

            visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" uses unsupported " + std::string(policyName) + " policy value \"" + std::to_string(policyValue) + "\".");
            throw std::logic_error("unreachable PixelLocal enum policy diagnostic path");
        }

        PixelLocalAccessPolicy resolvePixelLocalAccessPolicy(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            return resolvePixelLocalEnumPolicy<PixelLocalAccessPolicy>(
                visitor,
                fieldDecl,
                PixelLocalAccessPolicyIndex,
                "PixelLocalAccess",
                "UGL::PixelLocalAccess",
                {
                    {"ReadOnly", PixelLocalAccessPolicy::ReadOnly},
                    {"WriteOnly", PixelLocalAccessPolicy::WriteOnly},
                    {"ReadWrite", PixelLocalAccessPolicy::ReadWrite},
                });
        }

        PixelLocalStoragePolicy resolvePixelLocalStoragePolicy(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            return resolvePixelLocalEnumPolicy<PixelLocalStoragePolicy>(
                visitor,
                fieldDecl,
                PixelLocalStoragePolicyIndex,
                "PixelLocalStorage",
                "UGL::PixelLocalStorage",
                {
                    {"Transient", PixelLocalStoragePolicy::Transient},
                    {"Persistent", PixelLocalStoragePolicy::Persistent},
                });
        }

        PixelLocalLoadPolicy resolvePixelLocalLoadPolicy(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            return resolvePixelLocalEnumPolicy<PixelLocalLoadPolicy>(
                visitor,
                fieldDecl,
                PixelLocalLoadPolicyIndex,
                "PixelLocalLoad",
                "UGL::PixelLocalLoad",
                {
                    {"DontCare", PixelLocalLoadPolicy::DontCare},
                    {"Clear", PixelLocalLoadPolicy::Clear},
                    {"Load", PixelLocalLoadPolicy::Load},
                });
        }

        PixelLocalStorePolicy resolvePixelLocalStorePolicy(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            return resolvePixelLocalEnumPolicy<PixelLocalStorePolicy>(
                visitor,
                fieldDecl,
                PixelLocalStorePolicyIndex,
                "PixelLocalStore",
                "UGL::PixelLocalStore",
                {
                    {"Discard", PixelLocalStorePolicy::Discard},
                    {"Store", PixelLocalStorePolicy::Store},
                });
        }

        void validatePixelLocalReadPolicyOrThrow(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            if (resolvePixelLocalAccessPolicy(visitor, fieldDecl) == PixelLocalAccessPolicy::WriteOnly)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" is declared PixelLocalAccess::WriteOnly but is read by a pixel-local entry.");
            }
        }

        void validatePixelLocalWritePolicyOrThrow(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            if (resolvePixelLocalAccessPolicy(visitor, fieldDecl) == PixelLocalAccessPolicy::ReadOnly)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" is declared PixelLocalAccess::ReadOnly but is written by a pixel-local entry.");
            }
        }

        void validatePixelLocalStoragePolicyOrThrow(BaseASTVisitor &visitor, const clang::FieldDecl *fieldDecl)
        {
            if (resolvePixelLocalStoragePolicy(visitor, fieldDecl) != PixelLocalStoragePolicy::Transient)
            {
                return;
            }

            const PixelLocalLoadPolicy loadPolicy = resolvePixelLocalLoadPolicy(visitor, fieldDecl);
            const PixelLocalStorePolicy storePolicy = resolvePixelLocalStorePolicy(visitor, fieldDecl);
            if (loadPolicy == PixelLocalLoadPolicy::Load || storePolicy == PixelLocalStorePolicy::Store)
            {
                visitor.throwCodegenError(fieldDecl, "Pixel-local attachment field \"" + fieldDecl->getNameAsString() + "\" is declared PixelLocalStorage::Transient but requests persistent load/store semantics. Use PixelLocalLoad::Clear or DontCare with PixelLocalStore::Discard, or declare PixelLocalStorage::Persistent and back it with a persistent texture.");
            }
        }

        void validatePixelLocalColorAttachmentIndex(BaseASTVisitor &visitor, const clang::FieldDecl *field, uint32_t colorIndex)
        {
            if (colorIndex >= 64u)
            {
                visitor.throwCodegenError(field, "Pixel-local render target uses more than 64 color attachments; the current RHI attachment access mask supports up to 64 color slots.");
            }
        }

        bool isSameFunctionDecl(const clang::FunctionDecl *lhs, const clang::FunctionDecl *rhs)
        {
            return lhs != nullptr && rhs != nullptr && lhs->getCanonicalDecl() == rhs->getCanonicalDecl();
        }

        bool hasPixelLocalReadWriteOverlap(const PixelLocalAttachmentAccessMask &access)
        {
            return (access.colorReadMask & access.colorWriteMask) != 0u;
        }

        void mergePixelLocalAttachmentAccessMask(PixelLocalAttachmentAccessMask &destination, const PixelLocalAttachmentAccessMask &source)
        {
            destination.colorReadMask |= source.colorReadMask;
            destination.colorWriteMask |= source.colorWriteMask;
            destination.depthWrite = destination.depthWrite || source.depthWrite;
        }

        bool isUGLFunctionCall(const clang::CallExpr *callExpr, const std::string &qualifiedName)
        {
            if (callExpr == nullptr)
            {
                return false;
            }
            const clang::FunctionDecl *callee = callExpr->getDirectCallee();
            return callee != nullptr && callee->getQualifiedNameAsString() == qualifiedName;
        }
    } // namespace

    class PixelLocalReadCallValidator final : public clang::RecursiveASTVisitor<PixelLocalReadCallValidator>
    {
    public:
        PixelLocalReadCallValidator(const CPPPixelLocalAnalysis &analysis, BaseASTVisitor &visitor, const clang::FunctionDecl *currentFunction, const clang::FunctionDecl *allowedPixelFunction)
            : mAnalysis(analysis), mVisitor(visitor), mCurrentFunction(currentFunction), mAllowedPixelFunction(allowedPixelFunction)
        {
        }

        bool VisitVarDecl(clang::VarDecl *decl)
        {
            mAnalysis.validateAttachmentValueObjectVarOrThrow(decl);
            return true;
        }

        bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr *expr)
        {
            const clang::CXXMethodDecl *method = expr == nullptr ? nullptr : expr->getMethodDecl();
            if (method == nullptr || method->getNameAsString() != "read")
            {
                return true;
            }

            const std::string baseRecordName = mVisitor.getClassCanonicalName(method->getParent(), nullptr);
            if (baseRecordName != mUGLPixelLocalColorAttachmentName)
            {
                return true;
            }

            if (isSameFunctionDecl(mCurrentFunction, mAllowedPixelFunction) &&
                PixelLocalInputPlan::isDirectPixelLocalInputFieldRead(mVisitor, mCurrentFunction, expr))
            {
                return true;
            }

            mVisitor.throwCodegenError(
                expr,
                "PixelLocalColorAttachment::read() must be called directly on a PixelLocalColorAttachment field of a [[PixelLocalInput]] framebuffer parameter in IPixelLocalRenderClass::pixel(...).");
            return false;
        }

    private:
        const CPPPixelLocalAnalysis &mAnalysis;
        BaseASTVisitor &mVisitor;
        const clang::FunctionDecl *mCurrentFunction = nullptr;
        const clang::FunctionDecl *mAllowedPixelFunction = nullptr;
    };

    CPPPixelLocalAnalysis::CPPPixelLocalAnalysis(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    bool CPPPixelLocalAnalysis::renderTargetHasPixelLocalAttachment(const clang::CXXRecordDecl *recordDecl) const
    {
        if (recordDecl == nullptr)
        {
            return false;
        }

        for (const clang::FieldDecl *field : recordDecl->fields())
        {
            const clang::CXXRecordDecl *fieldRecord = field->getType()->getAsCXXRecordDecl();
            const std::string fieldTypeName = mVisitor.getClassCanonicalName(fieldRecord, nullptr);
            if (fieldTypeName == mUGLPixelLocalColorAttachmentName || fieldTypeName == mUGLPixelLocalDepthAttachmentName)
            {
                return true;
            }
        }
        return false;
    }

    bool CPPPixelLocalAnalysis::isPixelLocalAttachmentType(const clang::QualType &type) const
    {
        const clang::CXXRecordDecl *recordDecl = mVisitor.getUnqualifiedType(type)->getAsCXXRecordDecl();
        const std::string typeName = mVisitor.getClassCanonicalName(recordDecl, nullptr);
        return typeName == mUGLPixelLocalColorAttachmentName || typeName == mUGLPixelLocalDepthAttachmentName;
    }

    void CPPPixelLocalAnalysis::validateAttachmentValueObjectVarOrThrow(const clang::VarDecl *decl) const
    {
        if (decl == nullptr || !decl->isLocalVarDecl() || !decl->hasInit() || !isPixelLocalAttachmentType(decl->getType()))
        {
            return;
        }

        mVisitor.throwCodegenError(
            decl,
            "Pixel-local attachment local variable \"" + decl->getNameAsString() + "\" has type \"" + mVisitor.generateTypeCanonicalName(decl->getType()) + "\". PixelLocalColorAttachment/PixelLocalDepthAttachment value copy or move construction is treated as deleted; call read() directly on the [[PixelLocalInput]] framebuffer field instead.");
    }

    void CPPPixelLocalAnalysis::validateInputAndReadUsage(const clang::CXXRecordDecl *decl, const clang::FunctionDecl *allowedPixelFunction) const
    {
        for (const clang::CXXMethodDecl *method : decl->methods())
        {
            if (PixelLocalInputPlan::functionHasPixelLocalInputParameter(mVisitor, method) && !isSameFunctionDecl(method, allowedPixelFunction))
            {
                mVisitor.throwCodegenError(method, "[[PixelLocalInput]] parameters are only valid on IPixelLocalRenderClass::pixel(...).");
            }

            if (method->hasBody())
            {
                PixelLocalReadCallValidator readValidator(*this, mVisitor, method, allowedPixelFunction);
                readValidator.TraverseStmt(method->getBody());
            }
        }
    }

    void CPPPixelLocalAnalysis::accumulateReadAccess(const clang::FunctionDecl *shaderFunc, PixelLocalAttachmentAccessMask &access) const
    {
        if (shaderFunc == nullptr)
        {
            return;
        }

        for (const PixelLocalInputPlan::ParameterPlan &parameterPlan : PixelLocalInputPlan::collectFunctionPlan(mVisitor, shaderFunc).parameters)
        {
            for (const PixelLocalInputPlan::AttachmentPlan &attachment : parameterPlan.attachments)
            {
                validatePixelLocalColorAttachmentIndex(mVisitor, attachment.fieldDecl, attachment.colorAttachmentIndex);
                validatePixelLocalReadPolicyOrThrow(mVisitor, attachment.fieldDecl);
                access.colorReadMask |= (uint64_t{1u} << attachment.colorAttachmentIndex);
            }
        }
    }

    void CPPPixelLocalAnalysis::accumulateWriteAccess(
        const clang::CXXRecordDecl *renderTargetRecord,
        const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis,
        bool isPixelOnlyOperation,
        PixelLocalAttachmentAccessMask &access) const
    {
        if (renderTargetRecord == nullptr)
        {
            return;
        }

        uint32_t colorIndex = 0u;
        for (const clang::FieldDecl *field : renderTargetRecord->fields())
        {
            const clang::CXXRecordDecl *fieldRecord = field->getType()->getAsCXXRecordDecl();
            const std::string fieldTypeName = mVisitor.getClassCanonicalName(fieldRecord, nullptr);
            if (fieldTypeName == mUGLColorAttachmentName || fieldTypeName == mUGLPixelLocalColorAttachmentName)
            {
                validatePixelLocalColorAttachmentIndex(mVisitor, field, colorIndex);
                const bool writesField = writeAnalysis.conservativeAllWrites || writeAnalysis.fields.contains(field->getNameAsString());
                if (fieldTypeName == mUGLPixelLocalColorAttachmentName && writesField)
                {
                    validatePixelLocalWritePolicyOrThrow(mVisitor, field);
                }
                if (writesField)
                {
                    access.colorWriteMask |= (uint64_t{1u} << colorIndex);
                }
                ++colorIndex;
                continue;
            }

            if (fieldTypeName == mUGLDepthAttachmentName || fieldTypeName == mUGLPixelLocalDepthAttachmentName)
            {
                const bool writesDepth = writeAnalysis.conservativeAllWrites || writeAnalysis.fields.contains(field->getNameAsString());
                if (isPixelOnlyOperation)
                {
                    if (writesDepth)
                    {
                        const bool isPixelLocalDepthAttachment = fieldTypeName == mUGLPixelLocalDepthAttachmentName;
                        mVisitor.throwCodegenError(
                            field,
                            "IPixelLocalRenderClass::pixel() cannot write " + std::string(isPixelLocalDepthAttachment ? "PixelLocalDepthAttachment" : "depth attachment") + " field \"" + field->getNameAsString() + "\". Write native depth from IRenderClass::fragment(), or store depth needed by later pixel-local passes in a PixelLocalColorAttachment field.");
                    }
                    access.depthWrite = false;
                    continue;
                }
                if (writesDepth && fieldTypeName == mUGLPixelLocalDepthAttachmentName)
                {
                    validatePixelLocalWritePolicyOrThrow(mVisitor, field);
                }
                access.depthWrite = writesDepth;
            }
        }
    }

    void CPPPixelLocalAnalysis::validateEntryAccessOrThrow(
        const clang::FunctionDecl *shaderFunc,
        const PixelLocalAttachmentAccessMask &access,
        const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis) const
    {
        const bool hasReadWriteOverlap = (access.colorReadMask & access.colorWriteMask) != 0u;
        if (!hasReadWriteOverlap)
        {
            return;
        }

        if (writeAnalysis.conservativeAllWrites)
        {
            mVisitor.throwCodegenError(
                shaderFunc,
                "Pixel-local entry reads color attachments and returns a framebuffer write pattern UGLC cannot analyze precisely. Assign the returned framebuffer fields directly, or split the dependency with nextPixelLocalPass().");
        }
        else
        {
            mVisitor.throwCodegenError(
                shaderFunc,
                "Pixel-local entry reads and writes the same color attachment in one phase. Use nextPixelLocalPass() to read values produced by an earlier pixel-local task.");
        }
    }

    std::optional<PixelLocalAttachmentAccessMask> CPPPixelLocalAnalysis::resolveRenderClassAccess(const clang::CXXRecordDecl *renderClassDecl) const
    {
        if (renderClassDecl == nullptr)
        {
            return std::nullopt;
        }

        const bool isPixelLocalRenderClass = mVisitor.checkDerivedClassByName(renderClassDecl, mUGLPixelLocalRenderClassBaseName);
        const bool isOrdinaryRenderClass = mVisitor.checkDerivedClassByName(renderClassDecl, mUGLRenderClassBaseName);
        if (!isPixelLocalRenderClass && !isOrdinaryRenderClass)
        {
            return std::nullopt;
        }

        const auto *vertexMethod = mVisitor.getMethodFromClass(renderClassDecl, mUGLVertexShaderFunctionName, mVisitor.makeVertexShaderMethodLookupOptions());
        const auto *pixelMethod = mVisitor.getMethodFromClass(renderClassDecl, mUGLPixelShaderFunctionName, mVisitor.makeFragmentShaderMethodLookupOptions(std::nullopt));
        const clang::FunctionDecl *entryMethod = mVisitor.getMethodFromClass(
            renderClassDecl,
            mUGLFragmentShaderFunctionName,
            mVisitor.makeFragmentShaderMethodLookupOptions(vertexMethod != nullptr ? std::optional<clang::QualType>(vertexMethod->getReturnType()) : std::nullopt));
        if (isPixelLocalRenderClass)
        {
            entryMethod = pixelMethod;
        }
        if (entryMethod == nullptr)
        {
            return std::nullopt;
        }

        const auto *renderTargetRecord = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(entryMethod->getReturnType());
        if (renderTargetRecord == nullptr)
        {
            return std::nullopt;
        }

        PixelLocalAttachmentAccessMask access = {};
        const bool isPixelOnlyOperation = isPixelLocalRenderClass && pixelMethod != nullptr && entryMethod == pixelMethod;
        const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis writeAnalysis = PixelLocalFieldAnalysis::collectRenderTargetWriteFieldAnalysis(entryMethod, renderTargetRecord);
        if (isPixelLocalRenderClass)
        {
            accumulateReadAccess(entryMethod, access);
        }
        accumulateWriteAccess(renderTargetRecord, writeAnalysis, isPixelOnlyOperation, access);
        return access;
    }

    void CPPPixelLocalAnalysis::validatePassExpressionOrThrow(const clang::Expr *expr) const
    {
        const auto *callExpr = llvm::dyn_cast_or_null<clang::CallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(expr));
        if (!isUGLFunctionCall(callExpr, "UGL::pixelLocalPass"))
        {
            return;
        }

        PixelLocalAttachmentAccessMask phaseAccess = {};
        bool phaseHasTask = false;
        for (const clang::Expr *arg : callExpr->arguments())
        {
            const auto *argCall = llvm::dyn_cast_or_null<clang::CallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(arg));
            if (isUGLFunctionCall(argCall, "UGL::nextPixelLocalPass"))
            {
                if (phaseHasTask)
                {
                    phaseAccess = {};
                    phaseHasTask = false;
                }
                continue;
            }

            const auto *operatorCall = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(PixelLocalFieldAnalysis::stripTransparentExprWrappers(arg));
            if (operatorCall == nullptr || operatorCall->getOperator() != clang::OO_Call || operatorCall->getNumArgs() == 0)
            {
                continue;
            }

            const clang::Expr *renderClassObject = PixelLocalFieldAnalysis::stripTransparentExprWrappers(operatorCall->getArg(0));
            if (renderClassObject == nullptr)
            {
                continue;
            }

            const auto templateArgs = mVisitor.getTemplateArgumentsFromType(renderClassObject->getType());
            if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type)
            {
                continue;
            }

            const clang::CXXRecordDecl *renderClassDecl = templateArgs.front().getAsType()->getAsCXXRecordDecl();
            std::optional<PixelLocalAttachmentAccessMask> taskAccess = resolveRenderClassAccess(renderClassDecl);
            if (!taskAccess.has_value())
            {
                continue;
            }

            if (hasPixelLocalReadWriteOverlap(*taskAccess))
            {
                mVisitor.throwCodegenError(
                    arg,
                    "Pixel-local renderPass phase reads and writes the same color attachment across tasks. Insert nextPixelLocalPass() between the writer task and the reader task.");
            }

            mergePixelLocalAttachmentAccessMask(phaseAccess, *taskAccess);
            phaseHasTask = true;
            if (hasPixelLocalReadWriteOverlap(phaseAccess))
            {
                mVisitor.throwCodegenError(
                    arg,
                    "Pixel-local renderPass phase reads and writes the same color attachment across tasks. Insert nextPixelLocalPass() between the writer task and the reader task.");
            }
        }
    }

    std::string CPPPixelLocalAnalysis::resolveLoadOp(const clang::FieldDecl *fieldDecl) const
    {
        validatePixelLocalStoragePolicyOrThrow(mVisitor, fieldDecl);
        switch (resolvePixelLocalLoadPolicy(mVisitor, fieldDecl))
        {
        case PixelLocalLoadPolicy::Clear:
            return "GVM::RHI::LoadOp::Clear";
        case PixelLocalLoadPolicy::Load:
            return "GVM::RHI::LoadOp::Load";
        case PixelLocalLoadPolicy::DontCare:
            return "GVM::RHI::LoadOp::Undefined";
        }

        throw std::logic_error("unreachable PixelLocalLoad policy");
    }

    std::string CPPPixelLocalAnalysis::resolveStoreOp(const clang::FieldDecl *fieldDecl) const
    {
        validatePixelLocalStoragePolicyOrThrow(mVisitor, fieldDecl);
        switch (resolvePixelLocalStorePolicy(mVisitor, fieldDecl))
        {
        case PixelLocalStorePolicy::Store:
            return "GVM::RHI::StoreOp::Store";
        case PixelLocalStorePolicy::Discard:
            return "GVM::RHI::StoreOp::Discard";
        }

        throw std::logic_error("unreachable PixelLocalStore policy");
    }
} // namespace UGLC::CodeGen::CPP
