#include "MBindGroupLayout.hpp"
#include "MEnumUtils.hpp"
namespace GVM::RHI::Metal
{

    MBindGroupLayout::MBindGroupLayout() {}

    MBindGroupLayout::~MBindGroupLayout()
    {
    }

    void MBindGroupLayout::init(MDevice *device, const BindGroupLayoutDescriptor &descriptor)
    {
        this->mDevice = device;
        this->mDescriptor = descriptor;

        for (int entryIndex = 0; entryIndex < descriptor.entries.size(); entryIndex++)
        {
            const auto &entry = descriptor.entries[entryIndex];
            if (entry.sampler.type != SamplerBindingType::Undefined)
            {
                mResourceUsages.push_back(MTL::ResourceUsageSample);
            }
            else if (entry.texture.sampleType != TextureSampleType::Undefined)
            {
                mResourceUsages.push_back(MTL::ResourceUsageSample);
            }
            else if (entry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                mResourceUsages.push_back(translateStorageTextureAccessToMTL(entry.storageTexture.access));
            }
            else if (entry.buffer.type != BufferBindingType::Undefined)
            {
                mResourceUsages.push_back(translateStorageBufferAccessToMTL(entry.buffer.access));
            }
            else
            {
                throw std::runtime_error("Unsupported bind group binding type");
            }
        }
    }

    const BindGroupLayoutDescriptor &MBindGroupLayout::getDescriptor() const
    {
        return this->mDescriptor;
    }

    const eastl::vector<MTL::ResourceUsage> &MBindGroupLayout::getResourceUsages() const
    {
        return this->mResourceUsages;
    }

} // namespace GVM::RHI::Metal
