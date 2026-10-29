#pragma once

#include <CodeGen/BaseASTVisitor.hpp>

#include <clang/AST/ExprCXX.h>

#include <optional>
#include <string>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Lowers DSL texture member calls into HLSL texture intrinsics.
     *
     * Texture DSL methods abstract over sampled and read-write resources. HLSL
     * uses different syntax for load/store, sampling, gather, array-layer access,
     * and optional offset/default LOD arguments, so this class owns that mapping.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * UGL::float4 color = bindGroup->albedo.sample(sampler, uv);
     * UGL::float4 texel = bindGroup->arrayTex.read(pixel, layer, mip);
     * ```
     *
     * ```hlsl
     * float4 color = bindGroup_albedo.Sample(sampler, uv);
     * float4 texel = bindGroup_arrayTex.Load(int4(pixel, layer, mip));
     * ```
     */
    class HLSLTextureMemberCallLowering
    {
    public:
        /**
         * @brief Creates a texture lowering helper backed by the owning visitor.
         */
        explicit HLSLTextureMemberCallLowering(BaseASTVisitor &visitor);

        /**
         * @brief Lowers methods on `UGL::Texture2D<T>` and `UGL::RWTexture2D<T>`.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateTexture2DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                                  const std::string &textureExpr,
                                                                                  const std::string &methodName,
                                                                                  bool isReadWriteTexture);

        /**
         * @brief Lowers methods on `UGL::Texture2DArray<T>` and `UGL::RWTexture2DArray<T>`.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateTexture2DArrayMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                                       const std::string &textureExpr,
                                                                                       const std::string &methodName,
                                                                                       bool isReadWriteTexture);

        /**
         * @brief Lowers methods on `UGL::Texture3D<T>` and `UGL::RWTexture3D<T>`.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateTexture3DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                                  const std::string &textureExpr,
                                                                                  const std::string &methodName,
                                                                                  bool isReadWriteTexture);

    private:
        [[nodiscard]] static bool isDefaultArgumentExpr(const clang::Expr *expr);
        /** Translates one texture method argument through the owning visitor. */
        [[nodiscard]] std::string translateTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex) const;
        /** Translates one texture method argument or returns a fallback when the argument is omitted/defaulted. */
        [[nodiscard]] std::string translateOptionalTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex, std::string_view fallbackValue) const;
        /** Appends one optional texture method argument when the DSL call explicitly provides it. */
        void appendOptionalTextureArgument(std::string &result, const clang::CXXMemberCallExpr *expr, unsigned argIndex) const;

        /** @brief Owning visitor service object used for recursive expression translation. */
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::HLSL
