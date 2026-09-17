#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <cstdint>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    struct VKUploadSlice
    {
        vk::Buffer buffer = nullptr;
        uint64_t offset = 0;
        void *mappedData = nullptr;
    };

    class VKUploadAllocator final
    {
    public:
        static constexpr uint64_t DefaultAlignment = 256u;

        VKUploadAllocator() = default;
        ~VKUploadAllocator();

        void init(VKDevice *device);

        [[nodiscard]]
        VKUploadSlice allocateAndWrite(const void *data, uint64_t size, uint64_t alignment = DefaultAlignment);

        void onSubmission(const VKSubmissionCompletion &completion);
        void onCompleted();
        void destroy();

    private:
        void reclaimCompletedUploadSlabs();

        VKDevice *mDevice = nullptr;
        eastl::vector<eastl::shared_ptr<VKUploadAllocatorSlab>> mUploadSlabs;
        eastl::vector<VKUploadAllocatorSlab *> mPendingUploadSlabs;
    };
} // namespace GVM::RHI::Vulkan
