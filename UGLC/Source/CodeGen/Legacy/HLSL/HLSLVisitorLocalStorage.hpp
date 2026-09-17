#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Lowers HLSL local structured-buffer aliases into selector-based helper calls.
     *
     * The DSL allows shader-local variables of `UGL::StructuredBuffer<T>` or
     * `UGL::RWStructuredBuffer<T>` to alias one of several real bind-group
     * resources, including conditional aliases. HLSL resources cannot be stored
     * as ordinary local variables, so this class rewrites those locals into a
     * generated selector plus read/write helper functions.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * UGL::RWStructuredBuffer<uint> target = useA ? bg->a : bg->b;
     * target[index] = value;
     * ```
     *
     * ```hlsl
     * uint __uglc_target_resource_selector = useA ? 0u : 1u;
     * __uglc_target_resource_write(__uglc_target_resource_selector, index, value);
     * ```
     */
    class HLSLVisitorLocalStorage
    {
    public:
        /**
         * @brief Creates a lowering helper backed by the owning visitor services.
         */
        HLSLVisitorLocalStorage(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor);

        /**
         * @brief Clears per-shader emitted helper state.
         */
        void resetShaderScope();

        /**
         * @brief Records bind-group object names that have flat descriptor globals in the current shader.
         *
         * Local resource-alias helper functions are emitted at global HLSL scope,
         * so aliases sourced from shader entry bind groups must capture the flat
         * descriptor name instead of an entry-local handle variable. Names must
         * already be sanitized for HLSL emission.
         */
        void setFlatBindGroupNames(std::unordered_set<std::string> bindGroupNames);

        /**
         * @brief Clears per-function alias state before translating one function body.
         */
        void resetFunctionScope();

        /**
         * @brief Registers helper definitions needed by aliases observed in the current function.
         */
        void captureFunctionHelperDefinitions();

        /**
         * @brief Returns helper definitions accumulated for the current shader unit.
         */
        [[nodiscard]] const std::vector<std::string> &pendingHelperDefinitions() const;

        /**
         * @brief Returns the generated selector for a local alias declaration.
         */
        [[nodiscard]] std::optional<std::string> getAliasSelectorName(const clang::ValueDecl *decl) const;

        /**
         * @brief Translates a local `StructuredBuffer`/`RWStructuredBuffer` alias declaration.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateAliasVarDecl(const clang::VarDecl *decl);

        /**
         * @brief Translates writes through a writable local storage-buffer alias.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateAliasWrite(const clang::Expr *baseExpr,
                                                                        const clang::Expr *indexExpr,
                                                                        const clang::Expr *rhsExpr);

        /**
         * @brief Translates `operator=` forms that assign aliases or write through aliases.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateOperatorAssignment(const clang::CXXOperatorCallExpr *expr);

        /**
         * @brief Translates array-subscript reads through a local storage-buffer alias.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateArraySubscript(const clang::CXXOperatorCallExpr *expr);

        /**
         * @brief Returns true when an expression resolves to a writable alias atomic target.
         */
        [[nodiscard]] bool hasAtomicTarget(const clang::Expr *targetExpr) const;

        /**
         * @brief Builds an inline HLSL block for atomics targeting a local alias.
         */
        [[nodiscard]] std::optional<std::string> tryBuildAtomicDispatchBlock(const clang::Expr *targetExpr,
                                                                             std::string_view interlockedFunctionName,
                                                                             const std::string &resultVariableName,
                                                                             const std::string &resultTypeName,
                                                                             const std::vector<std::string> &atomicArgumentExpressions,
                                                                             bool compareExchange);

        /**
         * @brief Builds indented HLSL statements for atomics targeting a local alias.
         */
        [[nodiscard]] std::optional<std::string> tryBuildAtomicDispatchStatements(const clang::Expr *targetExpr,
                                                                                  std::string_view interlockedFunctionName,
                                                                                  const std::string &resultVariableName,
                                                                                  const std::vector<std::string> &atomicArgumentExpressions,
                                                                                  bool compareExchange,
                                                                                  std::string_view indent);

    private:
        struct AliasState
        {
            std::string selectorName;
            std::string helperFunctionName;
            std::string writeHelperFunctionName;
            bool writable = false;
            clang::QualType elementType;
            std::vector<std::string> resourceExpressions;
        };

        struct AtomicTarget
        {
            const AliasState *aliasState = nullptr;
            std::string indexExpression;
            std::string memberAccessSuffix;
        };

        [[nodiscard]] AliasState *findAlias(const clang::Expr *expr);
        [[nodiscard]] const AliasState *findAlias(const clang::Expr *expr) const;
        [[nodiscard]] bool hasAtomicTargetImpl(const clang::Expr *targetExpr) const;
        [[nodiscard]] std::optional<std::string> tryBuildSelectorExpression(AliasState &aliasState, const clang::Expr *expr);
        [[nodiscard]] std::optional<AtomicTarget> tryResolveAtomicTarget(const clang::Expr *expr);
        [[nodiscard]] std::string buildAtomicTargetExpression(const AtomicTarget &atomicTarget, size_t candidateIndex) const;
        [[nodiscard]] std::string buildAliasSubscript(const AliasState &aliasState, const std::string &indexExpression) const;
        [[nodiscard]] std::string buildAtomicCallStatement(const AtomicTarget &atomicTarget,
                                                           size_t candidateIndex,
                                                           std::string_view interlockedFunctionName,
                                                           const std::string &resultVariableName,
                                                           const std::vector<std::string> &atomicArgumentExpressions,
                                                           bool compareExchange) const;
        [[nodiscard]] std::string makeReadHelperDefinition(const AliasState &aliasState);
        [[nodiscard]] std::string makeWriteHelperDefinition(const AliasState &aliasState);
        [[nodiscard]] bool isImplicitOrDefaultInitializer(const clang::Expr *expr) const;
        [[nodiscard]] std::string generateTypeName(clang::QualType type);
        [[nodiscard]] static size_t registerAliasCandidate(AliasState &aliasState, const std::string &resourceExpression);
        [[nodiscard]] static std::string makeGeneratedIdentifier(const std::string &prefix, clang::SourceLocation location);
        [[nodiscard]] static std::string stripIllegalLocalVariableQualifiers(const std::string &typeName);

        BaseASTVisitor &mVisitor;
        AbstractTypeConvertor *mTypeConvertor = nullptr;
        std::unordered_map<const clang::ValueDecl *, AliasState> mAliases;
        std::vector<std::string> mPendingHelperDefinitions;
        std::unordered_set<std::string> mPendingHelperNames;
        std::unordered_set<std::string> mFlatBindGroupNames;
    };
} // namespace UGLC::CodeGen::HLSL
