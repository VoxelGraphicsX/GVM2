#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/RenderSetLayoutInfo.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>

#include <clang/AST/ExprCXX.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Emits HLSL declarations and helper calls for `UGL::RenderSet<T>` resources.
     *
     * RenderSet fields are lowered into several HLSL globals: an access-bound table,
     * per-component index lists, buffer or texture arrays, draw metadata, and safe helper
     * functions. RenderSet intentionally keeps this global-resource model because Vulkan
     * SPIR-V rejects HLSL structs that contain opaque descriptor arrays such as Texture2D[].
     * This class owns that naming and helper-emission policy so `HLSLVisitor` can keep only
     * the AST traversal and dispatch logic.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * UGL::RenderSet<MySet> scene;
     * auto albedo = scene->albedo.get(entity, slot);
     * ```
     *
     * ```hlsl
     * [[vk::binding(3, 0)]] Texture2D<float4> scene_albedo[] : register(t3, space0);
     * Texture2D<float4> scene_albedo_UGLGetSafe(uint entity, uint slot);
     * ```
     */
    class HLSLRenderSetEmitter
    {
    public:
        /**
         * @brief Creates an emitter using the owning visitor for formatting and AST helpers.
         */
        HLSLRenderSetEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor);

        /**
         * @brief Returns the lowered HLSL global name for a RenderSet resource field.
         */
        [[nodiscard]] std::string getResourceGlobalName(const std::string &renderSetName, const std::string &resourceName) const;

        /**
         * @brief Emits all global resources and safe access helpers for one RenderSet binding.
         */
        [[nodiscard]] std::string generateResourceDeclarations(const ShaderBindGroupInfo &bindGroupInfo);

        /**
         * @brief Lowers calls on `UGL::BufferComponent<T>` data packs.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateBufferComponentCall(const clang::CXXMemberCallExpr *expr,
                                                                                 const std::string &componentExpr,
                                                                                 const std::string &methodName);

        /**
         * @brief Lowers calls on `UGL::TextureComponent<T, N>` data packs.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateTextureComponentCall(const clang::CXXMemberCallExpr *expr,
                                                                                  const std::string &componentExpr,
                                                                                  const std::string &methodName);

        /**
         * @brief Lowers calls on RenderSet metadata access packs.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateDataPackCall(const clang::CXXMemberCallExpr *expr,
                                                                          const std::string &renderSetExpr,
                                                                          const std::string &methodName);

    private:
        [[nodiscard]] std::string getAccessBoundDataGlobalName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getComponentListGlobalName(const std::string &renderSetName, const std::string &resourceName) const;
        [[nodiscard]] std::string getEntityInfoGlobalName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getCMDParamsGlobalName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getLoadEntityInfoHelperName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getLoadCMDParamsHelperName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getCheckValidHelperName(const std::string &renderSetName) const;
        [[nodiscard]] std::string getBufferGetRawHelperName(const std::string &resourceGlobalName) const;
        [[nodiscard]] std::string getBufferCheckValidHelperName(const std::string &resourceGlobalName) const;
        [[nodiscard]] std::string getBufferGetHelperName(const std::string &resourceGlobalName) const;
        [[nodiscard]] std::string getTextureResolveIndexHelperName(const std::string &resourceGlobalName) const;
        /** Returns the HLSL helper name that returns one bindless texture selected by entity and slot. */
        [[nodiscard]] std::string getTextureGetHelperName(const std::string &resourceGlobalName) const;
        void appendBufferHelperDefinitions(std::string &helperDefinitions,
                                           const std::string &accessBoundDataGlobalName,
                                           const std::string &accessBoundEntryIndex,
                                           const std::string &componentListGlobalName,
                                           const std::string &resourceGlobalName,
                                           const std::string &elementTypeName);
        void appendTextureHelperDefinitions(std::string &helperDefinitions,
                                            const std::string &accessBoundDataGlobalName,
                                            const std::string &accessBoundEntryIndex,
                                            const std::string &componentListGlobalName,
                                            const std::string &resourceGlobalName,
                                            const std::string &textureSampleTypeName);
        [[nodiscard]] std::string generateTypeName(clang::QualType type);
        [[nodiscard]] std::string translateArg(const clang::CXXMemberCallExpr *expr, unsigned argIndex);
        [[nodiscard]] static std::string makeVulkanBindingAttribute(int bindingIndex, int bindGroupIndex);

        BaseASTVisitor &mVisitor;
        AbstractTypeConvertor *mTypeConvertor = nullptr;
    };
} // namespace UGLC::CodeGen::HLSL
