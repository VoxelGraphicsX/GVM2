#pragma once

#include <unordered_set>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>

namespace UGLC::CodeGen::CPP
{
    class CPPVisitor;

    /** Collects concrete static shader template variants referenced through UGL host wrapper types. */
    class CPPStaticShaderVariantCollector final : public clang::RecursiveASTVisitor<CPPStaticShaderVariantCollector>
    {
    public:
        /** Creates a collector that uses the visitor only as a read-only AST query facade. */
        explicit CPPStaticShaderVariantCollector(const CPPVisitor &visitor);

        /** Visits variable declarations so wrapper-typed locals and globals can materialize variants. */
        bool VisitVarDecl(clang::VarDecl *decl);

        /** Visits field declarations so wrapper-typed renderer and shader fields can materialize variants. */
        bool VisitFieldDecl(clang::FieldDecl *decl);

        /** Visits call expressions so create-return wrapper types can materialize variants. */
        bool VisitCallExpr(clang::CallExpr *expr);

        /** Returns every concrete template specialization that must be emitted as an erased ordinary type. */
        const std::unordered_set<const clang::ClassTemplateSpecializationDecl *> &getMaterializedTemplateSpecializations() const;

        /** Returns the subset of materialized specializations that are shader class artifact roots. */
        const std::unordered_set<const clang::ClassTemplateSpecializationDecl *> &getShaderVariantRootSpecializations() const;

    private:
        const CPPVisitor &mVisitor;
        std::unordered_set<const clang::ClassTemplateSpecializationDecl *> mMaterializedTemplateSpecializations;
        std::unordered_set<const clang::ClassTemplateSpecializationDecl *> mShaderVariantRootSpecializations;

        /** Records a wrapper type's concrete template argument when it names a supported UGL host wrapper. */
        void collectWrapperType(const clang::QualType &type);

        /** Recursively records a concrete template specialization and any nested type specializations it references. */
        void registerSpecializationType(const clang::QualType &type, bool isShaderRoot);
    };
} // namespace UGLC::CodeGen::CPP
