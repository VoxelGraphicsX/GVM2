#pragma once

#include <CodeGen/BaseASTVisitor.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>

#include <optional>
#include <string>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Validates and lowers HLSL-specific shader builtin calls.
     *
     * The DSL exposes backend-neutral shader builtins, while HLSL has a mixture
     * of statement keywords, intrinsic functions, and compatibility aliases.
     * This class owns that mapping so `HLSLVisitor` can keep call-expression
     * dispatch separate from shader-builtin policy.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * if (alpha < 0.5f) { UGL::discard_fragment(); }
     * UGL::DeviceMemoryBarrierWithGroupSync();
     * auto right = UGL::WaveReadAcrossX(value);
     * ```
     *
     * ```hlsl
     * if (alpha < 0.5f) { discard; }
     * DeviceMemoryBarrierWithGroupSync();
     * auto right = QuadReadAcrossX(value);
     * ```
     */
    class HLSLShaderBuiltinTranslator
    {
    public:
        /** @brief Creates a builtin translator backed by the owning visitor. */
        explicit HLSLShaderBuiltinTranslator(BaseASTVisitor &visitor);

        /**
         * @brief Validates shader-only builtin stage support for a call target.
         */
        void validateUnsupportedBuiltinOrThrow(const clang::FunctionDecl *callee,
                                               const clang::FunctionDecl *entryFunction) const;

        /**
         * @brief Returns backend-native HLSL when a call is a handled DSL shader builtin.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateCallExpr(const clang::CallExpr *expr,
                                                                       const clang::FunctionDecl *callee,
                                                                       const clang::FunctionDecl *entryFunction) const;

    private:
        /** @brief Owning visitor service object used for recursive argument translation. */
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::HLSL
