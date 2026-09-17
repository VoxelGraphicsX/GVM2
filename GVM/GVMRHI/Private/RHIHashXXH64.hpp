#pragma once

#include <EASTL/bit.h>
#include <EASTL/string_view.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace GVM::RHI::Detail
{
    class XXH64State
    {
    public:
        explicit XXH64State(uint64_t seed = 0)
        {
            reset(seed);
        }

        void reset(uint64_t seed = 0)
        {
            mSeed = seed;
            mTotalLength = 0;
            mBufferSize = 0;
            mV1 = seed + Prime1 + Prime2;
            mV2 = seed + Prime2;
            mV3 = seed + 0;
            mV4 = seed - Prime1;
        }

        void update(const void *data, size_t length)
        {
            const auto *input = static_cast<const uint8_t *>(data);
            if (input == nullptr || length == 0)
            {
                return;
            }

            mTotalLength += length;

            if (mBufferSize + length < BufferCapacity)
            {
                std::memcpy(mBuffer + mBufferSize, input, length);
                mBufferSize += length;
                return;
            }

            size_t offset = 0;
            if (mBufferSize > 0)
            {
                const size_t bytesNeeded = BufferCapacity - mBufferSize;
                std::memcpy(mBuffer + mBufferSize, input, bytesNeeded);
                processStripe(mBuffer);
                mBufferSize = 0;
                offset += bytesNeeded;
            }

            while (offset + BufferCapacity <= length)
            {
                processStripe(input + offset);
                offset += BufferCapacity;
            }

            if (offset < length)
            {
                mBufferSize = length - offset;
                std::memcpy(mBuffer, input + offset, mBufferSize);
            }
        }

        template <typename T>
        void updatePod(const T &value)
            requires(std::is_trivially_copyable_v<T>)
        {
            update(&value, sizeof(T));
        }

        template <typename T>
        void updateEnum(T value)
            requires(std::is_enum_v<T>)
        {
            using Underlying = std::underlying_type_t<T>;
            const Underlying raw = static_cast<Underlying>(value);
            update(&raw, sizeof(raw));
        }

        void updateString(eastl::string_view value)
        {
            const uint64_t length = static_cast<uint64_t>(value.size());
            updatePod(length);
            update(value.data(), value.size());
        }

        void updateFloat(float value)
        {
            updatePod(eastl::bit_cast<uint32_t>(value));
        }

        uint64_t digest() const
        {
            uint64_t hash = 0;
            const uint8_t *tail = mBuffer;
            size_t remaining = mBufferSize;

            if (mTotalLength >= BufferCapacity)
            {
                hash =
                    rotateLeft(mV1, 1) +
                    rotateLeft(mV2, 7) +
                    rotateLeft(mV3, 12) +
                    rotateLeft(mV4, 18);

                hash = mergeRound(hash, mV1);
                hash = mergeRound(hash, mV2);
                hash = mergeRound(hash, mV3);
                hash = mergeRound(hash, mV4);
            }
            else
            {
                hash = mSeed + Prime5;
            }

            hash += mTotalLength;

            while (remaining >= 8)
            {
                const uint64_t lane = read64(tail);
                hash ^= round(0, lane);
                hash = rotateLeft(hash, 27) * Prime1 + Prime4;
                tail += 8;
                remaining -= 8;
            }

            if (remaining >= 4)
            {
                hash ^= static_cast<uint64_t>(read32(tail)) * Prime1;
                hash = rotateLeft(hash, 23) * Prime2 + Prime3;
                tail += 4;
                remaining -= 4;
            }

            while (remaining > 0)
            {
                hash ^= static_cast<uint64_t>(*tail) * Prime5;
                hash = rotateLeft(hash, 11) * Prime1;
                ++tail;
                --remaining;
            }

            hash ^= hash >> 33;
            hash *= Prime2;
            hash ^= hash >> 29;
            hash *= Prime3;
            hash ^= hash >> 32;
            return hash;
        }

    private:
        static constexpr size_t BufferCapacity = 32;
        static constexpr uint64_t Prime1 = 11400714785074694791ull;
        static constexpr uint64_t Prime2 = 14029467366897019727ull;
        static constexpr uint64_t Prime3 = 1609587929392839161ull;
        static constexpr uint64_t Prime4 = 9650029242287828579ull;
        static constexpr uint64_t Prime5 = 2870177450012600261ull;

        static uint64_t rotateLeft(uint64_t value, int amount)
        {
            return (value << amount) | (value >> (64 - amount));
        }

        static uint32_t read32(const uint8_t *data)
        {
            return
                static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24);
        }

        static uint64_t read64(const uint8_t *data)
        {
            return
                static_cast<uint64_t>(data[0]) |
                (static_cast<uint64_t>(data[1]) << 8) |
                (static_cast<uint64_t>(data[2]) << 16) |
                (static_cast<uint64_t>(data[3]) << 24) |
                (static_cast<uint64_t>(data[4]) << 32) |
                (static_cast<uint64_t>(data[5]) << 40) |
                (static_cast<uint64_t>(data[6]) << 48) |
                (static_cast<uint64_t>(data[7]) << 56);
        }

        static uint64_t round(uint64_t accumulator, uint64_t lane)
        {
            accumulator += lane * Prime2;
            accumulator = rotateLeft(accumulator, 31);
            accumulator *= Prime1;
            return accumulator;
        }

        static uint64_t mergeRound(uint64_t accumulator, uint64_t value)
        {
            accumulator ^= round(0, value);
            accumulator = accumulator * Prime1 + Prime4;
            return accumulator;
        }

        void processStripe(const uint8_t *data)
        {
            mV1 = round(mV1, read64(data + 0));
            mV2 = round(mV2, read64(data + 8));
            mV3 = round(mV3, read64(data + 16));
            mV4 = round(mV4, read64(data + 24));
        }

        uint64_t mSeed = 0;
        uint64_t mTotalLength = 0;
        uint64_t mV1 = 0;
        uint64_t mV2 = 0;
        uint64_t mV3 = 0;
        uint64_t mV4 = 0;
        size_t mBufferSize = 0;
        uint8_t mBuffer[BufferCapacity] = {};
    };
} // namespace GVM::RHI::Detail
