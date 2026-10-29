#pragma once

#include <string>

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * Provides backend-neutral default expression translation for BaseASTVisitor.
     *
     * The translator owns the common C++ expression lowering rules used before a backend
     * applies its HLSL, MSL, or host C++ special cases. BaseASTVisitor keeps the original
     * public and virtual methods, so existing backend overrides keep their dispatch behavior.
     *
     * DSL example:
     * @code
     * float4 color = texture.Sample(samplerState, uv) * factor;
     * @endcode
     *
     * Generated shader example:
     * @code
     * float4 color = texture.Sample(samplerState, uv) * factor;
     * @endcode
     */
    class BaseExpressionTranslator
    {
    public:
        /** Creates a translator that calls back into the owning visitor for nested translation. */
        explicit BaseExpressionTranslator(BaseASTVisitor &visitor);

        /** Dispatches a Clang expression node to the matching BaseASTVisitor expression hook. */
        std::string translateExpression(const clang::Expr *expr);

        /** Emits a binary operator expression. */
        std::string translateBinaryOperator(const clang::BinaryOperator *expr);

        /** Emits an infix operand and adds parentheses when precedence would otherwise change. */
        std::string translateExprAsGroupedInfixOperand(const clang::Expr *expr);

        /** Emits a member expression with implicit-this and explicit-this handling. */
        std::string translateMemberExpr(const clang::MemberExpr *expr);

        /** Emits a free function call expression. */
        std::string translateCallExpr(const clang::CallExpr *expr);

        /** Emits a brace initializer list. */
        std::string translateInitListExpr(const clang::InitListExpr *expr);

        /** Emits a C++ constructor expression in the default base form. */
        std::string translateCXXConstructExpr(const clang::CXXConstructExpr *expr);

        /** Emits an implicit constructor expression. */
        std::string translateCXXConstructExprImplicit(const clang::CXXConstructExpr *expr);

        /** Emits a list-initialization constructor expression. */
        std::string translateCXXConstructExprList(const clang::CXXConstructExpr *expr);

        /** Emits a function-style constructor expression. */
        std::string translateCXXConstructExprFunction(const clang::CXXConstructExpr *expr);

        /** Emits a throw expression. */
        std::string translateCXXThrowExpr(const clang::CXXThrowExpr *expr);

        /** Emits a C++ overloaded operator call. */
        std::string translateCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *expr);

        /** Emits an overloaded infix binary operator. */
        std::string translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *expr);

        /** Emits an overloaded arrow operator. */
        std::string translateCXXOperatorCallExprArrow(const clang::CXXOperatorCallExpr *expr);

        /** Emits an overloaded function-call operator. */
        std::string translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *expr);

        /** Emits an overloaded subscript operator. */
        std::string translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *expr);

        /** Emits a member function call expression. */
        std::string translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *expr);

        /** Emits the shared member-call argument list after a backend has chosen the callee spelling. */
        std::string generateCXXMemberCallExpr(const clang::CXXMemberCallExpr *expr, const std::string &callee);

        /** Emits a unary operator expression. */
        std::string translateUnaryOperator(const clang::UnaryOperator *expr);

        /** Emits a C++ bool literal. */
        std::string translateCXXBoolLiteralExpr(const clang::CXXBoolLiteralExpr *expr);

        /** Emits a string literal, preserving source spelling when possible. */
        std::string translateStringLiteral(const clang::StringLiteral *literal);

        /** Emits sizeof/alignof expressions. */
        std::string translateUnaryExprOrTypeTraitExpr(const clang::UnaryExprOrTypeTraitExpr *expr);

        /** Emits a function-style cast expression. */
        std::string translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *expr);

        /** Emits a C-style cast expression. */
        std::string translateCStyleCastExpr(const clang::CStyleCastExpr *expr);

        /** Emits a static_cast expression. */
        std::string translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *expr);

        /** Emits a ternary conditional expression. */
        std::string translateConditionalOperator(const clang::ConditionalOperator *expr);

        /** Emits a native array subscript expression. */
        std::string translateArraySubscriptExpr(const clang::ArraySubscriptExpr *expr);

        /** Emits an offsetof expression. */
        std::string translateOffsetOfExpr(const clang::OffsetOfExpr *expr);

    private:
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
