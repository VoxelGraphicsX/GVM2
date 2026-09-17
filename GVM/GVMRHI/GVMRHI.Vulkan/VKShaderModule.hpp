#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKShaderReflection.hpp"

namespace GVM::RHI::Vulkan
{
    class VKShaderModule final : public ShaderModuleImpl
    {
    public:
        VKShaderModule() = default;

        void init(VKDevice &device, const ShaderModuleDescriptor &descriptor);

        [[nodiscard]]
        vk::ShaderModule getNativeShaderModule() const;

        [[nodiscard]]
        const ShaderModuleDescriptor &getDescriptor() const;

        [[nodiscard]]
        const Detail::ReflectedShaderModule &getReflection() const;

    private:
        VKDevice *mDevice = nullptr;
        ShaderModuleDescriptor mDescriptor = {};
        Detail::ReflectedShaderModule mReflection = {};
        vk::UniqueShaderModule mShaderModule;
    };
} // namespace GVM::RHI::Vulkan
