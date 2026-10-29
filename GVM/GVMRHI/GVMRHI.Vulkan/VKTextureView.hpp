#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKTexture.hpp"
#include "VKArgumentTable.hpp"
#include <EASTL/atomic.h>

namespace GVM::RHI::Vulkan
{
    class VKTextureView final : public TextureViewImpl
    {
    public:
        VKTextureView() = default;

        void init(VKDevice &device, VKTexture &texture, const TextureViewDescriptor &descriptor);
        void destroy() override;

        /** Reports explicit view destruction even when in-flight snapshots retain the native image view. */
        bool isDestroyRequested() const { return mDestroyRequested; }
        /** Returns the owning device for snapshot compatibility checks. */
        VKDevice &getDevice() const { return *mDevice; }
        /** Returns cached descriptor slots associated with this native view. */
        VKArgumentSlotCache &getArgumentSlots() { return mArgumentSlots; }
        /** Keeps both the native view and its parent allocation alive for one Args snapshot. */
        void retainReference();
        /** Releases one snapshot reference and completes any deferred view destruction. */
        void releaseReference();
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
        VKArgumentSlotCache mArgumentSlots;
        eastl::atomic<uint32_t> mCommandReferences{0};
        bool mDestroyRequested = false;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
