#include "BaseTemplateArgumentEvaluator.hpp"

#include "BaseASTVisitor.hpp"
#include "Diagnostics.hpp"

#include <clang/AST/Expr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/Casting.h>

#include <string>

namespace UGLC::CodeGen
{
    BaseTemplateArgumentEvaluator::BaseTemplateArgumentEvaluator(const BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::vector<clang::TemplateArgument> BaseTemplateArgumentEvaluator::getArgumentsFromType(const clang::QualType &qt) const
    {
        const clang::QualType realQt = mVisitor.getUnqualifiedType(qt);
        std::vector<clang::TemplateArgument> args;

        if (const auto *recordType = realQt->getAs<clang::RecordType>())
        {
            const clang::RecordDecl *recordDecl = recordType->getDecl();
            if (const auto *specDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordDecl))
            {
                const auto &templateArgs = specDecl->getTemplateArgs();
                for (unsigned i = 0; i < templateArgs.size(); ++i)
                {
                    collectFlattenedArguments(templateArgs.get(i), args);
                }
                return args;
            }
        }

        if (const auto *templateSpecType = realQt->getAs<clang::TemplateSpecializationType>())
        {
            for (const clang::TemplateArgument &arg : templateSpecType->template_arguments())
            {
                collectFlattenedArguments(arg, args);
            }
        }

        return args;
    }

    void BaseTemplateArgumentEvaluator::collectFlattenedArguments(const clang::TemplateArgument &arg, std::vector<clang::TemplateArgument> &outArgs) const
    {
        if (arg.getKind() == clang::TemplateArgument::Pack)
        {
            for (auto it = arg.pack_begin(); it != arg.pack_end(); ++it)
            {
                collectFlattenedArguments(*it, outArgs);
            }
            return;
        }

        if (arg.getKind() == clang::TemplateArgument::Type)
        {
            if (const auto resolvedType = mVisitor.tryResolveTemplateSubstitutionType(arg.getAsType()); resolvedType.has_value())
            {
                outArgs.emplace_back(*resolvedType);
                return;
            }
        }

        outArgs.push_back(arg);
    }

    int64_t BaseTemplateArgumentEvaluator::evaluateIntegerArgument(const clang::TemplateArgument &arg) const
    {
        if (arg.getKind() == clang::TemplateArgument::Integral)
        {
            return arg.getAsIntegral().getExtValue();
        }

        if (arg.getKind() != clang::TemplateArgument::Expression)
        {
            mVisitor.throwCodegenError("Template integer argument must be an integral constant or integral expression, but found unsupported template argument kind "
                                       + std::to_string(static_cast<int>(arg.getKind())) + ".");
        }

        const clang::Expr *initExpr = arg.getAsExpr();
        if (initExpr == nullptr)
        {
            mVisitor.throwCodegenError("Expected a compile-time integer template argument expression, but Clang provided a null expression node.");
        }

        clang::Expr::EvalResult intResult;
        if (initExpr->EvaluateAsInt(intResult, *mVisitor.Context))
        {
            return intResult.Val.getInt().getExtValue();
        }
        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(initExpr->IgnoreParenImpCasts()))
        {
            if (const auto resolvedValue = mVisitor.tryResolveTemplateSubstitutionValue(declRefExpr->getDecl()); resolvedValue.has_value())
            {
                return std::stoll(*resolvedValue);
            }
        }

        std::string expressionText = initExpr->getStmtClassName();
        if (mVisitor.Context != nullptr)
        {
            const clang::SourceManager &sourceManager = mVisitor.Context->getSourceManager();
            const clang::SourceLocation beginLoc = normalizeDiagnosticLocation(sourceManager, initExpr->getBeginLoc());
            const clang::SourceLocation endLoc = normalizeDiagnosticLocation(sourceManager, initExpr->getEndLoc());
            if (beginLoc.isValid() && endLoc.isValid())
            {
                const llvm::StringRef sourceText = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(beginLoc, endLoc),
                                                                               sourceManager,
                                                                               mVisitor.Context->getLangOpts());
                if (!sourceText.empty())
                {
                    expressionText = sourceText.str();
                }
            }
        }

        mVisitor.throwCodegenError(initExpr,
                                   "Template integer argument must be a compile-time integer constant, but expression \""
                                       + expressionText + "\" could not be evaluated as an integer.");
    }
} // namespace UGLC::CodeGen
