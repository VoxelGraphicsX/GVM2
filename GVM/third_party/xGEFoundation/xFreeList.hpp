#pragma once

#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/vector.h>
#include <mutex>


namespace xGE::Primitive
{
    using FreeListIndex = uint64_t;

    /**
     * @brief 工业级无限增长空闲链表 (Infinite FreeList)
     * 策略: Best-Fit (复用), Auto-Grow (自动扩容), Coalescing (合并), Thread-Safe
     */
    class xFreeList
    {
    public:
        static constexpr FreeListIndex kInvalidIndex = static_cast<FreeListIndex>(-1);

    private:
        mutable std::mutex mMutex;

        // Best-Fit 复用池
        eastl::multimap<FreeListIndex, FreeListIndex> mFreeBlocksBySize; // Size -> Offset
        eastl::map<FreeListIndex, FreeListIndex> mFreeBlocksByOffset;    // Offset -> Size

        // 高水位线：记录当前世界的"尽头"
        // 任何无法通过复用满足的请求，都会导致这个值增加
        FreeListIndex mNextNewIndex = 0;

        // 统计总空闲量（仅包含被回收的碎片，不包含未开辟的无限空间）
        size_t mRecycledFreeSize = 0;

    public:
        xFreeList() = default;
        ~xFreeList() = default;

        xFreeList(const xFreeList &) = delete;
        xFreeList &operator=(const xFreeList &) = delete;

        /**
         * @brief 分配索引
         * 逻辑: 优先在回收池里找(省空间)，找不到就往后开辟新空间(无限增长)
         */
        FreeListIndex allocIndex(FreeListIndex reqSize = 1)
        {
            if (reqSize == 0)
                return kInvalidIndex;

            std::lock_guard<std::mutex> lock(mMutex);

            // ---------------------------------------------------------
            // 1. 尝试复用 (Reuse / Best-Fit)
            // ---------------------------------------------------------
            auto itSize = mFreeBlocksBySize.lower_bound(reqSize);

            if (itSize != mFreeBlocksBySize.end())
            {
                // 找到了可复用的空闲块
                FreeListIndex blockSize = itSize->first;
                FreeListIndex blockOffset = itSize->second;

                // 移除记录
                mFreeBlocksBySize.erase(itSize);
                mFreeBlocksByOffset.erase(blockOffset);
                mRecycledFreeSize -= blockSize;

                // 切分 (Splitting)
                if (blockSize > reqSize)
                {
                    FreeListIndex remainingSize = blockSize - reqSize;
                    FreeListIndex remainingOffset = blockOffset + reqSize;

                    mFreeBlocksBySize.emplace(remainingSize, remainingOffset);
                    mFreeBlocksByOffset.emplace(remainingOffset, remainingSize);
                    mRecycledFreeSize += remainingSize;
                }

                return blockOffset;
            }

            // ---------------------------------------------------------
            // 2. 自动扩容 (Grow)
            // 没有合适的空闲块，直接开辟新地盘
            // ---------------------------------------------------------
            FreeListIndex newOffset = mNextNewIndex;

            // 简单地让水位线上涨
            mNextNewIndex += reqSize;

            return newOffset;
        }

        /**
         * @brief 回收索引
         * 即使是扩容产生的新索引，用完后也必须回收，以便下次被复用
         */
        void collectIndex(FreeListIndex startIndex, FreeListIndex spaceSize = 1)
        {
            if (spaceSize == 0)
                return;

            std::lock_guard<std::mutex> lock(mMutex);

            // 【AAA级优化】: 如果回收的块正好在水位线的末尾，直接降低水位线
            // 这样可以防止索引无限膨胀，保持紧凑
            if (startIndex + spaceSize == mNextNewIndex)
            {
                mNextNewIndex -= spaceSize;

                // 此时，新的末尾可能还能跟前面的空闲块合并，进而继续降低水位线
                // 这是一个循环回缩的过程 (Trim Top)
                while (true)
                {
                    if (mFreeBlocksByOffset.empty())
                        break;

                    // 找最后一个空闲块
                    auto lastBlockIt = mFreeBlocksByOffset.end();
                    --lastBlockIt; // 指向最大的 offset

                    // 如果最后一个空闲块紧贴着当前水位线
                    if (lastBlockIt->first + lastBlockIt->second == mNextNewIndex)
                    {
                        // 降低水位线
                        mNextNewIndex -= lastBlockIt->second;

                        // 从 Map 中移除这个记录，因为它已经变成“未分配空间”了
                        removeFreeBlockRecord(lastBlockIt->first, lastBlockIt->second);
                        mRecycledFreeSize -= lastBlockIt->second;
                    }
                    else
                    {
                        break; // 不连续，停止回缩
                    }
                }
                return;
            }

            // 常规回收逻辑 (Coalescing)
            FreeListIndex finalStart = startIndex;
            FreeListIndex finalSize = spaceSize;

            // 1. 向右合并
            auto itRight = mFreeBlocksByOffset.find(startIndex + spaceSize);
            if (itRight != mFreeBlocksByOffset.end())
            {
                finalSize += itRight->second;
                removeFreeBlockRecord(itRight->first, itRight->second);
            }

            // 2. 向左合并
            auto itLeft = mFreeBlocksByOffset.lower_bound(startIndex);
            if (itLeft != mFreeBlocksByOffset.begin())
            {
                --itLeft;
                if (itLeft->first + itLeft->second == startIndex)
                {
                    finalStart = itLeft->first;
                    finalSize += itLeft->second;
                    removeFreeBlockRecord(itLeft->first, itLeft->second);
                }
            }

            // 3. 存入空闲表
            mFreeBlocksByOffset.emplace(finalStart, finalSize);
            mFreeBlocksBySize.emplace(finalSize, finalStart);
            mRecycledFreeSize += spaceSize; // 注意这里只加回收量
        }

        size_t getRecycledBlockCount() const
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return mFreeBlocksByOffset.size();
        }

        /**
         * @brief 获取当前占用的最大边界（即开辟了多大的世界）
         */
        FreeListIndex getHighWaterMark() const
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return mNextNewIndex;
        }

        void reset()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mFreeBlocksBySize.clear();
            mFreeBlocksByOffset.clear();
            mNextNewIndex = 0;
            mRecycledFreeSize = 0;
        }

    private:
        void removeFreeBlockRecord(FreeListIndex offset, FreeListIndex size)
        {
            mFreeBlocksByOffset.erase(offset);
            auto range = mFreeBlocksBySize.equal_range(size);
            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == offset)
                {
                    mFreeBlocksBySize.erase(it);
                    break;
                }
            }
        }
    };
} // namespace xGE::Primitive
