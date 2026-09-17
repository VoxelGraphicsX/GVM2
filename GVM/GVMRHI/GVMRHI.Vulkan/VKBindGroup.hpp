#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKBindGroup final : public BindGroupImpl
    {
    public:
        struct ResolvedBinding
        {
            BindGroupLayoutEntry layoutEntry = {};
            BindGroupEntry descriptorEntry = {};
        };

        VKBindGroup() = default;
        ~VKBindGroup() override;

        void init(VKDevice *device, const BindGroupDescriptor &descriptor);

        [[nodiscard]] vk::DescriptorSet getNativeDescriptorSet() const;
        [[nodiscard]] const BindGroupLayout &getLayoutHandle() const;
        [[nodiscard]] const eastl::vector<ResolvedBinding> &getResolvedBindings() const;

        Logger getLogger() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

    private:
        VKDevice *mDevice = nullptr;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        BindGroupDescriptor mDescriptor = {};
        BindGroupLayout mLayout = nullptr;
        eastl::vector<ResolvedBinding> mResolvedBindings;
        bool mResourceReferencesRegistered = false;
        vk::DescriptorPool mDescriptorPool = nullptr;
        vk::DescriptorSet mDescriptorSet;
    };
} // namespace GVM::RHI::Vulkan
