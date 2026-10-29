#include "VKArgumentTable.hpp"
#include "VKArgumentResources.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** Default per-kind slot targets for the device-wide Args descriptor table. */
        constexpr uint32_t DefaultSampledImageCapacity = 65536u;
        constexpr uint32_t DefaultStorageImageCapacity = 16384u;
        constexpr uint32_t DefaultSamplerCapacity = 4096u;
    }

    void VKArgumentTable::init(VKDevice &device)
    {
        mDevice = &device;
        const auto &limits = device.getCapabilities().getDescriptorIndexingProperties();
        const auto &capacities = device.getArgumentResources().getTableCapacities();
        const uint32_t sampledTarget = capacities.overrideEnabled ? capacities.sampledImages : DefaultSampledImageCapacity;
        const uint32_t storageTarget = capacities.overrideEnabled ? capacities.storageImages : DefaultStorageImageCapacity;
        const uint32_t samplerTarget = capacities.overrideEnabled ? capacities.samplers : DefaultSamplerCapacity;
        uint32_t sampledCapacity = eastl::min(sampledTarget, eastl::min(limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
            limits.maxDescriptorSetUpdateAfterBindSampledImages));
        uint32_t storageCapacity = eastl::min(storageTarget, eastl::min(limits.maxPerStageDescriptorUpdateAfterBindStorageImages,
            limits.maxDescriptorSetUpdateAfterBindStorageImages));
        uint32_t samplerCapacity = eastl::min(samplerTarget, eastl::min(limits.maxPerStageDescriptorUpdateAfterBindSamplers,
            limits.maxDescriptorSetUpdateAfterBindSamplers));
        const uint64_t total = static_cast<uint64_t>(sampledCapacity) + storageCapacity + samplerCapacity;
        const uint64_t budget = eastl::min<uint64_t>(limits.maxPerStageUpdateAfterBindResources,
            limits.maxUpdateAfterBindDescriptorsInAllPools);
        if (total > budget)
        {
            sampledCapacity = eastl::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(sampledCapacity) * budget / total));
            storageCapacity = eastl::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(storageCapacity) * budget / total));
            samplerCapacity = eastl::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(samplerCapacity) * budget / total));
        }

        const bool storageSupported = device.getCapabilities().supportsStorageImageUpdateAfterBind();
        const uint32_t capacitiesByKind[3] = {sampledCapacity, storageCapacity, samplerCapacity};
        if (sampledCapacity == 0 || samplerCapacity == 0 || (storageSupported && storageCapacity == 0))
        {
            throw std::invalid_argument("Vulkan Args resource tables exceed the cached update-after-bind descriptor limits.");
        }
        for (uint32_t kind = 0; kind < 3u; ++kind)
        {
            if (kind == StorageImageBinding && !storageSupported)
            {
                continue;
            }
            BindGroupLayoutEntry entry{};
            entry.binding = kind;
            entry.visibility = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute;
            switch (kind)
            {
            case SampledImageBinding: entry.texture.sampleType = TextureSampleType::Float; break;
            case StorageImageBinding: entry.storageTexture.access = StorageTextureAccess::ReadWrite; break;
            default: entry.sampler.type = SamplerBindingType::Filtering; break;
            }
            mEntries.push_back(entry);
            const auto type = translateDescriptorType(entry);
            // All stages share the same typed root ABI, including entries optimized out of one shader.
            mBindings.push_back({kind, type, capacitiesByKind[kind], vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute});
        }

        eastl::vector<vk::DescriptorPoolSize> sizes;
        eastl::vector<vk::DescriptorBindingFlags> flags;
        mFreeSlots.resize(mEntries.size());
        for (size_t position = 0; position < mEntries.size(); ++position)
        {
            const uint32_t capacity = mBindings[position].descriptorCount;
            flags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound);
            sizes.push_back({mBindings[position].descriptorType, capacity});
            auto &slots = mFreeSlots[position];
            slots.reserve(capacity);
            for (uint32_t index = capacity; index != 0; --index)
            {
                slots.push_back(index - 1);
            }
        }
        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.bindingCount = static_cast<uint32_t>(flags.size());
        bindingFlags.pBindingFlags = flags.data();
        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &bindingFlags;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        layoutInfo.bindingCount = static_cast<uint32_t>(mBindings.size());
        layoutInfo.pBindings = mBindings.data();
        mLayout = device.getNativeDevice().createDescriptorSetLayoutUnique(layoutInfo);
        device.setDebugObjectName(vk::ObjectType::eDescriptorSetLayout, reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(mLayout.get())), "GVM Args descriptor table");
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();
        mPool = device.getNativeDevice().createDescriptorPoolUnique(poolInfo);
        const auto nativeLayout = mLayout.get();
        vk::DescriptorSetAllocateInfo allocateInfo{mPool.get(), 1, &nativeLayout};
        mSet = device.getNativeDevice().allocateDescriptorSets(allocateInfo)[0];
        device.setDebugObjectName(vk::ObjectType::eDescriptorSet, reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(mSet)), "GVM Args descriptor table");
    }

    uint32_t VKArgumentTable::allocate(uint32_t binding, const vk::DescriptorImageInfo &image)
    {
        ScopedLock lock(mMutex);
        const auto entry = eastl::lower_bound(mEntries.begin(), mEntries.end(), binding,
            [](const auto &candidate, uint32_t value) { return candidate.binding < value; });
        if (entry == mEntries.end() || entry->binding != binding)
        {
            throw std::invalid_argument("Vulkan Args descriptor-table binding is absent from the pipeline layout.");
        }
        const auto position = static_cast<size_t>(entry - mEntries.begin());
        auto &slots = mFreeSlots[position];
        if (slots.empty())
        {
            throw std::runtime_error("Vulkan Args descriptor table is full; release unused resources before allocating more slots.");
        }
        const uint32_t index = slots.back();
        const vk::WriteDescriptorSet write{mSet, binding, index, 1, mBindings[position].descriptorType, &image};
        mDevice->getNativeDevice().updateDescriptorSets(write, {});
        slots.pop_back();
        return index;
    }

    void VKArgumentTable::release(uint32_t binding, uint32_t index)
    {
        ScopedLock lock(mMutex);
        const auto entry = eastl::lower_bound(mEntries.begin(), mEntries.end(), binding,
            [](const auto &candidate, uint32_t value) { return candidate.binding < value; });
        const auto position = static_cast<size_t>(entry - mEntries.begin());
        if (entry == mEntries.end() || entry->binding != binding || index >= mBindings[position].descriptorCount)
        {
            throw std::logic_error("Vulkan Args descriptor-table release received a slot that this table never allocated.");
        }
        mFreeSlots[position].push_back(index);
    }
}

namespace GVM::RHI::Vulkan
{
    VKArgumentSlotCache::~VKArgumentSlotCache()
    {
        clear();
    }

    uint32_t VKArgumentSlotCache::get(VKArgumentTable &table, uint32_t binding, const vk::DescriptorImageInfo &image)
    {
        ScopedLock lock(mMutex);
        for (const auto &slot : mSlots)
        {
            if (slot.table.get() == &table && slot.binding == binding)
            {
                return slot.index;
            }
        }
        const uint32_t index = table.allocate(binding, image);
        try
        {
            mSlots.push_back({eastl::intrusive_ptr<VKArgumentTable>(&table), binding, index});
        }
        catch (...)
        {
            table.release(binding, index);
            throw;
        }
        return index;
    }

    void VKArgumentSlotCache::clear()
    {
        ScopedLock lock(mMutex);
        for (const auto &slot : mSlots)
        {
            slot.table->release(slot.binding, slot.index);
        }
        mSlots.clear();
    }
}
