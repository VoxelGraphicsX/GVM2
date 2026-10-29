#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /** Optional per-kind slot capacities applied when the shared Args table is created; used only by tests. */
    struct VKArgumentTableCapacities
    {
        uint32_t sampledImages = 0u;
        uint32_t storageImages = 0u;
        uint32_t samplers = 0u;
        bool overrideEnabled = false;
    };

    /** Owns the device-wide persistent set of typed image and sampler descriptor arrays shared by every Args pipeline layout. */
    class VKArgumentTable final : public RefCountedObject
    {
    public:
        /** Fixed set-zero bindings, one per Args resource kind, matching the bindings UGLC assigns to each kind. */
        static constexpr uint32_t SampledImageBinding = 0u;
        static constexpr uint32_t StorageImageBinding = 1u;
        static constexpr uint32_t SamplerBinding = 2u;

        /** Creates the fixed per-kind bindings once for the device lifetime, within cached device limits. */
        void init(VKDevice &device);
        /** Returns the persistent native set layout used at set zero in an Args pipeline. */
        vk::DescriptorSetLayout getLayout() const { return mLayout.get(); }
        /** Returns the persistent descriptor set; command snapshots retain every slot they can access. */
        vk::DescriptorSet getSet() const { return mSet; }
        /** Returns the fixed per-kind entries used for shader-reflection compatibility. */
        const eastl::vector<BindGroupLayoutEntry> &getEntries() const { return mEntries; }
        /** Returns native descriptor bindings with the actual fixed capacities. */
        const eastl::vector<vk::DescriptorSetLayoutBinding> &getBindings() const { return mBindings; }
        /** Writes a previously unused slot once; the resource cache must release it only after all GPU references end. */
        uint32_t allocate(uint32_t binding, const vk::DescriptorImageInfo &image);
        /** Returns a no-longer-referenced slot to its typed free list without overwriting pending descriptors; a mismatched slot is an internal invariant violation. */
        void release(uint32_t binding, uint32_t index);
    private:
        VKDevice *mDevice = nullptr;
        eastl::vector<BindGroupLayoutEntry> mEntries;
        eastl::vector<vk::DescriptorSetLayoutBinding> mBindings;
        eastl::vector<eastl::vector<uint32_t>> mFreeSlots;
        Mutex mMutex;
        vk::UniqueDescriptorSetLayout mLayout;
        vk::UniqueDescriptorPool mPool;
        vk::DescriptorSet mSet;
    };

    /** Caches a descriptor slot for one native view or sampler until its last command reference ends. */
    struct VKArgumentSlot
    {
        eastl::intrusive_ptr<VKArgumentTable> table;
        uint32_t binding = 0;
        uint32_t index = 0;
    };

    /** Reuses a native resource's descriptor slots across snapshots and returns them after native lifetime completion. */
    class VKArgumentSlotCache
    {
    public:
        /** Releases slots when their owning native resource is destroyed. */
        ~VKArgumentSlotCache();
        /** Returns a cached typed slot, allocating and writing it only on first use. */
        uint32_t get(VKArgumentTable &table, uint32_t binding, const vk::DescriptorImageInfo &image);
        /** Frees cached slots; callers must first ensure that no pending command references the resource. */
        void clear();
    private:
        Mutex mMutex;
        eastl::vector<VKArgumentSlot> mSlots;
    };
}
