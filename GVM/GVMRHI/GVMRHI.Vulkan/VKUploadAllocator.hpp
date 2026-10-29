#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <GVMRHI/Private/UploadPageAllocator.hpp>

#include <cstdint>

namespace GVM::RHI::Vulkan
{
    struct VKUploadSlice
    {
        vk::Buffer buffer = nullptr;
        uint64_t offset = 0;
        void *mappedData = nullptr;
    };

    /** One backend-owned upload staging page: a persistently mapped VMA transfer-source buffer and its capacity. */
    struct VKUploadPage
    {
        vk::Buffer buffer = nullptr;
        VmaAllocation allocation = nullptr;
        uint8_t *mappedData = nullptr;
        uint64_t size = 0;
    };

    /** Stages CPU uploads into persistently mapped VMA pages that recycle after their consuming submissions retire. */
    class VKUploadAllocator final
    {
    public:
        static constexpr uint64_t DefaultAlignment = 256u;

        VKUploadAllocator() = default;
        ~VKUploadAllocator();

        void init(VKDevice *device);

        [[nodiscard]]
        VKUploadSlice allocateAndWrite(const void *data, uint64_t size, uint64_t alignment = DefaultAlignment);

        void onSubmission(VKSubmissionSerial serial);
        void onCompleted(uint64_t lastRetiredSubmissionId);
        void destroy();

    private:
        VKDevice *mDevice = nullptr;
        Private::UploadPageAllocator<VKUploadPage> mPages;
    };
} // namespace GVM::RHI::Vulkan
