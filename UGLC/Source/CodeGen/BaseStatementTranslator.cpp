#include "BaseStatementTranslator.hpp"

#include "BaseASTVisitor.hpp"

#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen
{
    BaseStatementTranslator::BaseStatementTranslator(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::string BaseStatementTranslator::translateStatement(const clang::Stmt *stmt)
    {
        if (stmt == nullptr)
        {
            return "";
        }
        if (auto *compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt))
        {
            return mVisitor.translateCompoundStmt(compoundStmt);
        }
        if (auto *declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt))
        {
            return mVisitor.translateDeclStmt(declStmt);
        }
        if (auto *returnStmt = llvm::dyn_cast<clang::ReturnStmt>(stmt))
        {
            return mVisitor.translateReturnStmt(returnStmt);
        }
        if (auto *ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt))
        {
            return mVisitor.translateIfStmt(ifStmt);
        }
        if (auto *forStmt = llvm::dyn_cast<clang::ForStmt>(stmt))
        {
            return mVisitor.translateForStmt(forStmt);
        }
        if (auto *breakStmt = llvm::dyn_cast<clang::BreakStmt>(stmt))
        {
            return mVisitor.translateBreakStmt(breakStmt);
        }
        if (auto *continueStmt = llvm::dyn_cast<clang::ContinueStmt>(stmt))
        {
            return mVisitor.translateContinueStmt(continueStmt);
        }
        if (auto *whileStmt = llvm::dyn_cast<clang::WhileStmt>(stmt))
        {
            return mVisitor.translateWhileStmt(whileStmt);
        }
        if (auto *doStmt = llvm::dyn_cast<clang::DoStmt>(stmt))
        {
            return mVisitor.translateDoStmt(doStmt);
        }
        if (auto *expr = llvm::dyn_cast<clang::Expr>(stmt))
        {
            return mVisitor.TranslateExpr(expr);
        }
        if (auto *switchStmt = llvm::dyn_cast<clang::SwitchStmt>(stmt))
        {
            return mVisitor.translateSwitchStmt(switchStmt);
        }
        if (auto *caseStmt = llvm::dyn_cast<clang::CaseStmt>(stmt))
        {
            return mVisitor.translateCaseStmt(caseStmt);
        }
        if (auto *defaultStmt = llvm::dyn_cast<clang::DefaultStmt>(stmt))
        {
            return mVisitor.translateDefaultStmt(defaultStmt);
        }

        return mVisitor.makeUnsupportedPlaceholder("statement", stmt->getStmtClassName());
    }

    std::string BaseStatementTranslator::translateCompoundStmt(const clang::CompoundStmt *stmt)
    {
        std::string body = mVisitor.enterScope();
        for (auto *subStmt : stmt->body())
        {
            body += mVisitor.getLineDirective(subStmt->getBeginLoc());
            body += mVisitor.mSpaceManager.getSpace() + mVisitor.TranslateStmt(subStmt);
            body += mVisitor.EOS();
        }
        body += mVisitor.quitScope();
        return body;
    }

    std::string BaseStatementTranslator::translateDeclStmt(const clang::DeclStmt *stmt)
    {
        std::string result;
        for (auto *decl : stmt->decls())
        {
            if (auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
            {
                result += mVisitor.translateVarDecl(varDecl);
                if (!stmt->isSingleDecl())
                {
                    result += ";";
                }
            }
            else
            {
                result += mVisitor.makeUnsupportedPlaceholder("decl", decl->getDeclKindName());
            }
        }
        return result;
    }

    std::string BaseStatementTranslator::translateReturnStmt(const clang::ReturnStmt *stmt)
    {
        return "return " + (stmt->getRetValue() ? mVisitor.TranslateExpr(stmt->getRetValue()) : "");
    }

    std::string BaseStatementTranslator::translateIfStmt(const clang::IfStmt *stmt)
    {
        if (stmt->isConstexpr())
        {
            if (stmt->getInit() != nullptr)
            {
                mVisitor.throwCodegenError(stmt, "UGLC if constexpr does not support C++17 if initializers in shader code.");
            }

            bool conditionValue = false;
            if (!stmt->getCond()->EvaluateAsBooleanCondition(conditionValue, *mVisitor.Context))
            {
                if (const auto substitutedCondition = mVisitor.tryEvaluateTemplateSubstitutionBooleanCondition(stmt->getCond()); substitutedCondition.has_value())
                {
                    conditionValue = *substitutedCondition;
                }
                else
                {
                    mVisitor.throwCodegenError(stmt->getCond(), "UGLC if constexpr condition must be a compile-time boolean expression.");
                }
            }

            const clang::Stmt *selectedBranch = conditionValue ? stmt->getThen() : stmt->getElse();
            return selectedBranch == nullptr ? "" : mVisitor.TranslateStmt(selectedBranch);
        }

        std::string result = "if (" + mVisitor.TranslateExpr(stmt->getCond()) + ")" + mVisitor.NewLine();
        if (!llvm::isa<clang::CompoundStmt>(stmt->getThen()))
            result += "{" + mVisitor.NewLine();
        result += mVisitor.TranslateStmt(stmt->getThen());
        if (!llvm::isa<clang::CompoundStmt>(stmt->getThen()))
            result += mVisitor.EOS() + "}" + mVisitor.NewLine();
        if (stmt->getElse())
        {
            result += mVisitor.mSpaceManager.getSpace() + "else ";
            if (!llvm::isa<clang::CompoundStmt>(stmt->getElse()))
                result += "{" + mVisitor.NewLine();
            result += mVisitor.TranslateStmt(stmt->getElse());
            if (!llvm::isa<clang::CompoundStmt>(stmt->getElse()))
                result += mVisitor.EOS() + "}" + mVisitor.NewLine();
        }
        return result;
    }

    std::string BaseStatementTranslator::translateForStmt(const clang::ForStmt *stmt)
    {
        std::string result = "for (";

        if (stmt->getInit())
        {
            result += mVisitor.TranslateStmt(stmt->getInit());
        }
        result += "; ";

        if (stmt->getCond())
        {
            result += mVisitor.TranslateExpr(stmt->getCond());
        }
        result += "; ";

        if (stmt->getInc())
        {
            result += mVisitor.TranslateExpr(stmt->getInc());
        }
        result += ") " + mVisitor.NewLine();

        if (stmt->getBody())
        {
            result += mVisitor.TranslateStmt(stmt->getBody());
        }

        return result;
    }

    std::string BaseStatementTranslator::translateBreakStmt(const clang::BreakStmt *stmt)
    {
        (void)stmt;
        return "break";
    }

    std::string BaseStatementTranslator::translateContinueStmt(const clang::ContinueStmt *stmt)
    {
        (void)stmt;
        return "continue";
    }

    std::string BaseStatementTranslator::translateWhileStmt(const clang::WhileStmt *stmt)
    {
        std::string result = "while (";

        if (stmt->getCond())
        {
            result += mVisitor.TranslateExpr(stmt->getCond());
        }
        result += ") " + mVisitor.NewLine();

        if (stmt->getBody())
        {
            result += mVisitor.TranslateStmt(stmt->getBody());
        }

        return result;
    }

    std::string BaseStatementTranslator::translateDoStmt(const clang::DoStmt *stmt)
    {
        std::string result = "do" + mVisitor.NewLine();

        if (stmt->getBody())
        {
            result += mVisitor.TranslateStmt(stmt->getBody());
        }
        else
        {
            result += mVisitor.enterScope();
            result += mVisitor.quitScope();
        }

        result += mVisitor.mSpaceManager.getSpace() + "while (";
        if (stmt->getCond())
        {
            result += mVisitor.TranslateExpr(stmt->getCond());
        }
        result += ")";

        return result;
    }

    std::string BaseStatementTranslator::translateSwitchStmt(const clang::SwitchStmt *stmt)
    {
        std::string result;

        if (const clang::Stmt *initStmt = stmt->getInit())
        {
            result += mVisitor.TranslateStmt(initStmt) + ";\n";
        }

        result += "switch (";
        if (const clang::Expr *condition = stmt->getCond())
        {
            result += mVisitor.TranslateExpr(condition);
        }
        result += ")\n";
        result += mVisitor.TranslateStmt(stmt->getBody());

        return result;
    }

    std::string BaseStatementTranslator::translateCaseStmt(const clang::CaseStmt *stmt)
    {
        std::string result = "case ";
        result += mVisitor.TranslateExpr(stmt->getLHS());

        if (stmt->getRHS())
        {
            result += " ... " + mVisitor.TranslateExpr(stmt->getRHS());
        }

        result += ":\n";
        if (stmt->getSubStmt())
        {
            result += mVisitor.TranslateStmt(stmt->getSubStmt());
        }

        return result;
    }

    std::string BaseStatementTranslator::translateDefaultStmt(const clang::DefaultStmt *stmt)
    {
        std::string result = "default:\n";
        if (stmt->getSubStmt())
        {
            result += mVisitor.TranslateStmt(stmt->getSubStmt());
        }
        return result;
    }
} // namespace UGLC::CodeGen
