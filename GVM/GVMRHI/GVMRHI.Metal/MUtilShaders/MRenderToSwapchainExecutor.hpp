#pragma once

#include "../MDefines.hpp"
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Metal
{
    class MRenderPassEncoder;

    class MRenderToSwapchainExecutorImpl final : public GVM::RHI::RefCountedObject
    {
    public:
        void create(MDevice *device);
        void encode(MRenderPassEncoder *encoder, Texture sourceTexture, TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);

    private:
        RenderPipeline getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor);

        MDevice *mDevice = nullptr;
        Sampler mSampler = {};
        BindGroupLayout mBindGroupLayout = {};
        PipelineLayout mPipelineLayout = {};
        ShaderModule mVertexShader = {};
        ShaderModule mFragmentShader = {};
        eastl::unordered_map<uint64_t, RenderPipeline> mPipelines;
    };

    using MRenderToSwapchainExecutor = eastl::intrusive_ptr<MRenderToSwapchainExecutorImpl>;
} // namespace GVM::RHI::Metal
