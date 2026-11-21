#include "MRenderPassEncoder.hpp"
#include "MBindGroup.hpp"
#include "MBufferTableLayout.hpp"
#include "MBindGroupLayout.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MRenderPipeline.hpp"
#include "MPipelineLayout.hpp"
#include "MTexture.hpp"
#include "MTextureView.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
namespace GVM::RHI::Metal
{
    namespace
    {
        const MRenderPipeline &getActivePipeline(const DrawCommandCacheData &cacheData, const char *operationName)
        {
            if (cacheData.pipeline == nullptr)
            {
                throw std::runtime_error(std::string(operationName) + " requires a render pipeline to be bound first");
            }

            return *static_cast<MRenderPipeline *>(cacheData.pipeline.get());
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
                attachment->setEndOfVertexSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationEndIndex));
                attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
                attachment->setEndOfFragmentSampleIndex(MTL::CounterDontSample);
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
                attachment->setEndOfVertexSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticEndIndex));
                attachment->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
                attachment->setEndOfFragmentSampleIndex(MTL::CounterDontSample);
            }
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
        this->mEnded = false;
        this->mNativeRenderEncoder = nullptr;
        this->mNativeRenderEncoderDesp = nullptr;
        this->mWeakCommandEncoder = commandEncoder.get();
        this->mCommandBuffer = eastl::static_pointer_cast<GVM::RHI::Metal::MCommandEncoder>(commandEncoder)->getNativeCommandEncoder();
        mArgumentAllocations.clear();
        mIndexBuffers.clear();
        mIndirectDraws.clear();

        eastl::vector<Private::RenderPassAttachmentExtent> attachments;
        attachments.reserve(descriptor.colorAttachments.size() + 1u);
        for (const auto &colorAttachment : descriptor.colorAttachments)
        {
            auto *attachmentView = static_cast<MTextureView *>(colorAttachment.view.get());
            attachments.push_back({
                attachmentView != nullptr ? attachmentView->getParentTexture() : nullptr,
                attachmentView != nullptr ? attachmentView->getWidth() : 0u,
                attachmentView != nullptr ? attachmentView->getHeight() : 0u,
                attachmentView != nullptr ? attachmentView->getDescriptor().arrayLayerCount : 0u,
            });
        }
        if (descriptor.depthStencilAttachment.view.isNull() == false)
        {
            auto *depthStencilView = static_cast<MTextureView *>(descriptor.depthStencilAttachment.view.get());
            attachments.push_back({
                depthStencilView->getParentTexture(),
                depthStencilView->getWidth(),
                depthStencilView->getHeight(),
                depthStencilView->getDescriptor().arrayLayerCount,
            });
        }
        Private::RenderPassRecorderLimits limits = {};
        limits.maxBindGroups = MetalBindGroupCount;
        limits.maxVertexBuffers = MetalMaxVertexBufferCount;
        limits.maxDrawIndirectCount = UINT32_MAX;
        limits.maxIndirectCountDrawCount = device->getMaxIndirectCountDraws();
        limits.multiDrawIndirect = true;
        limits.drawIndirectCount = true;
        mRecorder.init(descriptor, attachments, limits);

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
        mRecorder.setScissorRect(x, y, width, height);
    }

    void MRenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        ensureOpen("MRenderPassEncoder::setViewport");
        mRecorder.setViewport(x, y, width, height, minDepth, maxDepth);
    }

    void MRenderPassEncoder::drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor)
    {
        ensureOpen("MRenderPassEncoder::drawFullscreenTexture");
        mRecorder.addFullscreenTextureSource(source);
        auto *targetView = static_cast<MTextureView *>(mDescriptor.colorAttachments[0].view.get());

        setViewport(0.0f, 0.0f, static_cast<float>(attachmentWidth), static_cast<float>(attachmentHeight), 0.0f, 1.0f);
        setScissorRect(0, 0, attachmentWidth, attachmentHeight);
        mDevice->getFullscreenTextureExecutor().encode(*this, source, targetView->getParentTexture()->getFormat(), descriptor);
    }

    void MRenderPassEncoder::setPipeline(RenderPipeline pipeline)
    {
        ensureOpen("MRenderPassEncoder::setPipeline");
        Private::RenderPassPipelineFacts facts = {};
        if (pipeline != nullptr)
        {
            auto *impl = static_cast<MRenderPipeline *>(pipeline.get());
            facts.descriptor = &impl->getDescriptor();
            facts.layout = impl->getDescriptor().layout;
            auto *layout = static_cast<MPipelineLayout *>(impl->getDescriptor().layout.get());
            facts.layoutDescriptor = layout != nullptr ? &layout->getDescriptor() : nullptr;
            facts.pixelLocalInputAttachmentCount = 0u;
            facts.supportsDrawPixels = impl->isPixelLocalFramebufferFetchPipeline();
        }
        mRecorder.setPipeline(pipeline, facts);
    }

    void MRenderPassEncoder::setArguments(Arguments arguments)
    {
        ensureOpen("MRenderPassEncoder::setArguments");
        auto *snapshot = dynamic_cast<MArguments *>(arguments.get());
        if (snapshot == nullptr)
        {
            throw std::invalid_argument("Render Args snapshot does not belong to the selected pipeline layout.");
        }
        mRecorder.checkArguments(snapshot->getLayout());
        const auto allocation = static_cast<MCommandEncoder *>(mWeakCommandEncoder)->allocateArguments(eastl::move(arguments));
        const uint32_t payload = static_cast<uint32_t>(mArgumentAllocations.size());
        mRecorder.setArguments(snapshot->getRenderTextureUses(), payload);
        mArgumentAllocations.push_back(allocation);

    }

    void MRenderPassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("MRenderPassEncoder::setBindGroup");
        if (group == nullptr)
        {
            throw std::invalid_argument("MRenderPassEncoder::setBindGroup requires a valid bind group.");
        }
        auto *metalGroup = static_cast<MBindGroup *>(group.get());
        mRecorder.setBindGroup(group, groupIndex, metalGroup->getDescriptor().layout, metalGroup->getRenderTextureUses());
    }

    void MRenderPassEncoder::setVertexBuffer(BufferRange buffer, uint32_t slot)
    {
        ensureOpen("MRenderPassEncoder::setVertexBuffer");
        mRecorder.setVertexBuffer(buffer, slot);
    }

    void MRenderPassEncoder::setIndexBuffer(BufferRange buffer, IndexFormat format)
    {
        ensureOpen("MRenderPassEncoder::setIndexBuffer");
        const uint32_t alignment = Private::RenderPassRecorder::getIndexFormatSize(format);
        auto &source = resolveBufferRange(*mDevice, buffer, BufferUsage::Index, alignment);
        const uint32_t payload = static_cast<uint32_t>(mIndexBuffers.size());
        mRecorder.setIndexBuffer(buffer, format, payload);
        mIndexBuffers.push_back(NS::RetainPtr(source.getNativeBuffer()));
    }

    void MRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ensureOpen("MRenderPassEncoder::draw");
        mRecorder.draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void MRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
    {
        ensureOpen("MRenderPassEncoder::drawIndexed");
        mRecorder.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void MRenderPassEncoder::drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndirect");
        if (indirectCommandCount == 0u)
        {
            return;
        }
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        const uint32_t payload = static_cast<uint32_t>(mIndirectDraws.size());
        mRecorder.drawIndirect(indirectBuffer, indirectCommandCount, stride, false, payload);
        MIndirectDrawState entry;
        entry.nativeIndirectBuffer = NS::RetainPtr(source.getNativeBuffer());
        if (indirectCommandCount > 1u)
        {
            entry.indirectCommands = source.getIndirectCommandCache();
        }
        mIndirectDraws.push_back(eastl::move(entry));
    }

    void MRenderPassEncoder::drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndexedIndirect");
        if (indirectCommandCount == 0u)
        {
            return;
        }
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        const uint32_t payload = static_cast<uint32_t>(mIndirectDraws.size());
        mRecorder.drawIndirect(indirectBuffer, indirectCommandCount, stride, true, payload);
        MIndirectDrawState entry;
        entry.nativeIndirectBuffer = NS::RetainPtr(source.getNativeBuffer());
        entry.indirectCommands = source.getIndirectCommandCache();
        mIndirectDraws.push_back(eastl::move(entry));
    }

    /** Records explicit GPU-counted inputs independently of shader arguments. */
    void MRenderPassEncoder::drawIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndirectCount");
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        auto &counter = resolveBufferRange(*mDevice, countBuffer, BufferUsage::Indirect, 4u);
        const uint32_t payload = static_cast<uint32_t>(mIndirectDraws.size());
        if (!mRecorder.drawIndirectCount(indirectBuffer, countBuffer, maxDrawCount, stride, false, payload))
        {
            return;
        }
        MIndirectDrawState entry;
        entry.nativeIndirectBuffer = NS::RetainPtr(source.getNativeBuffer());
        entry.nativeCountBuffer = NS::RetainPtr(counter.getNativeBuffer());
        entry.indirectCommands = source.getIndirectCommandCache();
        mIndirectDraws.push_back(eastl::move(entry));
    }

    /** Records explicit GPU-counted inputs independently of shader arguments. */
    void MRenderPassEncoder::drawIndexedIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride)
    {
        ensureOpen("MRenderPassEncoder::drawIndexedIndirectCount");
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        auto &counter = resolveBufferRange(*mDevice, countBuffer, BufferUsage::Indirect, 4u);
        const uint32_t payload = static_cast<uint32_t>(mIndirectDraws.size());
        if (!mRecorder.drawIndirectCount(indirectBuffer, countBuffer, maxDrawCount, stride, true, payload))
        {
            return;
        }
        MIndirectDrawState entry;
        entry.nativeIndirectBuffer = NS::RetainPtr(source.getNativeBuffer());
        entry.nativeCountBuffer = NS::RetainPtr(counter.getNativeBuffer());
        entry.indirectCommands = source.getIndirectCommandCache();
        mIndirectDraws.push_back(eastl::move(entry));
    }

    void MRenderPassEncoder::drawPixels()
    {
        ensureOpen("MRenderPassEncoder::drawPixels");
        mRecorder.drawPixels();
    }

    void MRenderPassEncoder::nextPixelLocalPass()
    {
        ensureOpen("MRenderPassEncoder::nextPixelLocalPass");
        mRecorder.nextPixelLocalPass();
    }

    void MRenderPassEncoder::prepareIndirectCommands()
    {
        eastl::vector<MIndirectIndexedRenderCommandConvertOptions> options;
        IndirectConversionCounts conversionCounts;
        DrawCommandCacheData state{};
        for (const auto &command : mRecorder.getCommands())
        {
            if (const auto *indexBinding = eastl::get_if<Private::RenderSetIndexBuffer>(&command))
            {
                state.indexBuffer = indexBinding->buffer;
                state.indexFormat = indexBinding->format;
                state.indexBufferPayload = indexBinding->payload;
                state.hasIndexBuffer = true;
                continue;
            }
            if (const auto *pipelineBinding = eastl::get_if<Private::RenderSetPipeline>(&command))
            {
                state.pipeline = pipelineBinding->pipeline;
                continue;
            }
            if (const auto *draw = eastl::get_if<Private::RenderDrawIndirect>(&command))
            {
                prepareIndirectDraw(*draw, mIndirectDraws[draw->payload], state, conversionCounts, options);
            }
        }
        if (options.empty())
        {
            return;
        }
        MCommandEncoder::DeferredRenderPrepassScope prepassScope(
            static_cast<MCommandEncoder *>(mWeakCommandEncoder));
        mDevice->getIndirectIndexedRenderCommandConvertShaderExecutor()->execute(
            mWeakCommandEncoder, mDescriptor.label, options);
    }

    void MRenderPassEncoder::prepareIndirectDraw(const Private::RenderDrawIndirect &draw,
        MIndirectDrawState &entry, const DrawCommandCacheData &state, IndirectConversionCounts &counts,
        eastl::vector<MIndirectIndexedRenderCommandConvertOptions> &options)
    {
        const auto &pipeline = getActivePipeline(state, draw.indexed ? "drawIndexedIndirect" : "drawIndirect");
        if (!draw.indexed && draw.maxCount <= 1u && draw.counter.buffer.isNull())
        {
            return;
        }
        MIndirectIndexedRenderCommandConvertOptions conversion{
            .indirectCommandCount = draw.maxCount,
            .indirectCommandStride = draw.stride,
            .indirectRenderCommandDrawPrimitiveType =
                translatePrimitiveTypeToIndirectToken(pipeline.getPrimitiveType()),
            .commandType = draw.indexed ? MTL::IndirectCommandTypeDrawIndexed : MTL::IndirectCommandTypeDraw,
            .indirectBuffer = draw.commands,
            .nativeIndirectBuffer = entry.nativeIndirectBuffer.get(),
        };
        if (draw.indexed)
        {
            conversion.indexType = translateIndexFormatToMTL(state.indexFormat);
            conversion.indexBuffer = state.indexBuffer;
            conversion.nativeIndexBuffer = mIndexBuffers[state.indexBufferPayload].get();
        }
        if (draw.counter.buffer.isNull() == false)
        {
            conversion.nativeCountBuffer = entry.nativeCountBuffer.get();
            conversion.countAddress = conversion.nativeCountBuffer->gpuAddress() + draw.counter.offset;
        }
        entry.nativeICB = assignIndirectCommandBuffer(conversion, counts, *entry.indirectCommands);
        entry.nativeExecutionRangeBuffer = draw.counter.buffer.isNull() == false ? conversion.nativeICBArgumentBuffer : nullptr;
        options.push_back(conversion);
    }

    MTL::IndirectCommandBuffer *MRenderPassEncoder::assignIndirectCommandBuffer(
        MIndirectIndexedRenderCommandConvertOptions &options,
        IndirectConversionCounts &counts, NativeIndirectCommandBufferCache &cache)
    {
        auto &bufferCounts = counts[options.nativeIndirectBuffer];
        const uint32_t typeIndex = options.commandType == MTL::IndirectCommandTypeDraw ? 0u : 1u;
        const uint32_t slot = bufferCounts[typeIndex]++;
        const auto &allocation = getOrCreateNativeICB(*mDevice, cache,
            options.indirectCommandCount, options.commandType, *mCommandBuffer->commandQueue(), slot);
        options.nativeICB = allocation.commands.get();
        options.nativeICBArgumentBuffer = allocation.argumentBuffer.get();
        return options.nativeICB;
    }

    void MRenderPassEncoder::beginNativeRenderPass(MCommandEncoder &commandEncoder)
    {
        mNativeRenderEncoderDesp = MTL::RenderPassDescriptor::alloc()->init();
        translateRenderPassDescriptor(mDevice, mNativeRenderEncoderDesp, mDescriptor);
        configureRenderTimestampWrites(mNativeRenderEncoderDesp, mDescriptor.timestampWrites);
        configureRenderPassCounterWrites(mNativeRenderEncoderDesp, mDescriptor.counterWrites);
        if (mDescriptor.timestampWrites.querySet != nullptr)
        {
            commandEncoder.retainQuerySet(mDescriptor.timestampWrites.querySet);
        }
        if (mDescriptor.counterWrites.stageUtilizationQuerySet != nullptr)
        {
            commandEncoder.retainQuerySet(mDescriptor.counterWrites.stageUtilizationQuerySet);
        }
        if (mDescriptor.counterWrites.statisticQuerySet != nullptr)
        {
            commandEncoder.retainQuerySet(mDescriptor.counterWrites.statisticQuerySet);
        }
        mNativeRenderEncoder = mCommandBuffer->renderCommandEncoder(mNativeRenderEncoderDesp);
        if (mDescriptor.label.empty() == false)
        {
            mNativeRenderEncoder->setLabel(NS::String::string(mDescriptor.label.c_str(), NS::UTF8StringEncoding));
        }
        mNativeRenderEncoderDesp->release();
    }

    /** Prepares independent indirect results and replays the original render-state sequence. */
    void MRenderPassEncoder::end()
    {
        if (mEnded)
        {
            throw std::runtime_error("MRenderPassEncoder::end called after end().");
        }
        ensureOpen("MRenderPassEncoder::end");
        mDescriptor.pixelLocal.attachmentAccesses = mRecorder.end();
        auto &commandEncoder = *static_cast<MCommandEncoder *>(mWeakCommandEncoder);
        prepareIndirectCommands();
        beginNativeRenderPass(commandEncoder);
        try
        {
            replayCommand();
        }
        catch (const std::exception &error)
        {
            this->mNativeRenderEncoder->endEncoding();
            commandEncoder.notifyPassEnded();
            mEnded = true;
            throw std::runtime_error(
                std::string("Metal render pass '") +
                mDescriptor.label.c_str() +
                "': " +
                error.what());
        }
        this->mNativeRenderEncoder->endEncoding();
        commandEncoder.notifyPassEnded();
        mEnded = true;
    }

    MTL::RenderCommandEncoder *MRenderPassEncoder::getNativeRenderEncoder() const
    {
        return this->mNativeRenderEncoder;
    }

    void MRenderPassEncoder::replayCommand()
    {
        DrawCommandCacheData cacheData{};
        for (const auto &cmd : mRecorder.getCommands())
        {
            eastl::visit(
                [&](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Private::RenderSetScissorRect>)
                    {
                        setScissorRectInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetViewport>)
                    {
                        setViewportInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetPipeline>)
                    {
                        if (cacheData.pipeline == nullptr || cacheData.pipeline != arg.pipeline)
                        {
                            cacheData.pipeline = arg.pipeline;
                            cacheData.bindgroups.clear();
                            setPipelineInternal(arg);
                            for (const auto &binding : cacheData.vertexBuffers)
                            {
                                setVertexBufferInternal(binding.second);
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetBindGroup>)
                    {
                        const auto &it = cacheData.bindgroups.find(arg.index);
                        if (it != cacheData.bindgroups.end() && it->second != arg.group)
                        {
                            setBindGroupInternal(arg, cacheData);
                            cacheData.bindgroups[arg.index] = arg.group;
                        }
                        if (it == cacheData.bindgroups.end())
                        {
                            cacheData.bindgroups.emplace(arg.index, arg.group);
                            setBindGroupInternal(arg, cacheData);
                        }
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetArguments>)
                    {
                        const auto &allocation = mArgumentAllocations[arg.payload];
                        auto *snapshot = static_cast<MArguments *>(allocation.arguments.get());
                        snapshot->trackUsage(*mNativeRenderEncoder);
                        if (allocation.buffer != nullptr)
                        {
                            mNativeRenderEncoder->setVertexBuffer(allocation.buffer, allocation.offset, MetalArgumentRootBufferIndex);
                            mNativeRenderEncoder->setFragmentBuffer(allocation.buffer, allocation.offset, MetalArgumentRootBufferIndex);
                        }
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetVertexBuffer>)
                    {
                        cacheData.vertexBuffers[arg.slot] = arg;
                        setVertexBufferInternal(arg);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderSetIndexBuffer>)
                    {
                        cacheData.indexBuffer = arg.buffer;
                        cacheData.indexFormat = arg.format;
                        cacheData.indexBufferPayload = arg.payload;
                        cacheData.hasIndexBuffer = true;
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderDraw>)
                    {
                        drawInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderDrawIndexed>)
                    {
                        drawIndexedInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderDrawIndirect>)
                    {
                        drawIndirectInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderDrawPixels>)
                    {
                        drawPixelsInternal(arg, cacheData);
                    }
                    else if constexpr (std::is_same_v<T, Private::RenderNextPixelLocalPass>)
                    {
                        // Metal framebuffer-fetch pixel-local passes do not have a Vulkan-style subpass transition to replay here.
                        // The command only advances pixel-local access analysis before encoder creation.
                    }
                },
                cmd);
        }
    }

    void MRenderPassEncoder::setScissorRectInternal(const Private::RenderSetScissorRect &descriptor)
    {
        MTL::ScissorRect scissorRect;
        scissorRect.x = descriptor.x;
        scissorRect.y = descriptor.y;
        scissorRect.width = std::min(descriptor.width, attachmentWidth);
        scissorRect.height = std::min(descriptor.height, attachmentHeight);
        this->mNativeRenderEncoder->setScissorRect(scissorRect);
    }

    void MRenderPassEncoder::setViewportInternal(const Private::RenderSetViewport &descriptor)
    {
        MTL::Viewport viewport;
        viewport.originX = descriptor.x;
        viewport.originY = descriptor.y;
        viewport.width = std::min(descriptor.width, float(attachmentWidth));
        viewport.height = std::min(descriptor.height, float(attachmentHeight));
        viewport.znear = descriptor.minDepth;
        viewport.zfar = descriptor.maxDepth;
        this->mNativeRenderEncoder->setViewport(viewport);
    }

    void MRenderPassEncoder::setPipelineInternal(const Private::RenderSetPipeline &descriptor)
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

    void MRenderPassEncoder::setBindGroupInternal(const Private::RenderSetBindGroup &descriptor, const DrawCommandCacheData &cacheData)
    {
        auto metalBindGroup = eastl::static_pointer_cast<MBindGroup>(descriptor.group);
        const uint32_t bindingIndex = descriptor.index;
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

        const MTL::RenderStages stages = (bindVertexStage ? MTL::RenderStageVertex : 0) |
            (bindFragmentStage ? MTL::RenderStageFragment : 0);
        metalBindGroup->trackUsage(this->mNativeRenderEncoder, stages);
        if (bindVertexStage)
        {
            mNativeRenderEncoder->setVertexBuffer(metalBindGroup->getNativeBuffer(), 0, bindingIndex);
        }
        if (bindFragmentStage)
        {
            mNativeRenderEncoder->setFragmentBuffer(metalBindGroup->getNativeBuffer(), 0, bindingIndex);
        }
    }

    void MRenderPassEncoder::setVertexBufferInternal(const Private::RenderSetVertexBuffer &descriptor)
    {

        this->mNativeRenderEncoder->setVertexBuffer(static_cast<MBuffer *>(descriptor.buffer.buffer.get())->getNativeBuffer(), descriptor.buffer.offset, metalVertexBufferIndex(descriptor.slot));
    }

    void MRenderPassEncoder::drawInternal(const Private::RenderDraw &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "draw");
        this->mNativeRenderEncoder->drawPrimitives(pipeline.getPrimitiveType(), descriptor.firstVertex, descriptor.vertexCount, descriptor.instanceCount, descriptor.firstInstance);
    }

    void MRenderPassEncoder::drawIndexedInternal(const Private::RenderDrawIndexed &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, "drawIndexed");
        const uint64_t elementSize = Private::RenderPassRecorder::getIndexFormatSize(cacheData.indexFormat);
        const uint64_t indexAdditionalOffset = uint64_t(descriptor.firstIndex) * elementSize;
        const auto &range = cacheData.indexBuffer;
        this->mNativeRenderEncoder->drawIndexedPrimitives(pipeline.getPrimitiveType(), descriptor.indexCount,
            translateIndexFormatToMTL(cacheData.indexFormat), mIndexBuffers[cacheData.indexBufferPayload].get(),
            range.offset + indexAdditionalOffset, descriptor.instanceCount, descriptor.baseVertex, descriptor.firstInstance);
    }

    /** Executes the recorded indirect result or the existing native single-command path. */
    void MRenderPassEncoder::drawIndirectInternal(const Private::RenderDrawIndirect &descriptor, const DrawCommandCacheData &cacheData)
    {
        const auto &pipeline = getActivePipeline(cacheData, descriptor.indexed ? "drawIndexedIndirect" : "drawIndirect");
        const MIndirectDrawState &entry = mIndirectDraws[descriptor.payload];
        if (!descriptor.indexed && descriptor.maxCount == 1u && descriptor.counter.buffer.isNull())
        {
            this->mNativeRenderEncoder->drawPrimitives(
                pipeline.getPrimitiveType(),
                entry.nativeIndirectBuffer.get(),
                descriptor.commands.offset);
            return;
        }

        if (descriptor.counter.buffer.isNull() == false)
        {
            mNativeRenderEncoder->executeCommandsInBuffer(entry.nativeICB,
                entry.nativeExecutionRangeBuffer, NativeIndirectCommandBuffer::ExecutionRangeOffset);
        }
        else
        {
            mNativeRenderEncoder->executeCommandsInBuffer(entry.nativeICB,
                NS::Range(0, descriptor.maxCount));
        }
    }

    void MRenderPassEncoder::drawPixelsInternal(const Private::RenderDrawPixels &descriptor, const DrawCommandCacheData &cacheData)
    {
        (void)descriptor;
        (void)cacheData;
        this->mNativeRenderEncoder->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    }

    void MRenderPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mWeakCommandEncoder == nullptr || mCommandBuffer == nullptr)
        {
            throw std::runtime_error(std::string(apiName) + " called on a closed or uninitialized render pass encoder.");
        }
    }

} // namespace GVM::RHI::Metal
