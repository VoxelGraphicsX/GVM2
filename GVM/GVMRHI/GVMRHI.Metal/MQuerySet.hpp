#pragma once

#include "MDefines.hpp"

#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>

namespace GVM::RHI::Metal
{
    class MQuerySet final : public QuerySetImpl
    {
    public:
        MQuerySet() = default;
        ~MQuerySet() override;

        void init(MDevice *device, const QuerySetDescriptor &descriptor, MTL::CounterSet *counterSet);

        QueryType getType() const override;
        uint32_t getCount() const override;
        uint32_t getResultStrideBytes() const override;

        [[nodiscard]]
        MTL::CounterSampleBuffer *getNativeCounterSampleBuffer() const;

    private:
        QueryType mType = QueryType::Timestamp;
        uint32_t mCount = 0u;
        uint32_t mResultStrideBytes = sizeof(uint64_t);
        MTL::CounterSampleBuffer *mCounterSampleBuffer = nullptr;
    };

    [[nodiscard]]
    MQuerySet *validateMetalTimestampWrites(const char *apiName, const PassTimestampWrites &timestampWrites);

    [[nodiscard]]
    MQuerySet *validateMetalPassCounterWrites(const char *apiName, QueryType expectedType, QuerySet querySet, uint32_t beginIndex, uint32_t endIndex);

    [[nodiscard]]
    NS::UInteger toMetalCounterSampleIndex(uint32_t index);
} // namespace GVM::RHI::Metal
