#include "VKRenderPassEncoder.hpp"

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
#include "VKRenderToSwapchainExecutor.hpp"
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
        struct AttachmentContext
        {
            eastl::vector<vk::ImageView> imageViews;
            eastl::vector<vk::ClearValue> clearValues;
            eastl::vector<Texture> textureHandles;
            eastl::vector<vk::ImageView> pixelLocalInputImageViews;
            eastl::vector<uint32_t> pixelLocalInputColorAttachmentIndices;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t layers = 1;
        };

        struct RenderPassSetup
        {
            AttachmentContext attachments = {};
            vk::RenderPass renderPass = nullptr;
            VKDevice::FramebufferHandle framebuffer = {};
        };

        /// Stores one transient pixel-local input descriptor set that is valid for the active command encoder frame.
        struct PixelLocalInputDescriptorCacheEntry
        {
            vk::RenderPass renderPass = nullptr;
            vk::DescriptorSetLayout descriptorSetLayout = nullptr;
            uint32_t pixelLocalPassIndex = 0u;
            RenderPipeline pipeline = nullptr;
            eastl::vector<vk::ImageView> framebufferImageViews;
            vk::DescriptorSet descriptorSet = nullptr;
        };

        [[nodiscard]]
        bool areBufferRangesEquivalent(const BufferRange &lhs, const BufferRange &rhs)
        {
            return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.size == rhs.size;
        }

        void appendUniqueLabel(eastl::vector<eastl::string> &labels, const eastl::string &label)
        {
            const eastl::string resolvedLabel = label.empty() ? eastl::string("<unlabeled>") : label;
            if (eastl::find(labels.begin(), labels.end(), resolvedLabel) != labels.end())
            {
                return;
            }
            labels.push_back(resolvedLabel);
        }

        [[nodiscard]]
        eastl::string buildLabelList(const eastl::vector<eastl::string> &labels, size_t limit = 8u)
        {
            if (labels.empty())
            {
                return "<none>";
            }

            eastl::string text;
            const size_t resolvedLimit = eastl::min(limit, labels.size());
            for (size_t index = 0; index < resolvedLimit; ++index)
            {
                if (index != 0u)
                {
                    text += "|";
                }
                text += labels[index];
            }
            if (labels.size() > resolvedLimit)
            {
                text += "|...";
            }
            return text;
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

        void validateAttachmentExtent(uint32_t attachmentWidth, uint32_t attachmentHeight, uint32_t attachmentLayers, AttachmentContext &context)
        {
            if (context.width == 0)
            {
                context.width = attachmentWidth;
                context.height = attachmentHeight;
                context.layers = attachmentLayers;
                return;
            }

            if (context.width != attachmentWidth || context.height != attachmentHeight || context.layers != attachmentLayers)
            {
                throw makeInvalidArgument("VKRenderPassEncoder requires all attachments in a render pass to share the same extent and layer count.");
            }
        }

        AttachmentContext resolveAttachments(VKDevice &device, const RenderPassDescriptor &descriptor)
        {
            AttachmentContext context = {};
            context.imageViews.reserve(descriptor.colorAttachments.size() + (!descriptor.depthStencilAttachment.view.isNull() ? 1u : 0u));
            context.clearValues.reserve(context.imageViews.capacity());
            context.textureHandles.reserve(context.imageViews.capacity());

            for (uint32_t colorIndex = 0; colorIndex < descriptor.colorAttachments.size(); ++colorIndex)
            {
                const RenderPassColorAttachment &attachment = descriptor.colorAttachments[colorIndex];
                if (attachment.view.isNull())
                {
                    throw makeInvalidArgument("VKRenderPassEncoder received a null color attachment view.");
                }
                auto &view = static_cast<VKTextureView &>(*attachment.view.get());
                VKTexture &texture = *view.getTexture();
                validateAttachmentExtent(view.getWidth(), view.getHeight(), view.getLayerCount(), context);

                context.imageViews.push_back(view.getNativeImageView());
                context.textureHandles.push_back(device.findTextureHandle(texture));
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
            }

            if (!descriptor.depthStencilAttachment.view.isNull())
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.depthStencilAttachment.view.get());
                VKTexture &texture = *view.getTexture();
                validateAttachmentExtent(view.getWidth(), view.getHeight(), view.getLayerCount(), context);

                context.imageViews.push_back(view.getNativeImageView());
                context.textureHandles.push_back(device.findTextureHandle(texture));
                context.clearValues.push_back(vk::ClearDepthStencilValue{descriptor.depthStencilAttachment.depthClearValue, 0u});
            }

            if (context.width == 0 || context.height == 0)
            {
                throw makeInvalidArgument("VKRenderPassEncoder requires at least one valid attachment.");
            }

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

        /// Resolves source-level pixel-local attachment access for a render pipeline.
        PixelLocalPassAttachmentAccess resolvePipelineAttachmentAccess(const VKRenderPipeline &pipeline)
        {
            const RenderPipelineDescriptor &descriptor = pipeline.getDescriptor();
            if (GVM::RHI::Private::hasExplicitPixelLocalAttachmentAccess(descriptor.pixelLocalAttachmentAccess))
            {
                return descriptor.pixelLocalAttachmentAccess;
            }

            if (pipeline.usesPixelLocalInputAttachments())
            {
                throw makeLogicError("VKRenderPassEncoder requires explicit pixelLocalAttachmentAccess metadata for pixel-local input attachment pipelines.");
            }

            return GVM::RHI::Private::derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(descriptor);
        }

        void bindRenderPassDescriptorBindingIfNeeded(vk::CommandBuffer commandBuffer, const RenderPipeline &pipelineHandle, const PipelineLayout &pipelineLayoutHandle, vk::PipelineLayout nativePipelineLayout, uint32_t groupIndex, const DescriptorBindingState &desiredState, eastl::unordered_map<uint32_t, DescriptorBindingState> &nativeBoundDescriptorBindings)
        {
            if (pipelineHandle == nullptr || pipelineLayoutHandle == nullptr)
            {
                return;
            }

            const auto &pipeline = static_cast<const VKRenderPipeline &>(*pipelineHandle);
            const auto &pipelineLayout = static_cast<const VKPipelineLayout &>(*pipelineLayoutHandle);
            if (!pipeline.usesPixelLocalInputAttachments())
            {
                bindDescriptorBindingStateIfNeeded(commandBuffer, vk::PipelineBindPoint::eGraphics, nativePipelineLayout, pipelineLayout, groupIndex, desiredState, nativeBoundDescriptorBindings);
                return;
            }

            if (!isDescriptorBindingStateCompatibleWithPipelineLayout(desiredState, pipelineLayout, groupIndex))
            {
                nativeBoundDescriptorBindings.erase(groupIndex);
                return;
            }

            const auto boundIt = nativeBoundDescriptorBindings.find(groupIndex);
            if (boundIt != nativeBoundDescriptorBindings.end() && areDescriptorBindingStatesEquivalent(boundIt->second, desiredState))
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

            nativeBoundDescriptorBindings[groupIndex] = desiredState;
        }

        void ensureRenderPassDrawBindingsValid(const char *apiName, const RenderPipeline &pipelineHandle, const PipelineLayout &pipelineLayoutHandle, const eastl::unordered_map<uint32_t, DescriptorBindingState> &descriptorBindings, bool &bindGroupStateDirty, bool &bindGroupStateValid)
        {
            if (pipelineHandle == nullptr || pipelineLayoutHandle == nullptr)
            {
                return;
            }

            const auto &pipeline = static_cast<const VKRenderPipeline &>(*pipelineHandle);
            const auto &pipelineLayout = static_cast<const VKPipelineLayout &>(*pipelineLayoutHandle);
            if (bindGroupStateDirty)
            {
                bindGroupStateValid = areRequiredDescriptorBindingsSatisfied(pipelineLayout, descriptorBindings);
                bindGroupStateDirty = false;
            }

            if (!bindGroupStateValid)
            {
                throw makeLogicError(buildMissingDescriptorBindingMessage(apiName, pipeline.getLabelName(), pipelineLayout, descriptorBindings));
            }
        }

        /// Resolves the active pass access used to bind pixel-local input descriptors.
        PixelLocalPassAttachmentAccess resolvePixelLocalAttachmentAccess(const RenderPassDescriptor &descriptor, uint32_t currentPixelLocalPassIndex)
        {
            if (descriptor.pixelLocal.attachmentAccesses.empty())
            {
                throw makeLogicError("VKRenderPassEncoder requires explicit pixelLocal attachmentAccesses metadata before binding pixel-local input attachments.");
            }
            if (currentPixelLocalPassIndex < descriptor.pixelLocal.attachmentAccesses.size())
            {
                return descriptor.pixelLocal.attachmentAccesses[currentPixelLocalPassIndex];
            }
            throw makeLogicError("VKRenderPassEncoder pixelLocal attachmentAccesses metadata does not cover the current pixel-local pass.");
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

            throw makeLogicError("VKRenderPassEncoder pixelLocal input attachment index exceeds the current pixelLocal pass input range.");
        }

        /// Resolves the descriptor image layout for a framebuffer attachment used by the current pixel-local pass.
        vk::ImageLayout resolvePixelLocalInputDescriptorLayout(const AttachmentContext &attachments, const PixelLocalPassAttachmentAccess &, uint32_t attachmentContextIndex)
        {
            if (attachmentContextIndex >= attachments.pixelLocalInputColorAttachmentIndices.size())
            {
                throw makeLogicError("VKRenderPassEncoder pixelLocal input attachment index does not map to a color attachment.");
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
            if (!descriptor.pixelLocal.enabled || currentPixelLocalPassIndex == 0u)
            {
                throw makeLogicError("VKRenderPassEncoder cannot bind pixelLocal input attachments before a previous pixelLocal pass has completed.");
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
                    resolvePixelLocalInputDescriptorLayout(attachments, access, attachmentContextIndex),
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
            setup.attachments = resolveAttachments(device, descriptor);
            for (const Texture &attachmentTexture : setup.attachments.textureHandles)
            {
                commandEncoder.retainTexture(attachmentTexture);
            }

            for (size_t colorIndex = 0; colorIndex < descriptor.colorAttachments.size(); ++colorIndex)
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.colorAttachments[colorIndex].view.get());
                if (!shouldSynchronizeAttachmentBeforeRenderPass(view))
                {
                    continue;
                }
                stateTracker.synchronizeTextureSubresources(commandBuffer, *view.getTexture(), vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, view.getDescriptor().aspect, view.getDescriptor().baseMipLevel, view.getDescriptor().mipLevelCount, view.getDescriptor().baseArrayLayer, view.getDescriptor().arrayLayerCount);
            }

            if (!descriptor.depthStencilAttachment.view.isNull())
            {
                auto &view = static_cast<VKTextureView &>(*descriptor.depthStencilAttachment.view.get());
                if (shouldSynchronizeAttachmentBeforeRenderPass(view))
                {
                    stateTracker.synchronizeTextureSubresources(
                        commandBuffer, *view.getTexture(), vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite, view.getDescriptor().aspect, view.getDescriptor().baseMipLevel, view.getDescriptor().mipLevelCount, view.getDescriptor().baseArrayLayer, view.getDescriptor().arrayLayerCount);
                }
            }

            setup.renderPass = device.getOrCreateRenderPass(descriptor);
            setup.framebuffer = device.getOrCreateFramebuffer(setup.renderPass, setup.attachments.imageViews, setup.attachments.width, setup.attachments.height, setup.attachments.layers);
            commandEncoder.retainNativeResource(eastl::static_pointer_cast<void>(setup.framebuffer.owner));
            return setup;
        }

    } // namespace

    void VKRenderPassEncoder::init(VKDevice *device, CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor)
    {
        if (device == nullptr || commandEncoder == nullptr)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::init requires a valid device and command encoder.");
        }

        mDevice = device;
        mCommandEncoder = commandEncoder;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mQueuedPixelLocalPassIndex = 0u;
    }

    void VKRenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        ensureOpen("VKRenderPassEncoder::setScissorRect");
        mCommands.push_back(CmdSetScissorRect{x, y, width, height});
    }

    void VKRenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        ensureOpen("VKRenderPassEncoder::setViewport");
        mCommands.push_back(CmdSetViewport{x, y, width, height, minDepth, maxDepth});
    }

    void VKRenderPassEncoder::drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor)
    {
        ensureOpen("VKRenderPassEncoder::drawFullscreenTexture");
        if (source.isNull())
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawFullscreenTexture requires a valid source texture.");
        }
        if (mDescriptor.colorAttachments.empty())
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawFullscreenTexture requires a color attachment target.");
        }
        if (source->getDepth() != 1u || source->getArrayLayerCount() != 1u)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawFullscreenTexture currently only supports non-array 2D textures.");
        }

        auto *targetView = static_cast<VKTextureView *>(mDescriptor.colorAttachments[0].view.get());
        if (targetView == nullptr)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawFullscreenTexture requires a valid color attachment view.");
        }

        setViewport(0.0f, 0.0f, static_cast<float>(targetView->getWidth()), static_cast<float>(targetView->getHeight()), 0.0f, 1.0f);
        setScissorRect(0, 0, targetView->getWidth(), targetView->getHeight());
        // RenderToSwapchain is a fixed fullscreen sample path. Queue an
        // explicit sampled-texture transition so the source image is legalized
        // before the render pass begins, then let bind-group preparation handle
        // descriptor binding as usual.
        enqueueExplicitSampledTexture(source);
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainTexture(source);
        mDevice->getRenderToSwapchainExecutor()->encode(this, source, targetView->getResolvedFormat(), descriptor);
    }

    void VKRenderPassEncoder::setPipeline(RenderPipeline pipeline)
    {
        ensureOpen("VKRenderPassEncoder::setPipeline");
        if (pipeline == nullptr)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::setPipeline requires a valid pipeline.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainRenderPipeline(pipeline);
        mCommands.push_back(CmdSetPipeline{pipeline});
    }

    void VKRenderPassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("VKRenderPassEncoder::setBindGroup");
        if (group == nullptr)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::setBindGroup requires a valid bind group.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainBindGroup(group);
        enqueueBindGroupPreparation(group);
        mCommands.push_back(CmdSetBindGroup{group, groupIndex});
    }

    void VKRenderPassEncoder::setTransientDescriptorSet(BindGroupLayout layout, vk::DescriptorSet descriptorSet, uint32_t groupIndex)
    {
        ensureOpen("VKRenderPassEncoder::setTransientDescriptorSet");
        if (layout == nullptr || descriptorSet == vk::DescriptorSet{})
        {
            throw makeInvalidArgument("VKRenderPassEncoder::setTransientDescriptorSet requires a valid layout and descriptor set.");
        }

        mCommands.push_back(CmdSetTransientDescriptorSet{
            .layout = layout,
            .descriptorSet = descriptorSet,
            .groupIndex = groupIndex,
        });
    }

    void VKRenderPassEncoder::setVertexBuffer(BufferRange buffer, uint32_t slot)
    {
        ensureOpen("VKRenderPassEncoder::setVertexBuffer");
        if (buffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKRenderPassEncoder::setVertexBuffer requires a valid buffer.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainBuffer(buffer.buffer);
        enqueueBufferPreparation(buffer, vk::PipelineStageFlagBits::eVertexInput, vk::AccessFlagBits::eVertexAttributeRead);
        mCommands.push_back(CmdSetVertexBuffer{buffer, slot});
    }

    void VKRenderPassEncoder::setIndexBuffer(BufferRange buffer, IndexFormat format)
    {
        ensureOpen("VKRenderPassEncoder::setIndexBuffer");
        if (buffer.buffer.isNull() || format == IndexFormat::Undefined)
        {
            throw makeInvalidArgument("VKRenderPassEncoder::setIndexBuffer requires a valid buffer and index format.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainBuffer(buffer.buffer);
        enqueueBufferPreparation(buffer, vk::PipelineStageFlagBits::eVertexInput, vk::AccessFlagBits::eIndexRead);
        mCommands.push_back(CmdSetIndexBuffer{buffer, format});
    }

    void VKRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        ensureOpen("VKRenderPassEncoder::draw");
        mCommands.push_back(CmdDraw{vertexCount, instanceCount, firstVertex, firstInstance});
    }

    void VKRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
    {
        ensureOpen("VKRenderPassEncoder::drawIndexed");
        mCommands.push_back(CmdDrawIndexed{indexCount, instanceCount, firstIndex, baseVertex, firstInstance});
    }

    void VKRenderPassEncoder::drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndirect");
        if (indirectBuffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawIndirect requires a valid indirect buffer.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainBuffer(indirectBuffer.buffer);
        enqueueBufferPreparation(indirectBuffer, vk::PipelineStageFlagBits::eDrawIndirect, vk::AccessFlagBits::eIndirectCommandRead);
        mCommands.push_back(CmdDrawIndirect{indirectBuffer, indirectCommandCount, stride});
    }

    void VKRenderPassEncoder::drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        ensureOpen("VKRenderPassEncoder::drawIndexedIndirect");
        if (indirectBuffer.buffer.isNull())
        {
            throw makeInvalidArgument("VKRenderPassEncoder::drawIndexedIndirect requires a valid indirect buffer.");
        }
        static_cast<VKCommandEncoder *>(mCommandEncoder.get())->retainBuffer(indirectBuffer.buffer);
        enqueueBufferPreparation(indirectBuffer, vk::PipelineStageFlagBits::eDrawIndirect, vk::AccessFlagBits::eIndirectCommandRead);
        mCommands.push_back(CmdDrawIndexedIndirect{indirectBuffer, indirectCommandCount, stride});
    }

    void VKRenderPassEncoder::drawPixels()
    {
        ensureOpen("VKRenderPassEncoder::drawPixels");
        mCommands.push_back(CmdDrawPixels{});
    }

    void VKRenderPassEncoder::nextPixelLocalPass()
    {
        ensureOpen("VKRenderPassEncoder::nextPixelLocalPass");
        const uint32_t pixelLocalPassCount = mDescriptor.pixelLocal.enabled ? (mDescriptor.pixelLocal.passCount == 0u ? 1u : mDescriptor.pixelLocal.passCount) : 1u;
        if (pixelLocalPassCount <= 1u)
        {
            throw makeLogicError("VKRenderPassEncoder::nextPixelLocalPass requires a render pass with multiple pixelLocal passes.");
        }
        if (mQueuedPixelLocalPassIndex + 1u >= pixelLocalPassCount)
        {
            throw makeLogicError("VKRenderPassEncoder::nextPixelLocalPass exceeded the render pass pixelLocal pass count.");
        }

        mCommands.push_back(CmdNextPixelLocalPass{});
        ++mQueuedPixelLocalPassIndex;
    }

    eastl::vector<PixelLocalPassAttachmentAccess> VKRenderPassEncoder::resolvePixelLocalAttachmentAccessesFromCommands() const
    {
        if (!mDescriptor.pixelLocal.enabled)
        {
            return {};
        }

        GVM::RHI::Private::PixelLocalPassAccessBuilder builder;
        RenderPipeline currentPipeline = nullptr;

        for (const RenderCommand &command : mCommands)
        {
            if (const CmdSetPipeline *pipelineCommand = eastl::get_if<CmdSetPipeline>(&command))
            {
                currentPipeline = pipelineCommand->pipeline;
                continue;
            }

            if (eastl::get_if<CmdNextPixelLocalPass>(&command) != nullptr)
            {
                builder.nextPass();
                continue;
            }

            const bool isDrawCommand = eastl::get_if<CmdDraw>(&command) != nullptr || eastl::get_if<CmdDrawIndexed>(&command) != nullptr || eastl::get_if<CmdDrawIndirect>(&command) != nullptr || eastl::get_if<CmdDrawIndexedIndirect>(&command) != nullptr || eastl::get_if<CmdDrawPixels>(&command) != nullptr;
            if (isDrawCommand && currentPipeline != nullptr)
            {
                const auto &pipeline = static_cast<const VKRenderPipeline &>(*currentPipeline);
                builder.appendTask(resolvePipelineAttachmentAccess(pipeline));
            }
        }

        return builder.finish();
    }

    void VKRenderPassEncoder::end()
    {
        if (mEnded)
        {
            throw makeLogicError("VKRenderPassEncoder::end called after end().");
        }
        ensureOpen("VKRenderPassEncoder::end");
        const uint32_t pixelLocalPassCount = mDescriptor.pixelLocal.enabled ? (mDescriptor.pixelLocal.passCount == 0u ? 1u : mDescriptor.pixelLocal.passCount) : 1u;
        if (pixelLocalPassCount > 1u && mQueuedPixelLocalPassIndex + 1u != pixelLocalPassCount)
        {
            throw makeLogicError("VKRenderPassEncoder::end called before all declared pixelLocal passes were reached.");
        }

        auto &commandEncoder = static_cast<VKCommandEncoder &>(*mCommandEncoder);
        vk::CommandBuffer commandBuffer = commandEncoder.getNativeCommandBuffer();
        auto &stateTracker = commandEncoder.getTaskDependencyResolver();
        mDescriptor.pixelLocal.attachmentAccesses = resolvePixelLocalAttachmentAccessesFromCommands();
        if (mDescriptor.pixelLocal.enabled && mDescriptor.pixelLocal.attachmentAccesses.size() != pixelLocalPassCount)
        {
            throw makeLogicError("VKRenderPassEncoder requires one explicit pixelLocal attachmentAccesses entry for each declared pixelLocal pass.");
        }
        const RenderPassSetup renderPassSetup = prepareRenderPassSetup(*mDevice, commandEncoder, commandBuffer, stateTracker, mDescriptor);

        {
            for (const auto &[_, sampledTextureHandle] : mExplicitSampledTextures)
            {
                if (sampledTextureHandle.isNull())
                {
                    continue;
                }
                auto *sampledTexture = static_cast<VKTexture *>(sampledTextureHandle.get());
                stateTracker.synchronizeTextureSubresources(commandBuffer, *sampledTexture, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead);
            }

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

            prepareBindGroupBindings(commandBuffer, stateTracker, bindGroupsToPrepare, vk::PipelineStageFlagBits::eAllGraphics, &mExplicitSampledTextures, renderPassSetup.attachments.textureHandles);

            eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest> bufferSyncRequests;
            for (const auto &[_, bufferPreparations] : mPendingBufferPreparations)
            {
                bufferSyncRequests.reserve(bufferSyncRequests.size() + bufferPreparations.size());
            }
            for (const auto &[_, bufferPreparations] : mPendingBufferPreparations)
            {
                for (const PendingBufferPreparation &bufferPreparation : bufferPreparations)
                {
                    bufferSyncRequests.push_back(VKTaskDependencyResolver::BufferRangeSyncRequest{
                        .buffer = static_cast<VKBuffer *>(bufferPreparation.buffer.get()),
                        .offset = bufferPreparation.offset,
                        .size = bufferPreparation.size,
                        .requiredStageMask = bufferPreparation.stageMask,
                        .requiredAccessMask = bufferPreparation.accessMask,
                    });
                }
            }
            stateTracker.synchronizeBufferRanges(commandBuffer, bufferSyncRequests);
        }

        const RenderPassSetup &setup = renderPassSetup;
        ReplayState state = {};
        ReplayDiagnostics diagnostics = {};
        eastl::vector<PixelLocalInputDescriptorCacheEntry> pixelLocalInputDescriptorCache;

        {
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

            commandEncoder.writePassTimestamp(mDescriptor.timestampWrites, true, vk::PipelineStageFlagBits::eTopOfPipe);
            commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
            commandBuffer.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(setup.attachments.width), static_cast<float>(setup.attachments.height), 0.0f, 1.0f});
            commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{setup.attachments.width, setup.attachments.height}});

            state.hasViewport = true;
            state.viewport = CmdSetViewport{
                0.0f,
                0.0f,
                static_cast<float>(setup.attachments.width),
                static_cast<float>(setup.attachments.height),
                0.0f,
                1.0f,
            };
            state.hasScissor = true;
            state.scissor = CmdSetScissorRect{
                0u,
                0u,
                setup.attachments.width,
                setup.attachments.height,
            };

            for (const RenderCommand &command : mCommands)
            {
                eastl::visit(
                    [&](const auto &typedCommand) {
                        using T = eastl::decay_t<decltype(typedCommand)>;
                        if constexpr (eastl::is_same_v<T, CmdSetScissorRect>)
                        {
                            if (state.hasScissor && state.scissor.x == typedCommand.x && state.scissor.y == typedCommand.y && state.scissor.width == typedCommand.width && state.scissor.height == typedCommand.height)
                            {
                                return;
                            }

                            commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{static_cast<int32_t>(typedCommand.x), static_cast<int32_t>(typedCommand.y)}, vk::Extent2D{typedCommand.width, typedCommand.height}});
                            state.scissor = CmdSetScissorRect{
                                typedCommand.x,
                                typedCommand.y,
                                typedCommand.width,
                                typedCommand.height,
                            };
                            state.hasScissor = true;
                        }
                        else if constexpr (eastl::is_same_v<T, CmdSetViewport>)
                        {
                            if (state.hasViewport && state.viewport.x == typedCommand.x && state.viewport.y == typedCommand.y && state.viewport.width == typedCommand.width && state.viewport.height == typedCommand.height && state.viewport.minDepth == typedCommand.minDepth && state.viewport.maxDepth == typedCommand.maxDepth)
                            {
                                return;
                            }

                            commandBuffer.setViewport(0, vk::Viewport{typedCommand.x, typedCommand.y, typedCommand.width, typedCommand.height, typedCommand.minDepth, typedCommand.maxDepth});
                            state.viewport = CmdSetViewport{
                                typedCommand.x,
                                typedCommand.y,
                                typedCommand.width,
                                typedCommand.height,
                                typedCommand.minDepth,
                                typedCommand.maxDepth,
                            };
                            state.hasViewport = true;
                        }
                        else if constexpr (eastl::is_same_v<T, CmdSetPipeline>)
                        {
                            if (state.pipeline == typedCommand.pipeline)
                            {
                                return;
                            }

                            auto &pipeline = static_cast<VKRenderPipeline &>(*typedCommand.pipeline);
                            const vk::PipelineLayout previousPipelineLayout = state.nativePipelineLayout;
                            state.pipeline = typedCommand.pipeline;
                            state.pipelineLayout = pipeline.getPipelineLayoutHandle();
                            state.nativePipelineLayout = pipeline.getNativePipelineLayout();
                            ++diagnostics.pipelineSwitchCount;
                            appendUniqueLabel(diagnostics.pipelineLabels, pipeline.getLabelName());
                            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getNativePipelineForRenderPassSubpass(setup.renderPass, state.currentPixelLocalPassIndex));
                            bindPixelLocalInputDescriptors(*mDevice, commandEncoder, commandBuffer, setup.renderPass, state.nativePipelineLayout, typedCommand.pipeline, pipeline, setup.attachments, mDescriptor, state.currentPixelLocalPassIndex, pixelLocalInputDescriptorCache);
                            state.bindGroupStateDirty = true;

                            if (previousPipelineLayout != state.nativePipelineLayout)
                            {
                                state.nativeBoundDescriptorBindings.clear();
                                for (const auto &[groupIndex, desiredState] : state.descriptorBindings)
                                {
                                    bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, groupIndex, desiredState, state.nativeBoundDescriptorBindings);
                                }
                            }
                        }
                        else if constexpr (eastl::is_same_v<T, CmdSetBindGroup>)
                        {
                            DescriptorBindingState desiredState = DescriptorBindingState{
                                .group = typedCommand.group,
                                .layout = nullptr,
                                .descriptorSet = vk::DescriptorSet{},
                                .descriptorSetOverride = false,
                            };

                            const auto existing = state.descriptorBindings.find(typedCommand.groupIndex);
                            if (existing != state.descriptorBindings.end() && areDescriptorBindingStatesEquivalent(existing->second, desiredState))
                            {
                                if (state.pipeline != nullptr)
                                {
                                    bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.groupIndex, desiredState, state.nativeBoundDescriptorBindings);
                                }
                                return;
                            }

                            state.descriptorBindings[typedCommand.groupIndex] = desiredState;
                            state.bindGroupStateDirty = true;
                            ++diagnostics.bindGroupSetCount;
                            appendUniqueLabel(diagnostics.bindGroupLabels, typedCommand.group != nullptr ? static_cast<const VKBindGroup &>(*typedCommand.group).getLabelName() : eastl::string{});
                            bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.groupIndex, desiredState, state.nativeBoundDescriptorBindings);
                        }
                        else if constexpr (eastl::is_same_v<T, CmdSetTransientDescriptorSet>)
                        {
                            const DescriptorBindingState desiredState = DescriptorBindingState{
                                .group = nullptr,
                                .layout = typedCommand.layout,
                                .descriptorSet = typedCommand.descriptorSet,
                                .descriptorSetOverride = true,
                            };

                            const auto existing = state.descriptorBindings.find(typedCommand.groupIndex);
                            if (existing != state.descriptorBindings.end() && areDescriptorBindingStatesEquivalent(existing->second, desiredState))
                            {
                                if (state.pipeline != nullptr)
                                {
                                    bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.groupIndex, desiredState, state.nativeBoundDescriptorBindings);
                                }
                                return;
                            }

                            state.descriptorBindings[typedCommand.groupIndex] = desiredState;
                            state.bindGroupStateDirty = true;
                            appendUniqueLabel(diagnostics.bindGroupLabels, eastl::string("descriptor_set_override_") + eastl::to_string(typedCommand.groupIndex));
                            bindRenderPassDescriptorBindingIfNeeded(commandBuffer, state.pipeline, state.pipelineLayout, state.nativePipelineLayout, typedCommand.groupIndex, desiredState, state.nativeBoundDescriptorBindings);
                        }
                        else if constexpr (eastl::is_same_v<T, CmdSetVertexBuffer>)
                        {
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
                        else if constexpr (eastl::is_same_v<T, CmdSetIndexBuffer>)
                        {
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
                        else if constexpr (eastl::is_same_v<T, CmdDraw>)
                        {
                            if (state.pipeline == nullptr)
                            {
                                throw makeLogicError("VKRenderPassEncoder::draw requires a pipeline to be bound first.");
                            }
                            ensureRenderPassDrawBindingsValid("VKRenderPassEncoder::draw", state.pipeline, state.pipelineLayout, state.descriptorBindings, state.bindGroupStateDirty, state.bindGroupStateValid);
                            ++diagnostics.drawCount;
                            commandBuffer.draw(typedCommand.vertexCount, typedCommand.instanceCount, typedCommand.firstVertex, typedCommand.firstInstance);
                        }
                        else if constexpr (eastl::is_same_v<T, CmdDrawIndexed>)
                        {
                            if (state.pipeline == nullptr)
                            {
                                throw makeLogicError("VKRenderPassEncoder::drawIndexed requires a pipeline to be bound first.");
                            }
                            ensureRenderPassDrawBindingsValid("VKRenderPassEncoder::drawIndexed", state.pipeline, state.pipelineLayout, state.descriptorBindings, state.bindGroupStateDirty, state.bindGroupStateValid);
                            ++diagnostics.drawIndexedCount;
                            commandBuffer.drawIndexed(typedCommand.indexCount, typedCommand.instanceCount, typedCommand.firstIndex, typedCommand.baseVertex, typedCommand.firstInstance);
                        }
                        else if constexpr (eastl::is_same_v<T, CmdDrawIndirect>)
                        {
                            if (state.pipeline == nullptr)
                            {
                                throw makeLogicError("VKRenderPassEncoder::drawIndirect requires a pipeline to be bound first.");
                            }
                            ensureRenderPassDrawBindingsValid("VKRenderPassEncoder::drawIndirect", state.pipeline, state.pipelineLayout, state.descriptorBindings, state.bindGroupStateDirty, state.bindGroupStateValid);
                            ++diagnostics.drawIndirectCount;
                            auto *buffer = static_cast<VKBuffer *>(typedCommand.indirectBuffer.buffer.get());
                            commandBuffer.drawIndirect(buffer->getNativeBuffer(), typedCommand.indirectBuffer.offset, typedCommand.indirectCommandCount, resolveIndirectStride(typedCommand.stride));
                        }
                        else if constexpr (eastl::is_same_v<T, CmdDrawIndexedIndirect>)
                        {
                            if (state.pipeline == nullptr)
                            {
                                throw makeLogicError("VKRenderPassEncoder::drawIndexedIndirect requires a pipeline to be bound first.");
                            }
                            ensureRenderPassDrawBindingsValid("VKRenderPassEncoder::drawIndexedIndirect", state.pipeline, state.pipelineLayout, state.descriptorBindings, state.bindGroupStateDirty, state.bindGroupStateValid);
                            ++diagnostics.drawIndexedIndirectCount;
                            auto *buffer = static_cast<VKBuffer *>(typedCommand.indirectBuffer.buffer.get());
                            commandBuffer.drawIndexedIndirect(buffer->getNativeBuffer(), typedCommand.indirectBuffer.offset, typedCommand.indirectCommandCount, resolveIndexedIndirectStride(typedCommand.stride));
                        }
                        else if constexpr (eastl::is_same_v<T, CmdDrawPixels>)
                        {
                            if (state.pipeline == nullptr)
                            {
                                throw makeLogicError("VKRenderPassEncoder::drawPixels requires a pipeline to be bound first.");
                            }
                            if (setup.attachments.width == 0u || setup.attachments.height == 0u)
                            {
                                throw makeLogicError("VKRenderPassEncoder::drawPixels requires a non-zero active render pass attachment extent.");
                            }
                            ensureRenderPassDrawBindingsValid("VKRenderPassEncoder::drawPixels", state.pipeline, state.pipelineLayout, state.descriptorBindings, state.bindGroupStateDirty, state.bindGroupStateValid);

                            const CmdSetViewport pixelViewport = CmdSetViewport{
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

                            const CmdSetScissorRect pixelScissor = CmdSetScissorRect{
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

                            ++diagnostics.drawCount;
                            commandBuffer.draw(3u, 1u, 0u, 0u);
                        }
                        else if constexpr (eastl::is_same_v<T, CmdNextPixelLocalPass>)
                        {
                            const uint32_t pixelLocalPassCount = mDescriptor.pixelLocal.enabled ? (mDescriptor.pixelLocal.passCount == 0u ? 1u : mDescriptor.pixelLocal.passCount) : 1u;
                            if (pixelLocalPassCount <= 1u)
                            {
                                throw makeLogicError("VKRenderPassEncoder::nextPixelLocalPass requires a render pass with multiple pixelLocal passes.");
                            }
                            if (state.currentPixelLocalPassIndex + 1u >= pixelLocalPassCount)
                            {
                                throw makeLogicError("VKRenderPassEncoder::nextPixelLocalPass exceeded the render pass pixelLocal pass count.");
                            }

                            commandBuffer.nextSubpass(vk::SubpassContents::eInline);
                            ++state.currentPixelLocalPassIndex;
                            commandBuffer.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(setup.attachments.width), static_cast<float>(setup.attachments.height), 0.0f, 1.0f});
                            commandBuffer.setScissor(0, vk::Rect2D{vk::Offset2D{0, 0}, vk::Extent2D{setup.attachments.width, setup.attachments.height}});
                            state.pipeline = nullptr;
                            state.pipelineLayout = nullptr;
                            state.nativePipelineLayout = nullptr;
                            state.bindGroupStateDirty = true;
                            state.bindGroupStateValid = false;
                            state.nativeBoundDescriptorBindings.clear();
                            state.viewport = CmdSetViewport{
                                0.0f,
                                0.0f,
                                static_cast<float>(setup.attachments.width),
                                static_cast<float>(setup.attachments.height),
                                0.0f,
                                1.0f,
                            };
                            state.scissor = CmdSetScissorRect{
                                0u,
                                0u,
                                setup.attachments.width,
                                setup.attachments.height,
                            };
                            state.hasViewport = true;
                            state.hasScissor = true;
                        }
                    },
                    command);
            }

            commandBuffer.endRenderPass();
            commandEncoder.writePassTimestamp(mDescriptor.timestampWrites, false, vk::PipelineStageFlagBits::eBottomOfPipe);
            GVMLogTrace(mDevice,
                        RenderPassEncoderLogCategory,
                        "event=render_pass_record_end command_encoder_ptr={} command_buffer_ptr={} pass_label={} render_pass_ptr={} framebuffer_ptr={} pipeline_switches={} unique_pipelines={} pipeline_labels={} bind_group_sets={} unique_bind_groups={} bind_group_labels={} draw_count={} draw_indexed_count={} draw_indirect_count={} draw_indexed_indirect_count={}",
                        static_cast<void *>(&commandEncoder),
                        reinterpret_cast<void *>(static_cast<VkCommandBuffer>(commandBuffer)),
                        safeLogLabel(mLabelName),
                        reinterpret_cast<void *>(static_cast<VkRenderPass>(setup.renderPass)),
                        reinterpret_cast<void *>(static_cast<VkFramebuffer>(setup.framebuffer.framebuffer)),
                        diagnostics.pipelineSwitchCount,
                        diagnostics.pipelineLabels.size(),
                        buildLabelList(diagnostics.pipelineLabels),
                        diagnostics.bindGroupSetCount,
                        diagnostics.bindGroupLabels.size(),
                        buildLabelList(diagnostics.bindGroupLabels),
                        diagnostics.drawCount,
                        diagnostics.drawIndexedCount,
                        diagnostics.drawIndirectCount,
                        diagnostics.drawIndexedIndirectCount);
            commandEncoder.notePassSummary();
        }

        commandEncoder.notifyPassEnded();
        mEnded = true;
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

    void VKRenderPassEncoder::enqueueBufferPreparation(BufferRange buffer, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask)
    {
        if (buffer.buffer.isNull())
        {
            return;
        }

        auto *vkBuffer = static_cast<VKBuffer *>(buffer.buffer.get());
        auto &pendingPreparations = mPendingBufferPreparations[vkBuffer];
        for (const PendingBufferPreparation &pendingPreparation : pendingPreparations)
        {
            if (pendingPreparation.buffer == buffer.buffer && pendingPreparation.offset == buffer.offset && pendingPreparation.size == buffer.size && pendingPreparation.stageMask == stageMask && pendingPreparation.accessMask == accessMask)
            {
                return;
            }
        }

        pendingPreparations.push_back(PendingBufferPreparation{
            .buffer = buffer.buffer,
            .offset = buffer.offset,
            .size = buffer.size,
            .stageMask = stageMask,
            .accessMask = accessMask,
        });
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

    VKCommandEncoder *VKRenderPassEncoder::getCommandEncoder() const
    {
        return static_cast<VKCommandEncoder *>(mCommandEncoder.get());
    }

    void VKRenderPassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded || mDevice == nullptr || mCommandEncoder == nullptr)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on an ended Vulkan render pass encoder.");
        }
    }
} // namespace GVM::RHI::Vulkan
