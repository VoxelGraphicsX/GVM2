#include "VKTransientDescriptorAllocator.hpp"

#include "VKDevice.hpp"

#include <chrono>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr uint32_t DescriptorSetsPerPage = 16u;
        using Clock = std::chrono::steady_clock;

        eastl::vector<vk::DescriptorPoolSize> buildPagePoolSizes(const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet)
        {
            eastl::vector<vk::DescriptorPoolSize> grownPoolSizes;
            grownPoolSizes.reserve(poolSizesPerSet.size());
            for (const vk::DescriptorPoolSize &poolSize : poolSizesPerSet)
            {
                grownPoolSizes.push_back(vk::DescriptorPoolSize{
                    poolSize.type,
                    eastl::max(poolSize.descriptorCount * DescriptorSetsPerPage, poolSize.descriptorCount)});
            }
            return grownPoolSizes;
        }

        uint64_t elapsedNs(const Clock::time_point &begin, const Clock::time_point &end)
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
        }
    } // namespace

    void VKTransientDescriptorAllocator::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKTransientDescriptorAllocator::init requires a valid device.");
        }

        mDevice = device;
    }

    void VKTransientDescriptorAllocator::clear()
    {
        mPoolPages.clear();
    }

    VKTransientDescriptorAllocator::AllocationResult VKTransientDescriptorAllocator::allocate(
        vk::DescriptorSetLayout layout,
        const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet)
    {
        if (mDevice == nullptr)
        {
            throw makeLogicError("VKTransientDescriptorAllocator::allocate was called before initialization.");
        }
        if (layout == vk::DescriptorSetLayout{})
        {
            throw makeInvalidArgument("VKTransientDescriptorAllocator::allocate requires a valid descriptor set layout.");
        }

        auto tryAllocateFromPool = [&](vk::DescriptorPool pool) -> vk::DescriptorSet
        {
            vk::DescriptorSetAllocateInfo allocateInfo = {};
            allocateInfo.descriptorPool = pool;
            allocateInfo.descriptorSetCount = 1u;
            allocateInfo.pSetLayouts = &layout;
            try
            {
                return mDevice->getNativeDevice().allocateDescriptorSets(allocateInfo).front();
            }
            catch (const vk::OutOfPoolMemoryError &)
            {
                return nullptr;
            }
            catch (const vk::FragmentedPoolError &)
            {
                return nullptr;
            }
        };

        for (PoolPage &poolPage : mPoolPages)
        {
            if (poolPage.poolSizes != poolSizesPerSet)
            {
                continue;
            }

            if (vk::DescriptorSet descriptorSet = tryAllocateFromPool(poolPage.pool.get()))
            {
                return AllocationResult{
                    .descriptorSet = descriptorSet,
                    .createdPoolPage = false,
                    .poolCreateNs = 0u,
                };
            }
        }

        vk::DescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.maxSets = DescriptorSetsPerPage;
        const eastl::vector<vk::DescriptorPoolSize> grownPoolSizes = buildPagePoolSizes(poolSizesPerSet);
        poolCreateInfo.poolSizeCount = static_cast<uint32_t>(grownPoolSizes.size());
        poolCreateInfo.pPoolSizes = grownPoolSizes.data();

        PoolPage poolPage = {};
        const Clock::time_point createBegin = Clock::now();
        poolPage.pool = mDevice->getNativeDevice().createDescriptorPoolUnique(poolCreateInfo);
        const uint64_t poolCreateNs = elapsedNs(createBegin, Clock::now());
        poolPage.poolSizes = poolSizesPerSet;
        vk::DescriptorSet descriptorSet = tryAllocateFromPool(poolPage.pool.get());
        if (descriptorSet == vk::DescriptorSet{})
        {
            throw makeRuntimeError("VKTransientDescriptorAllocator::allocate failed to allocate a descriptor set from a fresh transient pool.");
        }

        mPoolPages.push_back(eastl::move(poolPage));
        return AllocationResult{
            .descriptorSet = descriptorSet,
            .createdPoolPage = true,
            .poolCreateNs = poolCreateNs,
        };
    }
} // namespace GVM::RHI::Vulkan
