#include "VKRenderPassFramebufferCache.hpp"

#include "VKEnumUtils.hpp"
#include "VKPixelLocalLayoutPolicy.hpp"
#include "VKRenderPassHashUtils.hpp"
#include "VKTextureView.hpp"
#include "Private/PixelLocalPassAccess.hpp"
#include "../Private/VulkanTestHooks.hpp"

#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        TextureFormat resolveTextureViewFormat(const TextureView &view)
        {
            const auto &viewImpl = static_cast<const VKTextureView &>(*view.get());
            return viewImpl.getResolvedFormat();
        }

        vk::ImageLayout resolveAttachmentLayout(TextureFormat format)
        {
            return isDepthStencilFormat(format)
                ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                : vk::ImageLayout::eColorAttachmentOptimal;
        }

        /// Resolves the initial layout declared to Vulkan for an attachment before its first subpass use.
        vk::ImageLayout resolveAttachmentInitialLayout(TextureFormat format, LoadOp loadOp)
        {
            return loadOp == LoadOp::Load
                ? resolveAttachmentLayout(format)
                : vk::ImageLayout::eUndefined;
        }

        /// Normalizes pixel-local pass access for a Vulkan render pass signature.
        eastl::vector<PixelLocalPassAttachmentAccess> normalizeVulkanPixelLocalAttachmentAccesses(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            bool pixelLocalEnabled,
            uint32_t renderPassCount,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            return GVM::RHI::Private::normalizePixelLocalPassAttachmentAccesses(
                static_cast<uint32_t>(colorAttachments.size()),
                depthStencilAttachment.format != TextureFormat::Undefined,
                pixelLocalEnabled,
                renderPassCount,
                attachmentAccesses);
        }

        /// Resolves the color attachment layout for an attachment kept in the current pixel-local pass.
        vk::ImageLayout resolveColorReferenceLayout(
            const PixelLocalPassAttachmentAccess &access,
            uint32_t colorIndex)
        {
            return GVM::RHI::Private::isPixelLocalColorAttachmentRead(access, colorIndex)
                ? resolvePixelLocalColorInputLayout()
                : vk::ImageLayout::eColorAttachmentOptimal;
        }

        /// Resolves the input attachment layout used by the descriptor and render pass reference.
        vk::ImageLayout resolveInputColorReferenceLayout(
            const PixelLocalPassAttachmentAccess &,
            uint32_t)
        {
            return resolvePixelLocalColorInputLayout();
        }

        /// Appends a sparse color reference table whose locations match the framebuffer color attachment indices.
        /// Pixel-local reads are intentionally kept as live color references as well as input references.
        void appendSparseColorReferences(
            eastl::vector<vk::AttachmentReference> &colorReferences,
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const PixelLocalPassAttachmentAccess &access)
        {
            for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
            {
                const bool readsPixelLocalAttachment =
                    colorAttachments[colorIndex].pixelLocal &&
                    GVM::RHI::Private::isPixelLocalColorAttachmentRead(access, colorIndex);
                const bool writesAttachment =
                    GVM::RHI::Private::isPixelLocalColorAttachmentWritten(access, colorIndex);
                if (!readsPixelLocalAttachment && !writesAttachment)
                {
                    colorReferences.push_back(vk::AttachmentReference{
                        VK_ATTACHMENT_UNUSED,
                        vk::ImageLayout::eUndefined});
                    continue;
                }
                colorReferences.push_back(vk::AttachmentReference{
                    colorIndex,
                    readsPixelLocalAttachment ? resolvePixelLocalColorInputLayout() : resolveColorReferenceLayout(access, colorIndex)});
            }
        }

        /// Resolves depth attachment layout for a pixel-local pass.
        vk::ImageLayout resolveDepthReferenceLayout(const PixelLocalPassAttachmentAccess &)
        {
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        }

        /// Returns Vulkan stages for input-attachment reads.
        vk::PipelineStageFlags resolvePixelLocalReadStages(const PixelLocalPassAttachmentAccess &access)
        {
            vk::PipelineStageFlags stages = {};
            if (access.colorReadMask != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eFragmentShader;
            }
            return stages;
        }

        /// Returns Vulkan access flags for input-attachment reads.
        vk::AccessFlags resolvePixelLocalReadAccessFlags(const PixelLocalPassAttachmentAccess &access)
        {
            vk::AccessFlags accessFlags = {};
            if (access.colorReadMask != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eInputAttachmentRead;
            }
            return accessFlags;
        }

        /// Returns Vulkan stages for color attachment writes.
        vk::PipelineStageFlags resolvePixelLocalColorWriteStages(const PixelLocalPassAttachmentAccess &access)
        {
            vk::PipelineStageFlags stages = {};
            if (access.colorWriteMask != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
            }
            return stages;
        }

        /// Returns Vulkan access flags for color attachment writes.
        vk::AccessFlags resolvePixelLocalColorWriteAccessFlags(
            const PixelLocalPassAttachmentAccess &access,
            bool destinationAccess)
        {
            vk::AccessFlags accessFlags = {};
            if (access.colorWriteMask != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eColorAttachmentWrite;
                if (destinationAccess)
                {
                    accessFlags |= vk::AccessFlagBits::eColorAttachmentRead;
                }
            }
            return accessFlags;
        }

        /// Returns Vulkan stages for depth attachment writes.
        vk::PipelineStageFlags resolvePixelLocalDepthWriteStages(const PixelLocalPassAttachmentAccess &access)
        {
            vk::PipelineStageFlags stages = {};
            if (access.depthWrite)
            {
                stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            }
            return stages;
        }

        /// Returns Vulkan access flags for depth attachment writes.
        vk::AccessFlags resolvePixelLocalDepthWriteAccessFlags(const PixelLocalPassAttachmentAccess &access)
        {
            vk::AccessFlags accessFlags = {};
            if (access.depthWrite)
            {
                accessFlags |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }
            return accessFlags;
        }

        vk::PipelineStageFlags resolvePixelLocalWriteStages(const PixelLocalPassAttachmentAccess &access)
        {
            return resolvePixelLocalColorWriteStages(access) | resolvePixelLocalDepthWriteStages(access);
        }

        vk::AccessFlags resolvePixelLocalWriteAccessFlags(
            const PixelLocalPassAttachmentAccess &access,
            bool destinationAccess)
        {
            return resolvePixelLocalColorWriteAccessFlags(access, destinationAccess) |
                resolvePixelLocalDepthWriteAccessFlags(access);
        }

        vk::PipelineStageFlags resolvePixelLocalAllUseStages(const PixelLocalPassAttachmentAccess &access)
        {
            return resolvePixelLocalReadStages(access) |
                resolvePixelLocalColorWriteStages(access) |
                resolvePixelLocalDepthWriteStages(access);
        }

        vk::AccessFlags resolvePixelLocalAllUseAccessFlags(
            const PixelLocalPassAttachmentAccess &access,
            bool destinationAccess)
        {
            return resolvePixelLocalReadAccessFlags(access) |
                resolvePixelLocalColorWriteAccessFlags(access, destinationAccess) |
                resolvePixelLocalDepthWriteAccessFlags(access);
        }

        PixelLocalPassAttachmentAccess resolveEffectiveFirstPassLoadAccess(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const PixelLocalPassAttachmentAccess &access)
        {
            PixelLocalPassAttachmentAccess effectiveAccess = access;
            for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
            {
                if (colorAttachments[colorIndex].loadOp == LoadOp::Clear)
                {
                    effectiveAccess.colorWriteMask |= GVM::RHI::Private::makePixelLocalColorAttachmentBit(colorIndex);
                }
            }

            if (depthStencilAttachment.format != TextureFormat::Undefined &&
                depthStencilAttachment.loadOp == LoadOp::Clear)
            {
                effectiveAccess.depthWrite = true;
            }

            return effectiveAccess;
        }

        eastl::vector<PixelLocalPassAttachmentAccess> resolveEffectivePixelLocalPassAttachmentAccesses(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            eastl::vector<PixelLocalPassAttachmentAccess> effectiveAccesses = attachmentAccesses;
            if (!effectiveAccesses.empty())
            {
                effectiveAccesses.front() = resolveEffectiveFirstPassLoadAccess(
                    colorAttachments,
                    depthStencilAttachment,
                    effectiveAccesses.front());
            }

            return effectiveAccesses;
        }

        PixelLocalPassAttachmentAccess intersectTransitionHazards(
            const PixelLocalPassAttachmentAccess &sourceAccess,
            const PixelLocalPassAttachmentAccess &destinationAccess)
        {
            PixelLocalPassAttachmentAccess hazards = {};
            const uint64_t writeThenReadMask = sourceAccess.colorWriteMask & destinationAccess.colorReadMask;
            const uint64_t writeThenWriteMask = sourceAccess.colorWriteMask & destinationAccess.colorWriteMask;
            const uint64_t readThenWriteMask = sourceAccess.colorReadMask & destinationAccess.colorWriteMask;
            hazards.colorReadMask = readThenWriteMask;
            hazards.colorWriteMask = writeThenReadMask | writeThenWriteMask;
            hazards.depthWrite = sourceAccess.depthWrite && destinationAccess.depthWrite;
            return hazards;
        }

        struct PixelLocalSubpassTransitionHazard
        {
            uint32_t sourcePass = 0u;
            uint32_t destinationPass = 0u;
            PixelLocalPassAttachmentAccess hazards;
        };

        bool hasPixelLocalHazards(const PixelLocalPassAttachmentAccess &hazards)
        {
            return hazards.colorReadMask != 0u || hazards.colorWriteMask != 0u || hazards.depthWrite;
        }

        void mergeTransitionHazard(
            eastl::vector<PixelLocalSubpassTransitionHazard> &transitions,
            uint32_t sourcePass,
            uint32_t destinationPass,
            const PixelLocalPassAttachmentAccess &hazards)
        {
            if (!hasPixelLocalHazards(hazards))
            {
                return;
            }

            for (PixelLocalSubpassTransitionHazard &transition : transitions)
            {
                if (transition.sourcePass == sourcePass && transition.destinationPass == destinationPass)
                {
                    transition.hazards.colorReadMask |= hazards.colorReadMask;
                    transition.hazards.colorWriteMask |= hazards.colorWriteMask;
                    transition.hazards.depthWrite = transition.hazards.depthWrite || hazards.depthWrite;
                    return;
                }
            }

            transitions.push_back(PixelLocalSubpassTransitionHazard{
                .sourcePass = sourcePass,
                .destinationPass = destinationPass,
                .hazards = hazards,
            });
        }

        void appendColorAttachmentTransitionHazards(
            eastl::vector<PixelLocalSubpassTransitionHazard> &transitions,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses,
            uint32_t colorIndex)
        {
            constexpr uint32_t InvalidPassIndex = static_cast<uint32_t>(-1);
            uint32_t previousUsePass = InvalidPassIndex;
            for (uint32_t passIndex = 0; passIndex < attachmentAccesses.size(); ++passIndex)
            {
                const bool readsAttachment =
                    GVM::RHI::Private::isPixelLocalColorAttachmentRead(attachmentAccesses[passIndex], colorIndex);
                const bool writesAttachment =
                    GVM::RHI::Private::isPixelLocalColorAttachmentWritten(attachmentAccesses[passIndex], colorIndex);
                if (!readsAttachment && !writesAttachment)
                {
                    continue;
                }

                if (previousUsePass != InvalidPassIndex)
                {
                    PixelLocalPassAttachmentAccess hazards = intersectTransitionHazards(
                        attachmentAccesses[previousUsePass],
                        attachmentAccesses[passIndex]);
                    const uint64_t colorBit = GVM::RHI::Private::makePixelLocalColorAttachmentBit(colorIndex);
                    hazards.colorReadMask &= colorBit;
                    hazards.colorWriteMask &= colorBit;
                    hazards.depthWrite = false;
                    mergeTransitionHazard(transitions, previousUsePass, passIndex, hazards);
                }

                previousUsePass = passIndex;
            }
        }

        void appendDepthAttachmentTransitionHazards(
            eastl::vector<PixelLocalSubpassTransitionHazard> &transitions,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            constexpr uint32_t InvalidPassIndex = static_cast<uint32_t>(-1);
            uint32_t previousWritePass = InvalidPassIndex;
            for (uint32_t passIndex = 0; passIndex < attachmentAccesses.size(); ++passIndex)
            {
                if (!attachmentAccesses[passIndex].depthWrite)
                {
                    continue;
                }

                if (previousWritePass != InvalidPassIndex)
                {
                    PixelLocalPassAttachmentAccess hazards = {};
                    hazards.depthWrite = true;
                    mergeTransitionHazard(transitions, previousWritePass, passIndex, hazards);
                }

                previousWritePass = passIndex;
            }
        }

        eastl::vector<PixelLocalSubpassTransitionHazard> resolvePixelLocalSubpassTransitionHazards(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            eastl::vector<PixelLocalSubpassTransitionHazard> transitions;
            transitions.reserve(attachmentAccesses.size());

            for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
            {
                appendColorAttachmentTransitionHazards(transitions, attachmentAccesses, colorIndex);
            }

            if (depthStencilAttachment.format != TextureFormat::Undefined)
            {
                appendDepthAttachmentTransitionHazards(transitions, attachmentAccesses);
            }

            return transitions;
        }

        vk::PipelineStageFlags resolveTransitionSourceStages(
            const PixelLocalPassAttachmentAccess &sourceAccess,
            const PixelLocalPassAttachmentAccess &hazards)
        {
            vk::PipelineStageFlags stages = {};
            if ((sourceAccess.colorWriteMask & hazards.colorWriteMask) != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
            }
            if ((sourceAccess.colorReadMask & hazards.colorReadMask) != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eFragmentShader;
            }
            if (hazards.depthWrite)
            {
                stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            }
            return stages;
        }

        vk::AccessFlags resolveTransitionSourceAccessFlags(
            const PixelLocalPassAttachmentAccess &sourceAccess,
            const PixelLocalPassAttachmentAccess &hazards)
        {
            vk::AccessFlags accessFlags = {};
            if ((sourceAccess.colorWriteMask & hazards.colorWriteMask) != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eColorAttachmentWrite;
            }
            if ((sourceAccess.colorReadMask & hazards.colorReadMask) != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eInputAttachmentRead;
            }
            if (hazards.depthWrite)
            {
                accessFlags |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }
            return accessFlags;
        }

        vk::PipelineStageFlags resolveTransitionDestinationStages(
            const PixelLocalPassAttachmentAccess &destinationAccess,
            const PixelLocalPassAttachmentAccess &hazards)
        {
            vk::PipelineStageFlags stages = {};
            if ((destinationAccess.colorReadMask & hazards.colorWriteMask) != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eFragmentShader;
            }
            if ((destinationAccess.colorWriteMask & (hazards.colorWriteMask | hazards.colorReadMask)) != 0u)
            {
                stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
            }
            if (hazards.depthWrite)
            {
                stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            }
            return stages;
        }

        vk::AccessFlags resolveTransitionDestinationAccessFlags(
            const PixelLocalPassAttachmentAccess &destinationAccess,
            const PixelLocalPassAttachmentAccess &hazards)
        {
            vk::AccessFlags accessFlags = {};
            if ((destinationAccess.colorReadMask & hazards.colorWriteMask) != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eInputAttachmentRead;
            }
            if ((destinationAccess.colorWriteMask & (hazards.colorWriteMask | hazards.colorReadMask)) != 0u)
            {
                accessFlags |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            }
            if (hazards.depthWrite)
            {
                accessFlags |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }
            return accessFlags;
        }

        eastl::vector<PixelLocalPassAttachmentAccess> resolveLastUseAccessByPass(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            eastl::vector<PixelLocalPassAttachmentAccess> lastUseByPass(attachmentAccesses.size());
            if (attachmentAccesses.empty())
            {
                return lastUseByPass;
            }

            for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
            {
                for (uint32_t reverseIndex = 0; reverseIndex < attachmentAccesses.size(); ++reverseIndex)
                {
                    const uint32_t passIndex = static_cast<uint32_t>(attachmentAccesses.size() - 1u - reverseIndex);
                    const uint64_t colorBit = GVM::RHI::Private::makePixelLocalColorAttachmentBit(colorIndex);
                    const bool readsAttachment =
                        GVM::RHI::Private::isPixelLocalColorAttachmentRead(attachmentAccesses[passIndex], colorIndex);
                    const bool writesAttachment =
                        GVM::RHI::Private::isPixelLocalColorAttachmentWritten(attachmentAccesses[passIndex], colorIndex);
                    if (!readsAttachment && !writesAttachment)
                    {
                        continue;
                    }
                    if (readsAttachment)
                    {
                        lastUseByPass[passIndex].colorReadMask |= colorBit;
                    }
                    if (writesAttachment)
                    {
                        lastUseByPass[passIndex].colorWriteMask |= colorBit;
                    }
                    break;
                }
            }

            if (depthStencilAttachment.format != TextureFormat::Undefined)
            {
                for (uint32_t reverseIndex = 0; reverseIndex < attachmentAccesses.size(); ++reverseIndex)
                {
                    const uint32_t passIndex = static_cast<uint32_t>(attachmentAccesses.size() - 1u - reverseIndex);
                    if (attachmentAccesses[passIndex].depthWrite)
                    {
                        lastUseByPass[passIndex].depthWrite = true;
                        break;
                    }
                }
            }

            return lastUseByPass;
        }

        eastl::vector<vk::SubpassDependency> buildPixelLocalSubpassDependencies(
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            const uint32_t renderPassCount = static_cast<uint32_t>(attachmentAccesses.size());
            const eastl::vector<PixelLocalPassAttachmentAccess> effectiveAccesses =
                resolveEffectivePixelLocalPassAttachmentAccesses(
                    colorAttachments,
                    depthStencilAttachment,
                    attachmentAccesses);

            eastl::vector<vk::SubpassDependency> dependencies;
            dependencies.reserve(renderPassCount * 2u + 2u);

            const vk::PipelineStageFlags firstPassWriteStages = resolvePixelLocalWriteStages(effectiveAccesses.front());
            const vk::AccessFlags firstPassWriteAccessFlags = resolvePixelLocalWriteAccessFlags(effectiveAccesses.front(), true);
            if (firstPassWriteStages != vk::PipelineStageFlags{})
            {
                dependencies.push_back(vk::SubpassDependency{
                    VK_SUBPASS_EXTERNAL,
                    0u,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    firstPassWriteStages,
                    {},
                    firstPassWriteAccessFlags,
                    vk::DependencyFlagBits::eByRegion});
            }

            const eastl::vector<PixelLocalSubpassTransitionHazard> transitionHazards =
                resolvePixelLocalSubpassTransitionHazards(
                    colorAttachments,
                    depthStencilAttachment,
                    effectiveAccesses);
            for (const PixelLocalSubpassTransitionHazard &transition : transitionHazards)
            {
                const PixelLocalPassAttachmentAccess &sourceAccess = effectiveAccesses[transition.sourcePass];
                const PixelLocalPassAttachmentAccess &destinationAccess = effectiveAccesses[transition.destinationPass];
                const vk::PipelineStageFlags sourceStages = resolveTransitionSourceStages(sourceAccess, transition.hazards);
                const vk::AccessFlags sourceAccessFlags = resolveTransitionSourceAccessFlags(sourceAccess, transition.hazards);
                const vk::PipelineStageFlags destinationStages = resolveTransitionDestinationStages(destinationAccess, transition.hazards);
                const vk::AccessFlags destinationAccessFlags = resolveTransitionDestinationAccessFlags(destinationAccess, transition.hazards);

                if (sourceStages == vk::PipelineStageFlags{} || destinationStages == vk::PipelineStageFlags{})
                {
                    continue;
                }

                dependencies.push_back(vk::SubpassDependency{
                    transition.sourcePass,
                    transition.destinationPass,
                    sourceStages,
                    destinationStages,
                    sourceAccessFlags,
                    destinationAccessFlags,
                    vk::DependencyFlagBits::eByRegion});
            }

            const vk::PipelineStageFlags finalConsumerStages =
                vk::PipelineStageFlagBits::eFragmentShader |
                vk::PipelineStageFlagBits::eTransfer |
                vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const vk::AccessFlags finalConsumerAccessFlags =
                vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eTransferRead |
                vk::AccessFlagBits::eColorAttachmentRead |
                vk::AccessFlagBits::eColorAttachmentWrite;
            const eastl::vector<PixelLocalPassAttachmentAccess> lastUseByPass =
                resolveLastUseAccessByPass(colorAttachments, depthStencilAttachment, effectiveAccesses);
            for (uint32_t passIndex = 0; passIndex < renderPassCount; ++passIndex)
            {
                const vk::PipelineStageFlags sourceStages = resolvePixelLocalAllUseStages(lastUseByPass[passIndex]);
                const vk::AccessFlags sourceAccessFlags = resolvePixelLocalAllUseAccessFlags(lastUseByPass[passIndex], false);
                if (sourceStages == vk::PipelineStageFlags{} || sourceAccessFlags == vk::AccessFlags{})
                {
                    continue;
                }

                dependencies.push_back(vk::SubpassDependency{
                    passIndex,
                    VK_SUBPASS_EXTERNAL,
                    sourceStages,
                    finalConsumerStages,
                    sourceAccessFlags,
                    finalConsumerAccessFlags,
                    vk::DependencyFlagBits::eByRegion});
            }

            return dependencies;
        }

        vk::UniqueRenderPass createCachedRenderPass(
            vk::Device device,
            const eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> &colorAttachments,
            const CacheDetail::RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            uint32_t pixelLocalPassCount,
            bool pixelLocalEnabled,
            const eastl::vector<PixelLocalPassAttachmentAccess> &pixelLocalAttachmentAccesses)
        {
            eastl::vector<vk::AttachmentDescription> attachments;

            attachments.reserve(colorAttachments.size() + (depthStencilAttachment.format != TextureFormat::Undefined ? 1u : 0u));
            const uint32_t renderPassCount = pixelLocalPassCount == 0u ? 1u : pixelLocalPassCount;
            const bool hasPixelLocalPasses = renderPassCount > 1u;
            const eastl::vector<PixelLocalPassAttachmentAccess> attachmentAccesses = normalizeVulkanPixelLocalAttachmentAccesses(
                colorAttachments,
                depthStencilAttachment,
                pixelLocalEnabled,
                renderPassCount,
                pixelLocalAttachmentAccesses);
            const eastl::vector<PixelLocalPassAttachmentAccess> effectiveAttachmentAccesses =
                resolveEffectivePixelLocalPassAttachmentAccesses(
                    colorAttachments,
                    depthStencilAttachment,
                    attachmentAccesses);

            for (const CacheDetail::RenderPassColorAttachmentSignature &colorAttachment : colorAttachments)
            {
                attachments.push_back(vk::AttachmentDescription{
                    {},
                    translateTextureFormat(colorAttachment.format),
                    vk::SampleCountFlagBits::e1,
                    translateLoadOp(colorAttachment.loadOp),
                    translateStoreOp(colorAttachment.storeOp),
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    resolveAttachmentInitialLayout(colorAttachment.format, colorAttachment.loadOp),
                    resolveAttachmentLayout(colorAttachment.format)});
            }

            const bool hasDepth = depthStencilAttachment.format != TextureFormat::Undefined;
            uint32_t depthAttachmentIndex = VK_ATTACHMENT_UNUSED;
            if (hasDepth)
            {
                attachments.push_back(vk::AttachmentDescription{
                    {},
                    translateTextureFormat(depthStencilAttachment.format),
                    vk::SampleCountFlagBits::e1,
                    translateLoadOp(depthStencilAttachment.loadOp),
                    translateStoreOp(depthStencilAttachment.storeOp),
                    translateLoadOp(depthStencilAttachment.loadOp),
                    translateStoreOp(depthStencilAttachment.storeOp),
                    resolveAttachmentInitialLayout(depthStencilAttachment.format, depthStencilAttachment.loadOp),
                    resolveAttachmentLayout(depthStencilAttachment.format)});
                depthAttachmentIndex = static_cast<uint32_t>(attachments.size() - 1u);
            }

            eastl::vector<eastl::vector<vk::AttachmentReference>> colorReferencesPerPass(renderPassCount);
            eastl::vector<eastl::vector<vk::AttachmentReference>> inputReferencesPerPass(renderPassCount);
            eastl::vector<eastl::vector<uint32_t>> preserveAttachmentsPerPass(renderPassCount);
            eastl::vector<vk::AttachmentReference> depthReferences(renderPassCount);
            eastl::vector<vk::SubpassDescription> subpasses(renderPassCount);
            uint64_t colorAttachmentsWrittenBeforePass = 0u;
            for (uint32_t passIndex = 0; passIndex < renderPassCount; ++passIndex)
            {
                const PixelLocalPassAttachmentAccess &access = effectiveAttachmentAccesses[passIndex];
                auto &colorReferences = colorReferencesPerPass[passIndex];
                colorReferences.reserve(colorAttachments.size());
                appendSparseColorReferences(colorReferences, colorAttachments, access);

                if (hasDepth)
                {
                    depthReferences[passIndex].attachment = depthAttachmentIndex;
                    depthReferences[passIndex].layout = resolveDepthReferenceLayout(access);
                }

                auto &inputReferences = inputReferencesPerPass[passIndex];
                if (hasPixelLocalPasses && passIndex > 0u && access.colorReadMask != 0u)
                {
                    for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
                    {
                        if (!colorAttachments[colorIndex].pixelLocal)
                        {
                            continue;
                        }
                        if (GVM::RHI::Private::isPixelLocalColorAttachmentRead(access, colorIndex))
                        {
                            inputReferences.push_back(vk::AttachmentReference{
                                colorIndex,
                                resolveInputColorReferenceLayout(access, colorIndex)});
                        }
                    }
                }

                auto &preserveAttachments = preserveAttachmentsPerPass[passIndex];
                for (uint32_t colorIndex = 0; colorIndex < colorAttachments.size(); ++colorIndex)
                {
                    const uint64_t colorBit = GVM::RHI::Private::makePixelLocalColorAttachmentBit(colorIndex);
                    const bool writtenBeforePass = (colorAttachmentsWrittenBeforePass & colorBit) != 0u;
                    const bool usedInCurrentPass =
                        GVM::RHI::Private::isPixelLocalColorAttachmentRead(access, colorIndex) ||
                        GVM::RHI::Private::isPixelLocalColorAttachmentWritten(access, colorIndex);
                    if (writtenBeforePass && !usedInCurrentPass && GVM::RHI::Private::colorAttachmentWillBeReadBeforeNextWrite(effectiveAttachmentAccesses, passIndex + 1u, colorIndex))
                    {
                        preserveAttachments.push_back(colorIndex);
                    }
                }
                vk::SubpassDescription &subpass = subpasses[passIndex];
                subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
                subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
                subpass.pColorAttachments = colorReferences.data();
                subpass.inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
                subpass.pInputAttachments = inputReferences.data();
                subpass.pDepthStencilAttachment = hasDepth && access.depthWrite ? &depthReferences[passIndex] : nullptr;
                subpass.preserveAttachmentCount = static_cast<uint32_t>(preserveAttachments.size());
                subpass.pPreserveAttachments = preserveAttachments.data();

                colorAttachmentsWrittenBeforePass |= access.colorWriteMask;
            }

            const eastl::vector<vk::SubpassDependency> dependencies =
                buildPixelLocalSubpassDependencies(
                    colorAttachments,
                    depthStencilAttachment,
                    effectiveAttachmentAccesses);

            vk::RenderPassCreateInfo createInfo = {};
            createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
            createInfo.pSubpasses = subpasses.data();
            createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
            createInfo.pDependencies = dependencies.data();

            return device.createRenderPassUnique(createInfo);
        }

        CacheDetail::PipelineCompatibleRenderPassSignature makePipelineCompatibleRenderPassSignature(
            const RenderPipelineDescriptor &descriptor)
        {
            CacheDetail::PipelineCompatibleRenderPassSignature signature;
            signature.colorAttachments.reserve(descriptor.fragment.targets.size());

            for (const ColorTargetState &target : descriptor.fragment.targets)
            {
                signature.colorAttachments.push_back(CacheDetail::RenderPassColorAttachmentSignature{
                    .format = target.format,
                    .loadOp = LoadOp::Undefined,
                    .storeOp = StoreOp::Store,
                    .pixelLocal = target.pixelLocal,
                });
            }

            signature.depthStencilAttachment = CacheDetail::RenderPassDepthStencilAttachmentSignature{
                .format = descriptor.depthStencil.format,
                .loadOp = LoadOp::Undefined,
                .storeOp = StoreOp::Store,
                .pixelLocal = descriptor.depthStencil.pixelLocal,
            };

            signature.pixelLocalEnabled = GVM::RHI::Private::isPixelLocalRenderPipelineDescriptor(descriptor);
            if (signature.pixelLocalEnabled)
            {
                signature.pixelLocalAttachmentAccess =
                    GVM::RHI::Private::hasExplicitPixelLocalAttachmentAccess(descriptor.pixelLocalAttachmentAccess)
                        ? descriptor.pixelLocalAttachmentAccess
                        : GVM::RHI::Private::derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(descriptor);
            }

            return signature;
        }

        vk::UniqueRenderPass createPipelineCompatibleRenderPass(
            vk::Device device,
            const CacheDetail::PipelineCompatibleRenderPassSignature &signature)
        {
            eastl::vector<PixelLocalPassAttachmentAccess> attachmentAccesses;
            if (signature.pixelLocalEnabled)
            {
                attachmentAccesses.push_back(signature.pixelLocalAttachmentAccess);
            }

            return createCachedRenderPass(
                device,
                signature.colorAttachments,
                signature.depthStencilAttachment,
                1u,
                signature.pixelLocalEnabled,
                attachmentAccesses);
        }
    } // namespace

    namespace Testing
    {
        namespace
        {
            bool hasStageFlag(vk::PipelineStageFlags stages, vk::PipelineStageFlagBits flag)
            {
                return (stages & flag) != vk::PipelineStageFlags{};
            }

            bool hasAccessFlag(vk::AccessFlags accessFlags, vk::AccessFlagBits flag)
            {
                return (accessFlags & flag) != vk::AccessFlags{};
            }

            PixelLocalSubpassDependencySnapshot makeSnapshot(const vk::SubpassDependency &dependency)
            {
                PixelLocalSubpassDependencySnapshot snapshot = {};
                snapshot.sourceSubpass = dependency.srcSubpass;
                snapshot.destinationSubpass = dependency.dstSubpass;
                snapshot.sourceFragmentShaderStage =
                    hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eFragmentShader);
                snapshot.sourceColorAttachmentOutputStage =
                    hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eColorAttachmentOutput);
                snapshot.sourceEarlyFragmentTestsStage =
                    hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eEarlyFragmentTests);
                snapshot.sourceLateFragmentTestsStage =
                    hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eLateFragmentTests);
                snapshot.destinationFragmentShaderStage =
                    hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eFragmentShader);
                snapshot.destinationColorAttachmentOutputStage =
                    hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eColorAttachmentOutput);
                snapshot.destinationEarlyFragmentTestsStage =
                    hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eEarlyFragmentTests);
                snapshot.destinationLateFragmentTestsStage =
                    hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eLateFragmentTests);
                snapshot.sourceInputAttachmentReadAccess =
                    hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eInputAttachmentRead);
                snapshot.sourceColorAttachmentWriteAccess =
                    hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eColorAttachmentWrite);
                snapshot.sourceDepthStencilAttachmentWriteAccess =
                    hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eDepthStencilAttachmentWrite);
                snapshot.destinationInputAttachmentReadAccess =
                    hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eInputAttachmentRead);
                snapshot.destinationColorAttachmentReadAccess =
                    hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eColorAttachmentRead);
                snapshot.destinationColorAttachmentWriteAccess =
                    hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eColorAttachmentWrite);
                snapshot.destinationDepthStencilAttachmentWriteAccess =
                    hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eDepthStencilAttachmentWrite);
                return snapshot;
            }
        }

        eastl::vector<PixelLocalSubpassDependencySnapshot> buildPixelLocalSubpassDependenciesForTesting(
            const eastl::vector<PixelLocalDependencyColorAttachment> &colorAttachments,
            const PixelLocalDependencyDepthAttachment &depthAttachment,
            uint32_t pixelLocalPassCount,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
        {
            eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> colorSignatures;
            colorSignatures.reserve(colorAttachments.size());
            for (const PixelLocalDependencyColorAttachment &colorAttachment : colorAttachments)
            {
                colorSignatures.push_back(CacheDetail::RenderPassColorAttachmentSignature{
                    .format = colorAttachment.format,
                    .loadOp = colorAttachment.loadOp,
                    .storeOp = colorAttachment.storeOp,
                    .pixelLocal = colorAttachment.pixelLocal,
                });
            }

            CacheDetail::RenderPassDepthStencilAttachmentSignature depthSignature = {};
            depthSignature.format = depthAttachment.format;
            depthSignature.loadOp = depthAttachment.loadOp;
            depthSignature.storeOp = depthAttachment.storeOp;
            depthSignature.pixelLocal = depthAttachment.pixelLocal;

            const uint32_t renderPassCount = pixelLocalPassCount == 0u ? 1u : pixelLocalPassCount;
            const eastl::vector<PixelLocalPassAttachmentAccess> normalizedAccesses =
                normalizeVulkanPixelLocalAttachmentAccesses(
                    colorSignatures,
                    depthSignature,
                    true,
                    renderPassCount,
                    attachmentAccesses);
            const eastl::vector<vk::SubpassDependency> dependencies =
                buildPixelLocalSubpassDependencies(
                    colorSignatures,
                    depthSignature,
                    normalizedAccesses);

            eastl::vector<PixelLocalSubpassDependencySnapshot> snapshots;
            snapshots.reserve(dependencies.size());
            for (const vk::SubpassDependency &dependency : dependencies)
            {
                snapshots.push_back(makeSnapshot(dependency));
            }
            return snapshots;
        }
    } // namespace Testing

    VKRenderPassFramebufferCache::FramebufferOwner::~FramebufferOwner()
    {
        destroy();
    }

    void VKRenderPassFramebufferCache::FramebufferOwner::destroy()
    {
        if (framebuffer == vk::Framebuffer{})
        {
            return;
        }

        if (device)
        {
            device.destroyFramebuffer(framebuffer);
        }

        framebuffer = nullptr;
        device = nullptr;
    }

    void VKRenderPassFramebufferCache::init(vk::Device device)
    {
        mDevice = device;
        mPipelineRenderPassCache.clear();
        mRenderPassCache.clear();
        mFramebufferCache.clear();
        mFramebufferAccessSerial = 0u;
    }

    void VKRenderPassFramebufferCache::destroy()
    {
        eastl::vector<eastl::shared_ptr<FramebufferOwner>> retiredFramebuffers;
        retiredFramebuffers.reserve(mFramebufferCache.size());
        for (auto &cached : mFramebufferCache)
        {
            retiredFramebuffers.push_back(cached.second.owner);
        }

        mFramebufferCache.clear();
        mRenderPassCache.clear();
        mPipelineRenderPassCache.clear();
        mFramebufferAccessSerial = 0u;

        for (const eastl::shared_ptr<FramebufferOwner> &framebufferOwner : retiredFramebuffers)
        {
            if (framebufferOwner != nullptr)
            {
                framebufferOwner->destroy();
            }
        }

        mDevice = nullptr;
    }

    vk::RenderPass VKRenderPassFramebufferCache::getOrCreateRenderPass(const RenderPassDescriptor &descriptor)
    {
        eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> colorAttachments;
        colorAttachments.reserve(descriptor.colorAttachments.size());

        for (const RenderPassColorAttachment &attachment : descriptor.colorAttachments)
        {
            if (attachment.view.isNull())
            {
                throw makeInvalidArgument("VKRenderPassFramebufferCache::getOrCreateRenderPass received a null color attachment view.");
            }
            colorAttachments.push_back(CacheDetail::RenderPassColorAttachmentSignature{
                .format = resolveTextureViewFormat(attachment.view),
                .loadOp = attachment.loadOp,
                .storeOp = attachment.storeOp,
                .pixelLocal = attachment.pixelLocal,
            });
        }

        CacheDetail::RenderPassDepthStencilAttachmentSignature depthStencilAttachment = {};
        if (!descriptor.depthStencilAttachment.view.isNull())
        {
            depthStencilAttachment.format = resolveTextureViewFormat(descriptor.depthStencilAttachment.view);
            depthStencilAttachment.loadOp = descriptor.depthStencilAttachment.depthLoadOp;
            depthStencilAttachment.storeOp = descriptor.depthStencilAttachment.depthStoreOp;
            depthStencilAttachment.pixelLocal = descriptor.depthStencilAttachment.pixelLocal;
        }
        const uint32_t pixelLocalPassCount = descriptor.pixelLocal.enabled
            ? (descriptor.pixelLocal.passCount == 0u ? 1u : descriptor.pixelLocal.passCount)
            : 1u;
        const eastl::vector<PixelLocalPassAttachmentAccess> pixelLocalAttachmentAccesses = normalizeVulkanPixelLocalAttachmentAccesses(
            colorAttachments,
            depthStencilAttachment,
            descriptor.pixelLocal.enabled,
            pixelLocalPassCount,
            descriptor.pixelLocal.attachmentAccesses);
        const uint64_t renderPassHash = CacheDetail::hashRenderPassSignature(
            colorAttachments,
            depthStencilAttachment,
            pixelLocalPassCount,
            pixelLocalAttachmentAccesses);

        if (CachedRenderPass *cached = mRenderPassCache.findMatching(
                renderPassHash,
                [&](const CachedRenderPass &candidate)
                {
                    return candidate.colorAttachments == colorAttachments &&
                        candidate.depthStencilAttachment == depthStencilAttachment &&
                        candidate.pixelLocalPassCount == pixelLocalPassCount &&
                        candidate.pixelLocalAttachmentAccesses == pixelLocalAttachmentAccesses;
                }))
        {
            return cached->renderPass.get();
        }

        CachedRenderPass cached = {};
        cached.colorAttachments = colorAttachments;
        cached.depthStencilAttachment = depthStencilAttachment;
        cached.pixelLocalPassCount = pixelLocalPassCount;
        cached.pixelLocalAttachmentAccesses = pixelLocalAttachmentAccesses;
        cached.renderPass = createCachedRenderPass(
            mDevice,
            colorAttachments,
            depthStencilAttachment,
            pixelLocalPassCount,
            descriptor.pixelLocal.enabled,
            pixelLocalAttachmentAccesses);
        return mRenderPassCache.insert(renderPassHash, eastl::move(cached)).renderPass.get();
    }

    vk::RenderPass VKRenderPassFramebufferCache::getOrCreatePipelineRenderPass(const RenderPipelineDescriptor &descriptor)
    {
        const CacheDetail::PipelineCompatibleRenderPassSignature signature =
            makePipelineCompatibleRenderPassSignature(descriptor);
        const uint64_t renderPassHash = CacheDetail::hashPipelineRenderPassSignature(signature);

        if (CachedPipelineRenderPass *cached = mPipelineRenderPassCache.findMatching(
                renderPassHash,
                [&](const CachedPipelineRenderPass &candidate)
                {
                    return candidate.signature == signature;
                }))
        {
            return cached->renderPass.get();
        }

        CachedPipelineRenderPass cached = {};
        cached.signature = signature;
        cached.renderPass = createPipelineCompatibleRenderPass(mDevice, signature);
        return mPipelineRenderPassCache.insert(renderPassHash, eastl::move(cached)).renderPass.get();
    }

    VKRenderPassFramebufferCache::FramebufferHandle VKRenderPassFramebufferCache::getOrCreateFramebuffer(
        vk::RenderPass renderPass,
        const eastl::vector<vk::ImageView> &attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers)
    {
        if (renderPass == vk::RenderPass{})
        {
            throw makeInvalidArgument("VKRenderPassFramebufferCache::getOrCreateFramebuffer requires a valid render pass.");
        }
        if (attachments.empty())
        {
            throw makeInvalidArgument("VKRenderPassFramebufferCache::getOrCreateFramebuffer requires at least one attachment.");
        }
        if (width == 0 || height == 0 || layers == 0)
        {
            throw makeInvalidArgument("VKRenderPassFramebufferCache::getOrCreateFramebuffer requires non-zero extent and layer count.");
        }

        const uint64_t framebufferHash = CacheDetail::hashFramebufferSignature(renderPass, attachments, width, height, layers);

        if (FramebufferEntry *cached = mFramebufferCache.findMatching(
                framebufferHash,
                [&](const FramebufferEntry &candidate)
                {
                    return candidate.renderPass == renderPass &&
                        candidate.attachments == attachments &&
                        candidate.width == width &&
                        candidate.height == height &&
                        candidate.layers == layers;
                }))
        {
            cached->lastUsedSerial = ++mFramebufferAccessSerial;
            return {
                .framebuffer = cached->owner != nullptr ? cached->owner->framebuffer : vk::Framebuffer{},
                .owner = cached->owner,
            };
        }

        eastl::vector<eastl::shared_ptr<FramebufferOwner>> evictedFramebuffers;
        trimFramebufferCache(evictedFramebuffers);

        vk::FramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferCreateInfo.pAttachments = attachments.data();
        framebufferCreateInfo.width = width;
        framebufferCreateInfo.height = height;
        framebufferCreateInfo.layers = layers;

        FramebufferEntry cached = {};
        cached.renderPass = renderPass;
        cached.attachments = attachments;
        cached.width = width;
        cached.height = height;
        cached.layers = layers;
        cached.lastUsedSerial = ++mFramebufferAccessSerial;
        cached.owner = eastl::make_shared<FramebufferOwner>();
        cached.owner->device = mDevice;
        cached.owner->framebuffer = mDevice.createFramebuffer(framebufferCreateInfo);
        FramebufferEntry &inserted = mFramebufferCache.insert(framebufferHash, eastl::move(cached));
        return {
            .framebuffer = inserted.owner != nullptr ? inserted.owner->framebuffer : vk::Framebuffer{},
            .owner = inserted.owner,
        };
    }

    void VKRenderPassFramebufferCache::invalidateFramebufferCacheForImageView(vk::ImageView imageView)
    {
        if (imageView == vk::ImageView{})
        {
            return;
        }

        mFramebufferCache.eraseIf(
            [&](const FramebufferEntry &cached)
            {
                return eastl::find(cached.attachments.begin(), cached.attachments.end(), imageView) != cached.attachments.end();
            });
    }

    size_t VKRenderPassFramebufferCache::getFramebufferCacheSize() const
    {
        return mFramebufferCache.size();
    }

    size_t VKRenderPassFramebufferCache::getRenderPassCacheSize() const
    {
        return mRenderPassCache.size();
    }

    size_t VKRenderPassFramebufferCache::getPipelineCompatibleRenderPassCacheSize() const
    {
        return mPipelineRenderPassCache.size();
    }

    void VKRenderPassFramebufferCache::trimFramebufferCache(eastl::vector<eastl::shared_ptr<FramebufferOwner>> &evictedFramebuffers)
    {
        while (mFramebufferCache.size() >= MaxFramebufferCacheEntries)
        {
            auto oldestIt = mFramebufferCache.begin();
            if (oldestIt == mFramebufferCache.end())
            {
                break;
            }

            for (auto cachedIt = mFramebufferCache.begin(); cachedIt != mFramebufferCache.end(); ++cachedIt)
            {
                if (cachedIt->second.lastUsedSerial < oldestIt->second.lastUsedSerial)
                {
                    oldestIt = cachedIt;
                }
            }

            evictedFramebuffers.push_back(oldestIt->second.owner);
            mFramebufferCache.erase(oldestIt);
        }
    }
} // namespace GVM::RHI::Vulkan
