#include "GStagingLinearAllocator.hpp"

namespace GVM::Core
{

    // 预分配内存，避免频繁扩容
    void StagingLinearAllocator::reserve(size_t capacity)
    {
        mData.reserve(capacity);
    }

    // 重置分配器，复用内存
    void StagingLinearAllocator::reset()
    {
        mData.clear();
    }

    // 追加原始字节
    uint64_t StagingLinearAllocator::appendRaw(const void *data, size_t bytes, size_t align)
    {
        if (bytes == 0)
        {
            return mData.size();
        }
        if (data == nullptr) // 假设单次拷贝不超过 2GB，根据实际需求调整
        {
            // 可以在这里加日志或断言
            throw std::runtime_error("StagingLinearAllocator::appendRaw received invalid data (potential underflow)!");
            return mData.size();
        }
        // 2. 致命错误检查：拦截异常巨大的 Size (防止整数溢出导致的 Crash)
        // 0xFFFFFFF8 这种明显的下溢值会被拦截
        if (bytes > 0x80000000) // 假设单次拷贝不超过 2GB，根据实际需求调整
        {
            // 可以在这里加日志或断言
            throw std::runtime_error("StagingLinearAllocator::appendRaw received invalid size (potential underflow)! size: " + std::to_string(bytes));
            return mData.size();
        }

        // 计算对齐
        size_t currentSize = mData.size();
        size_t alignedSize = xGE::Math::IntAlign(currentSize, align);

        // 填补对齐产生的空隙 (Padding)
        if (alignedSize > currentSize)
        {
            mData.resize(alignedSize);
        }

        // 记录数据起始偏移
        uint64_t offset = mData.size();

        // 扩展 Buffer 并拷贝数据
        mData.insert(mData.end(), (const uint8_t *)data, (const uint8_t *)data + bytes);

        return offset;
    }

    uint64_t StagingLinearAllocator::appendUninitialized(size_t bytes, size_t align)
    {
        if (bytes == 0)
        {
            return mData.size();
        }
        size_t currentSize = mData.size();
        size_t alignedSize = xGE::Math::IntAlign(currentSize, align);
        if (alignedSize > currentSize)
        {
            mData.resize(alignedSize);
        }

        const uint64_t offset = mData.size();
        mData.resize(mData.size() + bytes);
        return offset;
    }

    const uint8_t *StagingLinearAllocator::data() const
    {
        return mData.data();
    }

    uint8_t *StagingLinearAllocator::mutableData()
    {
        return mData.data();
    }

    size_t StagingLinearAllocator::size() const
    {
        return mData.size();
    }

    bool StagingLinearAllocator::empty() const
    {
        return mData.empty();
    }


} // namespace GVM::Core
