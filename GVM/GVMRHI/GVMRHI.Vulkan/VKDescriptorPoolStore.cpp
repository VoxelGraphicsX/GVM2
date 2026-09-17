#include "VKDescriptorPoolStore.hpp"

#include "VKDevice.hpp"
#include "VKLogging.hpp"

#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
        namespace
    {
        constexpr eastl::string_view DescriptorPoolStoreLogCategory = "gvmrhi.vulkan.descriptor_pool_store";
    } // namespace

namespace
    {
        eastl::vector<vk::DescriptorPoolSize> normalizePoolSizesPerSet(const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet)
        {
            eastl::vector<vk::DescriptorPoolSize> normalizedPoolSizes = poolSizesPerSet;
            eastl::sort(
                normalizedPoolSizes.begin(),
                normalizedPoolSizes.end(),
                [](const vk::DescriptorPoolSize &lhs, const vk::DescriptorPoolSize &rhs)
                {
                    return static_cast<uint32_t>(lhs.type) < static_cast<uint32_t>(rhs.type);
                });

            eastl::vector<vk::DescriptorPoolSize> mergedPoolSizes;
            mergedPoolSizes.reserve(normalizedPoolSizes.size());
            for (const vk::DescriptorPoolSize &poolSize : normalizedPoolSizes)
            {
                if (!mergedPoolSizes.empty() && mergedPoolSizes.back().type == poolSize.type)
                {
                    mergedPoolSizes.back().descriptorCount += poolSize.descriptorCount;
                    continue;
                }
                mergedPoolSizes.push_back(poolSize);
            }
            return mergedPoolSizes;
        }

        bool poolSignatureMatches(
            const eastl::vector<vk::DescriptorPoolSize> &lhs,
            const eastl::vector<vk::DescriptorPoolSize> &rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (size_t index = 0u; index < lhs.size(); ++index)
            {
                if (lhs[index].type != rhs[index].type ||
                    lhs[index].descriptorCount != rhs[index].descriptorCount)
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    void VKDescriptorPoolStore::init(VKDevice *device)
    {
        mDeviceOwner = device;
        mLogger = device != nullptr ? device->getLogger() : Logger{};
        mLogContext = device != nullptr ? device->getLogContext() : eastl::shared_ptr<Internal::LogContext>{};
        mDevice = device != nullptr ? device->getNativeDevice() : vk::Device{};
        mDescriptorPools.clear();
        mNextPoolSequence = 1u;
        GVMLogInfo(this, DescriptorPoolStoreLogCategory, "event=descriptor_pool_store_config pool_reuse_policy=immediate");
    }

    void VKDescriptorPoolStore::destroy()
    {
        mDescriptorPools.clear();
        mDeviceOwner = nullptr;
        mDevice = nullptr;
        mLogger = nullptr;
        mLogContext.reset();
    }

    Logger VKDescriptorPoolStore::getLogger() const
    {
        return mLogger;
    }

    const eastl::shared_ptr<Internal::LogContext> &VKDescriptorPoolStore::getLogContext() const
    {
        return mLogContext;
    }

    vk::DescriptorSet VKDescriptorPoolStore::allocateDescriptorSet(
        vk::DescriptorSetLayout layout,
        const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
        vk::DescriptorPool &owningPool,
        uint64_t currentRetiredSubmissionId)
    {
        const eastl::vector<vk::DescriptorPoolSize> normalizedPoolSizes = normalizePoolSizesPerSet(poolSizesPerSet);
        auto tryAllocateFromPool = [&](vk::DescriptorPool pool) -> vk::DescriptorSet
        {
            vk::DescriptorSetAllocateInfo allocateInfo = {};
            allocateInfo.descriptorPool = pool;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &layout;
            try
            {
                return mDevice.allocateDescriptorSets(allocateInfo).front();
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

        for (DescriptorPoolArena &arena : mDescriptorPools)
        {
            if (!poolSignatureMatches(arena.poolSizesPerSet, normalizedPoolSizes))
            {
                continue;
            }
            if (vk::DescriptorSet descriptorSet = tryAllocateFromPool(arena.pool.get()))
            {
                owningPool = arena.pool.get();
                arena.allocationCount += 1u;
                GVMLogDebug(
                    this, DescriptorPoolStoreLogCategory,
                    "event=descriptor_pool_allocate_from_arena descriptor_pool_ptr={} pool_sequence={} descriptor_set_ptr={} allocation_count={} free_count={} reused_existing_pool={} pool_signature={} current_retired_submission_id={}",
                    reinterpret_cast<void *>(static_cast<VkDescriptorPool>(owningPool)),
                    arena.sequence,
                    reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
                    arena.allocationCount,
                    arena.freeCount,
                    true,
                    normalizedPoolSizes.size(),
                    currentRetiredSubmissionId);
                return descriptorSet;
            }
        }

        eastl::vector<vk::DescriptorPoolSize> grownPoolSizes;
        grownPoolSizes.reserve(normalizedPoolSizes.size());
        constexpr uint32_t maxSets = 64u;
        for (const vk::DescriptorPoolSize &poolSize : normalizedPoolSizes)
        {
            grownPoolSizes.push_back(vk::DescriptorPoolSize{
                poolSize.type,
                eastl::max(poolSize.descriptorCount * 64u, 64u)});
        }

        vk::DescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolCreateInfo.maxSets = maxSets;
        poolCreateInfo.poolSizeCount = static_cast<uint32_t>(grownPoolSizes.size());
        poolCreateInfo.pPoolSizes = grownPoolSizes.data();

        DescriptorPoolArena arena = {};
        arena.pool = mDevice.createDescriptorPoolUnique(poolCreateInfo);
        arena.poolSizesPerSet = normalizedPoolSizes;
        arena.sequence = mNextPoolSequence++;
        mDescriptorPools.push_back(eastl::move(arena));
        owningPool = mDescriptorPools.back().pool.get();

        vk::DescriptorSet descriptorSet = tryAllocateFromPool(owningPool);
        if (!descriptorSet)
        {
            throw makeRuntimeError("VKDescriptorPoolStore::allocateDescriptorSet failed to allocate a descriptor set from a fresh pool.");
        }
        mDescriptorPools.back().allocationCount = 1u;
        GVMLogInfo(
            this, DescriptorPoolStoreLogCategory,
            "event=descriptor_pool_arena_created descriptor_pool_ptr={} pool_sequence={} max_sets={} descriptor_set_ptr={} pool_signature={} current_retired_submission_id={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(owningPool)),
            mDescriptorPools.back().sequence,
            maxSets,
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            normalizedPoolSizes.size(),
            currentRetiredSubmissionId);
        return descriptorSet;
    }

    void VKDescriptorPoolStore::freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet, uint64_t currentRetiredSubmissionId) const
    {
        if (!mDevice || pool == vk::DescriptorPool{} || descriptorSet == vk::DescriptorSet{})
        {
            return;
        }
        if (DescriptorPoolArena *arena = const_cast<VKDescriptorPoolStore *>(this)->findArena(pool))
        {
            arena->freeCount += 1u;
            GVMLogDebug(
                this, DescriptorPoolStoreLogCategory,
                "event=descriptor_pool_arena_free descriptor_pool_ptr={} pool_sequence={} descriptor_set_ptr={} allocation_count={} free_count={} current_retired_submission_id={}",
                reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
                arena->sequence,
                reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
                arena->allocationCount,
                arena->freeCount,
                currentRetiredSubmissionId);
        }
        GVMLogTrace(
            this, DescriptorPoolStoreLogCategory,
            "event=descriptor_pool_store_driver_free_begin descriptor_pool_ptr={} descriptor_set_ptr={} current_retired_submission_id={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            currentRetiredSubmissionId);
        (void)mDevice.freeDescriptorSets(pool, descriptorSet);
        GVMLogTrace(
            this, DescriptorPoolStoreLogCategory,
            "event=descriptor_pool_store_driver_free_end descriptor_pool_ptr={} descriptor_set_ptr={} current_retired_submission_id={}",
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(pool)),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(descriptorSet)),
            currentRetiredSubmissionId);
    }

    VKDescriptorPoolStore::DescriptorPoolArena *VKDescriptorPoolStore::findArena(vk::DescriptorPool pool)
    {
        for (DescriptorPoolArena &arena : mDescriptorPools)
        {
            if (arena.pool.get() == pool)
            {
                return &arena;
            }
        }
        return nullptr;
    }

    const VKDescriptorPoolStore::DescriptorPoolArena *VKDescriptorPoolStore::findArena(vk::DescriptorPool pool) const
    {
        for (const DescriptorPoolArena &arena : mDescriptorPools)
        {
            if (arena.pool.get() == pool)
            {
                return &arena;
            }
        }
        return nullptr;
    }
} // namespace GVM::RHI::Vulkan
