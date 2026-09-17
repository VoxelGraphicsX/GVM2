#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKDescriptorPoolStore final
    {
    public:
        void init(VKDevice *device);
        void destroy();

        [[nodiscard]]
        vk::DescriptorSet allocateDescriptorSet(
            vk::DescriptorSetLayout layout,
            const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
            vk::DescriptorPool &owningPool,
            uint64_t currentRetiredSubmissionId);

        void freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet, uint64_t currentRetiredSubmissionId) const;
        Logger getLogger() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

    private:
        struct DescriptorPoolArena
        {
            vk::UniqueDescriptorPool pool;
            eastl::vector<vk::DescriptorPoolSize> poolSizesPerSet;
            uint64_t sequence = 0u;
            uint64_t allocationCount = 0u;
            uint64_t freeCount = 0u;
        };

        DescriptorPoolArena *findArena(vk::DescriptorPool pool);
        const DescriptorPoolArena *findArena(vk::DescriptorPool pool) const;

        VKDevice *mDeviceOwner = nullptr;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        vk::Device mDevice = nullptr;
        eastl::vector<DescriptorPoolArena> mDescriptorPools;
        uint64_t mNextPoolSequence = 1u;
    };
} // namespace GVM::RHI::Vulkan
