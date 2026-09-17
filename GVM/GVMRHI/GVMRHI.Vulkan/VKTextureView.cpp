#include "VKTextureView.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKTexture.hpp"

#include <EASTL/algorithm.h>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKTextureView::init(VKDevice &device, VKTexture &texture, const TextureViewDescriptor &descriptor)
    {
        mDevice = &device;
        mTexture = &texture;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;

        vk::ImageViewCreateInfo viewInfo = {};
        viewInfo.image = texture.getNativeImage();
        viewInfo.viewType = translateTextureViewDimension(descriptor.dimension);
        viewInfo.format = translateTextureFormat(descriptor.format);
        viewInfo.components = vk::ComponentMapping{};
        viewInfo.subresourceRange = texture.buildSubresourceRange(
            descriptor.aspect,
            descriptor.baseMipLevel,
            descriptor.mipLevelCount,
            descriptor.baseArrayLayer,
            descriptor.arrayLayerCount);

        mImageView = device.getNativeDevice().createImageViewUnique(viewInfo);
    }

    vk::ImageView VKTextureView::getNativeImageView() const
    {
        return mImageView.get();
    }

    const TextureViewDescriptor &VKTextureView::getDescriptor() const
    {
        return mDescriptor;
    }

    VKTexture *VKTextureView::getTexture() const
    {
        return mTexture;
    }

    TextureFormat VKTextureView::getResolvedFormat() const
    {
        return mDescriptor.format;
    }

    TextureFormat VKTextureView::getFormat() const
    {
        return getResolvedFormat();
    }

    uint32_t VKTextureView::getWidth() const
    {
        return eastl::max(1u, mTexture->getWidth() >> mDescriptor.baseMipLevel);
    }

    uint32_t VKTextureView::getHeight() const
    {
        return mTexture->getDimension() == TextureDimension::e1D ? 1u : eastl::max(1u, mTexture->getHeight() >> mDescriptor.baseMipLevel);
    }

    uint32_t VKTextureView::getLayerCount() const
    {
        return mTexture->is3D() ? 1u : mDescriptor.arrayLayerCount;
    }

    void VKTextureView::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        mImageView.reset();
        mDestroyed = true;
    }
} // namespace GVM::RHI::Vulkan
