#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    namespace Detail
    {
        struct ReflectedEntryPoint;
    }

    class VKRenderPipeline final : public RenderPipelineImpl
    {
    public:
        /// Maps one generated pixel-local input attachment descriptor binding to its render pass input index.
        struct PixelLocalInputBinding
        {
            uint32_t binding = 0u;
            uint32_t inputAttachmentIndex = 0u;
        };

        VKRenderPipeline() = default;

        void init(VKDevice &device, const RenderPipelineDescriptor &descriptor);

        [[nodiscard]]
        vk::Pipeline getNativePipeline() const;

        [[nodiscard]]
        vk::Pipeline getNativePipelineForRenderPassSubpass(vk::RenderPass renderPass, uint32_t subpassIndex);

        [[nodiscard]]
        vk::PipelineLayout getNativePipelineLayout() const;

        [[nodiscard]]
        bool usesPixelLocalInputAttachments() const;

        [[nodiscard]]
        vk::DescriptorSetLayout getPixelLocalInputDescriptorSetLayout() const;

        [[nodiscard]]
        uint32_t getPixelLocalInputDescriptorSetIndex() const;

        [[nodiscard]]
        const eastl::vector<vk::DescriptorPoolSize> &getPixelLocalInputDescriptorPoolSizes() const;

        [[nodiscard]]
        const eastl::vector<PixelLocalInputBinding> &getPixelLocalInputBindings() const;

        [[nodiscard]]
        const PipelineLayout &getPipelineLayoutHandle() const;

        [[nodiscard]]
        const RenderPipelineDescriptor &getDescriptor() const;

        [[nodiscard]]
        size_t getNativePipelineCacheSizeForTesting() const;

    private:
        /// Caches a native pipeline compiled for one Vulkan render pass/subpass pair.
        struct NativePipelineCacheEntry
        {
            vk::RenderPass renderPass = nullptr;
            uint32_t subpassIndex = 0u;
            vk::UniquePipeline pipeline;
        };

        vk::UniquePipeline createNativePipelineForRenderPassSubpass(vk::RenderPass renderPass, uint32_t subpassIndex);
        void initPixelLocalInputLayout(const Detail::ReflectedEntryPoint &fragmentEntryPoint);

        VKDevice *mDevice = nullptr;
        RenderPipelineDescriptor mDescriptor = {};
        PipelineLayout mPipelineLayout = nullptr;
        vk::UniquePipeline mPipeline;
        eastl::vector<PixelLocalInputBinding> mPixelLocalInputBindings;
        eastl::vector<vk::DescriptorPoolSize> mPixelLocalInputDescriptorPoolSizes;
        uint32_t mPixelLocalInputDescriptorSetIndex = 0u;
        vk::UniqueDescriptorSetLayout mPixelLocalInputDescriptorSetLayout;
        vk::UniqueDescriptorSetLayout mPixelLocalEmptyDescriptorSetLayout;
        vk::UniquePipelineLayout mPixelLocalPipelineLayout;
        eastl::vector<NativePipelineCacheEntry> mNativePipelineCacheByRenderPassSubpass;
    };
} // namespace GVM::RHI::Vulkan
