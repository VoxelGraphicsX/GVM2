#include "MQuerySet.hpp"

#include "MDevice.hpp"

#include <stdexcept>
#include <string>

namespace GVM::RHI::Metal
{
    namespace
    {
        uint32_t queryResultStrideBytes(QueryType type)
        {
            switch (type)
            {
            case QueryType::Timestamp:
                return sizeof(uint64_t);
            case QueryType::PassCounterStageUtilization:
                return sizeof(PassCounterStageUtilizationRawResult);
            case QueryType::PassCounterStatistic:
                return sizeof(PassCounterStatisticRawResult);
            default:
                return 0u;
            }
        }
    } // namespace

    MQuerySet::~MQuerySet()
    {
        if (mCounterSampleBuffer != nullptr)
        {
            mCounterSampleBuffer->release();
            mCounterSampleBuffer = nullptr;
        }
    }

    void MQuerySet::init(MDevice *device, const QuerySetDescriptor &descriptor, MTL::CounterSet *counterSet)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("MQuerySet::init requires a valid device.");
        }
        if (descriptor.type != QueryType::Timestamp &&
            descriptor.type != QueryType::PassCounterStageUtilization &&
            descriptor.type != QueryType::PassCounterStatistic)
        {
            throw std::invalid_argument("MQuerySet::init received an unsupported query type.");
        }
        if (descriptor.count == 0u)
        {
            throw std::invalid_argument("MQuerySet::init requires a non-zero query count.");
        }
        if (counterSet == nullptr)
        {
            throw std::runtime_error("MQuerySet::init requires the requested Metal counter set support.");
        }
        if (descriptor.type == QueryType::Timestamp)
        {
            const TimestampQuerySupport support = device->getTimestampQuerySupport();
            if (support.supported == False || support.passTimestampWritesSupported == False)
            {
                throw std::runtime_error("MQuerySet::init requires Metal pass timestamp query support.");
            }
        }
        else
        {
            const PassCounterQuerySupport support = device->getPassCounterQuerySupport();
            if (support.supported == False)
            {
                std::string errorMessage = "MQuerySet::init requires Metal pass counter query support.";
                if (support.unsupportedReason != nullptr && support.unsupportedReason[0] != '\0')
                {
                    errorMessage += " ";
                    errorMessage += support.unsupportedReason;
                }
                throw std::runtime_error(errorMessage);
            }
        }

        mLabelName = descriptor.label;
        mType = descriptor.type;
        mCount = descriptor.count;
        mResultStrideBytes = queryResultStrideBytes(descriptor.type);
        if (mResultStrideBytes == 0u)
        {
            throw std::invalid_argument("MQuerySet::init could not resolve the query result stride.");
        }

        auto *sampleBufferDescriptor = MTL::CounterSampleBufferDescriptor::alloc()->init();
        sampleBufferDescriptor->setCounterSet(counterSet);
        sampleBufferDescriptor->setSampleCount(descriptor.count);
        sampleBufferDescriptor->setStorageMode(MTL::StorageModePrivate);
        if (!descriptor.label.empty())
        {
            sampleBufferDescriptor->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
        }

        NS::Error *error = nullptr;
        mCounterSampleBuffer = device->getNativeDevice()->newCounterSampleBuffer(sampleBufferDescriptor, &error);
        sampleBufferDescriptor->release();
        if (mCounterSampleBuffer == nullptr)
        {
            std::string errorMessage = "MQuerySet::init failed to create a Metal counter sample buffer.";
            if (error != nullptr && error->localizedDescription() != nullptr)
            {
                errorMessage += " ";
                errorMessage += error->localizedDescription()->utf8String();
            }
            throw std::runtime_error(errorMessage);
        }
    }

    QueryType MQuerySet::getType() const
    {
        return mType;
    }

    uint32_t MQuerySet::getCount() const
    {
        return mCount;
    }

    uint32_t MQuerySet::getResultStrideBytes() const
    {
        return mResultStrideBytes;
    }

    MTL::CounterSampleBuffer *MQuerySet::getNativeCounterSampleBuffer() const
    {
        return mCounterSampleBuffer;
    }

    MQuerySet *validateMetalTimestampWrites(const char *apiName, const PassTimestampWrites &timestampWrites)
    {
        if (timestampWrites.querySet == nullptr)
        {
            return nullptr;
        }
        if (timestampWrites.beginningOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.endOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.beginningOfPassWriteIndex == timestampWrites.endOfPassWriteIndex)
        {
            throw std::invalid_argument(std::string(apiName) + " requires distinct begin and end query indices.");
        }

        auto *querySet = dynamic_cast<MQuerySet *>(timestampWrites.querySet.get());
        if (querySet == nullptr)
        {
            throw std::invalid_argument(std::string(apiName) + " received a non-Metal query set.");
        }
        if (querySet->getType() != QueryType::Timestamp)
        {
            throw std::invalid_argument(std::string(apiName) + " requires a timestamp query set.");
        }
        if (timestampWrites.beginningOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.beginningOfPassWriteIndex >= querySet->getCount())
        {
            throw std::out_of_range(std::string(apiName) + " begin query index exceeds the query set count.");
        }
        if (timestampWrites.endOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.endOfPassWriteIndex >= querySet->getCount())
        {
            throw std::out_of_range(std::string(apiName) + " end query index exceeds the query set count.");
        }
        return querySet;
    }

    MQuerySet *validateMetalPassCounterWrites(const char *apiName, QueryType expectedType, QuerySet querySet, uint32_t beginIndex, uint32_t endIndex)
    {
        if (querySet == nullptr)
        {
            return nullptr;
        }
        if (beginIndex != QuerySetIndexUndefined && endIndex != QuerySetIndexUndefined && beginIndex == endIndex)
        {
            throw std::invalid_argument(std::string(apiName) + " requires distinct begin and end pass counter query indices.");
        }

        auto *metalQuerySet = dynamic_cast<MQuerySet *>(querySet.get());
        if (metalQuerySet == nullptr)
        {
            throw std::invalid_argument(std::string(apiName) + " received a non-Metal pass counter query set.");
        }
        if (metalQuerySet->getType() != expectedType)
        {
            throw std::invalid_argument(std::string(apiName) + " received a pass counter query set with the wrong type.");
        }
        if (beginIndex != QuerySetIndexUndefined && beginIndex >= metalQuerySet->getCount())
        {
            throw std::out_of_range(std::string(apiName) + " begin pass counter query index exceeds the query set count.");
        }
        if (endIndex != QuerySetIndexUndefined && endIndex >= metalQuerySet->getCount())
        {
            throw std::out_of_range(std::string(apiName) + " end pass counter query index exceeds the query set count.");
        }
        return metalQuerySet;
    }

    NS::UInteger toMetalCounterSampleIndex(uint32_t index)
    {
        return index == QuerySetIndexUndefined ? MTL::CounterDontSample : static_cast<NS::UInteger>(index);
    }
} // namespace GVM::RHI::Metal
