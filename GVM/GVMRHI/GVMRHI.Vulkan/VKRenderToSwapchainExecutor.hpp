#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKRenderToSwapchainExecutorImpl final : public RefCountedObject
    {
    public:
        void init(VKDevice *device);
        void encode(VKRenderPassEncoder *encoder, Texture sourceTexture, TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);

    private:
        struct CachedBindGroupEntry
        {
            TextureView sourceView = {};
            BindGroup bindGroup = {};
        };

        RenderPipeline getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);
        BindGroup getOrCreateBindGroup(Texture sourceTexture);

        VKDevice *mDevice = nullptr;
        Sampler mSampler = {};
        BindGroupLayout mBindGroupLayout = {};
        PipelineLayout mPipelineLayout = {};
        ShaderModule mVertexShader = {};
        ShaderModule mFragmentShader = {};
        ShaderModule mFlippedFragmentShader = {};
        eastl::unordered_map<uint64_t, RenderPipeline> mPipelines;
        eastl::vector<CachedBindGroupEntry> mCachedBindGroups;
    };

    using VKRenderToSwapchainExecutor = eastl::intrusive_ptr<VKRenderToSwapchainExecutorImpl>;
} // namespace GVM::RHI::Vulkan
