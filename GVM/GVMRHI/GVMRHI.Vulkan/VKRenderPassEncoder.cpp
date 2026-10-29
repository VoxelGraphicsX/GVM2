#include "VKRenderPassEncoder.hpp"
#include "VKInstance.hpp"
#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>

#include "Private/PixelLocalPassAccess.hpp"
#include "VKBindGroup.hpp"
#include "VKBindingUtils.hpp"
#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"
#include "VKPixelLocalLayoutPolicy.hpp"
#include "VKPipelineLayout.hpp"
#include "VKRenderPipeline.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/type_traits.h>
#include <EASTL/unordered_map.h>

#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view RenderPassEncoderLogCategory = "gvmrhi.vulkan.render_pass_encoder";
    } // namespace

    namespace
    {
        [[nodiscard]]
        bool areBufferRangesEquivalent(const BufferRange &lhs, const BufferRange &rhs)
        {
            return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.size == rhs.size;
        }


        void appendAttachmentDetail(eastl::string &text, const char *kind, uint32_t index, const VKTextureView &view, LoadOp loadOp, StoreOp storeOp)
        {
            const VKTexture &texture = *view.getTexture();
            if (!text.empty())
            {
                text += ";";
            }

            text += kind;
            text += eastl::to_string(index);
            text += "{view_label=";
            text += safeLogLabel(view.getLabelName());
            text += ",texture_label=";
            text += safeLogLabel(texture.getLabelName());
            text += ",texture_ptr=";
            text += eastl::to_string(reinterpret_cast<uintptr_t>(&texture));
            text += ",image_ptr=";
            text += eastl::to_string(reinterpret_cast<uintptr_t>(static_cast<VkImage>(texture.getNativeImage())));
            text += ",image_view_ptr=";
            text += eastl::to_string(reinterpret_cast<uintptr_t>(static_cast<VkImageView>(view.getNativeImageView())));
            text += ",resolved_format=";
            text += eastl::to_string(static_cast<uint32_t>(view.getResolvedFormat()));
            text += ",aspect=";
            text += eastl::to_string(static_cast<uint32_t>(view.getDescriptor().aspect));
            text += ",base_mip=";
            text += eastl::to_string(view.getDescriptor().baseMipLevel);
            text += ",mip_count=";
            text += eastl::to_string(view.getDescriptor().mipLevelCount);
            text += ",base_layer=";
            text += eastl::to_string(view.getDescriptor().baseArrayLayer);
            text += ",layer_count=";
            text += eastl::to_string(view.getDescriptor().arrayLayerCount);
            text += ",extent=";
            text += eastl::to_string(view.getWidth());
            text += "x";
            text += eastl::to_string(view.getHeight());
            text += "x";
            text += eastl::to_string(view.getLayerCount());
            text += ",load_op=";
            text += eastl::to_string(static_cast<uint32_t>(loadOp));
            text += ",store_op=";
            text += eastl::to_string(static_cast<uint32_t>(storeOp));
            text += "}";
        }

        [[nodiscard]]
        eastl::string buildAttachmentSummary(const RenderPassDescriptor &descriptor)
        {
            eastl::string text;
            for (uint32_t colorIndex = 0; colorIndex < descriptor.colorAttachments.size(); ++colorIndex)
            {
                if (descriptor.colorAttachments[colorIndex].view.isNull())
                {
                    continue;
                }
                const auto &view = static_cast<const VKTextureView &>(*descriptor.colorAttachments[colorIndex].view.get());
                appendAttachmentDetail(text, "color", colorIndex, view, descriptor.colorAttachments[colorIndex].loadOp, descriptor.colorAttachments[colorIndex].storeOp);
            }

            if (!descriptor.depthStencilAttachment.view.isNull())
            {
                const auto &view = static_cast<const VKTextureView &>(*descriptor.depthStencilAttachment.view.get());
                appendAttachmentDetail(text, "depth", 0u, view, descriptor.depthStencilAttachment.depthLoadOp, descriptor.depthStencilAttachment.depthStoreOp);
            }

            return text.empty() ? eastl::string("<none>") : text;
        }

        void resolveAttachmentExtent(uint32_t attachmentWidth, uint32_t attachmentHeight, uint32_t attachmentLayers, AttachmentContext &context)
        {
            if (context.width == 0)
            {
                context.width = attachmentWidth;
                context.height = attachmentHeight;
                context.layers = attachmentLayers;
            }
        }

        AttachmentContext resolveAttachments(const RenderPassDescriptor &descriptor)
        {
            AttachmentContext context = {};
            context.imageViews.reserve(descriptor.colorAttachments.size() + (!descriptor.depthStencilAttachment.view.isNull() ? 1u : 0u));
            context.clearValues.reserve(context.imageViews.capacity());
            size_t clearValueCount = 0u;

            for (uint32_t colorIndex = 0; colorIndex < descriptor.colorAttachments.size(); ++colorIndex)
            {
                const RenderPassColorAttachment &attachment = descriptor.colorAttachments[colorIndex];
                auto &view = static_cast<VKTextureView &>(*attachment.view.get());
                resolveAttachmentExtent(view.getWidth(), view.getHeight(), view.getLayerCount(), context);

                context.imageViews.push_back(view.getNativeImageView());
                if (attachment.pixelLocal)
                {
                    context.pixelLocalInputImageViews.push_back(view.getNativeImageView());
                    context.pixelLocalInputColorAttachmentIndices.push_back(colorIndex);
                }
                vk::ClearColorValue clearColor = {};
                clearColor.float32[0] = static_cast<float>(attachment.clearValue.r);
                clearColor.float32[1] = static_cast<float>(attachment.clearValue.g);
                clearColor.float32[2] = static_cast<float>(attachment.clearValue.b);
                clearColor.float32[3] = static_cast<float>(attachment.clearValue.a);
                context.clearValues.push_back(clearColor);
                if (attachment.loadOp == LoadOp::Clear)
                {
                    clearValueCount = context.clearValues.size();
                }
            }

            if (!descriptor.depthStencilAttachment.view.isNull())
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.depthStencilAttachment.view.get());
                resolveAttachmentExtent(view.getWidth(), view.getHeight(), view.getLayerCount(), context);

                context.imageViews.push_back(view.getNativeImageView());
                context.clearValues.push_back(vk::ClearDepthStencilValue{descriptor.depthStencilAttachment.depthClearValue, 0u});
                if (descriptor.depthStencilAttachment.depthLoadOp == LoadOp::Clear)
                {
                    clearValueCount = context.clearValues.size();
                }
            }

            context.clearValues.resize(clearValueCount);
            return context;
        }

        uint32_t resolveIndirectStride(uint32_t stride)
        {
            return stride == 0u ? sizeof(IndirectRenderCommand) : stride;
        }

        uint32_t resolveIndexedIndirectStride(uint32_t stride)
        {
            return stride == 0u ? sizeof(IndirectIndexedRenderCommand) : stride;
        }

        /// Reports whether an attachment view must be synchronized by the external task dependency resolver before render-pass begin.
        bool shouldSynchronizeAttachmentBeforeRenderPass(const VKTextureView &view)
        {
            return view.getTexture()->getDescriptor().storageMode != TextureStorageMode::TransientAttachment;
        }

        void bindRenderPassDescriptorBindingIfNeeded(vk::CommandBuffer commandBuffer, const RenderPipeline &pipelineHandle, const PipelineLayout &pipelineLayoutHandle, vk::PipelineLayout nativePipelineLayout, uint32_t groupIndex, const DescriptorBindingState &desiredState, DescriptorBindingTable &nativeBoundDescriptorBindings)
        {
            if (pipelineHandle == nullptr || pipelineLayoutHandle == nullptr)
            {
                return;
            }

            const auto &pipeline = static_cast<const VKRenderPipeline &>(*pipelineHandle);
            const auto &pipelineLayout = static_cast<const VKPipelineLayout &>(*pipelineLayoutHandle);
            if (!pipeline.usesPixelLocalInputAttachments())
            {
                bindDescriptorBindingStateIfNeeded(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, &pipelineLayout, groupIndex, desiredState, nativeBoundDescriptorBindings);
                return;
            }

            if (!isDescriptorBindingStateCompatibleWithPipelineLayout(desiredState, pipelineLayout, groupIndex))
            {
                clearDescriptorBinding(nativeBoundDescriptorBindings, groupIndex);
                return;
            }

            const DescriptorBindingState *bound = getDescriptorBinding(nativeBoundDescriptorBindings, groupIndex);
            if (bound != nullptr && areDescriptorBindingStatesEquivalent(*bound, desiredState))
            {
                return;
            }

            if (desiredState.descriptorSetOverride)
            {
                bindDescriptorSet(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, desiredState.descriptorSet, groupIndex);
            }
            else
            {
                bindDescriptorSet(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, static_cast<const VKBindGroup &>(*desiredState.group), groupIndex);
            }

            setDescriptorBinding(nativeBoundDescriptorBindings, groupIndex, desiredState);
        }

        /// Resolves the active pass access used to bind pixel-local input descriptors.
        PixelLocalPassAttachmentAccess resolvePixelLocalAttachmentAccess(const RenderPassDescriptor &descriptor, uint32_t currentPixelLocalPassIndex)
        {
            return descriptor.pixelLocal.attachmentAccesses[currentPixelLocalPassIndex];
        }

        /// Resolves a shader input-attachment slot to the matching framebuffer attachment context index.
        uint32_t resolvePixelLocalInputAttachmentContextIndex(const AttachmentContext &attachments, const PixelLocalPassAttachmentAccess &access, uint32_t inputAttachmentIndex)
        {
            uint32_t compactInputIndex = 0u;
            for (uint32_t attachmentContextIndex = 0u; attachmentContextIndex < attachments.pixelLocalInputColorAttachmentIndices.size(); ++attachmentContextIndex)
            {
                const uint32_t colorAttachmentIndex = attachments.pixelLocalInputColorAttachmentIndices[attachmentContextIndex];
                if (!GVM::RHI::Private::isPixelLocalColorAttachmentRead(access, colorAttachmentIndex))
                {
                    continue;
                }

                if (compactInputIndex == inputAttachmentIndex)
                {
                    return attachmentContextIndex;
                }
                ++compactInputIndex;
            }

            throw std::logic_error("VKRenderPassEncoder pixelLocal input attachment index exceeds the current pixelLocal pass input range.");
        }

        /// Resolves the descriptor image layout for a framebuffer attachment used by the current pixel-local pass.
        vk::ImageLayout resolvePixelLocalInputDescriptorLayout(const AttachmentContext &attachments, uint32_t attachmentContextIndex)
        {
            if (attachmentContextIndex >= attachments.pixelLocalInputColorAttachmentIndices.size())
            {
                throw std::logic_error("VKRenderPassEncoder pixelLocal input attachment index does not map to a color attachment.");
            }

            return resolvePixelLocalColorInputLayout();
        }

        /// Reports whether a cached pixel-local descriptor set exactly matches the current pass inputs.
        bool matchesPixelLocalInputDescriptorCacheEntry(const PixelLocalInputDescriptorCacheEntry &entry, vk::RenderPass renderPass, const RenderPipeline &pipelineHandle, const VKRenderPipeline &pipeline, const AttachmentContext &attachments, uint32_t currentPixelLocalPassIndex)
        {
            return entry.renderPass == renderPass && entry.descriptorSetLayout == pipeline.getPixelLocalInputDescriptorSetLayout() && entry.pixelLocalPassIndex == currentPixelLocalPassIndex && entry.pipeline == pipelineHandle && entry.framebufferImageViews == attachments.imageViews;
        }

        /// Finds a cached pixel-local input descriptor set for the current render pass and pipeline.
        vk::DescriptorSet findCachedPixelLocalInputDescriptorSet(const eastl::vector<PixelLocalInputDescriptorCacheEntry> &cache, vk::RenderPass renderPass, const RenderPipeline &pipelineHandle, const VKRenderPipeline &pipeline, const AttachmentContext &attachments, uint32_t currentPixelLocalPassIndex)
        {
            for (const PixelLocalInputDescriptorCacheEntry &entry : cache)
            {
                if (matchesPixelLocalInputDescriptorCacheEntry(entry, renderPass, pipelineHandle, pipeline, attachments, currentPixelLocalPassIndex))
                {
                    return entry.descriptorSet;
                }
            }
            return nullptr;
        }

        /// Adds a pixel-local input descriptor set to the per-encoder transient descriptor cache.
        void appendPixelLocalInputDescriptorCacheEntry(eastl::vector<PixelLocalInputDescriptorCacheEntry> &cache, vk::RenderPass renderPass, const RenderPipeline &pipelineHandle, const VKRenderPipeline &pipeline, const AttachmentContext &attachments, uint32_t currentPixelLocalPassIndex, vk::DescriptorSet descriptorSet)
        {
            PixelLocalInputDescriptorCacheEntry entry;
            entry.renderPass = renderPass;
            entry.descriptorSetLayout = pipeline.getPixelLocalInputDescriptorSetLayout();
            entry.pixelLocalPassIndex = currentPixelLocalPassIndex;
            entry.pipeline = pipelineHandle;
            entry.framebufferImageViews = attachments.imageViews;
            entry.descriptorSet = descriptorSet;
            cache.push_back(eastl::move(entry));
        }

        /// Binds transient Vulkan input-attachment descriptors required by a pixel-local pipeline.
        void bindPixelLocalInputDescriptors(VKDevice &device, VKCommandEncoder &commandEncoder, vk::CommandBuffer commandBuffer, vk::RenderPass renderPass, vk::PipelineLayout nativePipelineLayout, const RenderPipeline &pipelineHandle, const VKRenderPipeline &pipeline, const AttachmentContext &attachments, const RenderPassDescriptor &descriptor, uint32_t currentPixelLocalPassIndex, eastl::vector<PixelLocalInputDescriptorCacheEntry> &descriptorCache)
        {
            if (!pipeline.usesPixelLocalInputAttachments())
            {
                return;
            }

            const eastl::vector<VKRenderPipeline::PixelLocalInputBinding> &inputBindings = pipeline.getPixelLocalInputBindings();
            const vk::DescriptorSet cachedDescriptorSet = findCachedPixelLocalInputDescriptorSet(descriptorCache, renderPass, pipelineHandle, pipeline, attachments, currentPixelLocalPassIndex);
            if (cachedDescriptorSet != vk::DescriptorSet{})
            {
                bindDescriptorSet(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, cachedDescriptorSet, pipeline.getPixelLocalInputDescriptorSetIndex());
                return;
            }

            const VKTransientDescriptorAllocator::AllocationResult allocation = commandEncoder.getTransientDescriptorAllocator().allocate(pipeline.getPixelLocalInputDescriptorSetLayout(), pipeline.getPixelLocalInputDescriptorPoolSizes());
            const PixelLocalPassAttachmentAccess access = resolvePixelLocalAttachmentAccess(descriptor, currentPixelLocalPassIndex);

            eastl::vector<vk::DescriptorImageInfo> imageInfos;
            imageInfos.resize(inputBindings.size());
            eastl::vector<vk::WriteDescriptorSet> writes;
            writes.reserve(inputBindings.size());

            for (uint32_t index = 0; index < inputBindings.size(); ++index)
            {
                const VKRenderPipeline::PixelLocalInputBinding &binding = inputBindings[index];
                const uint32_t attachmentContextIndex = resolvePixelLocalInputAttachmentContextIndex(
                    attachments,
                    access,
                    binding.inputAttachmentIndex);

                imageInfos[index] = vk::DescriptorImageInfo{
                    nullptr,
                    attachments.pixelLocalInputImageViews[attachmentContextIndex],
                    resolvePixelLocalInputDescriptorLayout(attachments, attachmentContextIndex),
                };
                vk::WriteDescriptorSet write = {};
                write.dstSet = allocation.descriptorSet;
                write.dstBinding = binding.binding;
                write.descriptorCount = 1u;
                write.descriptorType = vk::DescriptorType::eInputAttachment;
                write.setImageInfo(imageInfos[index]);
                writes.push_back(write);
            }

            device.getNativeDevice().updateDescriptorSets(writes, {});
            bindDescriptorSet(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, allocation.descriptorSet, pipeline.getPixelLocalInputDescriptorSetIndex());
            appendPixelLocalInputDescriptorCacheEntry(descriptorCache, renderPass, pipelineHandle, pipeline, attachments, currentPixelLocalPassIndex, allocation.descriptorSet);
        }

        [[nodiscard]]
        RenderPassSetup prepareRenderPassSetup(VKDevice &device, VKCommandEncoder &commandEncoder, vk::CommandBuffer commandBuffer, VKTaskDependencyResolver &stateTracker, const RenderPassDescriptor &descriptor)
        {
            RenderPassSetup setup = {};
            setup.attachments = resolveAttachments(descriptor);
            for (size_t colorIndex = 0; colorIndex < descriptor.colorAttachments.size(); ++colorIndex)
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.colorAttachments[colorIndex].view.get());
                if (!shouldSynchronizeAttachmentBeforeRenderPass(view))
                {
                    continue;
                }
                stateTracker.synchronizeTextureSubresources(commandBuffer, *view.getTexture(), vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite, view.getDescriptor().aspect, view.getDescriptor().baseMipLevel, view.getDescriptor().mipLevelCount, view.getDescriptor().baseArrayLayer, view.getDescriptor().arrayLayerCount);
            }

            if (!descriptor.depthStencilAttachment.view.isNull())
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.depthStencilAttachment.view.get());
                if (shouldSynchronizeAttachmentBeforeRenderPass(view))
                {
                    stateTracker.synchronizeTextureSubresources(
                        commandBuffer, *view.getTexture(), vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite, view.getDescriptor().aspect, view.getDescriptor().baseMipLevel, view.getDescriptor().mipLevelCount, view.getDescriptor().baseArrayLayer, view.getDescriptor().arrayLayerCount);
                }
            }

            setup.renderPass = device.getRenderPassFramebufferCache().getOrCreateRenderPass(descriptor);
            setup.framebuffer = device.getRenderPassFramebufferCache().getOrCreateFramebuffer(setup.renderPass, setup.attachments.imageViews, setup.attachments.width, setup.attachments.height, setup.attachments.layers);
            commandEncoder.retainNativeResource(eastl::static_pointer_cast<void>(setup.framebuffer.owner));
            return setup;
        }

    } // namespace

    void VKRenderPassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
        {
            throw std::invalid_argument("VKRenderPassEncoder::init requires a valid device and command encoder.");
        }

        mDevice = device;
        mRecordingOwner = commandEncoder;
        mCommandEncoder = static_cast<VKCommandEncoder *>(commandEncoder.get());
        mDescriptor = descriptor;
        mLabelName = descriptor.label;

        eastl::vector<Private::RenderPassAttachmentExtent> attachments;
        attachments.reserve(descriptor.colorAttachments.size() + 1u);
        for (const auto &attachment : descriptor.colorAttachments)
        {
            auto *view = static_cast<VKTextureView *>(attachment.view.get());
            attachments.push_back({
                view != nullptr ? static_cast<const TextureImpl *>(view->getTexture()) : nullptr,
                view != nullptr ? view->getWidth() : 0u,
                view != nullptr ? view->getHeight() : 0u,
                view != nullptr ? view->getLayerCount() : 0u,
            });
        }
        if (!descriptor.depthStencilAttachment.view.isNull())
        {
            auto *view = static_cast<VKTextureView *>(descriptor.depthStencilAttachment.view.get());
            attachments.push_back({view->getTexture(), view->getWidth(), view->getHeight(), view->getLayerCount()});
        }
        Private::RenderPassRecorderLimits limits = {};
        limits.maxBindGroups = VKMaxBindGroupCount;
        limits.maxVertexBuffers = device->getCapabilities().getPhysicalDeviceProperties().limits.maxVertexInputBindings;
        limits.maxDrawIndirectCount = device->getCapabilities().getPhysicalDeviceProperties().limits.maxDrawIndirectCount;
        limits.maxIndirectCountDrawCount = device->getCapabilities().getPhysicalDeviceProperties().limits.maxDrawIndirectCount;
        limits.multiDrawIndirect = device->getCapabilities().getEnabledShaderFeatures().multiDrawIndirect != 0u;
        limits.drawIndirectCount = device->getCapabilities().supportsDrawIndirectCount();
        mRecorder.init(descriptor, attachments, limits);

        mCommandEncoder->writePassTimestamp(descriptor.timestampWrites, true, vk::PipelineStageFlagBits2::eTopOfPipe);
        // Retain attachments before the user can retire them between end and submit.
        for (const auto &attachment : descriptor.colorAttachments)
        {
            if (!attachment.view.isNull())
            {
                mCommandEncoder->retainTexture(static_cast<VKTextureView *>(attachment.view.get())->getTexture()->getHandle());
            }
        }
        if (!descriptor.depthStencilAttachment.view.isNull())
        {
            mCommandEncoder->retainTexture(static_cast<VKTextureView *>(descriptor.depthStencilAttachment.view.get())->getTexture()->getHandle());
        }
    }

    void VKRenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        ensureOpen("VKRenderPassEncoder::setScissorRect");
        mRecorder.setScissorRect(x, y, width, height);
    }

    void VKRenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        ensureOpen("VKRenderPassEncoder::setViewport");
        mRecorder.setViewport(x, y, width, height, minDepth, maxDepth);
    }

    void VKRenderPassEncoder::drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor)
    {
        ensureOpen("VKRenderPassEncoder::drawFullscreenTexture");
        mRecorder.addFullscreenTextureSource(source);
        auto *targetView = static_cast<VKTextureView *>(mDescriptor.colorAttachments[0].view.get());

        setViewport(0.0f, 0.0f, static_cast<float>(targetView->getWidth()), static_cast<float>(targetView->getHeight()), 0.0f, 1.0f);
        setScissorRect(0, 0, targetView->getWidth(), targetView->getHeight());
        // RenderToSwapchain is a fixed fullscreen sample path. Queue an
        // explicit sampled-texture transition so the source image is legalized
        // before the render pass begins, then let bind-group preparation handle
        // descriptor binding as usual.
        enqueueExplicitSampledTexture(source);
        mCommandEncoder->retainTexture(source);
        mDevice->getFullscreenTextureExecutor().encode(*this, source, targetView->getResolvedFormat(), descriptor);
    }

    void VKRenderPassEncoder::setPipeline(RenderPipeline pipeline)
    {
        ensureOpen("VKRenderPassEncoder::setPipeline");
        Private::RenderPassPipelineFacts facts = {};
        if (pipeline != nullptr)
        {
            auto *impl = static_cast<VKRenderPipeline *>(pipeline.get());
            facts.descriptor = &impl->getDescriptor();
            facts.layout = impl->getPipelineLayoutHandle();
            auto *pipelineLayout = static_cast<VKPipelineLayout *>(impl->getPipelineLayoutHandle().get());
            facts.layoutDescriptor = pipelineLayout != nullptr ? &pipelineLayout->getDescriptor() : nullptr;
            for (const auto &binding : impl->getPixelLocalInputBindings())
            {
                facts.pixelLocalInputAttachmentCount = eastl::max(facts.pixelLocalInputAttachmentCount, binding.inputAttachmentIndex + 1u);
            }
            facts.supportsDrawPixels = true;
        }
        mRecorder.setPipeline(pipeline, facts);
        auto *selected = static_cast<VKRenderPipeline *>(pipeline.get());
        if (mRecordingPipeline != selected)
        {
            mCurrentArguments = nullptr;
        }
        mRecordingPipeline = selected;
        mCommandEncoder->retainRenderPipeline(pipeline);
    }

    void VKRenderPassEncoder::setArguments(Arguments arguments)
    {
        ensureOpen("VKRenderPassEncoder::setArguments");
        auto *snapshot = dynamic_cast<VKArguments *>(arguments.get());
        if (snapshot == nullptr || &snapshot->getDevice() != mDevice)
        {
            throw std::invalid_argument("Render Args snapshot does not belong to the selected pipeline layout.");
        }
        mRecorder.checkArguments(snapshot->getLayout());
        const auto allocation = mCommandEncoder->allocateArguments(eastl::move(arguments));
        const uint32_t payload = static_cast<uint32_t>(mArgumentAllocations.size());
        mRecorder.setArguments(snapshot->getRenderTextureUses(), payload);
        mArgumentAllocations.push_back(allocation);
        const auto &bufferUses = snapshot->getBufferUses();
        mPendingBufferPreparations.insert(mPendingBufferPreparations.end(), bufferUses.begin(), bufferUses.end());
        mCurrentArguments = snapshot;

    }

    void VKRenderPassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("VKRenderPassEncoder::setBindGroup");
        if (group == nullptr)
        {
            throw std::invalid_argument("VKRenderPassEncoder::setBindGroup requires a valid bind group.");
        }
        auto *vkGroup = static_cast<VKBindGroup *>(group.get());
        mRecorder.setBindGroup(group, groupIndex, vkGroup->getLayoutHandle(), vkGroup->getRenderTextureUses());
        mCommandEncoder->retainBindGroup(group);
        enqueueBindGroupPreparation(group);
    }

    void VKRenderPassEncoder::setVertexBuffer(BufferRange buffer, uint32_t slot)
    {
        ensureOpen("VKRenderPassEncoder::setVertexBuffer");
        mRecorder.setVertexBuffer(buffer, slot);
        mCommandEncoder->retainBuffer(buffer.buffer);
        mPendingBufferPreparations.push_back({static_cast<VKBuffer *>(buffer.buffer.get()), buffer.offset, buffer.size, vk::PipelineStageFlagBits2::eVertexInput, vk::AccessFlagBits2::eVertexAttributeRead});
    }

    void VKRenderPassEncoder::setIndexBuffer(BufferRange buffer, IndexFormat format)
    {
        ensureOpen("VKRenderPassEncoder::setIndexBuffer");
        const uint32_t alignment = Private::RenderPassRecorder::getIndexFormatSize(format);
        auto &source = resolveBufferRange(*mDevice, buffer, BufferUsage::Index, alignment);
        mRecorder.setIndexBuffer(buffer, format, 0u);
        mPendingBufferPreparations.push_back({&source, buffer.offset, buffer.size,
            vk::PipelineStageFlagBits2::eIndexInput, vk::AccessFlagBits2::eIndexRead});
        mCommandEncoder->retainBuffer(buffer.buffer);
    }

    void VKRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ensureOpen("VKRenderPassEncoder::draw");
        mRecorder.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        enqueueArgumentTextures();
    }

    void VKRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
    {
        ensureOpen("VKRenderPassEncoder::drawIndexed");
        mRecorder.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        enqueueArgumentTextures();
    }

    void VKRenderPassEncoder::drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndirect");
        if (indirectCommandCount == 0u)
        {
            return;
        }
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        mRecorder.drawIndirect(indirectBuffer, indirectCommandCount, stride, false, 0u);
        mPendingBufferPreparations.push_back({&source, indirectBuffer.offset, indirectBuffer.size,
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        enqueueArgumentTextures();
        mCommandEncoder->retainBuffer(indirectBuffer.buffer);
    }

    void VKRenderPassEncoder::drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndexedIndirect");
        if (indirectCommandCount == 0u)
        {
            return;
        }
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        mRecorder.drawIndirect(indirectBuffer, indirectCommandCount, stride, true, 0u);
        mPendingBufferPreparations.push_back({&source, indirectBuffer.offset, indirectBuffer.size,
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        enqueueArgumentTextures();
        mCommandEncoder->retainBuffer(indirectBuffer.buffer);
    }

    /** Records explicit GPU-counted inputs independently of shader arguments. */
    void VKRenderPassEncoder::drawIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndirectCount");
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        auto &counter = resolveBufferRange(*mDevice, countBuffer, BufferUsage::Indirect, 4u);
        if (!mRecorder.drawIndirectCount(indirectBuffer, countBuffer, maxDrawCount, stride, false, 0u))
        {
            return;
        }
        mPendingBufferPreparations.push_back({&source, indirectBuffer.offset, indirectBuffer.size,
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        mPendingBufferPreparations.push_back({&counter, countBuffer.offset, sizeof(uint32_t),
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        enqueueArgumentTextures();
        mCommandEncoder->retainBuffer(indirectBuffer.buffer);
        if (indirectBuffer.buffer != countBuffer.buffer)
        {
            mCommandEncoder->retainBuffer(countBuffer.buffer);
        }
    }

    /** Records explicit GPU-counted inputs independently of shader arguments. */
    void VKRenderPassEncoder::drawIndexedIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndexedIndirectCount");
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        auto &counter = resolveBufferRange(*mDevice, countBuffer, BufferUsage::Indirect, 4u);
        if (!mRecorder.drawIndirectCount(indirectBuffer, countBuffer, maxDrawCount, stride, true, 0u))
        {
            return;
        }
        mPendingBufferPreparations.push_back({&source, indirectBuffer.offset, indirectBuffer.size,
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        mPendingBufferPreparations.push_back({&counter, countBuffer.offset, sizeof(uint32_t),
            vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead});
        enqueueArgumentTextures();
        mCommandEncoder->retainBuffer(indirectBuffer.buffer);
        if (indirectBuffer.buffer != countBuffer.buffer)
        {
            mCommandEncoder->retainBuffer(countBuffer.buffer);
        }
    }

    void VKRenderPassEncoder::drawPixels()
    {
        ensureOpen("VKRenderPassEncoder::drawPixels");
        mRecorder.drawPixels();
        enqueueArgumentTextures();
    }

    void VKRenderPassEncoder::nextPixelLocalPass()
    {
        ensureOpen("VKRenderPassEncoder::nextPixelLocalPass");
        mRecorder.nextPixelLocalPass();
    }

    void VKRenderPassEncoder::end()
    {
        if (mEnded)
        {
            throw std::logic_error("VKRenderPassEncoder::end called after end().");
        }
        ensureOpen("VKRenderPassEncoder::end");
        mDescriptor.pixelLocal.attachmentAccesses = mRecorder.end();

        mCommandEncoder->appendPass(RenderPassEncoder(this));
        mCommandEncoder->writePassTimestamp(mDescriptor.timestampWrites, false, vk::PipelineStageFlagBits2::eBottomOfPipe);
        mCommandEncoder->notifyPassEnded();
        mEnded = true;
        mRecordingOwner = nullptr;
    }

    void VKRenderPassEncoder::encode(VKCommandEncoder &commandEncoder)
    {
        vk::CommandBuffer commandBuffer = commandEncoder.getNativeCommandBuffer();
        auto &stateTracker = commandEncoder.getTaskDependencyResolver();
        const RenderPassSetup renderPassSetup = prepareRenderPassSetup(*mDevice, commandEncoder, commandBuffer, stateTracker, mDescriptor);

        ReplayState state = {};
        VKRenderPassReplayDiagnostics diagnostics = {};
        diagnostics.begin(mDevice->getLogger());
        eastl::vector<PixelLocalInputDescriptorCacheEntry> pixelLocalInputDescriptorCache;
        ReplayContext context = {commandBuffer, &commandEncoder, &renderPassSetup, &state, &pixelLocalInputDescriptorCache, &diagnostics};
        beginReplay(context);

        for (const Private::RenderCommand &command : mRecorder.getCommands())
        {
            eastl::visit(
                [&](const auto &typedCommand) {
                    using T = eastl::decay_t<decltype(typedCommand)>;
                    if constexpr (eastl::is_same_v<T, Private::RenderSetScissorRect>)
                    {
                        replaySetScissorRect(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetViewport>)
                    {
                        replaySetViewport(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetPipeline>)
                    {
                        replaySetPipeline(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetArguments>)
                    {
                        replaySetArguments(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetBindGroup>)
                    {
                        replaySetBindGroup(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetVertexBuffer>)
                    {
                        replaySetVertexBuffer(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderSetIndexBuffer>)
                    {
                        replaySetIndexBuffer(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderDraw>)
                    {
                        diagnostics.recordDraw();
                        commandBuffer.draw(typedCommand.vertexCount, typedCommand.instanceCount, typedCommand.firstVertex, typedCommand.firstInstance);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderDrawIndexed>)
                    {
                        diagnostics.recordDrawIndexed();
                        commandBuffer.drawIndexed(typedCommand.indexCount, typedCommand.instanceCount, typedCommand.firstIndex, typedCommand.baseVertex, typedCommand.firstInstance);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderDrawIndirect>)
                    {
                        replayDrawIndirect(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderDrawPixels>)
                    {
                        replayDrawPixels(context, typedCommand);
                    }
                    else if constexpr (eastl::is_same_v<T, Private::RenderNextPixelLocalPass>)
                    {
                        replayNextPixelLocalPass(context, typedCommand);
                    }
                },
                command);
        }

        endReplay(context);
    }

    void VKRenderPassEncoder::beginReplay(ReplayContext &context)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        const char *passLabel = mLabelName.empty() ? "RenderPass" : mLabelName.c_str();
        if (mDevice->getInstance()->supportsDebugUtils())
        {
            VkDebugUtilsLabelEXT label = {};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = passLabel;
            vkCmdBeginDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(commandBuffer), &label);
        }
        auto &state = *context.state;
        const RenderPassSetup &setup = *context.setup;
        VKCommandEncoder &commandEncoder = *context.commandEncoder;
        auto &stateTracker = commandEncoder.getTaskDependencyResolver();

        for (const auto &[_, sampledTextureHandle] : mExplicitSampledTextures)
        {
            if (sampledTextureHandle.isNull())
            {
                continue;
            }
            auto *sampledTexture = static_cast<VKTexture *>(sampledTextureHandle.get());
            stateTracker.synchronizeTextureSubresources(commandBuffer, *sampledTexture, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        }

        {
            eastl::vector<const VKBindGroup *> bindGroupsToPrepare;
            bindGroupsToPrepare.reserve(mPendingBindGroupPreparations.size());
            for (const auto &[bindGroup, _] : mPendingBindGroupPreparations)
            {
                if (bindGroup == nullptr)
                {
                    continue;
                }
                bindGroupsToPrepare.push_back(bindGroup);
            }

            prepareBindGroupBindings(commandBuffer, stateTracker, bindGroupsToPrepare, vk::PipelineStageFlagBits2::eAllGraphics,
                nullptr, mPendingArgumentTextures, mPendingBufferPreparations);
        }

        GVMLogTrace(mDevice,
                    RenderPassEncoderLogCategory,
                    "event=render_pass_record_begin command_encoder_ptr={} command_buffer_ptr={} pass_label={} render_pass_ptr={} framebuffer_ptr={} framebuffer_owner_ptr={} color_attachment_count={} has_depth_attachment={} attachments={}",
                    static_cast<void *>(&commandEncoder),
                    reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
                    safeLogLabel(mLabelName),
                    reinterpret_cast<void *>(static_cast<VkRenderPass>(setup.renderPass)),
                    reinterpret_cast<void *>(static_cast<VkFramebuffer>(setup.framebuffer.framebuffer)),
                    static_cast<void *>(setup.framebuffer.owner.get()),
                    mDescriptor.colorAttachments.size(),
                    !mDescriptor.depthStencilAttachment.view.isNull(),
                    buildAttachmentSummary(mDescriptor));

        vk::RenderPassBeginInfo beginInfo = {};
        beginInfo.renderPass = setup.renderPass;
        beginInfo.framebuffer = setup.framebuffer.framebuffer;
        beginInfo.renderArea.offset = vk::Offset2D{0, 0};
        beginInfo.renderArea.extent = vk::Extent2D{setup.attachments.width, setup.attachments.height};
        beginInfo.clearValueCount = static_cast<uint32_t>(setup.attachments.clearValues.size());
        beginInfo.pClearValues = setup.attachments.clearValues.data();

        commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
        commandBuffer.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(setup.attachments.width), static_cast<float>(setup.attachments.height), 0.0f, 1.0f});
        commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{setup.attachments.width, setup.attachments.height}});

        state.hasViewport = true;
        state.viewport = Private::RenderSetViewport{
            0.0f,
            0.0f,
            static_cast<float>(setup.attachments.width),
            static_cast<float>(setup.attachments.height),
            0.0f,
            1.0f,
        };
        state.hasScissor = true;
        state.scissor = Private::RenderSetScissorRect{
            0u,
            0u,
            setup.attachments.width,
            setup.attachments.height,
        };
    }

    void VKRenderPassEncoder::endReplay(ReplayContext &context)
    {
        context.commandBuffer.endRenderPass();
        if (mDevice->getInstance()->supportsDebugUtils())
        {
            vkCmdEndDebugUtilsLabelEXT(static_cast<VkCommandBuffer>(context.commandBuffer));
        }
        context.diagnostics->emitEndOfReplayLog(
            mDevice,
            static_cast<void *>(context.commandEncoder),
            context.commandBuffer,
            mLabelName,
            context.setup->renderPass,
            context.setup->framebuffer.framebuffer);
        context.commandEncoder->notePassSummary();
    }

    void VKRenderPassEncoder::enqueueBindGroupPreparation(BindGroup group)
    {
        if (group == nullptr)
        {
            return;
        }

        auto *bindGroup = static_cast<VKBindGroup *>(group.get());
        mPendingBindGroupPreparations[bindGroup] = group;
    }

    void VKRenderPassEncoder::enqueueArgumentTextures()
    {
        if (mCurrentArguments == nullptr)
        {
            return;
        }
        for (const auto &use : mCurrentArguments->getTextureUses())
        {
            auto &indices = mArgumentTextureIndices[use.view->getTexture()];
            bool merged = false;
            for (const auto index : indices)
            {
                auto &pending = mPendingArgumentTextures[index];
                if (pending.view == use.view && pending.layout == use.layout)
                {
                    pending.stages |= use.stages;
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                indices.push_back(mPendingArgumentTextures.size());
                mPendingArgumentTextures.push_back(use);
            }
        }
    }

    void VKRenderPassEncoder::enqueueExplicitSampledTexture(Texture texture)
    {
        if (texture.isNull())
        {
            return;
        }

        auto *vkTexture = static_cast<VKTexture *>(texture.get());
        mExplicitSampledTextures[vkTexture] = texture;
    }

    void VKRenderPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on an ended Vulkan render pass encoder.").c_str());
        }
    }

    void VKRenderPassEncoder::replaySetScissorRect(ReplayContext &context, const Private::RenderSetScissorRect &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        if (state.hasScissor && state.scissor.x == typedCommand.x && state.scissor.y == typedCommand.y && state.scissor.width == typedCommand.width && state.scissor.height == typedCommand.height)
        {
            return;
        }

        commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{static_cast<int32_t>(typedCommand.x), static_cast<int32_t>(typedCommand.y)}, vk::Extent2D{typedCommand.width, typedCommand.height}});
        state.scissor = Private::RenderSetScissorRect{
            typedCommand.x,
            typedCommand.y,
            typedCommand.width,
            typedCommand.height,
        };
        state.hasScissor = true;
    }

    void VKRenderPassEncoder::replaySetViewport(ReplayContext &context, const Private::RenderSetViewport &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        if (state.hasViewport && state.viewport.x == typedCommand.x && state.viewport.y == typedCommand.y && state.viewport.width == typedCommand.width && state.viewport.height == typedCommand.height && state.viewport.minDepth == typedCommand.minDepth && state.viewport.maxDepth == typedCommand.maxDepth)
        {
            return;
        }

        commandBuffer.setViewport(0, vk::Viewport{typedCommand.x, typedCommand.y, typedCommand.width, typedCommand.height, typedCommand.minDepth, typedCommand.maxDepth});
        state.viewport = Private::RenderSetViewport{
            typedCommand.x,
            typedCommand.y,
            typedCommand.width,
            typedCommand.height,
            typedCommand.minDepth,
            typedCommand.maxDepth,
        };
        state.hasViewport = true;
    }

    void VKRenderPassEncoder::replaySetPipeline(ReplayContext &context, const Private::RenderSetPipeline &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        const RenderPassSetup &setup = *context.setup;
        if (state.pipeline == typedCommand.pipeline)
        {
            return;
        }

        auto &pipeline = static_cast<VKRenderPipeline &>(*typedCommand.pipeline);
        const vk::PipelineLayout previousPipelineLayout = state.nativePipelineLayout;
        state.pipeline = typedCommand.pipeline;
        state.pipelineLayout = pipeline.getPipelineLayoutHandle();
        state.nativePipelineLayout = pipeline.getNativePipelineLayout();
        context.diagnostics->recordPipelineSwitch(pipeline.getLabelName());
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getNativePipelineForRenderPassSubpass(setup.renderPass, state.currentPixelLocalPassIndex));
        bindPixelLocalInputDescriptors(*mDevice, *context.commandEncoder, commandBuffer, setup.renderPass, state.nativePipelineLayout, typedCommand.pipeline, pipeline, setup.attachments, mDescriptor, state.currentPixelLocalPassIndex, *context.pixelLocalInputDescriptorCache);

        if (previousPipelineLayout != state.nativePipelineLayout)
        {
            state.nativeBoundDescriptorBindings.boundMask = 0u;
            for (uint32_t groupIndex = 0; groupIndex < VKMaxBindGroupCount; ++groupIndex)
            {
                const DescriptorBindingState *desiredState = getDescriptorBinding(state.descriptorBindings, groupIndex);
                if (desiredState != nullptr)
                {
                    bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, groupIndex, *desiredState, state.nativeBoundDescriptorBindings);
                }
            }
        }
    }

    void VKRenderPassEncoder::replaySetArguments(ReplayContext &context, const Private::RenderSetArguments &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        const VKArgumentAllocation &allocation = mArgumentAllocations[typedCommand.payload];
        const auto table = allocation.arguments->getResourceTable();
        if (table)
        {
            const DescriptorBindingState desired{nullptr, nullptr, table, true};
            bindCompatibleDescriptorBindingStateIfNeeded(commandBuffer, vk::PipelineBindPoint::eGraphics,
                state.nativePipelineLayout, 0, desired, state.nativeBoundDescriptorBindings);
        }
        if (allocation.address != 0)
        {
            commandBuffer.pushConstants(state.nativePipelineLayout, ArgumentShaderStages, 0, 8, &allocation.address);
        }
    }

    void VKRenderPassEncoder::replaySetBindGroup(ReplayContext &context, const Private::RenderSetBindGroup &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        DescriptorBindingState desiredState = DescriptorBindingState{
            .group = typedCommand.group,
            .layout = nullptr,
            .descriptorSet = vk::DescriptorSet{},
            .descriptorSetOverride = false,
        };

        const DescriptorBindingState *existing = getDescriptorBinding(state.descriptorBindings, typedCommand.index);
        if (existing != nullptr && areDescriptorBindingStatesEquivalent(*existing, desiredState))
        {
            if (state.pipeline != nullptr)
            {
                bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.index, desiredState, state.nativeBoundDescriptorBindings);
            }
            return;
        }

        setDescriptorBinding(state.descriptorBindings, typedCommand.index, desiredState);
        context.diagnostics->recordBindGroupSet(typedCommand.group != nullptr ? static_cast<const VKBindGroup &>(*typedCommand.group).getLabelName() : eastl::string{});
        bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.index, desiredState, state.nativeBoundDescriptorBindings);
    }

    void VKRenderPassEncoder::replaySetVertexBuffer(ReplayContext &context, const Private::RenderSetVertexBuffer &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        const auto boundIt = state.nativeBoundVertexBuffers.find(typedCommand.slot);
        if (boundIt != state.nativeBoundVertexBuffers.end() && areBufferRangesEquivalent(boundIt->second, typedCommand.buffer))
        {
            return;
        }

        auto *buffer = static_cast<VKBuffer *>(typedCommand.buffer.buffer.get());
        const vk::DeviceSize offset = typedCommand.buffer.offset;
        commandBuffer.bindVertexBuffers(typedCommand.slot, buffer->getNativeBuffer(), offset);
        state.nativeBoundVertexBuffers[typedCommand.slot] = typedCommand.buffer;
    }

    void VKRenderPassEncoder::replaySetIndexBuffer(ReplayContext &context, const Private::RenderSetIndexBuffer &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        if (state.hasNativeIndexBuffer && state.nativeIndexFormat == typedCommand.format && areBufferRangesEquivalent(state.nativeIndexBuffer, typedCommand.buffer))
        {
            return;
        }

        auto *buffer = static_cast<VKBuffer *>(typedCommand.buffer.buffer.get());
        commandBuffer.bindIndexBuffer(buffer->getNativeBuffer(), typedCommand.buffer.offset, translateIndexFormat(typedCommand.format));
        state.nativeIndexBuffer = typedCommand.buffer;
        state.nativeIndexFormat = typedCommand.format;
        state.hasNativeIndexBuffer = true;
    }

    void VKRenderPassEncoder::replayDrawIndirect(ReplayContext &context, const Private::RenderDrawIndirect &typedCommand)
    {
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        context.diagnostics->recordDrawIndirect(typedCommand.indexed);
        auto *buffer = static_cast<VKBuffer *>(typedCommand.commands.buffer.get());
        if (!typedCommand.counter.buffer.isNull())
        {
            auto *counter = static_cast<VKBuffer *>(typedCommand.counter.buffer.get());
            if (typedCommand.indexed)
            {
                commandBuffer.drawIndexedIndirectCount(buffer->getNativeBuffer(), typedCommand.commands.offset,
                    counter->getNativeBuffer(), typedCommand.counter.offset,
                    typedCommand.maxCount, typedCommand.stride);
            }
            else
            {
                commandBuffer.drawIndirectCount(buffer->getNativeBuffer(), typedCommand.commands.offset,
                    counter->getNativeBuffer(), typedCommand.counter.offset,
                    typedCommand.maxCount, typedCommand.stride);
            }
        }
        else if (typedCommand.indexed)
        {
            commandBuffer.drawIndexedIndirect(buffer->getNativeBuffer(), typedCommand.commands.offset, typedCommand.maxCount, resolveIndexedIndirectStride(typedCommand.stride));
        }
        else
        {
            commandBuffer.drawIndirect(buffer->getNativeBuffer(), typedCommand.commands.offset, typedCommand.maxCount, resolveIndirectStride(typedCommand.stride));
        }
    }

    void VKRenderPassEncoder::replayDrawPixels(ReplayContext &context, const Private::RenderDrawPixels &typedCommand)
    {
        (void)typedCommand;
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        const RenderPassSetup &setup = *context.setup;
        const Private::RenderSetViewport pixelViewport = Private::RenderSetViewport{
            0.0f,
            0.0f,
            static_cast<float>(setup.attachments.width),
            static_cast<float>(setup.attachments.height),
            0.0f,
            1.0f,
        };
        if (!state.hasViewport || state.viewport.x != pixelViewport.x || state.viewport.y != pixelViewport.y || state.viewport.width != pixelViewport.width || state.viewport.height != pixelViewport.height || state.viewport.minDepth != pixelViewport.minDepth || state.viewport.maxDepth != pixelViewport.maxDepth)
        {
            commandBuffer.setViewport(0, vk::Viewport{pixelViewport.x, pixelViewport.y, pixelViewport.width, pixelViewport.height, pixelViewport.minDepth, pixelViewport.maxDepth});
            state.viewport = pixelViewport;
            state.hasViewport = true;
        }

        const Private::RenderSetScissorRect pixelScissor = Private::RenderSetScissorRect{
            0u,
            0u,
            setup.attachments.width,
            setup.attachments.height,
        };
        if (!state.hasScissor || state.scissor.x != pixelScissor.x || state.scissor.y != pixelScissor.y || state.scissor.width != pixelScissor.width || state.scissor.height != pixelScissor.height)
        {
            commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{pixelScissor.width, pixelScissor.height}});
            state.scissor = pixelScissor;
            state.hasScissor = true;
        }

        context.diagnostics->recordDraw();
        commandBuffer.draw(3u, 1u, 0u, 0u);
    }

    void VKRenderPassEncoder::replayNextPixelLocalPass(ReplayContext &context, const Private::RenderNextPixelLocalPass &typedCommand)
    {
        (void)typedCommand;
        vk::CommandBuffer commandBuffer = context.commandBuffer;
        auto &state = *context.state;
        const RenderPassSetup &setup = *context.setup;
        commandBuffer.nextSubpass(vk::SubpassContents::eInline);
        ++state.currentPixelLocalPassIndex;
        commandBuffer.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(setup.attachments.width), static_cast<float>(setup.attachments.height), 0.0f, 1.0f});
        commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{setup.attachments.width, setup.attachments.height}});
        state.pipeline = nullptr;
        state.pipelineLayout = nullptr;
        state.nativePipelineLayout = nullptr;
        state.nativeBoundDescriptorBindings.boundMask = 0u;
        state.viewport = Private::RenderSetViewport{
            0.0f,
            0.0f,
            static_cast<float>(setup.attachments.width),
            static_cast<float>(setup.attachments.height),
            0.0f,
            1.0f,
        };
        state.scissor = Private::RenderSetScissorRect{
            0u,
            0u,
            setup.attachments.width,
            setup.attachments.height,
        };
        state.hasViewport = true;
        state.hasScissor = true;
    }
} // namespace GVM::RHI::Vulkan
