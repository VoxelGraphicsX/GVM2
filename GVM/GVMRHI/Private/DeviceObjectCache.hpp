#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <mutex>

namespace GVM::RHI::Private
{
    /**
     * Canonical byte encoding of a descriptor (label excluded) plus strong references to the child
     * objects it names by identity. Keys compare equal exactly when two descriptors name the same
     * field values and the same canonical child objects.
     */
    struct DeviceObjectKey
    {
        eastl::vector<uint8_t> bytes;
        eastl::vector<eastl::intrusive_ptr<RefCountedObject>> children;
        uint64_t hash = 0;
    };

    /**
     * Deduplicates immutable device objects by descriptor content so equivalent create calls share one
     * backend object; thread-safe. Backends call find first, create outside the cache mutex, then call
     * insert and return its result, so a concurrent inserter wins deterministically.
     */
    class DeviceObjectCache final
    {
    public:
        /** Returns the cached shader module equivalent to descriptor, or nullptr. */
        ShaderModule find(const ShaderModuleDescriptor &descriptor);
        /** Stores created; returns the already cached equivalent instead when another thread inserted one first. */
        ShaderModule insert(const ShaderModuleDescriptor &descriptor, ShaderModule created);
        /** Returns the cached bind group layout equivalent to descriptor, or nullptr. */
        BindGroupLayout find(const BindGroupLayoutDescriptor &descriptor);
        /** Stores created; returns the already cached equivalent instead when another thread inserted one first. */
        BindGroupLayout insert(const BindGroupLayoutDescriptor &descriptor, BindGroupLayout created);
        /** Returns the cached pipeline layout equivalent to descriptor, or nullptr. */
        PipelineLayout find(const PipelineLayoutDescriptor &descriptor);
        /** Stores created; returns the already cached equivalent instead when another thread inserted one first. */
        PipelineLayout insert(const PipelineLayoutDescriptor &descriptor, PipelineLayout created);
        /** Returns the cached compute pipeline equivalent to descriptor, or nullptr. */
        ComputePipeline find(const ComputePipelineDescriptor &descriptor);
        /** Stores created; returns the already cached equivalent instead when another thread inserted one first. */
        ComputePipeline insert(const ComputePipelineDescriptor &descriptor, ComputePipeline created);
        /** Returns the cached render pipeline equivalent to descriptor, or nullptr. */
        RenderPipeline find(const RenderPipelineDescriptor &descriptor);
        /** Stores created; returns the already cached equivalent instead when another thread inserted one first. */
        RenderPipeline insert(const RenderPipelineDescriptor &descriptor, RenderPipeline created);

        /**
         * Drops entries whose object is referenced only by this cache; parents are checked before
         * children. When the reference count is 1 only the cache holds the object, and only the cache
         * (under its mutex) hands out new references, so the check cannot race a hand-out. Backend
         * destructors run after the mutex is released. Children that were still referenced by a freed
         * parent during this call keep a nonzero external count here and are released by a later call.
         */
        void releaseUnreferenced();
        /** Drops every entry; call before the native device is torn down. Backend destructors run after the mutex is released. */
        void clear();

    private:
        /** One cached object keyed by the canonical encoding of the descriptor that created it. */
        template <typename Handle>
        struct Entry
        {
            DeviceObjectKey key;
            Handle handle;
        };

        /** Bucket table mapping a 64-bit key hash to its colliding entries. */
        template <typename Handle>
        using Table = eastl::unordered_multimap<uint64_t, Entry<Handle>>;

        /** Returns the handle of the bucket entry whose stored key bytes equal key, or nullptr. Call only under mMutex. */
        template <typename Handle>
        static Handle findInTable(const Table<Handle> &table, const DeviceObjectKey &key)
        {
            const auto [bucketBegin, bucketEnd] = table.equal_range(key.hash);
            for (auto entryIt = bucketBegin; entryIt != bucketEnd; ++entryIt)
            {
                if (entryIt->second.key.bytes == key.bytes)
                {
                    return entryIt->second.handle;
                }
            }
            return {};
        }

        /** Returns the equivalent cached handle, or stores created under key and returns it. Call only under mMutex. */
        template <typename Handle>
        static Handle insertIntoTable(Table<Handle> &table, DeviceObjectKey &&key, const Handle &created)
        {
            if (Handle cached = findInTable(table, key))
            {
                return cached;
            }
            const uint64_t hash = key.hash;
            table.emplace(hash, Entry<Handle>{eastl::move(key), created});
            return created;
        }

        /** Moves entries whose handle is referenced only by this cache into released. Call only under mMutex. */
        template <typename Handle>
        static void collectUnreferenced(Table<Handle> &table, eastl::vector<Handle> &released)
        {
            for (auto entryIt = table.begin(); entryIt != table.end();)
            {
                if (entryIt->second.handle->getReferenceCount() == 1)
                {
                    released.push_back(eastl::move(entryIt->second.handle));
                    entryIt = table.erase(entryIt);
                    continue;
                }
                ++entryIt;
            }
        }

        Table<ShaderModule> mShaderModules;
        Table<BindGroupLayout> mBindGroupLayouts;
        Table<PipelineLayout> mPipelineLayouts;
        Table<ComputePipeline> mComputePipelines;
        Table<RenderPipeline> mRenderPipelines;
        std::mutex mMutex;
    };
} // namespace GVM::RHI::Private
