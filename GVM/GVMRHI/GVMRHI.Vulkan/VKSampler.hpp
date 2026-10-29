#pragma once

#include "VKCommon.hpp"
#include "VKResourceLifetime.hpp"
#include "VKDefines.hpp"
#include "VKArgumentTable.hpp"

namespace GVM::RHI::Vulkan
{
    class VKSampler final : public SamplerImpl
    {
    public:
        VKSampler() = default;

        void init(VKDevice &device, const SamplerDescriptor &descriptor);
        void destroy();

        [[nodiscard]]
        vk::Sampler getNativeSampler() const;

        /** Returns the logical device that owns this sampler. */
        VKDevice &getDevice() const { return *mDevice; }
        /** Returns immutable sampler properties captured during creation. */
        const SamplerDescriptor &getDescriptor() const { return mDescriptor; }
        /** Returns cached descriptor slots associated with this native sampler. */
        VKArgumentSlotCache &getArgumentSlots() { return mArgumentSlots; }
        /** Retains the native sampler until its last Args snapshot is released. */
        /** Stores the pool handle issued by the device right after allocation; used to enqueue this resource for destruction when its last reference is released. */
        void setHandle(Sampler handle);
        /** Returns the pool handle issued at creation. */
        [[nodiscard]] Sampler getHandle() const;
        /** Adds one holder reference keeping this resource alive past free*() until GPU work completes. */
        void retainReference();
        /** Drops one holder reference; enqueues the resource for destruction when the last one is released after free*(). */
        void releaseReference();
        /** Releases the user handle's reference exactly once; reports whether destruction may proceed. */
        [[nodiscard]] VKUserReleaseResult releaseUserReference();
        /** Reports whether free*() has been called on this resource. */
        [[nodiscard]] bool isDestroyRequested() const;
        /** Reports whether the user handle was released and no holder references remain. */
        [[nodiscard]] bool isDestroyable() const;
        /** Returns the live reference count, including the user handle's while it is held. */
        [[nodiscard]] uint32_t getReferenceCount() const;

    private:
        VKDevice *mDevice = nullptr;
        SamplerDescriptor mDescriptor = {};
        vk::UniqueSampler mSampler;
        VKArgumentSlotCache mArgumentSlots;
        VKResourceLifetime mLifetime = {};
        Sampler mHandle = {};
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
