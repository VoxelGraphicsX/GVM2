#include "VKSampler.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"

#include <EASTL/algorithm.h>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    void VKSampler::init(VKDevice &device, const SamplerDescriptor &descriptor)
    {
        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;

        vk::SamplerCreateInfo samplerInfo = {};
        samplerInfo.magFilter = translateFilter(descriptor.magFilter);
        samplerInfo.minFilter = translateFilter(descriptor.minFilter);
        samplerInfo.mipmapMode = translateMipmapFilter(descriptor.mipmapFilter);
        samplerInfo.addressModeU = translateAddressMode(descriptor.addressModeU);
        samplerInfo.addressModeV = translateAddressMode(descriptor.addressModeV);
        samplerInfo.addressModeW = translateAddressMode(descriptor.addressModeW);
        samplerInfo.mipLodBias = 0.0f;
        const bool wantsAnisotropy = descriptor.maxAnisotropy > 1;
        if (wantsAnisotropy && device.supportsSamplerAnisotropy())
        {
            samplerInfo.anisotropyEnable = vk::True;
            samplerInfo.maxAnisotropy = eastl::min(static_cast<float>(descriptor.maxAnisotropy), device.getMaxSamplerAnisotropy());
        }
        else
        {
            samplerInfo.anisotropyEnable = vk::False;
            samplerInfo.maxAnisotropy = 1.0f;
        }
        samplerInfo.compareEnable = descriptor.compare != CompareFunction::Undefined ? vk::True : vk::False;
        samplerInfo.compareOp = translateCompareFunction(descriptor.compare);
        samplerInfo.minLod = descriptor.lodMinClamp;
        samplerInfo.maxLod = descriptor.lodMaxClamp;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = vk::False;

        mSampler = device.getNativeDevice().createSamplerUnique(samplerInfo);
        mLifetime.ownerReferences.store(1u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(false, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mDestroyed = false;
    }

    vk::Sampler VKSampler::getNativeSampler() const
    {
        return mSampler.get();
    }

    void VKSampler::retainBindGroupReference()
    {
        incrementAtomicReference(mLifetime.bindGroupReferences);
        incrementAtomicReference(mLifetime.ownerReferences);
    }

    void VKSampler::releaseBindGroupReference()
    {
        decrementAtomicReference(mLifetime.bindGroupReferences, "VKSampler::releaseBindGroupReference");
        decrementAtomicReference(mLifetime.ownerReferences, "VKSampler::releaseBindGroupReference");
    }

    bool VKSampler::requestUserDestroy()
    {
        bool expected = false;
        if (!mLifetime.destroyRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }
        decrementAtomicReference(mLifetime.ownerReferences, "VKSampler::requestUserDestroy");
        return true;
    }

    uint32_t VKSampler::getTotalReferenceCount() const
    {
        return mLifetime.ownerReferences.load(std::memory_order_acquire);
    }

    uint32_t VKSampler::getBindGroupReferenceCount() const
    {
        return mLifetime.bindGroupReferences.load(std::memory_order_acquire);
    }

    bool VKSampler::isDestroyRequested() const
    {
        return mLifetime.destroyRequested.load(std::memory_order_acquire);
    }

    bool VKSampler::isReadyForDestroy() const
    {
        return isDestroyRequested() && getTotalReferenceCount() == 0u;
    }

    bool VKSampler::markPendingDestroyQueued()
    {
        bool expected = false;
        return mLifetime.pendingDestroyQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void VKSampler::clearPendingDestroyQueued()
    {
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
    }

    void VKSampler::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        mSampler.reset();
        mLifetime.ownerReferences.store(0u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(true, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mDestroyed = true;
    }
} // namespace GVM::RHI::Vulkan
