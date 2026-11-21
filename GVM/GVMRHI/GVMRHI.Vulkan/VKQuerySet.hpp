#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    class VKQuerySet final : public QuerySetImpl
    {
    public:
        VKQuerySet() = default;
        ~VKQuerySet() override = default;

        void init(VKDevice &device, const QuerySetDescriptor &descriptor);

        QueryType getType() const override;
        uint32_t getCount() const override;
        uint32_t getResultStrideBytes() const override;

        [[nodiscard]]
        vk::QueryPool getNativeQueryPool() const;

        /// Returns the last accepted use on a device timeline while holding the submission lock.
        uint64_t lastUse(uint32_t lane) const;
        /// Publishes an accepted query operation; other queues must wait before using this pool.
        void acceptUse(uint32_t lane, uint64_t value);
    private:
        uint64_t mLastUse[MaxDeviceQueues]{};
        QueryType mType = QueryType::Timestamp;
        uint32_t mCount = 0u;
        uint32_t mResultStrideBytes = sizeof(uint64_t);
        vk::UniqueQueryPool mQueryPool;
    };
} // namespace GVM::RHI::Vulkan
