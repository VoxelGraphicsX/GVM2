#pragma once
#include "BaseASTVisitor.hpp"
#include "ShaderBindGroupInfo.hpp"
#include <unordered_map>
#include <unordered_set>

namespace UGLC::CodeGen
{

    /**
     * @brief Collects the declarations that a shader entry point needs before backend emission.
     *
     * The visitor walks the resolved Clang AST from one shader entry function and records
     * referenced user records, helper functions, and top-level constants in source
     * order. Namespaces are preserved only as lexical scopes around each selected
     * declaration, so unrelated namespace members are not emitted into shader artifacts.
     */
    class ShaderReferenceVisitor final : public BaseASTVisitor
    {
        std::unordered_map<std::string, const clang::Decl *> mRefDefs;
        std::vector<const clang::Decl *> mRefResults;
        std::unordered_set<std::string> mTraversedFunctions;
        std::unordered_map<const clang::Type *, bool> mHostResourceTypeCache;
        std::unordered_map<const clang::CXXRecordDecl *, bool> mHostResourceRecordCache;

    public:
        explicit ShaderReferenceVisitor(clang::ASTContext *Context);

        /**
         * @brief Returns declarations collected by the most recent solve in source order.
         */
        std::vector<const clang::Decl *> getRefResult() const
        {
            return mRefResults;
        }

        /**
         * @brief Solves all source declarations needed by a shader entry.
         *
         * Example: a DSL entry that calls a namespace-level `shade()` helper and reads
         * a lookup table causes the helper function, table variable, return/input
         * records, and relevant namespace-scoped declaration wrappers to be emitted
         * before the backend entry text.
         */
        void solveReference(const clang::FunctionDecl *func,
                            const BindGroupInfoMap &bindGroupInfoMap,
                            const std::vector<clang::Decl *> &extraDecls,
                            const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr);

    private:
        bool shouldTraverseFunctionDefinition(const clang::FunctionDecl *func) const;
        bool shouldCollectFunctionDefinition(const clang::FunctionDecl *func) const;
        /** Returns a cached answer for whether one type contains a host-only resource handle. */
        bool shaderTypeContainsHostResourceHandle(const clang::QualType &type);
        /** Returns a cached answer for whether one record contains a host-only resource handle. */
        bool shaderRecordContainsHostResourceHandles(const clang::CXXRecordDecl *decl);
        /** Records all shader-visible record dependencies reachable from a concrete or substituted type. */
        void solveType(const clang::QualType &type, bool diagnoseHostOnlyUse = true);
        /** Records a concrete record dependency and rejects host-only records when shader code actually reaches them. */
        void solveRecord(const clang::CXXRecordDecl *decl, bool diagnoseHostOnlyUse = true);
        /** Records a reachable function-template definition without collecting unrelated namespace members. */
        void solveFunctionTemplate(const clang::FunctionTemplateDecl *decl);
        /** Records record dependencies that appear inside a template argument. */
        void solveTemplateArgumentRecords(const clang::TemplateArgument &arg);
        /** Records record dependencies that appear in one concrete bind-group resource binding. */
        void solveResourceBinding(const BaseShaderResourceBinding &resourceBinding);
        void solveFunction(const clang::FunctionDecl *func);
        void sortReferences();
        void traverseStmt(const clang::Stmt *stmt);
        void traverseVarDecl(const clang::VarDecl *decl);
        void traverseExpr(const clang::Expr *expr);
    };

} // namespace UGLC::CodeGen
