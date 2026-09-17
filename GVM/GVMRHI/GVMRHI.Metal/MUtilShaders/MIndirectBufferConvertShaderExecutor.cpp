#include "MIndirectBufferConvertShaderExecutor.hpp"
#include "../MBlitPassEncoder.hpp"
#include "../MBuffer.hpp"
#include "../MCommandEncoder.hpp"
#include "../MComputePassEncoder.hpp"
#include "../MComputePipeline.hpp"
#include "../MDevice.hpp"
#include "MUtilShaders.hpp"
#include <xGEFoundation/xMath.hpp>
namespace GVM::RHI::Metal
{

    MIndirectBufferConvertShaderExecutorImpl::MIndirectBufferConvertShaderExecutorImpl()
    {
    }

    MIndirectBufferConvertShaderExecutorImpl::~MIndirectBufferConvertShaderExecutorImpl()
    {
    }

    void MIndirectBufferConvertShaderExecutorImpl::init(MDevice *device, const MIndirectBufferConvertShaderExecutorDescriptor &descriptor)
    {
        mDevice = device;
        (void)descriptor;
        getOrCreateIndexedPipeline(MTL::IndexTypeUInt32, sizeof(IndirectIndexedRenderCommand));
        getOrCreateIndexedPipeline(MTL::IndexTypeUInt16, sizeof(IndirectIndexedRenderCommand));
        getOrCreateDrawPipeline(sizeof(IndirectRenderCommand));
    }

