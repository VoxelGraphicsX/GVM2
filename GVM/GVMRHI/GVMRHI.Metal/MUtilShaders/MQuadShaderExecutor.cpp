#include "MQuadShaderExecutor.hpp"
#include "../MBlitPassEncoder.hpp"
#include "../MBuffer.hpp"
#include "../MCommandEncoder.hpp"
#include "../MComputePassEncoder.hpp"
#include "../MComputePipeline.hpp"
#include "../MDevice.hpp"
#include "../MEnumUtils.hpp"
#include "MUtilShaders.hpp"
#include <xGEFoundation/xMath.hpp>
namespace GVM::RHI::Metal
{

    void MQuadShaderExecutorImpl::create(MDevice *device, GVM::RHI::TextureFormat swapchainTextureFormat)
    {
        mDevice = device;
        {
            auto shaderModule = device->createShaderModule({.code = getQuadComputeShader()});

            auto mPipeline = device->createComputePipeline({.compute = {.module = shaderModule, .entryPoint = "executeQuad", .workgroupX = 8, .workgroupY = 8, .workgroupZ = 1}});
            mQuadPipeline = mPipeline;
        }
    }

    void MQuadShaderExecutorImpl::execute(MTL::CommandBuffer *encoder, MTL::Texture *tempTexture, MTL::Texture *swapchainTexture)
    {
        auto nativeComputeEncoder = encoder->computeCommandEncoder();

        auto metalComputePipeline = eastl::static_pointer_cast<MComputePipeline>(mQuadPipeline);
        nativeComputeEncoder->setComputePipelineState(metalComputePipeline->getNativePipelineState());
        nativeComputeEncoder->setTexture(tempTexture, 0);
        nativeComputeEncoder->setTexture(swapchainTexture, 1);

        nativeComputeEncoder->dispatchThreadgroups(MTL::Size(xGE::Math::IntRoundUp(swapchainTexture->width(), metalComputePipeline->getLocalThreadGroupSize().width), xGE::Math::IntRoundUp(swapchainTexture->height(), metalComputePipeline->getLocalThreadGroupSize().height), 1), metalComputePipeline->getLocalThreadGroupSize());
        nativeComputeEncoder->endEncoding();
    }


} // namespace GVM::RHI::Metal
