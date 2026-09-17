#include "VKShaderModule.hpp"

#include "VKDevice.hpp"

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        bool hasUnsupportedGeometryStage(const Detail::ReflectedShaderModule &reflection)
        {
            for (const Detail::ReflectedEntryPoint &entryPoint : reflection.entryPoints)
            {
                if (entryPoint.stage == vk::ShaderStageFlagBits::eGeometry)
                {
                    return true;
                }
            }
            return false;
        }

        bool isPreRasterStorageRestrictedStage(vk::ShaderStageFlagBits stage)
        {
            switch (stage)
            {
            case vk::ShaderStageFlagBits::eVertex:
            case vk::ShaderStageFlagBits::eTessellationControl:
            case vk::ShaderStageFlagBits::eTessellationEvaluation:
            case vk::ShaderStageFlagBits::eGeometry:
                return true;
            default:
                return false;
            }
        }
    } // namespace

    void VKShaderModule::init(VKDevice &device, const ShaderModuleDescriptor &descriptor)
    {
        if (descriptor.spirv.empty())
        {
            const eastl::string shaderLabel = descriptor.label.empty() ? eastl::string("unnamed shader module") : eastl::string(descriptor.label.c_str());
            throw makeInvalidArgument(
                "VKShaderModule::init requires descriptor.spirv for the Vulkan backend, but shader module '" +
                shaderLabel + "' did not provide SPIR-V.");
        }

        mDevice = &device;
        mDescriptor = descriptor;
        mReflection = Detail::reflectShaderModule(descriptor.spirv);
        mLabelName = descriptor.label;
        const eastl::string shaderLabel = descriptor.label.empty() ? eastl::string("unnamed shader module") : eastl::string(descriptor.label.c_str());

        if (hasUnsupportedGeometryStage(mReflection))
        {
            throw makeInvalidArgument(
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' containing a geometry shader entry point, but the current GVM Vulkan backend does not support geometry shader stages.");
        }

        if (mReflection.usesTessellationCapability && !device.supportsTessellationShaderFeature())
        {
            throw makeInvalidArgument(
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring the Vulkan tessellationShader feature bit, but the selected Vulkan device does not support tessellationShader.");
        }

        if (mReflection.usesFloat16Capability && !device.supportsShaderFloat16())
        {
            throw makeInvalidArgument(
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring the Vulkan shaderFloat16 feature bit, but the selected Vulkan device does not support shaderFloat16.");
        }

        Detail::validate16BitStorageRequirements(mReflection, device.getEnabled16BitStorageFeatures(), shaderLabel);
        Detail::validateImageAndSubgroupRequirements(mReflection, device.getEnabledShaderFeatures(), device.getSubgroupProperties(),
                                                   device.supportsShaderSubgroupExtendedTypes(), shaderLabel);

        if ((mReflection.usesFragmentShaderBarycentricCapability || mReflection.usesFragmentShaderBarycentricExtension) &&
            !device.supportsFragmentShaderBarycentric())
        {
            throw makeInvalidArgument(
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring fragment shader barycentrics, but the selected Vulkan device does not support VK_KHR_fragment_shader_barycentric.");
        }

        if (!device.supportsVertexPipelineStoresAndAtomics())
        {
            for (const Detail::ReflectedEntryPoint &entryPoint : mReflection.entryPoints)
            {
                if (!isPreRasterStorageRestrictedStage(entryPoint.stage))
                {
                    continue;
                }

                for (const Detail::ReflectedDescriptorBinding &binding : entryPoint.descriptorBindings)
                {
                    if (binding.descriptorType == vk::DescriptorType::eStorageBuffer &&
                        binding.storageBufferAccess != StorageBufferAccess::ReadOnly)
                    {
                        throw makeInvalidArgument(
                            "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                            "' with a pre-raster shader entry point writing to a storage buffer, but the selected Vulkan device does not support vertexPipelineStoresAndAtomics.");
                    }
                    if (binding.descriptorType == vk::DescriptorType::eStorageImage &&
                        binding.storageTextureAccess != StorageTextureAccess::ReadOnly)
                    {
                        throw makeInvalidArgument(
                            "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                            "' with a pre-raster shader entry point writing to a storage image, but the selected Vulkan device does not support vertexPipelineStoresAndAtomics.");
                    }
                }
            }
        }

        vk::ShaderModuleCreateInfo createInfo = {};
        createInfo.codeSize = descriptor.spirv.size() * sizeof(uint32_t);
        createInfo.pCode = descriptor.spirv.data();

        mShaderModule = device.getNativeDevice().createShaderModuleUnique(createInfo);
    }

    vk::ShaderModule VKShaderModule::getNativeShaderModule() const
    {
        return mShaderModule.get();
    }

    const ShaderModuleDescriptor &VKShaderModule::getDescriptor() const
    {
        return mDescriptor;
    }

    const Detail::ReflectedShaderModule &VKShaderModule::getReflection() const
    {
        return mReflection;
    }
} // namespace GVM::RHI::Vulkan
