#include "VKQuerySet.hpp"

#include "VKDevice.hpp"

namespace GVM::RHI::Vulkan
{
    void VKQuerySet::init(VKDevice &device, const QuerySetDescriptor &descriptor)
    {
        if (descriptor.type != QueryType::Timestamp)
        {
            const PassCounterQuerySupport support = device.getPassCounterQuerySupport();
            eastl::string message = "VKQuerySet::init cannot create Vulkan pass counter query sets.";
            if (support.unsupportedReason != nullptr && support.unsupportedReason[0] != '\0')
            {
                message += " ";
                message += support.unsupportedReason;
            }
            throw makeRuntimeError(message);
        }
        if (descriptor.count == 0u)
        {
            throw makeInvalidArgument("VKQuerySet::init requires a non-zero query count.");
        }
        const TimestampQuerySupport support = device.getTimestampQuerySupport();
        if (support.supported == False)
        {
            throw makeRuntimeError("VKQuerySet::init requires Vulkan timestamp query support on the selected queue family.");
        }

        mLabelName = descriptor.label;
        mType = descriptor.type;
        mCount = descriptor.count;
        mResultStrideBytes = sizeof(uint64_t);

        vk::QueryPoolCreateInfo createInfo = {};
        createInfo.queryType = vk::QueryType::eTimestamp;
        createInfo.queryCount = descriptor.count;
        mQueryPool = device.getNativeDevice().createQueryPoolUnique(createInfo);
    }

    QueryType VKQuerySet::getType() const
    {
        return mType;
    }

    uint32_t VKQuerySet::getCount() const
    {
        return mCount;
    }

    uint32_t VKQuerySet::getResultStrideBytes() const
    {
        return mResultStrideBytes;
    }

    vk::QueryPool VKQuerySet::getNativeQueryPool() const
    {
        return mQueryPool.get();
    }
} // namespace GVM::RHI::Vulkan
