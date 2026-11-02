#pragma once
#include <GVMRHI/GVMRHI.hpp>

namespace GVM::Core::Private
{
    /// Describes one immutable resource access; texture accesses conservatively cover their allocation.
    struct PassResourceAccess
    {
        const void *resource = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        bool writes = false;
        /// Captures a buffer byte range without changing its resource state.
        static PassResourceAccess buffer(RHI::BufferRange range, bool writes);
        /// Captures a texture view's allocation identity; unknown identities require conservative ordering.
        static PassResourceAccess texture(RHI::TextureView view, bool writes);
        /// Reports a RAW, WAR or WAW conflict between overlapping accesses.
        bool conflicts(const PassResourceAccess &other) const;
    };

    /// Carries compiler-derived access metadata; incomplete legacy captures remain conservatively ordered.
    struct PassResourceUsage
    {
        eastl::vector<PassResourceAccess> accesses;
        bool complete = false;
    };
}
