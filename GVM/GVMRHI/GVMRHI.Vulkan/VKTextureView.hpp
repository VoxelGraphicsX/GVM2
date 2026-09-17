#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKTexture.hpp"

namespace GVM::RHI::Vulkan
{
    class VKTextureView final : public TextureViewImpl
    {
    public:
        VKTextureView() = default;

        void init(VKDevice &device, VKTexture &texture, const TextureViewDescriptor &descriptor);
        void destroy() override;

        [[nodiscard]]
        vk::ImageView getNativeImageView() const;

        [[nodiscard]]
        const TextureViewDescriptor &getDescriptor() const;

        [[nodiscard]]
        VKTexture *getTexture() const;

        [[nodiscard]]
        TextureFormat getResolvedFormat() const;

        [[nodiscard]]
        /// Returns the texture format exposed by this Vulkan texture view.
        TextureFormat getFormat() const override;

        [[nodiscard]]
        uint32_t getWidth() const override;

        [[nodiscard]]
        uint32_t getHeight() const override;

        [[nodiscard]]
        uint32_t getLayerCount() const;

    private:
        VKDevice *mDevice = nullptr;
        VKTexture *mTexture = nullptr;
        TextureViewDescriptor mDescriptor = {};
        vk::UniqueImageView mImageView;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
