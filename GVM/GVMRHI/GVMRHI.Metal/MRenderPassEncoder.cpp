#include "MRenderPassEncoder.hpp"
#include "MBindGroup.hpp"
#include "MBindGroupLayout.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MRenderPipeline.hpp"
#include "MTexture.hpp"
#include "MTextureView.hpp"
#include "MUtilShaders/MRenderToSwapchainExecutor.hpp"
#include "Private/PixelLocalPassAccess.hpp"
#include <stdexcept>
#include <string>
namespace GVM::RHI::Metal
{
    namespace
    {
        bool isStripTopology(PrimitiveTopology topology)
        {
            return topology == PrimitiveTopology::LineStrip || topology == PrimitiveTopology::TriangleStrip;
        }

        const MRenderPipeline &getActivePipeline(const DrawCommandCacheData &cacheData, const char *operationName)
        {
            if (cacheData.pipeline == nullptr)
            {
                throw std::runtime_error(std::string(operationName) + " requires a render pipeline to be bound first");
            }

            auto pipeline = eastl::static_pointer_cast<MRenderPipeline>(cacheData.pipeline);
            if (pipeline == nullptr)
            {
                throw std::runtime_error(std::string(operationName) + " failed to resolve the active Metal render pipeline");
            }

            return *pipeline;
        }

        uint32_t translatePrimitiveTypeToIndirectToken(MTL::PrimitiveType primitiveType)
        {
            switch (primitiveType)
            {
            case MTL::PrimitiveType::PrimitiveTypePoint:
                return MIndirectRenderCommandDrawPrimitiveType::point;
            case MTL::PrimitiveType::PrimitiveTypeLine:
                return MIndirectRenderCommandDrawPrimitiveType::line;
            case MTL::PrimitiveType::PrimitiveTypeLineStrip:
                return MIndirectRenderCommandDrawPrimitiveType::lineStrip;
            case MTL::PrimitiveType::PrimitiveTypeTriangle:
                return MIndirectRenderCommandDrawPrimitiveType::triangle;
            case MTL::PrimitiveType::PrimitiveTypeTriangleStrip:
                return MIndirectRenderCommandDrawPrimitiveType::triangleStrip;
            }

            throw std::runtime_error("Unsupported Metal primitive type for indirect draw conversion");
        }

        void validateIndexedDrawState(const DrawCommandCacheData &cacheData, const MRenderPipeline &pipeline, const char *operationName)
        {
            if (!cacheData.hasIndexBuffer || cacheData.setIndexBuffer.indexBuffer.buffer.isNull())
            {
                throw std::runtime_error(std::string(operationName) + " requires setIndexBuffer() before indexed drawing");
            }

            if (cacheData.setIndexBuffer.indexFormat == IndexFormat::Undefined)
            {
                throw std::runtime_error(std::string(operationName) + " requires a concrete index format");
            }

            if (isStripTopology(pipeline.getPrimitiveTopology()))
            {
                const auto stripIndexFormat = pipeline.getStripIndexFormat();
                if (stripIndexFormat == IndexFormat::Undefined)
                {
                    throw std::runtime_error(std::string(operationName) + " requires primitive.stripIndexFormat for strip topologies");
                }
                if (stripIndexFormat != cacheData.setIndexBuffer.indexFormat)
                {
                    throw std::runtime_error(std::string(operationName) + " index format does not match pipeline primitive.stripIndexFormat");
                }
            }
        }

        void validateIndirectCommandLayout(uint64_t offset, uint32_t stride, uint32_t expectedStride, const char *operationName)
        {
            if (stride < expectedStride || (stride % sizeof(uint32_t)) != 0u)
            {
                throw std::runtime_error(std::string(operationName) + " requires indirect stride to be at least the native command size and aligned to uint32_t");
            }
            if ((offset % sizeof(uint32_t)) != 0u)
            {
                throw std::runtime_error(std::string(operationName) + " requires indirect buffer offset to be aligned to uint32_t");
            }
        }

