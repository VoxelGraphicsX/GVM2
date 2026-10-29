#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Legacy/HLSL/HLSLVisitorLocalStorage.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Type.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Emits HLSL-safe aggregate and typed initializer expressions.
     *
     * HLSL can initialize vectors and simple scalars inline, but record aggregates
     * often need a generated construction helper when they are used as function
     * arguments or return expressions. This class owns that policy and the
     * per-shader helper cache.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * struct Varying { UGL::float4 pos; UGL::float2 uv; };
     * return Varying{pos, uv};
     * ```
     *
     * ```hlsl
     * Varying __uglc_make_Varying(float4 __uglc_field_0, float2 __uglc_field_1);
     * return __uglc_make_Varying(pos, uv);
     * ```
     */
    class HLSLAggregateInitializerEmitter
    {
    public:
        /**
         * @brief Creates an initializer emitter backed by the owning visitor services.
         */
        HLSLAggregateInitializerEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor, HLSLVisitorLocalStorage &localStorage);

        /**
         * @brief Clears per-shader aggregate helper registration state.
         */
        void resetShaderScope();

        /**
         * @brief Returns aggregate helper definitions required by the current shader.
         */
        [[nodiscard]] const std::vector<std::string> &pendingHelperDefinitions() const;

        /**
         * @brief Returns true when an initializer carries no user-written value.
         */
        [[nodiscard]] bool isImplicitOrDefaultInitializer(const clang::Expr *expr) const;

        /**
         * @brief Unwraps a conditional initializer that HLSL cannot materialize inline.
         */
        [[nodiscard]] const clang::ConditionalOperator *unwrapUnsupportedConditionalExpr(const clang::Expr *expr) const;

        /**
         * @brief Unwraps an atomic builtin call that returns the original value.
         */
        [[nodiscard]] const clang::CallExpr *unwrapAtomicReturningCall(const clang::Expr *expr) const;

        /**
         * @brief Builds a zero initializer expression for the requested target type.
         */
        [[nodiscard]] std::string makeZeroInitializer(const clang::QualType &targetType);

        /** Emits an aggregate initializer for implicit default construction, preserving record member defaults. */
        [[nodiscard]] std::string emitDefaultInitializer(const clang::QualType &targetType);

        /**
         * @brief Builds assignment statements that recursively zero a target expression.
         */
        [[nodiscard]] std::string buildZeroInitializationStatements(const clang::QualType &targetType, const std::string &targetExpression);

        /**
         * @brief Emits a typed initializer for variable declarations.
         */
        [[nodiscard]] std::string emitTypedInitializer(const clang::QualType &targetType, const clang::Expr *expr);

        /**
         * @brief Emits a typed initializer expression for call arguments and returns.
         */
        [[nodiscard]] std::string emitTypedInitializerExpression(const clang::QualType &targetType, const clang::Expr *expr);

        /**
         * @brief Builds a variable declaration initialized from an atomic builtin result.
         */
        [[nodiscard]] std::string buildAtomicReturningVarDecl(const clang::VarDecl *decl,
                                                              const std::vector<std::string> &qualifiers,
                                                              const std::string &variableTypeName,
                                                              const std::string &variableName,
                                                              const clang::CallExpr *atomicCall);

    private:
        struct HLSLVectorTypeInfo
        {
            std::string scalarType;
            int elementCount = 0;
        };

        struct HLSLMatrixTypeInfo
        {
            std::string scalarType;
            int rowCount = 0;
            int columnCount = 0;
        };

        enum class AtomicBuiltinKind
        {
            None,
            Add,
            Or,
            And,
            Load,
            Store,
            CompareExchange,
            Max,
            Min,
        };

        [[nodiscard]] std::string makeAggregateConstructionHelperName(const clang::CXXRecordDecl *recordDecl) const;
        [[nodiscard]] std::string registerAggregateConstructionHelper(const clang::CXXRecordDecl *recordDecl);
        [[nodiscard]] std::string makeAggregateConstructionHelperDefinition(const clang::CXXRecordDecl *recordDecl, const std::string &helperName);
        [[nodiscard]] static const clang::Expr *stripTransparentExprWrappers(const clang::Expr *expr);
        [[nodiscard]] static std::string makeScalarZeroLiteral(const std::string &typeName);
        [[nodiscard]] static std::optional<HLSLVectorTypeInfo> parseHLSLVectorType(const std::string &typeName);
        [[nodiscard]] static std::optional<HLSLMatrixTypeInfo> parseHLSLMatrixType(const std::string &typeName);
        [[nodiscard]] static std::string makeRepeatedArgumentList(const std::string &argExpr, int count);
        [[nodiscard]] static AtomicBuiltinKind getAtomicBuiltinKind(const clang::FunctionDecl *callee);
        [[nodiscard]] static bool atomicBuiltinReturnsOriginalValue(AtomicBuiltinKind kind);
        [[nodiscard]] static std::string getAtomicInterlockedFunctionName(AtomicBuiltinKind kind);
        [[nodiscard]] static std::string makeGeneratedIdentifier(const std::string &prefix, clang::SourceLocation location);

        /** @brief Owning visitor service object used for AST translation and formatting. */
        BaseASTVisitor &mVisitor;
        /** @brief Backend type converter used for HLSL canonical type names. */
        AbstractTypeConvertor *mTypeConvertor = nullptr;
        /** @brief Local storage alias lowering used when atomic targets are local aliases. */
        HLSLVisitorLocalStorage &mLocalStorage;
        /** @brief Helper definitions emitted once per record aggregate type. */
        std::vector<std::string> mPendingHelperDefinitions;
        /** @brief Cache from canonical record declarations to generated helper names. */
        std::unordered_map<const clang::CXXRecordDecl *, std::string> mPendingHelperNames;
    };
} // namespace UGLC::CodeGen::HLSL
