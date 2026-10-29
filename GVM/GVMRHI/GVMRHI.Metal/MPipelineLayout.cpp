#include "MPipelineLayout.hpp"
#include "MDevice.hpp"
#include <stdexcept>

namespace GVM::RHI::Metal {

MPipelineLayout::MPipelineLayout () {}

void MPipelineLayout::init(MDevice *device, const PipelineLayoutDescriptor &descriptor)
{
    const auto &arguments = descriptor.arguments;
    if (arguments.byteSize == 0 && (!arguments.buffers.empty() || !arguments.resources.empty()))
    {
        throw std::invalid_argument("A zero-byte Metal Args layout cannot contain shader resource fields.");
    }
    if (arguments.byteSize != 0)
    {
        if (!descriptor.bindGroupLayouts.empty() || arguments.byteSize > 1024u * 1024u || arguments.byteSize % 8 != 0 ||
            device->getArgumentBuffersTier() != MTL::ArgumentBuffersTier2)
        {
            throw std::invalid_argument("Metal Args require tier 2 argument buffers and a valid bounded root layout.");
        }
        for (const auto &field : arguments.buffers)
        {
            if (field.addressOffset > arguments.byteSize || arguments.byteSize - field.addressOffset < 8 ||
                field.lengthOffset > arguments.byteSize || arguments.byteSize - field.lengthOffset < 8 ||
                field.addressOffset % 8 != 0 || field.lengthOffset % 8 != 0 || field.addressOffset == field.lengthOffset ||
                (field.type != BufferBindingType::Uniform && field.type != BufferBindingType::Storage && field.type != BufferBindingType::ReadOnlyStorage))
            {
                throw std::invalid_argument("Metal Args buffer metadata is invalid.");
            }
        }
        for (const auto &field : arguments.resources)
        {
            const auto &entry = field.entry;
            const uint32_t kinds = uint32_t(entry.sampler.type != SamplerBindingType::Undefined) +
                uint32_t(entry.texture.sampleType != TextureSampleType::Undefined) +
                uint32_t(entry.storageTexture.access != StorageTextureAccess::Undefined);
            if (field.offset > arguments.byteSize || arguments.byteSize - field.offset < 8 || field.offset % 8 != 0 ||
                entry.buffer.type != BufferBindingType::Undefined || kinds != 1 || entry.maxCount != 1)
            {
                throw std::invalid_argument("Metal Args texture or sampler metadata is invalid.");
            }
        }
    }
    this->mDevice = device;
    this->mDescriptor = descriptor;
}

} // namespace GVM::RHI::Metal
