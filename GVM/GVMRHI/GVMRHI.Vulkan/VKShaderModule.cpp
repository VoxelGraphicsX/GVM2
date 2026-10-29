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

    } // namespace

    void VKShaderModule::init(VKDevice &device, const ShaderModuleDescriptor &descriptor)
    {
        if (descriptor.spirv.empty())
        {
            const eastl::string shaderLabel = descriptor.label.empty() ? eastl::string("unnamed shader module") : eastl::string(descriptor.label.c_str());
            throw std::invalid_argument((
                "VKShaderModule::init requires descriptor.spirv for the Vulkan backend, but shader module '" +
                shaderLabel + "' did not provide SPIR-V.").c_str());
        }

        mDevice = &device;
        mDescriptor.label = descriptor.label;
        mDescriptor.code = descriptor.code;
        mDescriptor.spirv = descriptor.spirv;
        mReflection = Detail::reflectShaderModule(descriptor.spirv);
        mLabelName = descriptor.label;
        const eastl::string &shaderLabel = mLabelName;

        if (hasUnsupportedGeometryStage(mReflection))
        {
            throw std::invalid_argument((
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' containing a geometry shader entry point, but the current GVM Vulkan backend does not support geometry shader stages.").c_str());
        }

        if (mReflection.usesTessellationCapability && !device.getCapabilities().supportsTessellationShaderFeature())
        {
            throw std::invalid_argument((
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring the Vulkan tessellationShader feature bit, but the selected Vulkan device does not support tessellationShader.").c_str());
        }

        if (mReflection.usesFloat16Capability && !device.getCapabilities().supportsShaderFloat16())
        {
            throw std::invalid_argument((
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring the Vulkan shaderFloat16 feature bit, but the selected Vulkan device does not support shaderFloat16.").c_str());
        }

        if (mReflection.usesStorageImageArrayDynamicIndexing && !device.getCapabilities().getEnabledShaderFeatures().shaderStorageImageArrayDynamicIndexing)
        {
            throw std::invalid_argument("Vulkan shader module requires shaderStorageImageArrayDynamicIndexing.");
        }
        if (mReflection.usesStorageImageArrayNonUniformIndexing && !device.getCapabilities().supportsStorageImageArrayNonUniformIndexing())
        {
            throw std::invalid_argument("Vulkan shader module requires shaderStorageImageArrayNonUniformIndexing.");
        }
        Detail::validate16BitStorageRequirements(mReflection, device.getCapabilities().getEnabled16BitStorageFeatures(), shaderLabel);
        Detail::validateImageAndSubgroupRequirements(mReflection, device.getCapabilities().getEnabledShaderFeatures(), device.getCapabilities().getSubgroupProperties(),
                                                   device.getCapabilities().supportsShaderSubgroupExtendedTypes(), shaderLabel);

        if ((mReflection.usesFragmentShaderBarycentricCapability || mReflection.usesFragmentShaderBarycentricExtension) &&
            !device.getCapabilities().supportsFragmentShaderBarycentric())
        {
            throw std::invalid_argument((
                "VKShaderModule::init encountered SPIR-V module '" + shaderLabel +
                "' requiring fragment shader barycentrics, but the selected Vulkan device does not support VK_KHR_fragment_shader_barycentric.").c_str());
        }

        vk::ShaderModuleCreateInfo createInfo = {};
        createInfo.codeSize = descriptor.spirv.size() * sizeof(uint32_t);
        createInfo.pCode = descriptor.spirv.data();

        mShaderModule = device.getNativeDevice().createShaderModuleUnique(createInfo);
        mDevice->setDebugObjectName(vk::ObjectType::eShaderModule, reinterpret_cast<uint64_t>(static_cast<VkShaderModule>(mShaderModule.get())), mLabelName.c_str());
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
