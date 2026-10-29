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
    /** Matches the uniform layout consumed by the native indirect conversion shaders. */
    struct IndirectCommandInfo
    {
        uint32_t PrimitiveType;
        uint32_t MaxCommandCount;
        uint32_t IterationCount;
        uint32_t MaxThreadsCount;
        uint32_t CommandStride;
        uint64_t CountAddress;
    };

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
        mDrawIndexed32Pipeline = createIndexedPipeline(MTL::IndexTypeUInt32);
        mDrawIndexed16Pipeline = createIndexedPipeline(MTL::IndexTypeUInt16);
        mDrawPipeline = createDrawPipeline();
    }

    ComputePipeline MIndirectBufferConvertShaderExecutorImpl::createIndexedPipeline(MTL::IndexType indexType)
    {
        const eastl::string indexTypeName = indexType == MTL::IndexTypeUInt16 ? "ushort" : "uint";
        auto shaderModule = mDevice->createShaderModule({
            .code = getIndirectIndexedRenderCommandConvertShader(indexTypeName),
        });
        return mDevice->createComputePipeline({
            .compute = {
                .module = shaderModule,
                .entryPoint = "convertIndirectIndexedCommands",
                .workgroupX = 64,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });
    }

    ComputePipeline MIndirectBufferConvertShaderExecutorImpl::createDrawPipeline()
    {
        auto shaderModule = mDevice->createShaderModule({
            .code = getIndirectRenderCommandConvertShader(),
        });
        return mDevice->createComputePipeline({
            .compute = {
                .module = shaderModule,
                .entryPoint = "convertIndirectCommands",
                .workgroupX = 64,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });
    }


    /** Encodes ordered preparation phases using each draw's independently selected destination. */
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

    /** Clears the logical range of the destination that this draw will execute. */
    void MIndirectBufferConvertShaderExecutorImpl::resetICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeEncoder = eastl::static_pointer_cast<MBlitPassEncoder>(encoder)->getNativeCommandEncoder();
        nativeEncoder->resetCommandsInBuffer(
            options.nativeICB, NS::Range(0, options.indirectCommandCount));
    }

    /** Translates the captured source and index state into this conversion's exclusive ICB. */
    void MIndirectBufferConvertShaderExecutorImpl::convertICB(ComputePassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeComputeEncoder = eastl::static_pointer_cast<MComputePassEncoder>(encoder)->getNativeEncoder();
        if (options.commandType == MTL::IndirectCommandTypeDrawIndexed)
        {
            encoder->setPipeline(options.indexType == MTL::IndexTypeUInt16 ? mDrawIndexed16Pipeline : mDrawIndexed32Pipeline);
        }
        else
        {
            encoder->setPipeline(mDrawPipeline);
        }

        IndirectCommandInfo ICBInfo;

        ICBInfo.PrimitiveType = options.indirectRenderCommandDrawPrimitiveType;
        const uint64_t commandSize = options.commandType == MTL::IndirectCommandTypeDrawIndexed ?
            sizeof(IndirectIndexedRenderCommand) : sizeof(IndirectRenderCommand);
        const uint64_t availableCommands = options.indirectBuffer.size < commandSize ? 0u :
            1u + (options.indirectBuffer.size - commandSize) / options.indirectCommandStride;
        ICBInfo.MaxCommandCount = static_cast<uint32_t>(eastl::min<uint64_t>(options.indirectCommandCount, availableCommands));

        ICBInfo.IterationCount = options.indirectCommandCount / MaxThreads +
            (options.indirectCommandCount % MaxThreads != 0);
        ICBInfo.MaxThreadsCount = MaxThreads;
        ICBInfo.CommandStride = options.indirectCommandStride;
        ICBInfo.CountAddress = options.countAddress;
        const uint32_t threadsCount = eastl::min(options.indirectCommandCount, MaxThreads);
        nativeComputeEncoder->setBytes(&ICBInfo, sizeof(ICBInfo), 0);

        nativeComputeEncoder->setBuffer(options.nativeIndirectBuffer, options.indirectBuffer.offset, 1);

        nativeComputeEncoder->setBuffer(options.nativeICBArgumentBuffer, 0, 2);
        nativeComputeEncoder->useResource(options.nativeICB, MTL::ResourceUsageWrite);
        if (options.nativeCountBuffer != nullptr)
        {
            nativeComputeEncoder->useResource(options.nativeCountBuffer, MTL::ResourceUsageRead);
        }
        if (options.commandType == MTL::IndirectCommandTypeDrawIndexed)
        {
            nativeComputeEncoder->setBuffer(options.nativeIndexBuffer, options.indexBuffer.offset, 3);
        }
        encoder->dispatchWorkgroups(eastl::max(xGE::Math::IntRoundUp(threadsCount, 64), uint32_t(1)), 1, 1);
    }

    /** Optimizes exactly the same destination range that the recorded draw executes. */
    void MIndirectBufferConvertShaderExecutorImpl::optimizeICB(BlitPassEncoder encoder, const MIndirectIndexedRenderCommandConvertOptions &options)
    {
        auto nativeEncoder = eastl::static_pointer_cast<MBlitPassEncoder>(encoder)->getNativeCommandEncoder();
        nativeEncoder->optimizeIndirectCommandBuffer(
            options.nativeICB, NS::Range(0, options.indirectCommandCount));
    }


} // namespace GVM::RHI::Metal
