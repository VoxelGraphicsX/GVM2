#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

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

        void retainBindGroupReference();
        void releaseBindGroupReference();

        [[nodiscard]]
        bool requestUserDestroy();

        [[nodiscard]]
        uint32_t getTotalReferenceCount() const;

        [[nodiscard]]
        uint32_t getBindGroupReferenceCount() const;

        [[nodiscard]]
        bool isDestroyRequested() const;

        [[nodiscard]]
        bool isReadyForDestroy() const;

        [[nodiscard]]
        bool markPendingDestroyQueued();

        void clearPendingDestroyQueued();

    private:
        VKDevice *mDevice = nullptr;
        SamplerDescriptor mDescriptor = {};
        vk::UniqueSampler mSampler;
        VKAtomicResourceLifetimeState mLifetime = {};
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
