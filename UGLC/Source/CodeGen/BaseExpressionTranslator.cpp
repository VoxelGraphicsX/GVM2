#include "BaseExpressionTranslator.hpp"

#include "BaseASTVisitor.hpp"

#include <clang/Lex/Lexer.h>
#include <llvm/Support/Casting.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace UGLC::CodeGen
{
    namespace
    {
        const clang::Expr *stripTransparentExprWrappers(const clang::Expr *expr)
        {
            const clang::Expr *current = expr;
            while (current != nullptr)
            {
                current = current->IgnoreParenImpCasts();
                if (const auto *cleanupsExpr = llvm::dyn_cast<clang::ExprWithCleanups>(current))
                {
                    current = cleanupsExpr->getSubExpr();
                    continue;
                }
                if (const auto *materializeExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
                {
                    current = materializeExpr->getSubExpr();
                    continue;
                }
                if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
                {
                    current = bindTemporaryExpr->getSubExpr();
                    continue;
                }
                if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(current))
                {
                    current = constantExpr->getSubExpr();
                    continue;
                }
                return current;
            }
            return expr;
        }

        bool shouldParenthesizeInfixOperand(const clang::Expr *expr)
        {
            const clang::Expr *normalizedExpr = stripTransparentExprWrappers(expr);
            if (normalizedExpr == nullptr)
            {
                return false;
            }
            if (llvm::isa<clang::BinaryOperator>(normalizedExpr) || llvm::isa<clang::AbstractConditionalOperator>(normalizedExpr))
            {
                return true;
            }
            if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(normalizedExpr))
            {
                return operatorCallExpr->isInfixBinaryOp();
            }
            return false;
        }

        bool isFullyParenthesizedExpression(const std::string &expression)
        {
            if (expression.size() < 2 || expression.front() != '(' || expression.back() != ')')
            {
                return false;
            }

            int depth = 0;
            for (size_t index = 0; index < expression.size(); ++index)
            {
                const char current = expression[index];
                if (current == '(')
                {
                    ++depth;
                    continue;
                }
                if (current != ')')
                {
                    continue;
                }

                --depth;
                if (depth == 0 && index + 1 < expression.size())
                {
                    return false;
                }
                if (depth < 0)
                {
                    return false;
                }
            }
            return depth == 0;
        }

        void appendFixedHex(std::string &result, uint32_t value, unsigned width)
        {
            static constexpr char kHexDigits[] = "0123456789ABCDEF";
            for (unsigned index = 0; index < width; ++index)
            {
                const unsigned shift = (width - index - 1) * 4;
                result.push_back(kHexDigits[(value >> shift) & 0xF]);
            }
        }

        void appendFixedOctal(std::string &result, uint32_t value)
        {
            result.push_back('\\');
            result.push_back(static_cast<char>('0' + ((value >> 6) & 0x7)));
            result.push_back(static_cast<char>('0' + ((value >> 3) & 0x7)));
            result.push_back(static_cast<char>('0' + (value & 0x7)));
        }

        std::string getStringLiteralPrefix(const clang::StringLiteral *literal)
        {
            if (literal->isWide())
            {
                return "L";
            }
            if (literal->isUTF8())
            {
                return "u8";
            }
            if (literal->isUTF16())
            {
                return "u";
            }
            if (literal->isUTF32())
            {
                return "U";
            }
            return "";
        }

        std::string buildSemanticStringLiteral(const clang::StringLiteral *literal)
        {
            std::string result = getStringLiteralPrefix(literal);
            result.push_back('"');

            for (unsigned index = 0; index < literal->getLength(); ++index)
            {
                const uint32_t codeUnit = literal->getCodeUnit(index);
                switch (codeUnit)
                {
                case '\a':
                    result += "\\a";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                case '\v':
                    result += "\\v";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                case '?':
                    result += "\\?";
                    break;
                case 0:
                    result += "\\000";
                    break;
                default:
                    if (codeUnit >= 0x20 && codeUnit <= 0x7E)
                    {
                        result.push_back(static_cast<char>(codeUnit));
                        break;
                    }

                    if (literal->getCharByteWidth() == 1)
                    {
                        appendFixedOctal(result, codeUnit & 0xFF);
                        break;
                    }

                    if (codeUnit <= 0xFFFF)
                    {
                        result += "\\u";
                        appendFixedHex(result, codeUnit, 4);
                        break;
                    }

                    result += "\\U";
                    appendFixedHex(result, codeUnit, 8);
                    break;
                }
            }

            result.push_back('"');
            return result;
        }
    } // namespace

    BaseExpressionTranslator::BaseExpressionTranslator(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::string BaseExpressionTranslator::translateExpression(const clang::Expr *expr)
    {
        if (!expr)
            return "";

        std::string exprKindName = expr->getStmtClassName();
        if (const auto *recoveryExpr = llvm::dyn_cast<clang::RecoveryExpr>(expr))
        {
            if (auto diagnostic = mVisitor.tryBuildRecoveryExprDiagnostic(recoveryExpr); diagnostic.has_value())
            {
                mVisitor.throwCodegenError(recoveryExpr, *diagnostic);
            }
            return mVisitor.makeUnsupportedPlaceholder("expression", exprKindName);
        }
        if (auto *exprWithCleanups = llvm::dyn_cast<clang::ExprWithCleanups>(expr))
        {
            return mVisitor.TranslateExpr(exprWithCleanups->getSubExpr());
        }
        if (auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr))
        {
            return mVisitor.TranslateExpr(bindTemporaryExpr->getSubExpr());
        }
        if (auto *materializeTemporaryExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr))
        {
            return mVisitor.TranslateExpr(materializeTemporaryExpr->getSubExpr());
        }
        if (auto *implicitCastExpr = llvm::dyn_cast<clang::ImplicitCastExpr>(expr))
        {
            return mVisitor.TranslateExpr(implicitCastExpr->getSubExpr());
        }

        if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(expr))
        {
            return "";
        }
        if (llvm::dyn_cast<clang::ImplicitValueInitExpr>(expr))
        {
            return "";
        }
        if (auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(expr))
        {
            return mVisitor.TranslateExpr(constantExpr->getSubExpr());
        }
        if (auto *parenExpr = llvm::dyn_cast<clang::ParenExpr>(expr))
        {
            return "(" + mVisitor.TranslateExpr(parenExpr->getSubExpr()) + ")";
        }

        expr = expr->IgnoreParenImpCasts();

        if (const auto *thisExpr = llvm::dyn_cast<clang::CXXThisExpr>(expr))
        {
            if (thisExpr->isImplicit() || !mVisitor.isUserWrittenThisExpr(thisExpr))
            {
                return "";
            }
            if (!mVisitor.allowExplicitThisPointerAccess())
            {
                mVisitor.throwExplicitThisPointerAccessError(thisExpr);
            }
            return "this";
        }

        if (auto *cStyleCastExpr = llvm::dyn_cast<clang::CStyleCastExpr>(expr))
        {
            return mVisitor.translateCStyleCastExpr(cStyleCastExpr);
        }
        if (auto *staticCastExpr = llvm::dyn_cast<clang::CXXStaticCastExpr>(expr))
        {
            return mVisitor.translateCXXStaticCastExpr(staticCastExpr);
        }
        if (auto *offsetOfExpr = llvm::dyn_cast<clang::OffsetOfExpr>(expr))
        {
            return mVisitor.translateOffsetOfExpr(offsetOfExpr);
        }
        if (auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr))
        {
            return mVisitor.translateArraySubscriptExpr(arraySubscriptExpr);
        }
        if (auto *conditionalOperator = llvm::dyn_cast<clang::ConditionalOperator>(expr))
        {
            return mVisitor.translateConditionalOperator(conditionalOperator);
        }
        if (auto *functionalCastExpr = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(expr))
        {
            return mVisitor.translateCXXFunctionalCastExpr(functionalCastExpr);
        }
        if (auto *unaryTraitExpr = llvm::dyn_cast<clang::UnaryExprOrTypeTraitExpr>(expr))
        {
            return mVisitor.translateUnaryExprOrTypeTraitExpr(unaryTraitExpr);
        }
        if (auto *stringLiteral = llvm::dyn_cast<clang::StringLiteral>(expr))
        {
            return mVisitor.translateStringLiteral(stringLiteral);
        }
        if (auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr))
        {
            return mVisitor.translateCXXOperatorCallExpr(operatorCallExpr);
        }
        if (auto *memberCallExpr = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr))
        {
            return mVisitor.translateCXXMemberCallExpr(memberCallExpr);
        }
        if (auto *callExpr = llvm::dyn_cast<clang::CallExpr>(expr))
        {
            return mVisitor.translateCallExpr(callExpr);
        }
        if (auto *binaryOperator = llvm::dyn_cast<clang::BinaryOperator>(expr))
        {
            return mVisitor.translateBinaryOperator(binaryOperator);
        }
        if (auto *unaryOperator = llvm::dyn_cast<clang::UnaryOperator>(expr))
        {
            return mVisitor.translateUnaryOperator(unaryOperator);
        }
        if (auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(expr))
        {
            return mVisitor.translateMemberExpr(memberExpr);
        }
        if (auto *dependentMemberExpr = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(expr))
        {
            return mVisitor.translateCXXDependentScopeMemberExpr(dependentMemberExpr);
        }
        if (auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr))
        {
            return mVisitor.translateCXXConstructExpr(constructExpr);
        }
        if (auto *throwExpr = llvm::dyn_cast<clang::CXXThrowExpr>(expr))
        {
            return mVisitor.translateCXXThrowExpr(throwExpr);
        }
        if (auto *unresolvedLookupExpr = llvm::dyn_cast<clang::UnresolvedLookupExpr>(expr))
        {
            return mVisitor.translateUnresolvedLookupExpr(unresolvedLookupExpr);
        }
        if (auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(expr))
        {
            return mVisitor.translateDeclRefExpr(declRefExpr);
        }
        if (auto *dependentDeclRefExpr = llvm::dyn_cast<clang::DependentScopeDeclRefExpr>(expr))
        {
            return mVisitor.translateDependentScopeDeclRefExpr(dependentDeclRefExpr);
        }
        if (auto *boolLiteral = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(expr))
        {
            return mVisitor.translateCXXBoolLiteralExpr(boolLiteral);
        }
        if (auto *floatingLiteral = llvm::dyn_cast<clang::FloatingLiteral>(expr))
        {
            return mVisitor.translateFloatingLiteral(floatingLiteral);
        }
        if (auto *integerLiteral = llvm::dyn_cast<clang::IntegerLiteral>(expr))
        {
            return mVisitor.translateIntegerLiteral(integerLiteral);
        }
        if (auto *initListExpr = llvm::dyn_cast<clang::InitListExpr>(expr))
        {
            return mVisitor.translateInitListExpr(initListExpr);
        }
        if (auto *stdInitListExpr = llvm::dyn_cast<clang::CXXStdInitializerListExpr>(expr))
        {
            return mVisitor.TranslateExpr(stdInitListExpr->getSubExpr());
        }

        return mVisitor.makeUnsupportedPlaceholder("expression", exprKindName);
    }

    std::string BaseExpressionTranslator::translateBinaryOperator(const clang::BinaryOperator *expr)
    {
        std::string lhs = mVisitor.translateExprAsGroupedInfixOperand(expr->getLHS());
        std::string rhs = mVisitor.translateExprAsGroupedInfixOperand(expr->getRHS());
        return lhs + " " + expr->getOpcodeStr().str() + " " + rhs;
    }

    std::string BaseExpressionTranslator::translateExprAsGroupedInfixOperand(const clang::Expr *expr)
    {
        const std::string translatedExpr = mVisitor.TranslateExpr(expr);
        if (translatedExpr.empty() || !shouldParenthesizeInfixOperand(expr))
        {
            return translatedExpr;
        }
        if (isFullyParenthesizedExpression(translatedExpr))
        {
            return translatedExpr;
        }
        return "(" + translatedExpr + ")";
    }

    std::string BaseExpressionTranslator::translateMemberExpr(const clang::MemberExpr *expr)
    {
        clang::Expr *baseExpr = expr->getBase();
        std::string memberName = expr->getMemberNameInfo().getName().getAsString();
        if (expr->isImplicitAccess())
        {
            return memberName;
        }

        if (const auto *thisExpr = mVisitor.tryGetCXXThisExpr(baseExpr))
        {
            if (thisExpr->isImplicit() || !mVisitor.isUserWrittenThisExpr(thisExpr))
            {
                return memberName;
            }
            if (!mVisitor.allowExplicitThisPointerAccess())
            {
                mVisitor.throwExplicitThisPointerAccessError(thisExpr);
            }
        }

        std::string baseStr = mVisitor.TranslateExpr(expr->getBase());
        if (!baseStr.empty() && (baseStr.back() == '.' || (baseStr.length() > 1 && baseStr.substr(baseStr.length() - 2) == "->")))
        {
            return baseStr + memberName;
        }

        return baseStr + (expr->isArrow() ? "->" : ".") + memberName;
    }

    std::string BaseExpressionTranslator::translateCallExpr(const clang::CallExpr *expr)
    {
        std::string callee = mVisitor.TranslateExpr(expr->getCallee());
        std::string templateArgs = mVisitor.generateTemplateCallArguments(expr->getCallee());

        bool ignoreParams = false;
        if (auto *funcDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(expr->getCalleeDecl()); funcDecl != nullptr && mVisitor.mDefaultFunctionConvertor)
        {
            auto calleeRes = mVisitor.mDefaultFunctionConvertor->convertFunc(funcDecl->getQualifiedNameAsString(), mVisitor.generateTemplateCallArgumentsStr(expr->getCallee()));
            if (calleeRes.empty() == false)
            {
                callee = calleeRes;
            }
            ignoreParams = mVisitor.mDefaultFunctionConvertor->checkShouldIgnoreParams(funcDecl->getQualifiedNameAsString());
        }
        std::string result = callee + templateArgs;

        if (ignoreParams == false)
        {
            result += "(";
            std::vector<std::string> argStrings;
            for (unsigned i = 0; i < expr->getNumArgs(); ++i)
            {
                std::string argument = mVisitor.TranslateExpr(expr->getArg(i));
                if (!argument.empty())
                {
                    argStrings.push_back(std::move(argument));
                }
            }
            for (size_t index = 0; index < argStrings.size(); ++index)
            {
                result += argStrings[index];
                if (index + 1 < argStrings.size())
                {
                    result += ", ";
                }
            }
            result += ")";
        }
        return result;
    }

    std::string BaseExpressionTranslator::translateInitListExpr(const clang::InitListExpr *expr)
    {
        if (expr->isExplicit() && expr->getNumInits() == 0)
        {
            return "{}";
        }

        const clang::InitListExpr *syntacticExpr = expr->getSyntacticForm();
        if (!syntacticExpr)
        {
            syntacticExpr = expr;
        }

        std::string result = "{ ";
        std::vector<std::string> initStrings;

        for (clang::Expr *initExpr : syntacticExpr->inits())
        {
            std::string currentInitStr;

            if (auto *designatedInitExpr = llvm::dyn_cast<clang::DesignatedInitExpr>(initExpr))
            {
                for (const auto &designator : designatedInitExpr->designators())
                {
                    if (designator.isFieldDesignator())
                    {
                        currentInitStr += "." + designator.getFieldName()->getName().str();
                    }
                }

                currentInitStr += " = ";
                currentInitStr += mVisitor.TranslateExpr(designatedInitExpr->getInit());
            }
            else
            {
                currentInitStr = mVisitor.TranslateExpr(initExpr);
            }

            if (!currentInitStr.empty())
            {
                initStrings.push_back(currentInitStr);
            }
        }

        for (size_t i = 0; i < initStrings.size(); ++i)
        {
            result += initStrings[i];
            if (i < initStrings.size() - 1)
            {
                result += ", ";
            }
        }

        result += " }";
        return result;
    }

    std::string BaseExpressionTranslator::translateCXXConstructExpr(const clang::CXXConstructExpr *expr)
    {
        if (!expr->getParenOrBraceRange().isValid())
        {
            return mVisitor.translateCXXConstructExprImplicit(expr);
        }

        if (expr->isListInitialization())
        {
            return mVisitor.translateCXXConstructExprList(expr);
        }

        return mVisitor.translateCXXConstructExprFunction(expr);
    }

    std::string BaseExpressionTranslator::translateCXXConstructExprImplicit(const clang::CXXConstructExpr *expr)
    {
        if (expr->getNumArgs() >= 1)
        {
            return mVisitor.TranslateExpr(expr->getArg(0));
        }
        return "";
    }

    std::string BaseExpressionTranslator::translateCXXConstructExprList(const clang::CXXConstructExpr *expr)
    {
        if (llvm::dyn_cast<clang::CXXTemporaryObjectExpr>(expr))
        {
            std::string typeName = mVisitor.generateTypeCanonicalName(expr->getType());
            std::string listContent = (expr->getNumArgs() > 0) ? mVisitor.TranslateExpr(expr->getArg(0)) : "{}";
            return typeName + listContent;
        }

        if (expr->getNumArgs() > 0)
        {
            return mVisitor.TranslateExpr(expr->getArg(0));
        }
        return "{}";
    }

    std::string BaseExpressionTranslator::translateCXXConstructExprFunction(const clang::CXXConstructExpr *expr)
    {
        std::string result;
        result += mVisitor.generateTypeCanonicalName(expr->getType(), mVisitor.mDefaultTypeConvertor);

        result += "(";

        std::vector<std::string> argStrings;
        for (unsigned i = 0; i < expr->getNumArgs(); ++i)
        {
            std::string argString = mVisitor.TranslateExpr(expr->getArg(i));
            if (!argString.empty())
            {
                argStrings.push_back(argString);
            }
        }

        for (size_t i = 0; i < argStrings.size(); ++i)
        {
            result += argStrings[i];
            if (i < argStrings.size() - 1)
            {
                result += ", ";
            }
        }

        result += ")";
        return result;
    }

    std::string BaseExpressionTranslator::translateCXXThrowExpr(const clang::CXXThrowExpr *expr)
    {
        if (const clang::Expr *subExpr = expr->getSubExpr())
        {
            const std::string thrownExpr = mVisitor.TranslateExpr(subExpr);
            if (thrownExpr.empty())
            {
                return "throw";
            }
            return "throw " + thrownExpr;
        }
        return "throw";
    }

    std::string BaseExpressionTranslator::translateCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *expr)
    {
        auto op = expr->getOperator();

        if (expr->isInfixBinaryOp())
        {
            if (expr->getNumArgs() >= 2)
            {
                return mVisitor.translateCXXOperatorCallExprBinaryOp(expr);
            }
        }

        if (expr->getNumArgs() == 1)
        {
            switch (op)
            {
            case clang::OO_Minus:
            case clang::OO_Plus:
            case clang::OO_Exclaim:
            case clang::OO_Tilde:
            {
                std::string opcodeStr = clang::getOperatorSpelling(op);
                std::string subExprStr = mVisitor.TranslateExpr(expr->getArg(0));
                return opcodeStr + subExprStr;
            }
            default:
                break;
            }
        }

        if (op == clang::OO_Arrow)
        {
            if (expr->getNumArgs() >= 1)
            {
                return mVisitor.TranslateExpr(expr->getArg(0));
            }
        }

        if (op == clang::OO_Call)
        {
            if (expr->getNumArgs() >= 1)
            {
                return mVisitor.translateCXXOperatorCallExprFuncCall(expr);
            }
        }

        if (op == clang::OO_Subscript)
        {
            if (expr->getNumArgs() >= 2)
            {
                return mVisitor.translateCXXOperatorCallExprArraySubScript(expr);
            }
        }

        return mVisitor.makeUnsupportedPlaceholder("operator call", std::string(clang::getOperatorSpelling(op)));
    }

    std::string BaseExpressionTranslator::translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *expr)
    {
        auto op = expr->getOperator();
        return mVisitor.translateExprAsGroupedInfixOperand(expr->getArg(0)) + " "
               + clang::getOperatorSpelling(op) + " "
               + mVisitor.translateExprAsGroupedInfixOperand(expr->getArg(1));
    }

    std::string BaseExpressionTranslator::translateCXXOperatorCallExprArrow(const clang::CXXOperatorCallExpr *expr)
    {
        return mVisitor.TranslateExpr(expr->getArg(0));
    }

    std::string BaseExpressionTranslator::translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *expr)
    {
        std::string callee = mVisitor.TranslateExpr(expr->getArg(0));

        std::string result = callee + "(";
        std::string args;
        for (unsigned i = 1; i < expr->getNumArgs(); ++i)
        {
            args += mVisitor.TranslateExpr(expr->getArg(i));
            if (i < expr->getNumArgs() - 1)
            {
                args += ", ";
            }
        }
        result += args + ")";
        return result;
    }

    std::string BaseExpressionTranslator::translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *expr)
    {
        std::string base = mVisitor.TranslateExpr(expr->getArg(0));
        std::string index = mVisitor.TranslateExpr(expr->getArg(1));

        return base + "[" + index + "]";
    }

    std::string BaseExpressionTranslator::translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *expr)
    {
        if (llvm::isa<clang::CXXConversionDecl>(expr->getMethodDecl()))
        {
            return mVisitor.TranslateExpr(expr->getImplicitObjectArgument());
        }

        std::string callee = mVisitor.TranslateExpr(expr->getCallee());

        std::string result;
        result += mVisitor.generateCXXMemberCallExpr(expr, callee);
        return result;
    }

    std::string BaseExpressionTranslator::generateCXXMemberCallExpr(const clang::CXXMemberCallExpr *expr, const std::string &callee)
    {
        std::string templateArgs = mVisitor.generateTemplateCallArguments(expr->getCallee());

        std::string result = callee + templateArgs + "(";
        std::vector<std::string> argStrings;
        for (unsigned i = 0; i < expr->getNumArgs(); ++i)
        {
            std::string argString = mVisitor.TranslateExpr(expr->getArg(i));
            if (!argString.empty())
            {
                argStrings.push_back(argString);
            }
        }

        for (size_t i = 0; i < argStrings.size(); ++i)
        {
            result += argStrings[i];
            if (i < argStrings.size() - 1)
            {
                result += ", ";
            }
        }
        result += ")";
        return result;
    }

    std::string BaseExpressionTranslator::translateUnaryOperator(const clang::UnaryOperator *expr)
    {
        std::string subExprStr = mVisitor.TranslateExpr(expr->getSubExpr());
        std::string opcodeStr = clang::UnaryOperator::getOpcodeStr(expr->getOpcode()).str();

        if (expr->isPostfix())
        {
            return subExprStr + opcodeStr;
        }
        return opcodeStr + subExprStr;
    }

    std::string BaseExpressionTranslator::translateCXXBoolLiteralExpr(const clang::CXXBoolLiteralExpr *expr)
    {
        return expr->getValue() ? "true" : "false";
    }

    std::string BaseExpressionTranslator::translateStringLiteral(const clang::StringLiteral *literal)
    {
        clang::SourceManager &sourceManager = mVisitor.Context->getSourceManager();
        const clang::LangOptions &langOptions = mVisitor.Context->getLangOpts();
        const clang::SourceRange range = literal->getSourceRange();

        if (!range.getBegin().isMacroID() && !range.getEnd().isMacroID())
        {
            const llvm::StringRef sourceText = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range), sourceManager, langOptions);
            if (!sourceText.empty())
            {
                return sourceText.str();
            }
        }

        return buildSemanticStringLiteral(literal);
    }

    std::string BaseExpressionTranslator::translateUnaryExprOrTypeTraitExpr(const clang::UnaryExprOrTypeTraitExpr *expr)
    {
        std::string traitStr;
        switch (expr->getKind())
        {
        case clang::UETT_SizeOf:
            traitStr = "sizeof";
            break;
        case clang::UETT_AlignOf:
            traitStr = "alignof";
            break;
        default:
            return mVisitor.makeUnsupportedPlaceholder("unary trait expression", "kind");
        }

        if (expr->isArgumentType())
        {
            clang::QualType argType = expr->getArgumentType();
            return traitStr + "(" + mVisitor.generateTypeCanonicalName(argType) + ")";
        }

        return traitStr + "(" + mVisitor.TranslateExpr(expr->getArgumentExpr()) + ")";
    }

    std::string BaseExpressionTranslator::translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *expr)
    {
        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr->getSubExpr()->IgnoreParenCasts()))
        {
            if (const auto *castRecordDecl = expr->getType()->getAsRecordDecl())
            {
                if (castRecordDecl == constructExpr->getConstructor()->getParent())
                {
                    return mVisitor.TranslateExpr(constructExpr);
                }
            }
        }

        std::string typeStr = mVisitor.generateTypeCanonicalName(expr->getType(), mVisitor.mDefaultTypeConvertor);
        std::string subExprStr = mVisitor.TranslateExpr(expr->getSubExpr());
        return typeStr + "(" + subExprStr + ")";
    }

    std::string BaseExpressionTranslator::translateCStyleCastExpr(const clang::CStyleCastExpr *expr)
    {
        clang::QualType targetType = expr->getType();
        std::string targetTypeStr = mVisitor.generateTypeCanonicalName(targetType, mVisitor.mDefaultTypeConvertor);

        const clang::Expr *subExpr = expr->getSubExpr();

        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(subExpr->IgnoreParenCasts()))
        {
            if (mVisitor.Context->getCanonicalType(constructExpr->getType()) == mVisitor.Context->getCanonicalType(targetType))
            {
                if (constructExpr->getNumArgs() == 1)
                {
                    std::string argStr = mVisitor.TranslateExpr(constructExpr->getArg(0));
                    return "(" + targetTypeStr + ")" + argStr;
                }
            }
        }

        std::string subExprStr = mVisitor.TranslateExpr(subExpr);
        return "(" + targetTypeStr + ")" + subExprStr;
    }

    std::string BaseExpressionTranslator::translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *expr)
    {
        if (expr == nullptr)
        {
            return "";
        }

        const clang::Expr *subExpr = expr->getSubExpr();
        if (subExpr == nullptr)
        {
            return "";
        }

        switch (expr->getCastKind())
        {
        case clang::CK_NoOp:
        case clang::CK_LValueToRValue:
        case clang::CK_DerivedToBase:
        case clang::CK_UncheckedDerivedToBase:
        case clang::CK_DerivedToBaseMemberPointer:
        case clang::CK_BaseToDerived:
        case clang::CK_BaseToDerivedMemberPointer:
            return mVisitor.TranslateExpr(subExpr);
        default:
            break;
        }

        const clang::QualType targetType = expr->getTypeAsWritten().isNull() ? expr->getType() : expr->getTypeAsWritten();
        const std::string targetTypeStr = mVisitor.generateTypeCanonicalName(targetType, mVisitor.mDefaultTypeConvertor);
        const std::string subExprStr = mVisitor.TranslateExpr(subExpr);
        return "(" + targetTypeStr + ")" + subExprStr;
    }

    std::string BaseExpressionTranslator::translateConditionalOperator(const clang::ConditionalOperator *expr)
    {
        std::string condStr = mVisitor.TranslateExpr(expr->getCond());
        std::string trueStr = mVisitor.TranslateExpr(expr->getTrueExpr());
        std::string falseStr = mVisitor.TranslateExpr(expr->getFalseExpr());

        return "(" + condStr + ") ? (" + trueStr + ") : (" + falseStr + ")";
    }

    std::string BaseExpressionTranslator::translateArraySubscriptExpr(const clang::ArraySubscriptExpr *expr)
    {
        std::string baseStr = mVisitor.TranslateExpr(expr->getBase());
        std::string indexStr = mVisitor.TranslateExpr(expr->getIdx());

        return baseStr + "[" + indexStr + "]";
    }

    std::string BaseExpressionTranslator::translateOffsetOfExpr(const clang::OffsetOfExpr *expr)
    {
        std::string result = "offsetof(";

        clang::QualType type = expr->getTypeSourceInfo()->getType();
        result += mVisitor.generateTypeCanonicalName(type, mVisitor.mDefaultTypeConvertor);
        result += ", ";

        std::string memberStr;
        for (unsigned i = 0; i < expr->getNumComponents(); ++i)
        {
            const clang::OffsetOfNode &node = expr->getComponent(i);

            if (node.getKind() == clang::OffsetOfNode::Field)
            {
                if (i > 0)
                {
                    memberStr += ".";
                }
                memberStr += node.getFieldName()->getName().str();
            }
            else if (node.getKind() == clang::OffsetOfNode::Array)
            {
                memberStr += "[" + mVisitor.TranslateExpr(expr->getIndexExpr(node.getArrayExprIndex())) + "]";
            }
        }

        result += memberStr + ")";
        return result;
    }
} // namespace UGLC::CodeGen
