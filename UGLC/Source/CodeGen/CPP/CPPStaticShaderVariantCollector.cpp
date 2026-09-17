#include "CPPStaticShaderVariantCollector.hpp"

#include "CPPVisitor.hpp"
#include "../UGLC.Constants.hpp"

#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen::CPP
{
    CPPStaticShaderVariantCollector::CPPStaticShaderVariantCollector(const CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    bool CPPStaticShaderVariantCollector::VisitVarDecl(clang::VarDecl *decl)
    {
        collectWrapperType(decl->getType());
        return true;
    }

    bool CPPStaticShaderVariantCollector::VisitFieldDecl(clang::FieldDecl *decl)
    {
        collectWrapperType(decl->getType());
        return true;
    }

    bool CPPStaticShaderVariantCollector::VisitCallExpr(clang::CallExpr *expr)
    {
        collectWrapperType(expr->getType());
        return true;
    }

    const std::unordered_set<const clang::ClassTemplateSpecializationDecl *> &CPPStaticShaderVariantCollector::getMaterializedTemplateSpecializations() const
    {
        return mMaterializedTemplateSpecializations;
    }

    const std::unordered_set<const clang::ClassTemplateSpecializationDecl *> &CPPStaticShaderVariantCollector::getShaderVariantRootSpecializations() const
    {
        return mShaderVariantRootSpecializations;
    }

    void CPPStaticShaderVariantCollector::collectWrapperType(const clang::QualType &type)
    {
        if (type.isNull())
        {
            return;
        }

        const clang::CXXRecordDecl *recordDecl = mVisitor.getUnqualifiedType(type)->getAsCXXRecordDecl();
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string wrapperName = mVisitor.getClassCanonicalName(recordDecl);
        const bool isShaderWrapper = wrapperName == "UGL::RenderClass" || wrapperName == "UGL::ComputeClass";
        const bool isMaterializedWrapper = isShaderWrapper || wrapperName == mUGLRenderSetName || wrapperName == mUGLBindGroupName;
        if (!isMaterializedWrapper)
        {
            return;
        }

        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(type);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type)
        {
            return;
        }

        registerSpecializationType(templateArgs.front().getAsType(), isShaderWrapper);
    }

    void CPPStaticShaderVariantCollector::registerSpecializationType(const clang::QualType &type, bool isShaderRoot)
    {
        if (type.isNull())
        {
            return;
        }

        const clang::CXXRecordDecl *recordDecl = mVisitor.getUnqualifiedType(type)->getAsCXXRecordDecl();
        const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
        if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
        {
            return;
        }

        const clang::CXXRecordDecl *primaryDecl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
        if (primaryDecl == nullptr || mVisitor.isFromExcludedFile(primaryDecl->getLocation()))
        {
            return;
        }

        const auto *canonicalSpecialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(specializationDecl->getCanonicalDecl());
        mMaterializedTemplateSpecializations.emplace(canonicalSpecialization);
        if (isShaderRoot)
        {
            mShaderVariantRootSpecializations.emplace(canonicalSpecialization);
        }

        const clang::TemplateArgumentList &templateArgs = specializationDecl->getTemplateArgs();
        for (unsigned index = 0; index < templateArgs.size(); ++index)
        {
            const clang::TemplateArgument &argument = templateArgs.get(index);
            if (argument.getKind() == clang::TemplateArgument::Type)
            {
                registerSpecializationType(argument.getAsType(), false);
            }
            else if (argument.getKind() == clang::TemplateArgument::Pack)
            {
                for (auto it = argument.pack_begin(); it != argument.pack_end(); ++it)
                {
                    if (it->getKind() == clang::TemplateArgument::Type)
                    {
                        registerSpecializationType(it->getAsType(), false);
                    }
                }
            }
        }
    }
} // namespace UGLC::CodeGen::CPP
