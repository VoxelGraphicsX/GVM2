#include "VKResourceStateDB.hpp"
#include "VKBuffer.hpp"
#include "VKBufferRangeStateUtils.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

namespace GVM::RHI::Vulkan
{
    VKResourceTextureState VKResourceStateDB::makeTextureState(const VKTexture &texture)
    {
        VKResourceTextureState state;
        state.planeAspects = resolveTexturePlaneAspects(&texture);
        state.subresourceCountPerPlane = resolveTexturePlaneSubresourceCount(&texture);
        state.subresources.resize(state.planeAspects.size() * state.subresourceCountPerPlane);
        return state;
    }

    eastl::vector<VKResourceBufferState> &VKResourceStateDB::editBuffer(VKBuffer &buffer)
    {
        ScopedLock lock(mMutex);
        auto [it, inserted] = mBuffers.try_emplace(&buffer);
        if (inserted)
        {
            it->second.push_back({&buffer, 0, buffer.getStorageSize(), {}});
        }
        return it->second;
    }

    VKResourceTextureState &VKResourceStateDB::editTexture(VKTexture &texture)
    {
        ScopedLock lock(mMutex);
        auto [it, inserted] = mTextures.try_emplace(&texture);
        if (inserted)
        {
            it->second = makeTextureState(texture);
        }
        return it->second;
    }

    bool VKResourceStateDB::tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &out) const
    {
        ScopedLock lock(mMutex);
        out = {const_cast<VKBuffer *>(&buffer), 0, buffer.getStorageSize(), {}};
        const auto it = mBuffers.find(&buffer);
        if (it == mBuffers.end())
        {
            return true;
        }
        for (const auto &range : it->second)
        {
            out.access.current.stages |= range.access.current.stages;
            out.access.current.accesses |= range.access.current.accesses;
            out.access.producer.stages |= range.access.producer.stages;
            out.access.producer.accesses |= range.access.producer.accesses;
            out.access.initialized |= range.access.initialized;
            out.access.hasProducer |= range.access.hasProducer;
        }
        return true;
    }

    bool VKResourceStateDB::tryGetSharedTextureStateForTransition(const VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout &layout, vk::PipelineStageFlags2 &stages, vk::AccessFlags2 &accesses) const
    {
        ScopedLock lock(mMutex);
        const auto it = mTextures.find(&texture);
        if (it == mTextures.end())
        {
            layout = vk::ImageLayout::eUndefined;
            stages = {};
            accesses = {};
            return true;
        }
        const auto &state = it->second;
        const auto range = resolveTextureSubresourceRange(&texture, aspect, mip, mipCount, layer, layerCount);
        bool first = true;
        for (size_t plane = 0; plane < state.planeAspects.size(); ++plane)
        {
            if (!texturePlaneMatches(range, state.planeAspects[plane]))
            {
                continue;
            }
            for (uint32_t m = range.baseMipLevel; m < range.mipLevelEnd(); ++m)
            {
                for (uint32_t l = range.baseArrayLayer; l < range.arrayLayerEnd(); ++l)
                {
                    const auto &cell = state.subresources[plane * state.subresourceCountPerPlane + resolveTextureSubresourceIndex(&texture, m, l)];
                    if (!first && layout != cell.layout)
                    {
                        return false;
                    }
                    if (first)
                    {
                        layout = cell.layout;
                        stages = {};
                        accesses = {};
                        first = false;
                    }
                    stages |= cell.access.current.stages | cell.access.producer.stages;
                    accesses |= cell.access.current.accesses | cell.access.producer.accesses;
                }
            }
        }
        return !first;
    }

    void VKResourceStateDB::recordExternalBufferUsage(VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        size = resolveBufferRangeSize("recordExternalBufferUsage", &buffer, offset, size);
        auto &ranges = editBuffer(buffer);
        splitBufferRangeStates(ranges, offset);
        splitBufferRangeStates(ranges, offset + size);
        for (auto &range : ranges)
        {
            if (bufferRangesOverlap(range.offset, range.size, offset, size))
            {
                range.access = {};
                range.access.current = {stages, accesses};
                range.access.initialized = true;
                range.access.hasProducer = !isReadOnlyAccess(accesses);
                if (range.access.hasProducer)
                {
                    range.access.producer = {stages, extractWriteAccessMask(accesses)};
                }
            }
        }
    }

    void VKResourceStateDB::recordExternalBufferUsage(VKBuffer &buffer, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        recordExternalBufferUsage(buffer, 0, WholeSize, stages, accesses);
    }

    void VKResourceStateDB::recordExternalTextureState(VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout layout, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        auto &state = editTexture(texture);
        const auto range = resolveTextureSubresourceRange(&texture, aspect, mip, mipCount, layer, layerCount);
        for (size_t plane = 0; plane < state.planeAspects.size(); ++plane)
        {
            if (!texturePlaneMatches(range, state.planeAspects[plane]))
            {
                continue;
            }
            for (uint32_t m = range.baseMipLevel; m < range.mipLevelEnd(); ++m)
            {
                for (uint32_t l = range.baseArrayLayer; l < range.arrayLayerEnd(); ++l)
                {
                    auto &cell = state.subresources[plane * state.subresourceCountPerPlane + resolveTextureSubresourceIndex(&texture, m, l)];
                    cell = {};
                    cell.layout = layout;
                    cell.access.current = {stages, accesses};
                    cell.access.initialized = true;
                    cell.access.producer = {stages, extractWriteAccessMask(accesses)};
                    cell.access.hasProducer = true;
                }
            }
        }
    }

    bool VKResourceStateDB::isTextureInSteadyStateLayout(const VKTexture &texture) const
    {
        ScopedLock lock(mMutex);
        if (!texture.hasSteadyStateLayout())
        {
            return true;
        }
        const auto it = mTextures.find(&texture);
        if (it == mTextures.end())
        {
            return false;
        }
        for (const auto &cell : it->second.subresources)
        {
            if (cell.layout != texture.getSteadyStateLayout())
            {
                return false;
            }
        }
        return true;
    }

    void VKResourceStateDB::discardBufferState(const VKBuffer &buffer)
    {
        ScopedLock lock(mMutex);
        mBuffers.erase(&buffer);
    }
    void VKResourceStateDB::discardTextureState(const VKTexture &texture)
    {
        ScopedLock lock(mMutex);
        mTextures.erase(&texture);
    }
    void VKResourceStateDB::reset()
    {
        ScopedLock lock(mMutex);
        mBuffers.clear();
        mTextures.clear();
    }
} // namespace GVM::RHI::Vulkan
