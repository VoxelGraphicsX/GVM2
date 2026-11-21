#pragma once
#include <GVMRHI/GVMRHI.hpp>

namespace GVM::Core::Private
{
    /// Describes one immutable resource access; buffers carry byte ranges and textures carry subresource ranges.
    struct PassResourceAccess
    {
        const void *resource = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        bool writes = false;
        bool isTexture = false;
        uint32_t baseMipLevel = 0;
        uint32_t mipLevelCount = 0;
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayerCount = 0;
        RHI::TextureAspectFlags aspect = RHI::TextureAspect::All;
        /// Captures a buffer byte range without changing its resource state.
        static PassResourceAccess buffer(RHI::BufferRange range, bool writes);
        /// Captures a texture view's allocation identity and subresource range; unknown identities require conservative ordering.
        static PassResourceAccess texture(RHI::TextureView view, bool writes);
        /// Captures an explicit texture subresource range without requiring a TextureView.
        static PassResourceAccess textureRange(const RHI::TextureImpl *texture, bool writes,
            uint32_t baseMipLevel = 0, uint32_t mipLevelCount = 0,
            uint32_t baseArrayLayer = 0, uint32_t arrayLayerCount = 0,
            RHI::TextureAspectFlags aspect = RHI::TextureAspect::All);
        /// Reports a RAW, WAR or WAW conflict between overlapping buffer ranges or texture subresources.
        bool conflicts(const PassResourceAccess &other) const;
        /// Reports whether this access's buffer byte interval or texture subresource range completely covers inner on the same resource.
        bool coversRange(const PassResourceAccess &inner) const;
    };

    /// Carries compiler-derived access metadata; incomplete legacy captures remain conservatively ordered.
    struct PassResourceUsage
    {
        eastl::vector<PassResourceAccess> accesses;
        bool complete = false;
    };
}
