#pragma once

#include "GStagingCopyGPUVector.hpp"
#include "GUnifiedMemoryGPUVector.hpp"

#include <EASTL/unique_ptr.h>
#include <stdexcept>

namespace GVM::Core
{
    /**
     * Creates typed GPU storage that matches the selected RHI device memory topology.
     * The returned object is either UnifiedMemoryGPUVector<T> or StagingCopyGPUVector<T>, selected from DeviceMemoryProperties.
     */
    template <class T>
    eastl::unique_ptr<GPUVectorBase<T>> createGPUVectorStorage(
        GVM::RHI::Device device,
        const eastl::string &label,
        GVM::RHI::BufferUsageFlags usage,
        uint64_t initialCapacityBytes,
        uint64_t growthAlignmentBytes)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("createGPUVectorStorage requires a valid device.");
        }

        const GVM::RHI::DeviceMemoryProperties memoryProperties = device->getMemoryProperties();
        if (memoryProperties.unifiedMemory != GVM::RHI::False)
        {
            return eastl::unique_ptr<GPUVectorBase<T>>(new UnifiedMemoryGPUVector<T>(device, label, usage, initialCapacityBytes, growthAlignmentBytes));
        }
        return eastl::unique_ptr<GPUVectorBase<T>>(new StagingCopyGPUVector<T>(device, label, usage, initialCapacityBytes, growthAlignmentBytes));
    }
} // namespace GVM::Core