    ComputePipeline MIndirectBufferConvertShaderExecutorImpl::getOrCreateIndexedPipeline(
        MTL::IndexType indexType,
        uint32_t commandStride)
    {
        auto &pipelines = indexType == MTL::IndexTypeUInt16
            ? mDrawIndirectIndexedPipelinesIndex16
            : mDrawIndirectIndexedPipelinesIndex32;
        const auto existingPipeline = pipelines.find(commandStride);
        if (existingPipeline != pipelines.end())
        {
            return existingPipeline->second;
        }

        const eastl::string indexTypeName = indexType == MTL::IndexTypeUInt16 ? "ushort" : "uint";
        auto shaderModule = mDevice->createShaderModule({
            .code = getIndirectIndexedRenderCommandConvertShader(indexTypeName, commandStride),
        });
        auto pipeline = mDevice->createComputePipeline({
            .compute = {
                .module = shaderModule,
                .entryPoint = "convertIndirectIndexedCommands",
                .workgroupX = 64,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });
        pipelines.emplace(commandStride, pipeline);
        return pipeline;
    }

    ComputePipeline MIndirectBufferConvertShaderExecutorImpl::getOrCreateDrawPipeline(uint32_t commandStride)
    {
        const auto existingPipeline = mDrawIndirectPipelines.find(commandStride);
        if (existingPipeline != mDrawIndirectPipelines.end())
        {
            return existingPipeline->second;
        }

        auto shaderModule = mDevice->createShaderModule({
            .code = getIndirectRenderCommandConvertShader(commandStride),
        });
        auto pipeline = mDevice->createComputePipeline({
            .compute = {
                .module = shaderModule,
                .entryPoint = "convertIndirectCommands",
                .workgroupX = 64,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });
        mDrawIndirectPipelines.emplace(commandStride, pipeline);
        return pipeline;
    }


    void MIndirectBufferConvertShaderExecutorImpl::execute(CommandEncoder encoder, const eastl::string &renderpassLabel, const eastl::vector<MIndirectIndexedRenderCommandConvertOptions> &options)
    {
        auto resetBlitEncoder = encoder->beginBlitPass({.label = renderpassLabel + " :: icb reset"});
        for (const auto &opt : options)
        {
            resetICB(resetBlitEncoder, opt);
        }
        resetBlitEncoder->end();
        auto computeEncoder = encoder->beginComputePass({.label = renderpassLabel + " :: ICB Convert Compute Pass"});
        for (const auto &opt : options)
        {
            convertICB(computeEncoder, opt);
        }
        computeEncoder->end();
        auto optimizeBlitEncoder = encoder->beginBlitPass({.label = renderpassLabel + " :: icb optimize"});
        for (const auto &opt : options)
        {
            optimizeICB(optimizeBlitEncoder, opt);
        }
        optimizeBlitEncoder->end();
    }

    void MIndirectBufferConvertShaderExecutorImpl::resetICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeICBArgument = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICBArgumentBuffer(options.commandType);
        auto nativeICB = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICB(options.commandType);

        {
            // auto resetBlitEncoder = nativeCommandEncoder->beginBlitPass({.label = "icb reset"});

            auto nativeResetBlitEncoder = eastl::static_pointer_cast<MBlitPassEncoder>(encoder)->getNativeCommandEncoder();
            nativeResetBlitEncoder->resetCommandsInBuffer(nativeICB, NS::Range(0, options.indirectCommandCount));
        }
    }

    void MIndirectBufferConvertShaderExecutorImpl::convertICB(ComputePassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeICBArgument = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICBArgumentBuffer(options.commandType);
        auto nativeICB = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICB(options.commandType);


        auto nativeComputeEncoder = eastl::static_pointer_cast<MComputePassEncoder>(encoder)->getNativeEncoder();
        if (options.commandType == MTL::IndirectCommandTypeDrawIndexed)
        {
            encoder->setPipeline(getOrCreateIndexedPipeline(options.indexType, options.indirectCommandStride));
        }
        else
        {
            encoder->setPipeline(getOrCreateDrawPipeline(options.indirectCommandStride));
        }

        struct IndirectCommandInfo
        {
            uint32_t PrimitiveType;
            uint32_t MaxCommandCount;
            uint32_t IterationCount;
            uint32_t MaxThreadsCount;
        } ICBInfo;

        ICBInfo.PrimitiveType = options.indirectRenderCommandDrawPrimitiveType;
        ICBInfo.MaxCommandCount = static_cast<uint32_t>(eastl::min<uint64_t>(
            options.indirectCommandCount,
            options.indirectBuffer.size / options.indirectCommandStride));

        ICBInfo.IterationCount = xGE::Math::IntRoundUp(options.indirectCommandCount, MaxThreads);
        ICBInfo.MaxThreadsCount = MaxThreads;
        const uint32_t threadsCount = eastl::min(options.indirectCommandCount, MaxThreads);
        nativeComputeEncoder->setBytes(&ICBInfo, sizeof(ICBInfo), 0);

        nativeComputeEncoder->setBuffer(static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getNativeBuffer(), options.indirectBuffer.offset, 1);

        nativeComputeEncoder->setBuffer(nativeICBArgument, 0, 2);
        nativeComputeEncoder->useResource(nativeICB, MTL::ResourceUsageWrite);
        if (options.commandType == MTL::IndirectCommandTypeDrawIndexed)
        {
            nativeComputeEncoder->setBuffer(static_cast<MBuffer *>(options.indexBuffer.buffer.get())->getNativeBuffer(), options.indexBuffer.offset, 3);
        }
        encoder->dispatchWorkgroups(eastl::max(xGE::Math::IntRoundUp(threadsCount, 64), uint32_t(1)), 1, 1);
        // encoder->end();
    }

    void MIndirectBufferConvertShaderExecutorImpl::optimizeICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeICBArgument = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICBArgumentBuffer(options.commandType);
        auto nativeICB = static_cast<MBuffer *>(options.indirectBuffer.buffer.get())->getOrCreateNativeICB(options.commandType);


        {
            auto nativeOptimizeBlitEncoder = eastl::static_pointer_cast<MBlitPassEncoder>(encoder)->getNativeCommandEncoder();
            nativeOptimizeBlitEncoder->optimizeIndirectCommandBuffer(nativeICB, NS::Range(0, options.indirectCommandCount));
        }
    }


} // namespace GVM::RHI::Metal
