#include "ArgumentValidation.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

#include <stdexcept>

namespace GVM::RHI::Private
{
    void validateArgumentSnapshotShape(const ArgumentLayoutDescriptor &layout, const ArgumentsDescriptor &descriptor)
    {
        if (descriptor.values.size() != layout.byteSize || descriptor.buffers.size() != layout.buffers.size() ||
            descriptor.resources.size() != layout.resources.size())
        {
            throw std::invalid_argument("Args snapshot does not match its pipeline argument layout.");
        }
    }

    uint64_t validateArgumentBufferRange(const ArgumentBufferLayout &field, BufferUsageFlags bufferUsage,
        uint64_t storageSize, const BufferRange &range)
    {
        const auto requiredUsage = field.type == BufferBindingType::Uniform ? BufferUsage::Uniform : BufferUsage::Storage;
        if ((bufferUsage & requiredUsage) == 0)
        {
            throw std::invalid_argument("Args buffer usage does not match the declared resource type.");
        }
        if (range.offset > storageSize)
        {
            throw std::out_of_range("Args buffer offset exceeds its allocation.");
        }
        const auto length = range.size == WholeSize ? storageSize - range.offset : range.size;
        if (length == 0 || length > storageSize - range.offset)
        {
            throw std::out_of_range("Args buffer range is empty or exceeds its allocation.");
        }
        if (range.offset % 4 != 0)
        {
            throw std::invalid_argument("Args buffer offsets must be aligned to four bytes.");
        }
        return length;
    }

    void validateArgumentSampler(const BindGroupLayoutEntry &entry, const ArgumentResource &resource,
        bool samplerLive, bool comparisonSampler)
    {
        if (const auto *textureView = eastl::get_if<TextureView>(&resource);
            textureView != nullptr && !textureView->isNull())
        {
            throw std::invalid_argument("Args sampler field cannot contain a texture view.");
        }
        if (!samplerLive)
        {
            throw std::invalid_argument("Args require a live sampler from the same device.");
        }
        if ((entry.sampler.type == SamplerBindingType::Comparison) != comparisonSampler)
        {
            throw std::invalid_argument("Args sampler comparison mode does not match its declared type.");
        }
    }

    void validateArgumentTextureView(const BindGroupLayoutEntry &entry, const ArgumentResource &resource,
        bool viewLive, TextureViewDimension viewDimension, TextureUsageFlags textureUsage, TextureFormat viewFormat)
    {
        if (const auto *sampler = eastl::get_if<Sampler>(&resource);
            sampler != nullptr && !sampler->isNull())
        {
            throw std::invalid_argument("Args texture field cannot contain a sampler.");
        }
        if (!viewLive)
        {
            throw std::invalid_argument("Args require a live texture view from the same device.");
        }
        const bool storage = entry.storageTexture.access != StorageTextureAccess::Undefined;
        const auto dimension = storage ? entry.storageTexture.viewDimension : entry.texture.viewDimension;
        const auto usage = storage ? TextureUsage::StorageBinding : TextureUsage::TextureBinding;
        if (viewDimension != dimension || (textureUsage & usage) == 0)
        {
            throw std::invalid_argument("Args texture usage or view dimension does not match its declared type.");
        }
        if (storage ? viewFormat != entry.storageTexture.format :
            getTextureSampleType(viewFormat) != entry.texture.sampleType)
        {
            throw std::invalid_argument("Args texture format does not match its declared type.");
        }
    }

    uint64_t validateBindGroupBufferRange(uint64_t storageSize, const BufferRange &range)
    {
        if (range.offset > storageSize)
        {
            throw std::out_of_range("Bind group buffer offset exceeds its allocation.");
        }
        const auto length = range.size == WholeSize ? storageSize - range.offset : range.size;
        if (length == 0 || length > storageSize - range.offset)
        {
            throw std::out_of_range("Bind group buffer range is empty or exceeds its allocation.");
        }
        if (range.offset % 4 != 0)
        {
            throw std::invalid_argument("Bind group buffer offsets must be aligned to four bytes.");
        }
        return length;
    }

    void requireArgumentSnapshot(bool pipelineDeclaresArguments, bool snapshotBound)
    {
        if (pipelineDeclaresArguments && !snapshotBound)
        {
            throw std::logic_error("Args pipelines require a snapshot before drawing or dispatching.");
        }
    }

} // namespace GVM::RHI::Private
