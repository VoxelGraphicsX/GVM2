#include "VKDescriptorManager.hpp"

#include "VKBindingUtils.hpp"
#include "VKBuffer.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKSampler.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include <EASTL/string.h>

#include <cstdint>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        bool bufferSupportsBinding(const VKBuffer *buffer, BufferBindingType bindingType)
        {
            switch (bindingType)
            {
            case BufferBindingType::Uniform: return buffer->supportsUniformBinding();
            case BufferBindingType::Storage:
            case BufferBindingType::ReadOnlyStorage: return buffer->supportsStorageBinding();
            default: return false;
            }
        }

        bool textureSupportsBinding(const VKTexture *texture, const BindGroupLayoutEntry &entry)
        {
            const TextureUsageFlags usage = texture->getUsage();
            if (entry.texture.sampleType != TextureSampleType::Undefined)
            {
                return (usage & TextureUsage::TextureBinding) != 0u;
            }
            if (entry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                return (usage & TextureUsage::StorageBinding) != 0u;
            }
            return false;
        }

        bool isStorageTextureBinding(const BindGroupLayoutEntry &entry)
        {
            return entry.storageTexture.access != StorageTextureAccess::Undefined;
        }

        const char *describeTextureViewDimension(TextureViewDimension dimension)
        {
            switch (dimension)
            {
            case TextureViewDimension::Undefined: return "Undefined";
            case TextureViewDimension::e1D: return "e1D";
            case TextureViewDimension::e2D: return "e2D";
            case TextureViewDimension::e2DArray: return "e2DArray";
            case TextureViewDimension::Cube: return "Cube";
            case TextureViewDimension::CubeArray: return "CubeArray";
            case TextureViewDimension::e3D: return "e3D";
            default: return "Unknown";
            }
        }

        const char *describeTextureDimension(TextureDimension dimension)
        {
            switch (dimension)
            {
            case TextureDimension::e1D: return "e1D";
            case TextureDimension::e2D: return "e2D";
            case TextureDimension::e3D: return "e3D";
            default: return "Unknown";
            }
        }

        eastl::string describeTextureLabel(const VKTexture *texture)
        {
            if (texture == nullptr)
            {
                return "unnamed texture";
            }
            const auto &descriptor = texture->getDescriptor();
            return descriptor.label.empty() ? "unnamed texture" : eastl::string(descriptor.label.c_str());
        }

        eastl::string describeTextureState(const VKTexture *texture, const TextureViewDescriptor &viewDescriptor)
        {
            if (texture == nullptr)
            {
                return "texture=<null>";
            }

            const auto &textureDescriptor = texture->getDescriptor();
            return "textureDimension=" + eastl::string(describeTextureDimension(textureDescriptor.dimension)) +
                ", textureSize=(" + eastl::to_string(textureDescriptor.size.width) + "x" +
                eastl::to_string(textureDescriptor.size.height) + "x" +
                eastl::to_string(textureDescriptor.size.depth) + "), textureLayers=" +
                eastl::to_string(textureDescriptor.arrayLayerCount) + ", textureMipLevels=" +
                eastl::to_string(textureDescriptor.mipLevelCount) + ", viewDimension=" +
                eastl::string(describeTextureViewDimension(viewDescriptor.dimension)) +
                ", viewLayers=" + eastl::to_string(viewDescriptor.arrayLayerCount) +
                ", viewBaseLayer=" + eastl::to_string(viewDescriptor.baseArrayLayer) +
                ", viewMipLevels=" + eastl::to_string(viewDescriptor.mipLevelCount) +
                ", viewBaseMipLevel=" + eastl::to_string(viewDescriptor.baseMipLevel) +
                ", viewFormat=" + eastl::to_string(static_cast<uint32_t>(viewDescriptor.format));
        }

        TextureViewDimension getExpectedTextureViewDimension(const BindGroupLayoutEntry &entry)
        {
            if (entry.texture.sampleType != TextureSampleType::Undefined)
            {
                return entry.texture.viewDimension;
            }
            if (entry.storageTexture.access != StorageTextureAccess::Undefined)
            {
                return entry.storageTexture.viewDimension;
            }
            return TextureViewDimension::Undefined;
        }

        void validateTextureViewAgainstLayoutEntry(const VKTextureView *view, const BindGroupLayoutEntry &layoutEntry)
        {
            if (view == nullptr)
            {
                throw makeInvalidArgument("VKDescriptorManager encountered a null texture view implementation.");
            }

            const auto *texture = view->getTexture();
            if (texture == nullptr)
            {
                throw makeInvalidArgument("VKDescriptorManager encountered a texture view without a parent texture.");
            }

            const TextureViewDescriptor &actualDescriptor = view->getDescriptor();
            const TextureViewDimension actualDimension = actualDescriptor.dimension;
            const TextureViewDimension expectedDimension = getExpectedTextureViewDimension(layoutEntry);

            if (actualDimension == TextureViewDimension::e1D || texture->getDimension() == TextureDimension::e1D)
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager received texture view for texture '" + describeTextureLabel(texture) +
                    "' that resolved to an unexpected 1D texture/view. GVM does not author 1D textures, so this indicates an upstream descriptor/codegen bug before bind-group creation. Binding " +
                    eastl::to_string(layoutEntry.binding) + ", actual state: " + describeTextureState(texture, actualDescriptor) + ".");
            }

            if (expectedDimension != TextureViewDimension::Undefined && actualDimension != expectedDimension)
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager binding " + eastl::to_string(layoutEntry.binding) +
                    " expected texture view dimension " + describeTextureViewDimension(expectedDimension) +
                    " but received " + describeTextureViewDimension(actualDimension) +
                    " for texture '" + describeTextureLabel(texture) + "'. Actual state: " +
                    describeTextureState(texture, actualDescriptor) + ".");
            }

            if (actualDimension == TextureViewDimension::e2DArray && actualDescriptor.arrayLayerCount <= 1u)
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager binding " + eastl::to_string(layoutEntry.binding) +
                    " received a 2D-array texture view with arrayLayerCount <= 1 for texture '" +
                    describeTextureLabel(texture) + "'. Actual state: " +
                    describeTextureState(texture, actualDescriptor) + ".");
            }

            if (actualDimension == TextureViewDimension::Cube && actualDescriptor.arrayLayerCount != 6u)
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager binding " + eastl::to_string(layoutEntry.binding) +
                    " received a cube texture view with arrayLayerCount != 6 for texture '" +
                    describeTextureLabel(texture) + "'. Actual state: " +
                    describeTextureState(texture, actualDescriptor) + ".");
            }

            if (actualDimension == TextureViewDimension::CubeArray &&
                (actualDescriptor.arrayLayerCount < 6u || (actualDescriptor.arrayLayerCount % 6u) != 0u))
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager binding " + eastl::to_string(layoutEntry.binding) +
                    " received a cube-array texture view whose layer count is not a positive multiple of 6 for texture '" +
                    describeTextureLabel(texture) + "'. Actual state: " +
                    describeTextureState(texture, actualDescriptor) + ".");
            }

            if (layoutEntry.storageTexture.access != StorageTextureAccess::Undefined &&
                view->getResolvedFormat() != layoutEntry.storageTexture.format)
            {
                throw makeInvalidArgument(
                    "VKDescriptorManager binding " + eastl::to_string(layoutEntry.binding) +
                    " expected storage texture format enum " + eastl::to_string(static_cast<uint32_t>(layoutEntry.storageTexture.format)) +
                    " but received format enum " + eastl::to_string(static_cast<uint32_t>(view->getResolvedFormat())) +
                    " for texture '" + describeTextureLabel(texture) + "'. Actual state: " +
                    describeTextureState(texture, actualDescriptor) + ".");
            }
        }
    } // namespace

    VKDescriptorManager::BindGroupWriteResult VKDescriptorManager::writeBindGroupDescriptorSet(
        VKDevice *device,
        vk::DescriptorSet descriptorSet,
        const eastl::vector<VKBindGroup::ResolvedBinding> &bindings)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKDescriptorManager::writeBindGroupDescriptorSet requires a valid device.");
        }
        if (descriptorSet == vk::DescriptorSet{})
        {
            throw makeInvalidArgument("VKDescriptorManager::writeBindGroupDescriptorSet requires a valid descriptor set.");
        }

        BindGroupWriteResult result = {};
        eastl::vector<vk::WriteDescriptorSet> writes;
        eastl::vector<eastl::vector<vk::DescriptorBufferInfo>> bufferInfos;
        eastl::vector<eastl::vector<vk::DescriptorImageInfo>> imageInfos;
        writes.reserve(bindings.size());
        bufferInfos.reserve(bindings.size());
        imageInfos.reserve(bindings.size());

        for (const VKBindGroup::ResolvedBinding &binding : bindings)
        {
            vk::WriteDescriptorSet write = {};
            write.dstSet = descriptorSet;
            write.dstBinding = binding.layoutEntry.binding;
            write.descriptorType = translateDescriptorType(binding.layoutEntry);

            const auto bindingKind = Detail::getBindGroupLayoutEntryKind(binding.layoutEntry);
            switch (bindingKind)
            {
            case Detail::BindGroupEntryKind::Buffer:
            {
                auto &infos = bufferInfos.emplace_back();
                infos.reserve(binding.descriptorEntry.buffer.size());
                for (const BufferRange &bufferRange : binding.descriptorEntry.buffer)
                {
                    if (bufferRange.buffer.isNull())
                    {
                        throw makeInvalidArgument("VKDescriptorManager encountered a null buffer binding.");
                    }
                    auto *buffer = static_cast<VKBuffer *>(bufferRange.buffer.get());
                    if (buffer == nullptr || !bufferSupportsBinding(buffer, binding.layoutEntry.buffer.type))
                    {
                        throw makeInvalidArgument("VKDescriptorManager received a buffer incompatible with its layout binding.");
                    }

                    infos.push_back(vk::DescriptorBufferInfo{
                        buffer->getNativeBuffer(),
                        bufferRange.offset,
                        bufferRange.size == WholeSize ? VK_WHOLE_SIZE : bufferRange.size});
                }
                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pBufferInfo = infos.data();
                break;
            }
            case Detail::BindGroupEntryKind::Sampler:
            {
                auto &infos = imageInfos.emplace_back();
                infos.reserve(binding.descriptorEntry.sampler.size());
                for (const Sampler &samplerHandle : binding.descriptorEntry.sampler)
                {
                    if (samplerHandle.isNull())
                    {
                        throw makeInvalidArgument("VKDescriptorManager encountered a null sampler binding.");
                    }
                    auto *sampler = static_cast<VKSampler *>(samplerHandle.get());
                    infos.push_back(vk::DescriptorImageInfo{
                        sampler->getNativeSampler(),
                        nullptr,
                        vk::ImageLayout::eUndefined});
                }
                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pImageInfo = infos.data();
                break;
            }
            case Detail::BindGroupEntryKind::TextureView:
            {
                auto &infos = imageInfos.emplace_back();
                infos.reserve(binding.descriptorEntry.textureView.size());
                for (const TextureView &viewHandle : binding.descriptorEntry.textureView)
                {
                    if (viewHandle.isNull())
                    {
                        throw makeInvalidArgument("VKDescriptorManager encountered a null texture view binding.");
                    }
                    auto *view = static_cast<VKTextureView *>(viewHandle.get());
                    auto *texture = view->getTexture();
                    if (texture == nullptr || !textureSupportsBinding(texture, binding.layoutEntry))
                    {
                        throw makeInvalidArgument("VKDescriptorManager received a texture view incompatible with its layout binding.");
                    }
                    validateTextureViewAgainstLayoutEntry(view, binding.layoutEntry);

                    vk::ImageLayout descriptorImageLayout = isStorageTextureBinding(binding.layoutEntry)
                        ? vk::ImageLayout::eGeneral
                        : vk::ImageLayout::eShaderReadOnlyOptimal;

                    infos.push_back(vk::DescriptorImageInfo{
                        nullptr,
                        view->getNativeImageView(),
                        descriptorImageLayout});
                }
                write.descriptorCount = static_cast<uint32_t>(infos.size());
                write.pImageInfo = infos.data();
                break;
            }
            default:
                throw makeInvalidArgument("VKDescriptorManager encountered an unsupported binding kind.");
            }

            writes.push_back(write);
        }

        device->getNativeDevice().updateDescriptorSets(writes, {});
        result.writeCount = writes.size();
        return result;
    }
} // namespace GVM::RHI::Vulkan
