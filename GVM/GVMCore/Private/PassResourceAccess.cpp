#include "PassResourceAccess.hpp"

namespace GVM::Core::Private
{
    PassResourceAccess PassResourceAccess::buffer(RHI::BufferRange range, bool writes)
    {
        return {range.buffer.get(), range.offset, range.size, writes};
    }

    PassResourceAccess PassResourceAccess::texture(RHI::TextureView view, bool writes)
    {
        if (view.isNull())
        {
            return {nullptr, 0, UINT64_MAX, writes, true};
        }
        const auto desc = view->getViewDescriptor();
        return textureRange(
            view->getSourceTexture(),
            writes,
            desc.baseMipLevel,
            desc.mipLevelCount,
            desc.baseArrayLayer,
            desc.arrayLayerCount,
            desc.aspect);
    }

    PassResourceAccess PassResourceAccess::textureRange(const RHI::TextureImpl *texture, bool writes,
        uint32_t baseMipLevel, uint32_t mipLevelCount,
        uint32_t baseArrayLayer, uint32_t arrayLayerCount,
        RHI::TextureAspectFlags aspect)
    {
        return {
            texture,
            0,
            UINT64_MAX,
            writes,
            true,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount,
            aspect,
        };
    }

    bool PassResourceAccess::conflicts(const PassResourceAccess &other) const
    {
        if (resource != other.resource || (!writes && !other.writes))
        {
            return false;
        }
        if (isTexture || other.isTexture)
        {
            if (isTexture != other.isTexture)
            {
                return true;
            }
            const auto rangesOverlap = [](uint32_t aBase, uint32_t aCount, uint32_t bBase, uint32_t bCount) -> bool
            {
                const uint64_t aEnd = aCount == 0 ? UINT64_MAX : static_cast<uint64_t>(aBase) + aCount;
                const uint64_t bEnd = bCount == 0 ? UINT64_MAX : static_cast<uint64_t>(bBase) + bCount;
                return static_cast<uint64_t>(aBase) < bEnd && static_cast<uint64_t>(bBase) < aEnd;
            };
            const auto aspectMatches = [](RHI::TextureAspectFlags lhs, RHI::TextureAspectFlags rhs) -> bool
            {
                return lhs == RHI::TextureAspect::All || rhs == RHI::TextureAspect::All || (lhs & rhs) != 0;
            };
            return aspectMatches(aspect, other.aspect) &&
                rangesOverlap(baseMipLevel, mipLevelCount, other.baseMipLevel, other.mipLevelCount) &&
                rangesOverlap(baseArrayLayer, arrayLayerCount, other.baseArrayLayer, other.arrayLayerCount);
        }
        if (size == 0 || other.size == 0)
        {
            return false;
        }
        return offset <= other.offset ? other.offset - offset < size : offset - other.offset < other.size;
    }

    namespace
    {
        /// Reports whether [outerBase, outerBase + outerCount) completely contains [innerBase, innerBase + innerCount).
        bool textureSubrangeCovers(uint32_t outerBase, uint32_t outerCount, uint32_t innerBase, uint32_t innerCount)
        {
            const uint64_t outerEnd = outerCount == 0 ? UINT64_MAX : static_cast<uint64_t>(outerBase) + outerCount;
            const uint64_t innerEnd = innerCount == 0 ? UINT64_MAX : static_cast<uint64_t>(innerBase) + innerCount;
            return outerBase <= innerBase && outerEnd >= innerEnd;
        }
    } // namespace

    bool PassResourceAccess::coversRange(const PassResourceAccess &inner) const
    {
        if (resource == nullptr || resource != inner.resource)
        {
            return false;
        }
        if (isTexture || inner.isTexture)
        {
            if (!isTexture || !inner.isTexture)
            {
                return false;
            }
            const bool aspectCovers = aspect == RHI::TextureAspect::All ||
                (inner.aspect != RHI::TextureAspect::All && (aspect & inner.aspect) == inner.aspect);
            if (!aspectCovers)
            {
                return false;
            }
            return textureSubrangeCovers(baseMipLevel, mipLevelCount, inner.baseMipLevel, inner.mipLevelCount) &&
                textureSubrangeCovers(baseArrayLayer, arrayLayerCount, inner.baseArrayLayer, inner.arrayLayerCount);
        }
        if (size == 0 || inner.size == 0 || offset > inner.offset)
        {
            return false;
        }
        const uint64_t delta = inner.offset - offset;
        return delta <= size && inner.size <= size - delta;
    }
}
