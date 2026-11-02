#include "PassResourceAccess.hpp"

namespace GVM::Core::Private
{
    PassResourceAccess PassResourceAccess::buffer(RHI::BufferRange range, bool writes)
    {
        return {range.buffer.get(), range.offset, range.size, writes};
    }

    PassResourceAccess PassResourceAccess::texture(RHI::TextureView view, bool writes)
    {
        return {view.isNull() ? nullptr : view->getSourceTexture(), 0, UINT64_MAX, writes};
    }

    bool PassResourceAccess::conflicts(const PassResourceAccess &other) const
    {
        if (resource != other.resource || (!writes && !other.writes) || size == 0 || other.size == 0)
        {
            return false;
        }
        return offset <= other.offset ? other.offset - offset < size : offset - other.offset < other.size;
    }
}
