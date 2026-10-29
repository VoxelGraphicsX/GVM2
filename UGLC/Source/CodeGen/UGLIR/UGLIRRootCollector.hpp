#pragma once

#include "UGLIRShaderRoots.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/ADT/ArrayRef.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    /** Discovers concrete UGL shader class roots by walking a translation unit once. */
    class UGLIRRootCollector : public clang::RecursiveASTVisitor<UGLIRRootCollector>
    {
    public:
        /** Creates a collector bound to one Clang AST context. */
        explicit UGLIRRootCollector(clang::ASTContext &context);

        /** Collects every shader class root reachable from the translation unit. */
        std::vector<ShaderClassRoot> collectRoots();

        /** Collects shader class roots while walking the translation unit. */
        bool VisitCXXRecordDecl(const clang::CXXRecordDecl *decl);

        /** Collects shader specializations that are only reachable through host wrapper fields. */
        bool VisitFieldDecl(const clang::FieldDecl *decl);

        /** Collects shader specializations that are only reachable through host wrapper variables. */
        bool VisitVarDecl(const clang::VarDecl *decl);

        /** Collects shader wrapper specializations returned by device factory calls. */
        bool VisitCallExpr(const clang::CallExpr *expression);

    private:
        bool isUninstantiatedTemplatePattern(const clang::CXXRecordDecl *recordDecl) const;
        bool tryClassifyShaderClass(const clang::CXXRecordDecl *decl, ShaderClassKind &kind) const;
        void addShaderRoot(const clang::CXXRecordDecl *recordDecl, ShaderClassKind kind);
        void collectShaderWrapperType(const clang::QualType &type);

        clang::ASTContext &mContext;
        clang::SourceManager &mSourceManager;
        std::vector<ShaderClassRoot> mShaderRoots;
        std::unordered_set<std::string> mShaderRootNames;
    };
} // namespace UGLC::CodeGen::UGLIR
