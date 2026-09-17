#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/utility.h>

#include <cstddef>
#include <cstdint>

namespace GVM::RHI::Vulkan::CacheDetail
{
    template <typename Entry>
    class VKHashBucketCache final
    {
    public:
        using Storage = eastl::unordered_multimap<uint64_t, Entry>;
        using iterator = typename Storage::iterator;
        using const_iterator = typename Storage::const_iterator;

        template <typename MatchFn>
        Entry *findMatching(uint64_t hash, MatchFn &&match)
        {
            const auto [bucketBegin, bucketEnd] = mEntries.equal_range(hash);
            for (auto entryIt = bucketBegin; entryIt != bucketEnd; ++entryIt)
            {
                if (match(entryIt->second))
                {
                    return &entryIt->second;
                }
            }

            return nullptr;
        }

        template <typename MatchFn, typename MatchContext>
        Entry *findMatching(uint64_t hash, MatchFn &&match, const MatchContext &context)
        {
            const auto [bucketBegin, bucketEnd] = mEntries.equal_range(hash);
            for (auto entryIt = bucketBegin; entryIt != bucketEnd; ++entryIt)
            {
                if (match(entryIt->second, context))
                {
                    return &entryIt->second;
                }
            }

            return nullptr;
        }

        template <typename MatchFn>
        const Entry *findMatching(uint64_t hash, MatchFn &&match) const
        {
            const auto [bucketBegin, bucketEnd] = mEntries.equal_range(hash);
            for (auto entryIt = bucketBegin; entryIt != bucketEnd; ++entryIt)
            {
                if (match(entryIt->second))
                {
                    return &entryIt->second;
                }
            }

            return nullptr;
        }

        template <typename MatchFn, typename MatchContext>
        const Entry *findMatching(uint64_t hash, MatchFn &&match, const MatchContext &context) const
        {
            const auto [bucketBegin, bucketEnd] = mEntries.equal_range(hash);
            for (auto entryIt = bucketBegin; entryIt != bucketEnd; ++entryIt)
            {
                if (match(entryIt->second, context))
                {
                    return &entryIt->second;
                }
            }

            return nullptr;
        }

        Entry &insert(uint64_t hash, Entry entry)
        {
            auto inserted = mEntries.emplace(hash, eastl::move(entry));
            return inserted->second;
        }

        template <typename Predicate>
        size_t eraseIf(Predicate &&predicate)
        {
            size_t removedCount = 0u;
            for (auto entryIt = mEntries.begin(); entryIt != mEntries.end();)
            {
                if (predicate(entryIt->second))
                {
                    auto eraseIt = entryIt;
                    ++entryIt;
                    mEntries.erase(eraseIt);
                    ++removedCount;
                    continue;
                }

                ++entryIt;
            }

            return removedCount;
        }

        void erase(iterator entryIt)
        {
            mEntries.erase(entryIt);
        }

        void clear()
        {
            mEntries.clear();
        }

        [[nodiscard]]
        bool empty() const
        {
            return mEntries.empty();
        }

        [[nodiscard]]
        size_t size() const
        {
            return mEntries.size();
        }

        [[nodiscard]]
        iterator begin()
        {
            return mEntries.begin();
        }

        [[nodiscard]]
        const_iterator begin() const
        {
            return mEntries.begin();
        }

        [[nodiscard]]
        iterator end()
        {
            return mEntries.end();
        }

        [[nodiscard]]
        const_iterator end() const
        {
            return mEntries.end();
        }

    private:
        Storage mEntries;
    };
} // namespace GVM::RHI::Vulkan::CacheDetail
