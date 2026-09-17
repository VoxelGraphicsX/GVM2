#include "VKUploadAllocator.hpp"

#include "VKUploadAllocatorSlab.hpp"

#include <EASTL/algorithm.h>

#include <cstring>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr uint64_t kDefaultUploadSlabSize = 4u * 1024u * 1024u;

        uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            if (alignment <= 1u)
            {
                return value;
            }

            const uint64_t remainder = value % alignment;
            return remainder == 0u ? value : (value + (alignment - remainder));
        }

        uint64_t selectUploadSlabSize(uint64_t requestedSize)
        {
            return eastl::max(kDefaultUploadSlabSize, alignUp(requestedSize, VKUploadAllocator::DefaultAlignment));
        }
    } // namespace

    VKUploadAllocator::~VKUploadAllocator() = default;

    void VKUploadAllocator::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKUploadAllocator::init requires a valid Vulkan device.");
        }

        mDevice = device;
        mUploadSlabs.clear();
        mPendingUploadSlabs.clear();
    }

    VKUploadSlice VKUploadAllocator::allocateAndWrite(const void *data, uint64_t size, uint64_t alignment)
    {
        if (mDevice == nullptr)
        {
            throw makeRuntimeError("VKUploadAllocator::allocateAndWrite was called before init.");
        }
        if (data == nullptr)
        {
            throw makeInvalidArgument("VKUploadAllocator::allocateAndWrite requires non-null source data.");
        }
        if (size == 0)
        {
            throw makeInvalidArgument("VKUploadAllocator::allocateAndWrite requires a non-zero allocation size.");
        }

        VKUploadAllocatorSlab *selectedSlab = nullptr;
        for (const eastl::shared_ptr<VKUploadAllocatorSlab> &slab : mUploadSlabs)
        {
            if (slab != nullptr && slab->hasSpaceFor(size, alignment))
            {
                selectedSlab = slab.get();
                break;
            }
        }

        if (selectedSlab == nullptr)
        {
            auto newSlab = eastl::make_shared<VKUploadAllocatorSlab>(mDevice, selectUploadSlabSize(size));
            selectedSlab = newSlab.get();
            mUploadSlabs.push_back(eastl::move(newSlab));
        }

        VKUploadSlice slice = selectedSlab->allocate(size, alignment);
        memcpy(slice.mappedData, data, size);
        selectedSlab->flush(slice.offset, size);

        if (selectedSlab->tryMarkPendingSubmission())
        {
            mPendingUploadSlabs.push_back(selectedSlab);
        }

        return slice;
    }

    void VKUploadAllocator::onSubmission(const VKSubmissionCompletion &completion)
    {
        for (VKUploadAllocatorSlab *slab : mPendingUploadSlabs)
        {
            if (slab == nullptr)
            {
                continue;
            }
            slab->markSubmitted(completion);
        }
        mPendingUploadSlabs.clear();
    }

    void VKUploadAllocator::onCompleted()
    {
        reclaimCompletedUploadSlabs();
    }

    void VKUploadAllocator::destroy()
    {
        mPendingUploadSlabs.clear();
        mUploadSlabs.clear();
    }

    void VKUploadAllocator::reclaimCompletedUploadSlabs()
    {
        for (const eastl::shared_ptr<VKUploadAllocatorSlab> &slab : mUploadSlabs)
        {
            if (slab == nullptr || !slab->isReadyForReuse())
            {
                continue;
            }
            slab->reset();
        }
    }
} // namespace GVM::RHI::Vulkan
