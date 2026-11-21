#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI
{
    const TextureImpl *TextureViewImpl::getSourceTexture() const
    {
        return nullptr;
    }

    TextureViewDescriptor TextureViewImpl::getViewDescriptor() const
    {
        TextureViewDescriptor fallback;
        fallback.baseMipLevel = 0u;
        fallback.mipLevelCount = UINT32_MAX;
        fallback.baseArrayLayer = 0u;
        fallback.arrayLayerCount = UINT32_MAX;
        fallback.aspect = TextureAspect::All;
        return fallback;
    }
}
