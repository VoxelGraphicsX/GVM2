#include "VKArgumentResources.hpp"

#include "VKDevice.hpp"

#include <EASTL/make_intrusive.h>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    VKArgumentResources::VKArgumentResources(VKDevice &device)
        : mDevice(device)
    {
    }

    void VKArgumentResources::createPagePool()
    {
        mPagePool = eastl::make_intrusive<VKArgumentPagePool>(mDevice);
    }

    const eastl::intrusive_ptr<VKArgumentPagePool> &VKArgumentResources::getPagePool() const
    {
        return mPagePool;
    }

    VKArgumentTable &VKArgumentResources::getOrCreateTable()
    {
        mDevice.ensureAlive("VKDevice::getOrCreateArgumentTable");
        ScopedLock lock(mTableMutex);
        if (mTable == nullptr)
        {
            mTable = eastl::intrusive_ptr<VKArgumentTable>(new VKArgumentTable());
            mTable->init(mDevice);
        }
        return *mTable;
    }

    void VKArgumentResources::setTableCapacitiesForTesting(uint32_t sampledImages, uint32_t storageImages, uint32_t samplers)
    {
        mDevice.ensureAlive("VKDevice::setArgumentTableCapacitiesForTesting");
        ScopedLock lock(mTableMutex);
        if (mTable != nullptr)
        {
            throw std::logic_error("VKDevice::setArgumentTableCapacitiesForTesting must run before the shared Args descriptor table exists.");
        }
        mTableCapacities = {sampledImages, storageImages, samplers, true};
    }

    const VKArgumentTableCapacities &VKArgumentResources::getTableCapacities() const
    {
        return mTableCapacities;
    }

    void VKArgumentResources::setBudget(uint64_t bytes)
    {
        mPagePool->setLimit(bytes);
    }

    ArgumentMemoryStatistics VKArgumentResources::statistics() const
    {
        return mPagePool->statistics();
    }

    void VKArgumentResources::destroyFreePages()
    {
        if (mPagePool != nullptr)
        {
            mPagePool->destroyFreePages();
            mPagePool = nullptr;
        }
    }

    void VKArgumentResources::destroyTable()
    {
        ScopedLock lock(mTableMutex);
        mTable = nullptr;
    }
} // namespace GVM::RHI::Vulkan
