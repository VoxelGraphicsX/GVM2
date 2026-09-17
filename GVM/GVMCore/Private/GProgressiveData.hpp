#pragma once
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <algorithm>
#include <shared_mutex>
#include <xGEFoundation/xMath.hpp>
#include <stdexcept>
namespace GVM::Core
{
    struct ProgressiveDataCreateInfo
    {
        eastl::string dataName;
        uint64_t dataElementStorageSize = 0;
        uint64_t dataElementIncreamentCount = 10000;
    };

    template <class T>
    class ProgressiveData final
    {
        eastl::vector<T> mData;
        uint64_t mDataElementIncreamentCount = 0;
        mutable std::shared_mutex mMutex;

    public:
        ProgressiveData() = default;

        ProgressiveData(const ProgressiveData &) = delete;
        ProgressiveData &operator=(const ProgressiveData &) = delete;

        void create(const ProgressiveDataCreateInfo &info, const T &initValue)
        {
            std::unique_lock<std::shared_mutex> lock(mMutex);
            mDataElementIncreamentCount = eastl::max<uint64_t>(1u, info.dataElementIncreamentCount);
            mData.assign(mDataElementIncreamentCount, initValue);
        }

        void resize(uint64_t index)
        {
            std::unique_lock lock(mMutex);
            if (index < mData.size())
            {
                return;
            }
            const uint64_t increment = eastl::max<uint64_t>(1u, mDataElementIncreamentCount);
            mData.resize(xGE::Math::IntAlign(index + 1, increment));
        }

        void write(uint64_t index, const T &value)
        {
            resize(index);
            std::unique_lock<std::shared_mutex> lock(mMutex);
            mData[index] = value;
        }

        void writeRange(uint64_t startIndex, const T *values, uint64_t length)
        {
            if (length == 0)
            {
                return;
            }
            if (values == nullptr)
            {
                throw std::invalid_argument("ProgressiveData::writeRange requires a non-null source pointer.");
            }

            resize(startIndex + length - 1);
            std::unique_lock<std::shared_mutex> lock(mMutex);
            std::copy_n(values, length, mData.begin() + startIndex);
        }

        T read(uint64_t index) const
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);
            if (index >= mData.size())
            {
                throw std::out_of_range("ProgressiveData::read index is out of range.");
            }
            return mData[index];
        }

        void copyRangeTo(uint64_t startIndex, T *destination, uint64_t length) const
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);
            if (length == 0)
            {
                return;
            }
            if (destination == nullptr)
            {
                throw std::invalid_argument("ProgressiveData::copyRangeTo requires a non-null destination pointer.");
            }
            if (startIndex > mData.size() || length > (mData.size() - startIndex))
            {
                throw std::out_of_range("ProgressiveData::copyRangeTo range is out of bounds.");
            }
            std::copy_n(mData.data() + startIndex, length, destination);
        }

        void fillRange(const T &value, uint64_t startIndex, uint64_t length)
        {
            if (length == 0)
            {
                return;
            }

            std::unique_lock<std::shared_mutex> lock(mMutex);
            if (startIndex > mData.size() || length > (mData.size() - startIndex))
            {
                throw std::out_of_range("ProgressiveData::fillRange range is out of bounds.");
            }
            std::fill_n(mData.data() + startIndex, length, value);
        }

        uint64_t getLength() const
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);
            return mData.size();
        }
        uint64_t getByteSize() const
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);
            return mData.size() * sizeof(T);
        }

        bool contains(uint64_t index) const
        {
            std::shared_lock<std::shared_mutex> lock(mMutex);
            return index < mData.size();
        }

        void clear()
        {
            std::unique_lock<std::shared_mutex> lock(mMutex);
            mData.clear();
        }
    };

} // namespace GVM::Core
