#pragma once

#include <string>

#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * Provides backend-neutral default statement translation for BaseASTVisitor.
     *
     * The class owns only the common C++ statement lowering rules. Backend visitors can still
     * override the public BaseASTVisitor virtual methods, while the base implementation delegates
     * its default statement bodies here.
     *
     * DSL example:
     * @code
     * if (threadID.x < count)
     * {
     *     output[threadID.x] = input[threadID.x];
     * }
     * @endcode
     *
     * Generated shader example:
     * @code
     * if (threadID.x < count)
     * {
     *     output[threadID.x] = input[threadID.x];
     * };
     * @endcode
     */
    class BaseStatementTranslator
    {
    public:
        /** Creates a translator that calls back into the owning visitor for expressions and virtual hooks. */
        explicit BaseStatementTranslator(BaseASTVisitor &visitor);

        /** Dispatches a Clang statement node to the matching BaseASTVisitor translation hook. */
        std::string translateStatement(const clang::Stmt *stmt);

        /** Emits a compound statement body with the current visitor indentation policy. */
        std::string translateCompoundStmt(const clang::CompoundStmt *stmt);

        /** Emits variable declarations contained in a declaration statement. */
        std::string translateDeclStmt(const clang::DeclStmt *stmt);

        /** Emits the default return statement spelling. */
        std::string translateReturnStmt(const clang::ReturnStmt *stmt);

        /** Emits an if/else statement and folds if constexpr to the selected compile-time branch. */
        std::string translateIfStmt(const clang::IfStmt *stmt);

        /** Emits a for loop with initializer, condition, increment, and body. */
        std::string translateForStmt(const clang::ForStmt *stmt);

        /** Emits a break statement. */
        std::string translateBreakStmt(const clang::BreakStmt *stmt);

        /** Emits a continue statement. */
        std::string translateContinueStmt(const clang::ContinueStmt *stmt);

        /** Emits a while loop. */
        std::string translateWhileStmt(const clang::WhileStmt *stmt);

        /** Emits a do/while loop. */
        std::string translateDoStmt(const clang::DoStmt *stmt);

        /** Emits a switch statement and preserves an optional C++17 switch initializer. */
        std::string translateSwitchStmt(const clang::SwitchStmt *stmt);

        /** Emits a case label, including GNU range-case spelling when present. */
        std::string translateCaseStmt(const clang::CaseStmt *stmt);

        /** Emits a default label. */
        std::string translateDefaultStmt(const clang::DefaultStmt *stmt);

    private:
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
