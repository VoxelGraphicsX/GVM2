#include "VKSampler.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>

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
        if (wantsAnisotropy && device.getCapabilities().supportsSamplerAnisotropy())
        {
            samplerInfo.anisotropyEnable = vk::True;
            samplerInfo.maxAnisotropy = eastl::min(static_cast<float>(descriptor.maxAnisotropy), device.getCapabilities().getMaxSamplerAnisotropy());
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
        mDevice->setDebugObjectName(vk::ObjectType::eSampler, reinterpret_cast<uint64_t>(static_cast<VkSampler>(mSampler.get())), mLabelName.c_str());
        mDestroyed = false;
    }

    vk::Sampler VKSampler::getNativeSampler() const
    {
        return mSampler.get();
    }

    void VKSampler::setHandle(Sampler handle)
    {
        mHandle = handle;
    }

    Sampler VKSampler::getHandle() const
    {
        return mHandle;
    }

    void VKSampler::retainReference()
    {
        mLifetime.retain();
    }

    void VKSampler::releaseReference()
    {
        if (mLifetime.release("VKSampler::releaseReference"))
        {
            mDevice->getResourceRegistry().enqueueRetiredSampler(mHandle);
        }
    }

    VKUserReleaseResult VKSampler::releaseUserReference()
    {
        return mLifetime.releaseUser();
    }

    bool VKSampler::isDestroyRequested() const
    {
        return mLifetime.isUserReleased();
    }

    bool VKSampler::isDestroyable() const
    {
        return mLifetime.isDestroyable();
    }

    uint32_t VKSampler::getReferenceCount() const
    {
        return mLifetime.getReferenceCount();
    }

    void VKSampler::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        mArgumentSlots.clear();
        mSampler.reset();
        mDestroyed = true;
    }
} // namespace GVM::RHI::Vulkan
