#pragma once

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>

#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>

#include <string>

namespace UGLC::CodeGen::HLSL
{
    /** Returns the global HLSL record type name shared by definitions, forward declarations, and record-body emitters. */
    inline std::string generateGlobalHLSLRecordTypeName(BaseASTVisitor &visitor, const clang::CXXRecordDecl *decl, const AbstractTypeConvertor *typeConvertor)
    {
        if (decl == nullptr)
        {
            return {};
        }
        const auto *currentSpecialization = visitor.getCurrentTemplateSubstitutionContext();
        const bool isCurrentTemplatePrimary = currentSpecialization != nullptr &&
                                              currentSpecialization->getSpecializedTemplate() != nullptr &&
                                              currentSpecialization->getSpecializedTemplate()->getTemplatedDecl()->getCanonicalDecl() == decl->getCanonicalDecl();
        if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl) ||
            decl->getDescribedClassTemplate() != nullptr ||
            isCurrentTemplatePrimary)
        {
            return visitor.generateRecordDefinitionName(decl, typeConvertor);
        }
        return flattenQualifiedHLSLIdentifierForHelperName(decl->getQualifiedNameAsString());
    }
} // namespace UGLC::CodeGen::HLSL
