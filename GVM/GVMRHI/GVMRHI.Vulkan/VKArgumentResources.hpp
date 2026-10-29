#pragma once

#include "VKCommon.hpp"
#include "VKArguments.hpp"
#include "VKArgumentTable.hpp"

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    /** Owns the Vulkan Args page pool and the shared Args descriptor table that VKDevice previously kept inline:
        the page pool that backs argument storage, the lazily created table, and the pending capacity override. */
    class VKArgumentResources final
    {
    public:
        /** Binds the resources owner to its device. */
        explicit VKArgumentResources(VKDevice &device);

        /** Creates the Args page pool; called where the VMA allocator is initialized. */
        void createPagePool();
        /** Returns the shared page pool retained by snapshots and submission storages. */
        [[nodiscard]] const eastl::intrusive_ptr<VKArgumentPagePool> &getPagePool() const;
        /** Returns the shared device-wide Args descriptor table, creating it lazily and thread-safely on first use. */
        [[nodiscard]] VKArgumentTable &getOrCreateTable();
        /** Overrides the shared Args table slot capacities before the table exists; throws std::logic_error afterwards. */
        void setTableCapacitiesForTesting(uint32_t sampledImages, uint32_t storageImages, uint32_t samplers);
        /** Returns the pending capacity override used when the shared Args table is first created. */
        [[nodiscard]] const VKArgumentTableCapacities &getTableCapacities() const;
        /** Sets the parameter-page byte limit; applies to later page allocations. */
        void setBudget(uint64_t bytes);
        /** Returns current Args snapshot and parameter-page usage. */
        [[nodiscard]] ArgumentMemoryStatistics statistics() const;
        /** Drains pooled free pages while the VMA allocator is still alive. */
        void destroyFreePages();
        /** Releases the shared Args descriptor table during device teardown. */
        void destroyTable();

    private:
        VKDevice &mDevice;
        eastl::intrusive_ptr<VKArgumentPagePool> mPagePool;
        Mutex mTableMutex;
        eastl::intrusive_ptr<VKArgumentTable> mTable;
        VKArgumentTableCapacities mTableCapacities{};
    };
} // namespace GVM::RHI::Vulkan
