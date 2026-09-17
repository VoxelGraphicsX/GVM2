#include "GRenderBufferComponent.hpp"
#include "GGPUVectorFactory.hpp"
#include <EASTL/numeric_limits.h>
#include <iostream>
#include <stdexcept>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
    namespace
    {
        uint64_t getRequiredBufferStorageSize(uint64_t elementStorageSize, uint64_t dataStorageSize, uint32_t instanceCount)
        {
            if (instanceCount != 0 && elementStorageSize > (eastl::numeric_limits<uint64_t>::max() / instanceCount))
            {
                throw std::overflow_error("BufferComponent storage size overflowed uint64_t.");
            }
            const uint64_t instanceStorageSize = uint64_t(instanceCount) * elementStorageSize;
            return eastl::max(dataStorageSize, instanceStorageSize);
        }
    }

    RenderComponent::RenderComponent()
    {
    }

    uint64_t BufferComponent::getBlockCountFromDataStorageSize(uint64_t dataStorageSize) const
    {
        if (mElementStorageSize == 0 || mElementCountInBlock == 0)
        {
            throw std::logic_error("BufferComponent is not initialized with a valid element size and block size.");
        }
        const uint64_t elementCount = xGE::Math::IntRoundUp(dataStorageSize, mElementStorageSize);
        return xGE::Math::IntAlign(elementCount, mElementCountInBlock);
    }

    void BufferComponent::create(GVM::RHI::Device device, const BufferComponentCreateInfo &info)
    {
        mDevice = device;
        mElementStorageSize = info.dataElementStorageSize;
        mElementCountInBlock = info.dataElementCountInBlock;
        mBufferComponentName = info.bufferComponentName;

        const uint64_t dataGrowthBytes = info.dataElementStorageSize * info.dataElementIncreamentCount;
        mDataBuffer = createGPUVectorStorage<uint8_t>(device, info.bufferComponentName, info.bufferUsage, dataGrowthBytes, dataGrowthBytes);
        mDataBuffer->reserveGPUBytes(dataGrowthBytes);
        mDataBuffer->commitStorage();

        const uint64_t indexGrowthBytes = sizeof(RenderComponentIndex) * info.dataElementIncreamentCount;
        mComponentIndexBuffer = createGPUVectorStorage<uint8_t>(device, info.bufferComponentName + "_Index", GVM::RHI::BufferUsage::Storage, indexGrowthBytes, indexGrowthBytes);
        mComponentIndexBuffer->reserveGPUBytes(indexGrowthBytes);
        mComponentIndexBuffer->commitStorage();

        mComponentIndex.create({.dataName = info.bufferComponentName + "_IndexData", .dataElementStorageSize = sizeof(RenderComponentIndex), .dataElementIncreamentCount = info.dataElementIncreamentCount}, RenderComponentNullIndex);

        mEntities.create({.dataName = info.bufferComponentName + "_mEntities", .dataElementStorageSize = sizeof(ComponentUseInfo), .dataElementIncreamentCount = info.dataElementIncreamentCount}, {});
    }

    RenderComponentIndex BufferComponent::alloc(RenderEntityIndex entity, const eastl::string &bufferName, uint64_t dataStorageSize, uint32_t instanceCount)
    {
        RenderComponentIndex index = 0;
        ComponentUseInfo componentUseInfo;

        std::lock_guard lock(mMtx);
        if (!bufferName.empty())
        {
            const auto existingInfoIt = mBufferUseInfos.find(bufferName);
            if (existingInfoIt != mBufferUseInfos.end())
            {
                auto &bufferUseInfo = existingInfoIt->second;
                // In this codebase, callers are allowed to reference an already-created shared
                // buffer by name only. A zero payload size means "bind the existing buffer"
                // rather than "redeclare its capacity", so the existing allocation becomes the
                // authoritative storage contract for this entity.
                const bool isReferenceOnlyRequest = (dataStorageSize == 0);
                if (!isReferenceOnlyRequest)
                {
                    const uint64_t requiredStorageSize = getRequiredBufferStorageSize(mElementStorageSize, dataStorageSize, instanceCount);
                    if (bufferUseInfo.bufferStorageSize != requiredStorageSize)
                    {
                        throw std::invalid_argument("BufferComponent received the same buffer name with a different explicit storage size.");
                    }
                }
                bufferUseInfo.useCount++;
                index = bufferUseInfo.index;

                componentUseInfo.blockCount = bufferUseInfo.blockCount;
                componentUseInfo.bufferStorageSize = bufferUseInfo.bufferStorageSize;
                componentUseInfo.bufferName = bufferName;
                componentUseInfo.index = index;

                mEntities.write(entity, componentUseInfo);
                mComponentIndex.write(entity, componentUseInfo.index);

                this->mComponentIndexBuffer->reserveGPUBytes((entity + 1) * sizeof(RenderComponentIndex));
                this->mDataBuffer->reserveGPUBytes((uint64_t(componentUseInfo.index) * mElementStorageSize) + componentUseInfo.bufferStorageSize);
                return index;
            }
        }

        const uint64_t requiredStorageSize = getRequiredBufferStorageSize(mElementStorageSize, dataStorageSize, instanceCount);
        if (requiredStorageSize == 0)
        {
            throw std::invalid_argument("BufferComponent cannot allocate a new buffer with zero storage size.");
        }

        const uint64_t blockCount = eastl::max<uint64_t>(1u, getBlockCountFromDataStorageSize(requiredStorageSize));
        index = mFreeList.allocIndex(blockCount);
        componentUseInfo.bufferName = bufferName;
        componentUseInfo.index = index;
        componentUseInfo.blockCount = blockCount;
        componentUseInfo.bufferStorageSize = requiredStorageSize;

        if (!bufferName.empty())
        {
            mBufferUseInfos.emplace(bufferName, BufferUseInfo{
                                                    .index = index,
                                                    .blockCount = blockCount,
                                                    .useCount = 1,
                                                    .bufferStorageSize = requiredStorageSize,
                                                });
        }

        mEntities.write(entity, componentUseInfo);
        mComponentIndex.write(entity, componentUseInfo.index);

        this->mComponentIndexBuffer->reserveGPUBytes((entity + 1) * sizeof(RenderComponentIndex));
        this->mDataBuffer->reserveGPUBytes((uint64_t(componentUseInfo.index) * mElementStorageSize) + componentUseInfo.bufferStorageSize);

        return index;
    }

    RenderComponentIndexCopyInfo BufferComponent::getComponentIndexAllocInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mComponentIndex.contains(entity))
        {
            return info;
        }
        const RenderComponentIndex componentIndex = mComponentIndex.read(entity);
        info.GPUBufferDstOffset = entity * sizeof(RenderComponentIndex);
        info.size = sizeof(RenderComponentIndex);
        info.CPUDataSrcOffset = allocator.appendRaw(&componentIndex, info.size, alignof(RenderComponentIndex));
        return info;
    }

    RenderComponentIndexCopyInfo BufferComponent::getComponentIndexRemoveInfo(RenderEntityIndex entity, StagingLinearAllocator &allocator)
    {
        RenderComponentIndexCopyInfo info{};
        if (!mComponentIndex.contains(entity))
        {
            return info;
        }
        info.GPUBufferDstOffset = entity * sizeof(RenderComponentIndex);
        info.size = sizeof(RenderComponentIndex);
        info.CPUDataSrcOffset = allocator.appendRaw(&RenderComponentNullIndex, info.size, alignof(RenderComponentIndex));
        return info;
    }


    uint64_t BufferComponent::getPerEntityComponentIndexStorageBytes() const
    {
        return sizeof(RenderComponentIndex);
    }

    GVM::RHI::Buffer BufferComponent::getDataBuffer() const
    {
        return mDataBuffer != nullptr ? mDataBuffer->gpuBuffer() : GVM::RHI::Buffer{};
    }

    GVM::RHI::Buffer BufferComponent::getComponentIndexBuffer() const
    {
        return mComponentIndexBuffer != nullptr ? mComponentIndexBuffer->gpuBuffer() : GVM::RHI::Buffer{};
    }

    void BufferComponent::appendBindGroupLayoutEntries(eastl::vector<GVM::RHI::BindGroupLayoutEntry> &entries, uint32_t &bindingIndex) const
    {
        GVM::RHI::BindGroupLayoutEntry componentIndexEntry = {};
        componentIndexEntry.binding = bindingIndex++;
        componentIndexEntry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        componentIndexEntry.buffer.type = GVM::RHI::BufferBindingType::Storage;
        componentIndexEntry.buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        entries.push_back(componentIndexEntry);

        GVM::RHI::BindGroupLayoutEntry resourceEntry = getBindGroupLayoutEntry();
        resourceEntry.binding = bindingIndex++;
        resourceEntry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        entries.push_back(resourceEntry);
    }

    void BufferComponent::appendBindGroupEntries(eastl::vector<GVM::RHI::BindGroupEntry> &entries, uint32_t &bindingIndex) const
    {
        GVM::RHI::BindGroupEntry componentIndexEntry = {};
        componentIndexEntry.binding = bindingIndex++;
        componentIndexEntry.buffer = GVM::RHI::BufferRange(getComponentIndexBuffer());
        entries.push_back(componentIndexEntry);

        GVM::RHI::BindGroupEntry resourceEntry = getResourceBindGroupEntry();
        resourceEntry.binding = bindingIndex++;
        entries.push_back(resourceEntry);
    }

    RenderComponentBindingGeneration BufferComponent::getBindGroupGeneration() const
    {
        return {
            .resource = getResourceGenerationCounter(),
            .componentIndex = getComponentIndexBufferGenerationCounter(),
        };
    }

    uint64_t BufferComponent::getResourceGenerationCounter() const
    {
        return mDataBuffer != nullptr ? mDataBuffer->generation() : 0u;
    }

    uint64_t BufferComponent::getComponentIndexBufferGenerationCounter() const
    {
        return mComponentIndexBuffer != nullptr ? mComponentIndexBuffer->generation() : 0u;
    }

    bool BufferComponent::check(const eastl::string &bufferName) const
    {
        std::lock_guard lock(mMtx);
        if (bufferName.empty())
        {
            return false;
        }
        auto res = mBufferUseInfos.find(bufferName);
        return res != mBufferUseInfos.end();
    }

    bool BufferComponent::check(RenderEntityIndex entity) const
    {
        std::lock_guard lock(mMtx);
        return mComponentIndex.contains(entity) && mComponentIndex.read(entity) != RenderComponentNullIndex;
    }

    RenderComponentIndex BufferComponent::getComponentIndexByEntity(RenderEntityIndex entity) const
    {
        if (check(entity) == false)
        {
            return RenderComponentNullIndex;
        }
        return mEntities.read(entity).index;
    }

    GVM::RHI::BindGroupLayoutEntry BufferComponent::getBindGroupLayoutEntry() const
    {
        GVM::RHI::BindGroupLayoutEntry entry = {};
        entry.visibility = GVM::RHI::ShaderStage::Compute | GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        entry.buffer.type = GVM::RHI::BufferBindingType::Storage;
        entry.buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
        entry.maxCount = 1;

        return entry;
    }

    GVM::RHI::BindGroupEntry BufferComponent::getResourceBindGroupEntry() const
    {
        GVM::RHI::BindGroupEntry entry = {};
        entry.buffer = GVM::RHI::BufferRange(getDataBuffer());

        return entry;
    }

    uint64_t BufferComponent::getResourceElementCountForRenderSetAccessBounds() const
    {
        if (mElementStorageSize == 0u)
        {
            return 0u;
        }
        return mDataBuffer != nullptr ? (mDataBuffer->gpuCapacityBytes() / mElementStorageSize) : 0u;
    }

    uint64_t BufferComponent::getComponentIndexElementCountForRenderSetAccessBounds() const
    {
        return mComponentIndexBuffer != nullptr ? (mComponentIndexBuffer->gpuCapacityBytes() / sizeof(RenderComponentIndex)) : 0u;
    }

    uint64_t BufferComponent::getComponentStorageByteByName(const eastl::string &bufferName) const
    {
        std::lock_guard lock(mMtx);
        auto res = mBufferUseInfos.find(bufferName);
        if (res != mBufferUseInfos.end())
        {
            return res->second.bufferStorageSize;
        }
        throw std::runtime_error(std::string("cannot find buffer component: ") + bufferName.c_str());
    }

    uint64_t BufferComponent::getComponentElementStorageBytes() const
    {
        return mElementStorageSize;
    }

    void BufferComponent::resizeBuffer()
    {
        this->mComponentIndexBuffer->commitStorage();
        this->mDataBuffer->commitStorage();
    }


    uint32_t BufferComponent::getDataBufferCopyOffset(RenderEntityIndex entity, uint32_t instanceStartIndex) const
    {
        const auto &componentInfo = mEntities.read(entity);
        return (uint32_t)((componentInfo.index + instanceStartIndex) * mElementStorageSize);
    }

    void BufferComponent::update()
    {
        this->mComponentIndexBuffer->commitStorage();
        this->mDataBuffer->commitStorage();
    }


    void BufferComponent::remove(RenderEntityIndex entity)
    {
        std::lock_guard lock(mMtx);
        if (!mComponentIndex.contains(entity) || mComponentIndex.read(entity) == RenderComponentNullIndex)
        {
            return;
        }
        auto componentInfo = mEntities.read(entity);

        if (mBufferUseInfos.find(componentInfo.bufferName) != mBufferUseInfos.end())
        {
            auto &bufferUseInfo = mBufferUseInfos.at(componentInfo.bufferName);
            bufferUseInfo.useCount--;
            if (bufferUseInfo.useCount == 0)
            {
                mFreeList.collectIndex(bufferUseInfo.index, bufferUseInfo.blockCount);
                mBufferUseInfos.erase(componentInfo.bufferName);
            }
        }
        else
        {
            mFreeList.collectIndex(mComponentIndex.read(entity), componentInfo.blockCount);
        }
        mEntities.write(entity, {});
        mComponentIndex.write(entity, RenderComponentNullIndex);
    }

    void BufferComponent::destroy()
    {
        if (mDataBuffer != nullptr)
        {
            mDataBuffer->destroy();
            mDataBuffer.reset();
        }
        if (mComponentIndexBuffer != nullptr)
        {
            mComponentIndexBuffer->destroy();
            mComponentIndexBuffer.reset();
        }
        mComponentIndex.clear();
        mEntities.clear();
        mBufferUseInfos.clear();
    }

} // namespace GVM::Core
