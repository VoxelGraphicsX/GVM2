#pragma once
#include "../MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MQuadShaderExecutorImpl final : public GVM::RHI::RefCountedObject
    {
    public:
        void create(MDevice *device, GVM::RHI::TextureFormat swapchainTextureFormat);
        void execute(MTL::CommandBuffer *encoder, MTL::Texture *tempTexture, MTL::Texture *swapchainTexture);

    private:
        ComputePipeline mQuadPipeline = nullptr;
        MDevice *mDevice = nullptr;
        MTL::SamplerState *mSampler = nullptr;
    };
    using MQuadShaderExecutor = eastl::intrusive_ptr<MQuadShaderExecutorImpl>;
} // namespace GVM::RHI::Metal