        MTL::RenderPassDescriptor *translateRenderPassDescriptor(MDevice *device, MTL::RenderPassDescriptor *nativePass, const RenderPassDescriptor &pass)
        {

            int colorAttachmentCounter = 0;
            for (const auto &colorAttachment : pass.colorAttachments)
            {
                auto nativeTextureView = static_cast<MTextureView *>(device->mTextureViewPool.getByHandle(colorAttachment.view))->getNativeTextureView();
                MTL::RenderPassColorAttachmentDescriptor *colorAttachmentDescriptor = nativePass->colorAttachments()->object(colorAttachmentCounter);
                colorAttachmentDescriptor->setClearColor(translateClearColorToMTL(colorAttachment.clearValue));
                colorAttachmentDescriptor->setLoadAction(translateLoadActionToMTL(colorAttachment.loadOp));
                colorAttachmentDescriptor->setStoreAction(translateStoreActionToMTL(colorAttachment.storeOp));
                colorAttachmentDescriptor->setTexture(nativeTextureView);
                colorAttachmentCounter++;
            }
            if (pass.depthStencilAttachment.view.isNull() == false)
            {
                auto nativeTextureView = static_cast<MTextureView *>(device->mTextureViewPool.getByHandle(pass.depthStencilAttachment.view))->getNativeTextureView();
                MTL::RenderPassDepthAttachmentDescriptor *depthAttachmentDescriptor = nativePass->depthAttachment();
                depthAttachmentDescriptor->setClearDepth((double)pass.depthStencilAttachment.depthClearValue);
                depthAttachmentDescriptor->setLoadAction(translateLoadActionToMTL(pass.depthStencilAttachment.depthLoadOp));
                depthAttachmentDescriptor->setStoreAction(translateStoreActionToMTL(pass.depthStencilAttachment.depthStoreOp));
                depthAttachmentDescriptor->setTexture(nativeTextureView);
            }

            return nativePass;
        }

        void configureRenderTimestampWrites(MTL::RenderPassDescriptor *nativePass, const PassTimestampWrites &timestampWrites)
        {
            MQuerySet *querySet = validateMetalTimestampWrites("MRenderPassEncoder::end", timestampWrites);
            if (querySet == nullptr)
            {
                return;
            }

            auto *attachment = nativePass->sampleBufferAttachments()->object(0);
            attachment->setSampleBuffer(querySet->getNativeCounterSampleBuffer());
            attachment->setStartOfVertexSampleIndex(toMetalCounterSampleIndex(timestampWrites.beginningOfPassWriteIndex));
            attachment->setEndOfVertexSampleIndex(MTL::CounterDontSample);
            attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
            attachment->setEndOfFragmentSampleIndex(toMetalCounterSampleIndex(timestampWrites.endOfPassWriteIndex));
        }

