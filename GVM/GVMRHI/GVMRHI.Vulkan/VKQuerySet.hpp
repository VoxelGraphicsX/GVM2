#pragma once

#include "VKCommon.hpp"
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

    private:
        QueryType mType = QueryType::Timestamp;
        uint32_t mCount = 0u;
        uint32_t mResultStrideBytes = sizeof(uint64_t);
        vk::UniqueQueryPool mQueryPool;
    };
} // namespace GVM::RHI::Vulkan
