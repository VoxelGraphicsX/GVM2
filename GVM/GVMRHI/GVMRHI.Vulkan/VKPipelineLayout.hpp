#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKArgumentTable.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKPipelineLayout final : public PipelineLayoutImpl
    {
    public:
        VKPipelineLayout() = default;

        void init(VKDevice &device, const PipelineLayoutDescriptor &descriptor);
        /** Returns the owning logical device for Args snapshot compatibility. */
        VKDevice &getDevice() const { return *mDevice; }

        /** Returns the persistent typed descriptor table, or null for buffer-only and legacy layouts. */
        VKArgumentTable *getArgumentTable() const { return mArgumentTable.get(); }
        [[nodiscard]]
        vk::PipelineLayout getNativePipelineLayout() const;

        [[nodiscard]]
        const PipelineLayoutDescriptor &getDescriptor() const;

        [[nodiscard]]
        uint32_t getBindGroupLayoutCount() const;

        [[nodiscard]]
        const BindGroupLayout &getBindGroupLayoutHandle(uint32_t index) const;

    private:
        VKDevice *mDevice = nullptr;
        PipelineLayoutDescriptor mDescriptor = {};
        eastl::vector<BindGroupLayout> mBindGroupLayouts;
        vk::UniqueDescriptorSetLayout mEmptyDescriptorSetLayout;
        eastl::intrusive_ptr<VKArgumentTable> mArgumentTable;
        vk::UniquePipelineLayout mLayout;
    };
} // namespace GVM::RHI::Vulkan