        void configureRenderPassCounterWrites(MTL::RenderPassDescriptor *nativePass, const PassCounterWrites &counterWrites)
        {
            MQuerySet *stageQuerySet = validateMetalPassCounterWrites(
                "MRenderPassEncoder::init",
                QueryType::PassCounterStageUtilization,
                counterWrites.stageUtilizationQuerySet,
                counterWrites.stageUtilizationBeginIndex,
                counterWrites.stageUtilizationEndIndex);
            if (stageQuerySet != nullptr)
            {
                auto *attachment = nativePass->sampleBufferAttachments()->object(1);
                attachment->setSampleBuffer(stageQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfVertexSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationBeginIndex));
                attachment->setEndOfVertexSampleIndex(MTL::CounterDontSample);
                attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
                attachment->setEndOfFragmentSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationEndIndex));
            }

            MQuerySet *statisticQuerySet = validateMetalPassCounterWrites(
                "MRenderPassEncoder::init",
                QueryType::PassCounterStatistic,
                counterWrites.statisticQuerySet,
                counterWrites.statisticBeginIndex,
                counterWrites.statisticEndIndex);
            if (statisticQuerySet != nullptr)
            {
                auto *attachment = nativePass->sampleBufferAttachments()->object(2);
                attachment->setSampleBuffer(statisticQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfVertexSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticBeginIndex));
                attachment->setEndOfVertexSampleIndex(MTL::CounterDontSample);
                attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
                attachment->setEndOfFragmentSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticEndIndex));
            }
        }

        PixelLocalPassAttachmentAccess resolvePipelineAttachmentAccess(const MRenderPipeline &pipeline)
        {
            const RenderPipelineDescriptor &descriptor = pipeline.getDescriptor();
            if (GVM::RHI::Private::hasExplicitPixelLocalAttachmentAccess(descriptor.pixelLocalAttachmentAccess))
            {
                return descriptor.pixelLocalAttachmentAccess;
            }

            return GVM::RHI::Private::derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(descriptor);
        }
    } // namespace
    MRenderPassEncoder::MRenderPassEncoder()
    {
    }

    MRenderPassEncoder::~MRenderPassEncoder()
    {
    }

    void MRenderPassEncoder::init(MDevice *device, GVM::RHI::CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor)
    {
        this->mDevice = device;
        this->mDescriptor = descriptor;
        this->mQueuedPixelLocalPassIndex = 0u;
        this->mEnded = false;
        this->mNativeRenderEncoder = nullptr;
        this->mNativeRenderEncoderDesp = nullptr;
        this->mDrawCommands.clear();
        this->mWeakCommandEncoder = commandEncoder.get();
        this->mCommandBuffer = eastl::static_pointer_cast<GVM::RHI::Metal::MCommandEncoder>(commandEncoder)->getNativeCommandEncoder();
        if (descriptor.colorAttachments.empty() == false)
        {
            attachmentWidth = static_cast<MTextureView *>(descriptor.colorAttachments[0].view.get())->getParentTexture()->getWidth();
            attachmentHeight = static_cast<MTextureView *>(descriptor.colorAttachments[0].view.get())->getParentTexture()->getHeight();
        }
        else
        {
            attachmentWidth = static_cast<MTextureView *>(descriptor.depthStencilAttachment.view.get())->getParentTexture()->getWidth();
            attachmentHeight = static_cast<MTextureView *>(descriptor.depthStencilAttachment.view.get())->getParentTexture()->getHeight();
        }
    }

    void MRenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        ensureOpen("MRenderPassEncoder::setScissorRect");

        CmdSetScissorRect cmdSetScissorRect;
        cmdSetScissorRect.rect = {.x = x, .y = y, .width = std::min(width, attachmentWidth), .height = std::min(height, attachmentHeight)};

        mDrawCommands.emplace_back(cmdSetScissorRect);
    }

    void MRenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        ensureOpen("MRenderPassEncoder::setViewport");

        CmdSetViewport cmdSetViewport;
        cmdSetViewport.viewport = {.x = x, .y = y, .width = std::min(width, float(attachmentWidth)), .height = std::min(height, float(attachmentHeight)), .minDepth = minDepth, .maxDepth = maxDepth};

        mDrawCommands.emplace_back(cmdSetViewport);
    }

    void MRenderPassEncoder::drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor)
    {
        ensureOpen("MRenderPassEncoder::drawFullscreenTexture");
        if (source.isNull())
        {
            throw std::invalid_argument("MRenderPassEncoder::drawFullscreenTexture requires a valid source texture.");
        }
        if (mDescriptor.colorAttachments.empty())
        {
            throw std::invalid_argument("MRenderPassEncoder::drawFullscreenTexture requires a color attachment target.");
        }
        if (source->getDepth() != 1u || source->getArrayLayerCount() != 1u)
        {
            throw std::invalid_argument("MRenderPassEncoder::drawFullscreenTexture currently only supports non-array 2D textures.");
        }

        auto *targetView = static_cast<MTextureView *>(mDescriptor.colorAttachments[0].view.get());
        if (targetView == nullptr || targetView->getParentTexture() == nullptr)
        {
            throw std::invalid_argument("MRenderPassEncoder::drawFullscreenTexture requires a valid color attachment view.");
        }

        setViewport(0.0f, 0.0f, static_cast<float>(attachmentWidth), static_cast<float>(attachmentHeight), 0.0f, 1.0f);
        setScissorRect(0, 0, attachmentWidth, attachmentHeight);
        mDevice->getRenderToSwapchainExecutor()->encode(this, source, targetView->getParentTexture()->getFormat(), descriptor);
    }

    void MRenderPassEncoder::setPipeline(RenderPipeline pipeline)
    {
        ensureOpen("MRenderPassEncoder::setPipeline");
        if (pipeline == nullptr)
        {
            throw std::invalid_argument("MRenderPassEncoder::setPipeline requires a valid pipeline.");
        }

        CmdSetPipeline cmdSetPipeline;
        cmdSetPipeline.pipeline = pipeline;

        mDrawCommands.emplace_back(cmdSetPipeline);
    }

    void MRenderPassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("MRenderPassEncoder::setBindGroup");
        if (group == nullptr)
        {
            throw std::invalid_argument("MRenderPassEncoder::setBindGroup requires a valid bind group.");
        }

        CmdSetBindGroup cmdSetBindGroup;
        cmdSetBindGroup.group.bindGroup = group;
        cmdSetBindGroup.group.groupIndex = groupIndex;
        mDrawCommands.emplace_back(cmdSetBindGroup);
    }

    void MRenderPassEncoder::setVertexBuffer(BufferRange buffer, uint32_t slot)
    {
        ensureOpen("MRenderPassEncoder::setVertexBuffer");
        if (buffer.buffer.isNull())
        {
            throw std::invalid_argument("MRenderPassEncoder::setVertexBuffer requires a valid buffer.");
        }

        CmdSetVertexBuffer cmdSetVertexBuffer;
        cmdSetVertexBuffer.vb.buffer = buffer;
        cmdSetVertexBuffer.vb.slot = slot;
        mDrawCommands.emplace_back(cmdSetVertexBuffer);
    }

    void MRenderPassEncoder::setIndexBuffer(BufferRange buffer, IndexFormat format)
    {
        ensureOpen("MRenderPassEncoder::setIndexBuffer");
        if (buffer.buffer.isNull() || format == IndexFormat::Undefined)
        {
            throw std::invalid_argument("MRenderPassEncoder::setIndexBuffer requires a valid buffer and index format.");
        }

        CmdSetIndexBuffer cmdSetIndexBuffer;
        cmdSetIndexBuffer.indexBuffer = buffer;
        cmdSetIndexBuffer.indexFormat = format;

        mDrawCommands.emplace_back(cmdSetIndexBuffer);
    }

    void MRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ensureOpen("MRenderPassEncoder::draw");

        CmdDraw cmdDraw;
        cmdDraw.vertexCount = vertexCount;
        cmdDraw.instanceCount = instanceCount;
        cmdDraw.firstVertex = firstVertex;
        cmdDraw.firstInstance = firstInstance;

        mDrawCommands.emplace_back(cmdDraw);
    }

    void MRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
    {
        ensureOpen("MRenderPassEncoder::drawIndexed");

        CmdDrawIndexed cmdDrawIndexed;
        cmdDrawIndexed.indexCount = indexCount;
        cmdDrawIndexed.instanceCount = instanceCount;
        cmdDrawIndexed.firstIndex = firstIndex;
        cmdDrawIndexed.firstVertex = baseVertex;
        cmdDrawIndexed.firstInstance = firstInstance;

        mDrawCommands.emplace_back(cmdDrawIndexed);
    }

    void MRenderPassEncoder::drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndirect");
        if (indirectBuffer.buffer.isNull())
        {
            throw std::invalid_argument("MRenderPassEncoder::drawIndirect requires a valid indirect buffer.");
        }

        CmdDrawIndirect cmdDrawIndirect;
        cmdDrawIndirect.indirectBuffer = indirectBuffer;
        cmdDrawIndirect.commandCount = indirectCommandCount;
        cmdDrawIndirect.indirectBufferStride = stride == 0 ? uint32_t(sizeof(IndirectRenderCommand)) : stride;

        mDrawCommands.emplace_back(cmdDrawIndirect);
    }

    void MRenderPassEncoder::drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndexedIndirect");
        if (indirectBuffer.buffer.isNull())
        {
            throw std::invalid_argument("MRenderPassEncoder::drawIndexedIndirect requires a valid indirect buffer.");
        }

        CmdDrawIndexedIndirect cmdDrawIndexedIndirect;
        cmdDrawIndexedIndirect.indirectBuffer = indirectBuffer;
        cmdDrawIndexedIndirect.commandCount = indirectCommandCount;
        cmdDrawIndexedIndirect.indirectBufferStride = stride == 0 ? uint32_t(sizeof(IndirectIndexedRenderCommand)) : stride;


        mDrawCommands.emplace_back(cmdDrawIndexedIndirect);
    }

    void MRenderPassEncoder::drawPixels()
    {
        ensureOpen("MRenderPassEncoder::drawPixels");
        mDrawCommands.emplace_back(CmdDrawPixels{});
    }

    void MRenderPassEncoder::nextPixelLocalPass()
    {
        ensureOpen("MRenderPassEncoder::nextPixelLocalPass");

        const uint32_t pixelLocalPassCount = mDescriptor.pixelLocal.enabled
            ? (mDescriptor.pixelLocal.passCount == 0u ? 1u : mDescriptor.pixelLocal.passCount)
            : 1u;
        if (pixelLocalPassCount <= 1u)
        {
            throw std::runtime_error("MRenderPassEncoder::nextPixelLocalPass requires a render pass with multiple pixelLocal passes.");
        }
        if (mQueuedPixelLocalPassIndex + 1u >= pixelLocalPassCount)
        {
            throw std::runtime_error("MRenderPassEncoder::nextPixelLocalPass exceeded the render pass pixelLocal pass count.");
        }

        mDrawCommands.emplace_back(CmdNextPixelLocalPass{});
        ++mQueuedPixelLocalPassIndex;
    }

    void MRenderPassEncoder::end()
    {
        if (mEnded)
        {
            throw std::runtime_error("MRenderPassEncoder::end called after end().");
        }
        ensureOpen("MRenderPassEncoder::end");
        const uint32_t pixelLocalPassCount = mDescriptor.pixelLocal.enabled
            ? (mDescriptor.pixelLocal.passCount == 0u ? 1u : mDescriptor.pixelLocal.passCount)
            : 1u;
        if (pixelLocalPassCount > 1u && mQueuedPixelLocalPassIndex + 1u != pixelLocalPassCount)
        {
            throw std::runtime_error("MRenderPassEncoder::end called before all declared pixelLocal passes were reached.");
        }
        mDescriptor.pixelLocal.attachmentAccesses = resolvePixelLocalAttachmentAccessesFromCommands();
        if (mDescriptor.pixelLocal.enabled && mDescriptor.pixelLocal.attachmentAccesses.size() != pixelLocalPassCount)
        {
            throw std::runtime_error("MRenderPassEncoder requires one explicit pixelLocal attachmentAccesses entry for each declared pixelLocal pass.");
        }
        auto *commandEncoder = static_cast<MCommandEncoder *>(mWeakCommandEncoder);
        {
            eastl::vector<MIndirectIndexedRenderCommandConvertOptions> indirectOptions;
            DrawCommandCacheData cacheData{};
            for (const auto &cmd : mDrawCommands)
            {
                std::visit(
                    [&](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, CmdSetIndexBuffer>)
                        {
                            cacheData.setIndexBuffer = arg;
                            cacheData.hasIndexBuffer = true;
                        }
                        else if constexpr (std::is_same_v<T, CmdSetPipeline>)
                        {
                            cacheData.pipeline = arg.pipeline;
                        }
                        else if constexpr (std::is_same_v<T, CmdDrawIndirect>)
                        {
                            const auto &pipeline = getActivePipeline(cacheData, "drawIndirect");
                            validateIndirectCommandLayout(arg.indirectBuffer.offset, arg.indirectBufferStride, sizeof(IndirectRenderCommand), "drawIndirect");
                            if (arg.commandCount == 1u)
                            {
                                return;
                            }
                            indirectOptions.emplace_back(MIndirectIndexedRenderCommandConvertOptions{
                                .indirectCommandCount = arg.commandCount,
                                .indirectCommandStride = arg.indirectBufferStride,
                                .indirectRenderCommandDrawPrimitiveType = translatePrimitiveTypeToIndirectToken(pipeline.getPrimitiveType()),
                                .commandType = MTL::IndirectCommandTypeDraw,
                                .indirectBuffer = arg.indirectBuffer,
                            });
                        }
                        else if constexpr (std::is_same_v<T, CmdDrawIndexedIndirect>)
                        {
                            const auto &pipeline = getActivePipeline(cacheData, "drawIndexedIndirect");
                            validateIndexedDrawState(cacheData, pipeline, "drawIndexedIndirect");
                            validateIndirectCommandLayout(arg.indirectBuffer.offset, arg.indirectBufferStride, sizeof(IndirectIndexedRenderCommand), "drawIndexedIndirect");
                            indirectOptions.emplace_back(MIndirectIndexedRenderCommandConvertOptions{
                                .indirectCommandCount = arg.commandCount,
                                .indirectCommandStride = arg.indirectBufferStride,
                                .indirectRenderCommandDrawPrimitiveType = translatePrimitiveTypeToIndirectToken(pipeline.getPrimitiveType()),
                                .commandType = MTL::IndirectCommandTypeDrawIndexed,
                                .indexType = translateIndexFormatToMTL(cacheData.setIndexBuffer.indexFormat),
                                .indexBuffer = cacheData.setIndexBuffer.indexBuffer,
                                .indirectBuffer = arg.indirectBuffer,
                            });
                        }
                    },
                    cmd);
            }
            if (indirectOptions.empty() == false)
            {
                MCommandEncoder::DeferredRenderPrepassScope prepassScope(commandEncoder);
                mDevice->getIndirectIndexedRenderCommandConvertShaderExecutor()->execute(mWeakCommandEncoder, mDescriptor.label, indirectOptions);
            }
        }
        {
            mNativeRenderEncoderDesp = MTL::RenderPassDescriptor::alloc()->init();
            translateRenderPassDescriptor(mDevice, mNativeRenderEncoderDesp, mDescriptor);
            configureRenderTimestampWrites(mNativeRenderEncoderDesp, mDescriptor.timestampWrites);
            configureRenderPassCounterWrites(mNativeRenderEncoderDesp, mDescriptor.counterWrites);
            if (mDescriptor.timestampWrites.querySet != nullptr && commandEncoder != nullptr)
            {
                commandEncoder->retainQuerySet(mDescriptor.timestampWrites.querySet);
            }
            if (mDescriptor.counterWrites.stageUtilizationQuerySet != nullptr && commandEncoder != nullptr)
            {
                commandEncoder->retainQuerySet(mDescriptor.counterWrites.stageUtilizationQuerySet);
            }
            if (mDescriptor.counterWrites.statisticQuerySet != nullptr && commandEncoder != nullptr)
            {
                commandEncoder->retainQuerySet(mDescriptor.counterWrites.statisticQuerySet);
            }
            this->mNativeRenderEncoder = mCommandBuffer->renderCommandEncoder(mNativeRenderEncoderDesp);
            if (mDescriptor.label.empty() == false)
            {
                mNativeRenderEncoder->setLabel(NS::String::string(mDescriptor.label.c_str(), NS::UTF8StringEncoding));
            }
            mNativeRenderEncoderDesp->release();
        }
        try
        {
            replayCommand();
        }
        catch (const std::exception &error)
        {
            this->mNativeRenderEncoder->endEncoding();
            if (commandEncoder != nullptr)
            {
                commandEncoder->notifyPassEnded();
            }
            mDrawCommands.clear();
            mEnded = true;
            throw std::runtime_error(
                std::string("Metal render pass '") +
                mDescriptor.label.c_str() +
                "': " +
                error.what());
        }
        this->mNativeRenderEncoder->endEncoding();
        if (commandEncoder != nullptr)
        {
            commandEncoder->notifyPassEnded();
        }
        mDrawCommands.clear();
        mEnded = true;
    }

    MTL::RenderCommandEncoder *MRenderPassEncoder::getNativeRenderEncoder() const
    {
        return this->mNativeRenderEncoder;
    }

    void MRenderPassEncoder::replayCommand()
    {
        DrawCommandCacheData cacheData{};
        for (const auto &cmd : mDrawCommands)
        {
            std::visit(
                [&](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, CmdSetScissorRect>)
                    {
                        setScissorRectInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, CmdSetViewport>)
                    {
                        setViewportInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, CmdSetPipeline>)
                    {
                        cacheData.maxVertexBufferCountFromPipeline = eastl::static_pointer_cast<MRenderPipeline>(arg.pipeline)->getVertexBufferCount();
                        if (cacheData.pipeline == nullptr || cacheData.pipeline != arg.pipeline)
                        {
                            cacheData.pipeline = arg.pipeline;
                            cacheData.bindgroups.clear();
                            setPipelineInternal(arg);
                        }
                    }
                    else if constexpr (std::is_same_v<T, CmdSetBindGroup>)
                    {
                        const auto &it = cacheData.bindgroups.find(arg.group.groupIndex);
                        if (it != cacheData.bindgroups.end() && it->second != arg.group.bindGroup)
                        {
                            setBindGroupInternal(arg, cacheData);
                            cacheData.bindgroups[arg.group.groupIndex] = arg.group.bindGroup;
                        }
                        if (it == cacheData.bindgroups.end())
                        {
                            cacheData.bindgroups.emplace(arg.group.groupIndex, arg.group.bindGroup);
                            setBindGroupInternal(arg, cacheData);
                        }
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVertexBuffer>)
                    {
                        setVertexBufferInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, CmdSetIndexBuffer>)
                    {
                        cacheData.setIndexBuffer = arg;
                        cacheData.hasIndexBuffer = true;
                        setIndexBufferInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdDraw>)
                    {
                        drawInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdDrawIndexed>)
                    {
                        drawIndexedInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdDrawIndirect>)
                    {
                        drawIndirectInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdDrawIndexedIndirect>)
                    {
                        drawIndexedIndirectInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdDrawPixels>)
                    {
                        drawPixelsInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, CmdNextPixelLocalPass>)
                    {
                        // Metal framebuffer-fetch pixel-local passes do not have a Vulkan-style subpass transition to replay here.
                        // The command only advances pixel-local access analysis before encoder creation.
                    }
                },
                cmd);
        }
    }

    void MRenderPassEncoder::setScissorRectInternal(const CmdSetScissorRect &descriptor)
    {
        MTL::ScissorRect scissorRect;
        scissorRect.x = descriptor.rect.x;
        scissorRect.y = descriptor.rect.y;
        scissorRect.width = descriptor.rect.width;
        scissorRect.height = descriptor.rect.height;
        this->mNativeRenderEncoder->setScissorRect(scissorRect);
    }

    void MRenderPassEncoder::setViewportInternal(const CmdSetViewport &descriptor)
    {
        MTL::Viewport viewport;
        viewport.originX = descriptor.viewport.x;
        viewport.originY = descriptor.viewport.y;
        viewport.width = descriptor.viewport.width;
        viewport.height = descriptor.viewport.height;
        viewport.znear = descriptor.viewport.minDepth;
        viewport.zfar = descriptor.viewport.maxDepth;
        this->mNativeRenderEncoder->setViewport(viewport);
    }

    void MRenderPassEncoder::setPipelineInternal(const CmdSetPipeline &descriptor)
    {
        auto mpipe = eastl::static_pointer_cast<MRenderPipeline>(descriptor.pipeline);
        this->mNativeRenderEncoder->setRenderPipelineState(mpipe->getNativePipelineState());
        auto depthState = mpipe->getNativeDepthState();
        if (depthState != nullptr)
        {
            this->mNativeRenderEncoder->setDepthStencilState(depthState);
        }
        this->mNativeRenderEncoder->setCullMode(mpipe->getCullMode());
        this->mNativeRenderEncoder->setFrontFacingWinding(mpipe->getFrontFaceWinding());
        this->mNativeRenderEncoder->setDepthBias(mpipe->getDepthBias(), mpipe->getDepthBiasSlopeScale(), mpipe->getDepthBiasClamp());
    }

    void MRenderPassEncoder::setBindGroupInternal(const CmdSetBindGroup &descriptor, const DrawCommandCacheData &cacheData)
    {
        auto metalBindGroup = eastl::static_pointer_cast<MBindGroup>(descriptor.group.bindGroup);
        const int vertexBufferCount = cacheData.maxVertexBufferCountFromPipeline; // eastl::static_pointer_cast<MRenderPipeline>(descriptor.pipeline)->getVertexBufferCount();
        const uint32_t bindingIndex = vertexBufferCount + descriptor.group.groupIndex;
        auto renderPipeline = eastl::static_pointer_cast<MRenderPipeline>(cacheData.pipeline);
        const auto bindGroupLayout = eastl::static_pointer_cast<MBindGroupLayout>(metalBindGroup->getDescriptor().layout);
        GVM::RHI::ShaderStageFlags visibilityMask = GVM::RHI::ShaderStage::None;
        for (const auto &entry : bindGroupLayout->getDescriptor().entries)
        {
            visibilityMask |= entry.visibility;
        }

        const bool bindVertexFromLayout = (visibilityMask & GVM::RHI::ShaderStage::Vertex) != 0;
        const bool bindFragmentFromLayout = (visibilityMask & GVM::RHI::ShaderStage::Fragment) != 0;
        const bool useReflection = renderPipeline != nullptr;
        const bool bindVertexStage =
            bindVertexFromLayout || (useReflection && renderPipeline->usesVertexArgumentBuffer(bindingIndex));
        const bool bindFragmentStage =
            bindFragmentFromLayout || (useReflection && renderPipeline->usesFragmentArgumentBuffer(bindingIndex));

        metalBindGroup->trackUsage(this->mNativeRenderEncoder);
        if (bindVertexStage)
        {
            mNativeRenderEncoder->setVertexBuffer(metalBindGroup->getNativeBuffer(), 0, bindingIndex);
        }
        if (bindFragmentStage)
        {
            mNativeRenderEncoder->setFragmentBuffer(metalBindGroup->getNativeBuffer(), 0, bindingIndex);
        }
    }

    void MRenderPassEncoder::setVertexBufferInternal(const CmdSetVertexBuffer &descriptor)
    {

        this->mNativeRenderEncoder->setVertexBuffer(static_cast<MBuffer *>(descriptor.vb.buffer.buffer.get())->getNativeBuffer(), descriptor.vb.buffer.offset, descriptor.vb.slot);
    }

    void MRenderPassEncoder::setIndexBufferInternal(const CmdSetIndexBuffer &descriptor, const DrawCommandCacheData &cacheData)
    {
        if (descriptor.indexBuffer.buffer.isNull())
        {
            throw std::runtime_error("setIndexBuffer requires a valid buffer");
        }
        if (descriptor.indexFormat == IndexFormat::Undefined)
        {
            throw std::runtime_error("setIndexBuffer requires a concrete index format");
        }

        static_cast<MBuffer *>(descriptor.indexBuffer.buffer.get())->getNativeBuffer();

        if (cacheData.pipeline != nullptr)
        {
            getActivePipeline(cacheData, "setIndexBuffer");
        }
    }

    void MRenderPassEncoder::drawInternal(const CmdDraw &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "draw");
        this->mNativeRenderEncoder->drawPrimitives(pipeline.getPrimitiveType(), descriptor.firstVertex, descriptor.vertexCount, descriptor.instanceCount, descriptor.firstInstance);
    }

    void MRenderPassEncoder::drawIndexedInternal(const CmdDrawIndexed &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "drawIndexed");
        validateIndexedDrawState(cacheData, pipeline, "drawIndexed");

        uint64_t indexAdditionalOffset = descriptor.firstIndex * Private::getIndexBufferElementStorageSizeFromFormat(cacheData.setIndexBuffer.indexFormat);
        this->mNativeRenderEncoder->drawIndexedPrimitives(pipeline.getPrimitiveType(), descriptor.indexCount, translateIndexFormatToMTL(cacheData.setIndexBuffer.indexFormat), static_cast<MBuffer *>(cacheData.setIndexBuffer.indexBuffer.buffer.get())->getNativeBuffer(), cacheData.setIndexBuffer.indexBuffer.offset + indexAdditionalOffset, descriptor.instanceCount, descriptor.firstVertex, descriptor.firstInstance);
    }

    void MRenderPassEncoder::drawIndirectInternal(const CmdDrawIndirect &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "drawIndirect");
        validateIndirectCommandLayout(descriptor.indirectBuffer.offset, descriptor.indirectBufferStride, sizeof(IndirectRenderCommand), "drawIndirect");

        if (descriptor.commandCount == 1u)
        {
            this->mNativeRenderEncoder->drawPrimitives(
                pipeline.getPrimitiveType(),
                static_cast<MBuffer *>(descriptor.indirectBuffer.buffer.get())->getNativeBuffer(),
                descriptor.indirectBuffer.offset);
            return;
        }

        this->mNativeRenderEncoder->executeCommandsInBuffer(
            static_cast<MBuffer *>(descriptor.indirectBuffer.buffer.get())->getOrCreateNativeICB(MTL::IndirectCommandTypeDraw),
            NS::Range(0, descriptor.commandCount));
    }

    void MRenderPassEncoder::drawIndexedIndirectInternal(const CmdDrawIndexedIndirect &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "drawIndexedIndirect");
        validateIndexedDrawState(cacheData, pipeline, "drawIndexedIndirect");
        validateIndirectCommandLayout(descriptor.indirectBuffer.offset, descriptor.indirectBufferStride, sizeof(IndirectIndexedRenderCommand), "drawIndexedIndirect");

        this->mNativeRenderEncoder->executeCommandsInBuffer(
            static_cast<MBuffer *>(descriptor.indirectBuffer.buffer.get())->getOrCreateNativeICB(MTL::IndirectCommandTypeDrawIndexed),
            NS::Range(0, descriptor.commandCount));
    }

    void MRenderPassEncoder::drawPixelsInternal(const CmdDrawPixels &descriptor, const DrawCommandCacheData &cacheData)
    {
        (void)descriptor;
        const auto &pipeline = getActivePipeline(cacheData, "drawPixels");
        if (pipeline.isPixelLocalFramebufferFetchPipeline())
        {
            this->mNativeRenderEncoder->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            return;
        }

        throw std::runtime_error("MRenderPassEncoder::drawPixels requires a Metal pixelLocal framebuffer_fetch pipeline.");
    }

    eastl::vector<PixelLocalPassAttachmentAccess> MRenderPassEncoder::resolvePixelLocalAttachmentAccessesFromCommands() const
    {
        if (!mDescriptor.pixelLocal.enabled)
        {
            return {};
        }

        GVM::RHI::Private::PixelLocalPassAccessBuilder builder;
        RenderPipeline currentPipeline = nullptr;

        for (const DrawCommand &command : mDrawCommands)
        {
            if (const CmdSetPipeline *pipelineCommand = std::get_if<CmdSetPipeline>(&command))
            {
                currentPipeline = pipelineCommand->pipeline;
                continue;
            }

            if (std::get_if<CmdNextPixelLocalPass>(&command) != nullptr)
            {
                builder.nextPass();
                continue;
            }

            const bool isDrawCommand = std::get_if<CmdDraw>(&command) != nullptr ||
                std::get_if<CmdDrawIndexed>(&command) != nullptr ||
                std::get_if<CmdDrawIndirect>(&command) != nullptr ||
                std::get_if<CmdDrawIndexedIndirect>(&command) != nullptr ||
                std::get_if<CmdDrawPixels>(&command) != nullptr;
            if (isDrawCommand && currentPipeline != nullptr)
            {
                const auto pipeline = eastl::static_pointer_cast<MRenderPipeline>(currentPipeline);
                builder.appendTask(resolvePipelineAttachmentAccess(*pipeline));
            }
        }

        return builder.finish();
    }

    void MRenderPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mWeakCommandEncoder == nullptr || mCommandBuffer == nullptr)
        {
            throw std::runtime_error(std::string(apiName) + " called on a closed or uninitialized render pass encoder.");
        }
    }

} // namespace GVM::RHI::Metal
