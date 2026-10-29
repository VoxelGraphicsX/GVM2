#include "PixelLocalPassAccess.hpp"

#include <stdexcept>
#include <string>

namespace GVM::RHI::Private
{
    namespace
    {
        void validateNoPixelLocalReadWriteOverlap(
            const PixelLocalPassAttachmentAccess &access,
            const char *context)
        {
            if ((access.colorReadMask & access.colorWriteMask) != 0u)
            {
                throw std::invalid_argument(
                    std::string(context) +
                    " cannot read and write the same pixel-local color attachment in one phase. Insert nextPixelLocalPass() before reading a value written by an earlier task.");
            }
        }
    } // namespace

    uint64_t makePixelLocalColorAttachmentBit(uint32_t colorAttachmentIndex)
    {
        if (colorAttachmentIndex >= 64u)
        {
            throw std::invalid_argument("Pixel-local attachment access masks support up to 64 color attachments.");
        }
        return uint64_t{1u} << colorAttachmentIndex;
    }

    uint64_t makeAllPixelLocalColorAttachmentMask(uint32_t colorAttachmentCount)
    {
        if (colorAttachmentCount > 64u)
        {
            throw std::invalid_argument("Pixel-local attachment access masks support up to 64 color attachments.");
        }
        return colorAttachmentCount == 64u ? ~uint64_t{0u} : ((uint64_t{1u} << colorAttachmentCount) - 1u);
    }

    bool isPixelLocalColorAttachmentRead(
        const PixelLocalPassAttachmentAccess &access,
        uint32_t colorAttachmentIndex)
    {
        return colorAttachmentIndex < 64u &&
            ((access.colorReadMask & makePixelLocalColorAttachmentBit(colorAttachmentIndex)) != 0u);
    }

    bool isPixelLocalColorAttachmentWritten(
        const PixelLocalPassAttachmentAccess &access,
        uint32_t colorAttachmentIndex)
    {
        return colorAttachmentIndex < 64u &&
            ((access.colorWriteMask & makePixelLocalColorAttachmentBit(colorAttachmentIndex)) != 0u);
    }

    bool hasExplicitPixelLocalAttachmentAccess(const PixelLocalPassAttachmentAccess &access)
    {
        return access.colorReadMask != 0u || access.colorWriteMask != 0u || access.depthWrite;
    }

    bool isPixelLocalRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor)
    {
        if (hasExplicitPixelLocalAttachmentAccess(descriptor.pixelLocalAttachmentAccess))
        {
            return true;
        }
        if (descriptor.depthStencil.pixelLocal)
        {
            return true;
        }
        for (const ColorTargetState &target : descriptor.fragment.targets)
        {
            if (target.pixelLocal)
            {
                return true;
            }
        }
        return false;
    }

    PixelLocalPassAttachmentAccess derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor)
    {
        PixelLocalPassAttachmentAccess access = {};
        for (uint32_t colorIndex = 0u; colorIndex < descriptor.fragment.targets.size(); ++colorIndex)
        {
            const ColorTargetState &target = descriptor.fragment.targets[colorIndex];
            if (target.writeMask != ColorWriteMask::None)
            {
                access.colorWriteMask |= makePixelLocalColorAttachmentBit(colorIndex);
            }
        }
        access.depthWrite = descriptor.depthStencil.format != TextureFormat::Undefined && descriptor.depthStencil.depthWriteEnabled;
        return access;
    }

    void mergePixelLocalPassAttachmentAccess(
        PixelLocalPassAttachmentAccess &destination,
        const PixelLocalPassAttachmentAccess &source)
    {
        destination.colorReadMask |= source.colorReadMask;
        destination.colorWriteMask |= source.colorWriteMask;
        destination.depthWrite = destination.depthWrite || source.depthWrite;
    }

    void PixelLocalPassAccessBuilder::appendTask(const PixelLocalPassAttachmentAccess &access)
    {
        validateNoPixelLocalReadWriteOverlap(access, "Pixel-local task");
        ensurePass();
        mergePixelLocalPassAttachmentAccess(mPasses.back(), access);
        validateNoPixelLocalReadWriteOverlap(mPasses.back(), "Pixel-local pass");
    }

    void PixelLocalPassAccessBuilder::nextPass()
    {
        ensurePass();
        mPasses.emplace_back();
    }

    eastl::vector<PixelLocalPassAttachmentAccess> PixelLocalPassAccessBuilder::finish()
    {
        if (mPasses.empty())
        {
            mPasses.emplace_back();
        }
        return mPasses;
    }

    void PixelLocalPassAccessBuilder::ensurePass()
    {
        if (mPasses.empty())
        {
            mPasses.emplace_back();
        }
    }

    namespace
    {
        PixelLocalPassAttachmentAccess makeRenderPassCompatibilityAccess(
            uint32_t colorAttachmentCount,
            bool hasDepthAttachment)
        {
            PixelLocalPassAttachmentAccess access = {};
            access.colorWriteMask = makeAllPixelLocalColorAttachmentMask(colorAttachmentCount);
            access.depthWrite = hasDepthAttachment;
            return access;
        }
    } // namespace

    eastl::vector<PixelLocalPassAttachmentAccess> normalizePixelLocalPassAttachmentAccesses(
        uint32_t colorAttachmentCount,
        bool hasDepthAttachment,
        bool pixelLocalEnabled,
        uint32_t passCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
    {
        const uint32_t resolvedPassCount = passCount == 0u ? 1u : passCount;
        eastl::vector<PixelLocalPassAttachmentAccess> normalized;
        normalized.reserve(resolvedPassCount);

        if (pixelLocalEnabled)
        {
            if (attachmentAccesses.size() != resolvedPassCount)
            {
                throw std::invalid_argument("Pixel-local render passes require one exact attachmentAccesses entry for each declared pass.");
            }
            for (uint32_t passIndex = 0u; passIndex < resolvedPassCount; ++passIndex)
            {
                normalized.push_back(attachmentAccesses[passIndex]);
            }
            return normalized;
        }

        if (attachmentAccesses.empty())
        {
            for (uint32_t passIndex = 0u; passIndex < resolvedPassCount; ++passIndex)
            {
                normalized.push_back(makeRenderPassCompatibilityAccess(
                    colorAttachmentCount,
                    hasDepthAttachment));
            }
            return normalized;
        }

        for (uint32_t passIndex = 0u; passIndex < resolvedPassCount; ++passIndex)
        {
            normalized.push_back(passIndex < attachmentAccesses.size()
                                     ? attachmentAccesses[passIndex]
                                     : PixelLocalPassAttachmentAccess{});
        }
        return normalized;
    }

    bool colorAttachmentWillBeReadBeforeNextWrite(
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses,
        uint32_t startPassIndex,
        uint32_t colorAttachmentIndex)
    {
        for (uint32_t passIndex = startPassIndex; passIndex < attachmentAccesses.size(); ++passIndex)
        {
            const PixelLocalPassAttachmentAccess &access = attachmentAccesses[passIndex];
            if (isPixelLocalColorAttachmentRead(access, colorAttachmentIndex))
            {
                return true;
            }
            if (isPixelLocalColorAttachmentWritten(access, colorAttachmentIndex))
            {
                return false;
            }
        }
        return false;
    }

} // namespace GVM::RHI::Private
