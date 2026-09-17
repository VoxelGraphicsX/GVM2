#include "MSampler.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
namespace GVM::RHI::Metal
{

    MSampler::MSampler()
    {
    }

    MSampler::~MSampler()
    {
        if (mNativeSampler != nullptr)
        {
            mNativeSampler->release();
            mNativeSampler = nullptr;
        }
    }

    void MSampler::init(MDevice *device, const SamplerDescriptor &descriptor)
    {
        this->mDevice = device;
        MTL::SamplerDescriptor *samplerDescriptor = MTL::SamplerDescriptor::alloc()->init();
        // samplerDescriptor->autorelease();
        samplerDescriptor->setMinFilter(translateSamplerFilterToMTL(descriptor.minFilter));
        samplerDescriptor->setMagFilter(translateSamplerFilterToMTL(descriptor.magFilter));
        samplerDescriptor->setMipFilter(translateSamplerMipFilterToMTL(descriptor.mipmapFilter));
        samplerDescriptor->setSAddressMode(translateAddressModeToMTL(descriptor.addressModeU));
        samplerDescriptor->setTAddressMode(translateAddressModeToMTL(descriptor.addressModeV));
        samplerDescriptor->setRAddressMode(translateAddressModeToMTL(descriptor.addressModeW));
        samplerDescriptor->setLodMinClamp(descriptor.lodMinClamp);
        samplerDescriptor->setLodMaxClamp(descriptor.lodMaxClamp);
        if (descriptor.maxAnisotropy > 0)
        {
            samplerDescriptor->setMaxAnisotropy(descriptor.maxAnisotropy);
        }
        samplerDescriptor->setSupportArgumentBuffers(true);
        // samplerDescriptor->setCompareFunction(translateCompareFunctionToMTL(descriptor.compare));
        this->mNativeSampler = device->getNativeDevice()->newSamplerState(samplerDescriptor);
        samplerDescriptor->release();
    }

    MTL::SamplerState *MSampler::getNativeSampler() const
    {
        return this->mNativeSampler;
    }

} // namespace GVM::RHI::Metal
