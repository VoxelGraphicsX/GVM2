#include "Private/DeviceObjectCache.hpp"

#include "Private/RHIHashXXH64.hpp"

#include <EASTL/sort.h>
#include <EASTL/string_view.h>

#include <type_traits>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Appends the bytes of a scalar, enum, bool, float or small flags field to the key; never a whole struct, so padding stays out. */
        template <typename T>
        void appendPod(DeviceObjectKey &key, const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= 8,
                "appendPod only encodes scalar, enum, bool, float and small flags fields");
            const auto *data = reinterpret_cast<const uint8_t *>(&value);
            key.bytes.insert(key.bytes.end(), data, data + sizeof(T));
        }

        /** Appends raw bytes to the key; empty input writes nothing (a count was already written by the caller). */
        void appendBytes(DeviceObjectKey &key, const void *data, size_t size)
        {
            if (data == nullptr || size == 0u)
            {
                return;
            }
            const auto *bytes = static_cast<const uint8_t *>(data);
            key.bytes.insert(key.bytes.end(), bytes, bytes + size);
        }

        /** Appends a length-prefixed string to the key. */
        void appendString(DeviceObjectKey &key, const eastl::string_view value)
        {
            appendPod(key, static_cast<uint64_t>(value.size()));
            appendBytes(key, value.data(), value.size());
        }

        /** Appends a child handle's identity to the key and retains the canonical child object. */
        template <typename Handle>
        void appendChild(DeviceObjectKey &key, const Handle &handle)
        {
            appendPod(key, reinterpret_cast<uintptr_t>(handle.get()));
            key.children.emplace_back(handle.get());
        }

        void appendKey(DeviceObjectKey &key, const BufferBindingLayout &layout)
        {
            appendPod(key, layout.type);
            appendPod(key, layout.access);
        }

        void appendKey(DeviceObjectKey &key, const SamplerBindingLayout &layout)
        {
            appendPod(key, layout.type);
        }

        void appendKey(DeviceObjectKey &key, const TextureBindingLayout &layout)
        {
            appendPod(key, layout.sampleType);
            appendPod(key, layout.viewDimension);
        }

        void appendKey(DeviceObjectKey &key, const StorageTextureBindingLayout &layout)
        {
            appendPod(key, layout.access);
            appendPod(key, layout.format);
            appendPod(key, layout.viewDimension);
        }

        void appendKey(DeviceObjectKey &key, const BindGroupLayoutEntry &entry)
        {
            appendPod(key, entry.binding);
            appendPod(key, entry.maxCount);
            appendPod(key, entry.visibility);
            appendKey(key, entry.buffer);
            appendKey(key, entry.sampler);
            appendKey(key, entry.texture);
            appendKey(key, entry.storageTexture);
        }

        void appendKey(DeviceObjectKey &key, const VertexAttribute &attribute)
        {
            appendPod(key, attribute.format);
            appendPod(key, attribute.offset);
            appendPod(key, attribute.shaderLocation);
        }

        void appendKey(DeviceObjectKey &key, const VertexBufferLayout &layout)
        {
            appendPod(key, layout.arrayStride);
            appendPod(key, layout.stepMode);
            appendPod(key, static_cast<uint64_t>(layout.attributes.size()));
            for (const VertexAttribute &attribute : layout.attributes)
            {
                appendKey(key, attribute);
            }
        }

        void appendKey(DeviceObjectKey &key, const BlendComponent &component)
        {
            appendPod(key, component.operation);
            appendPod(key, component.srcFactor);
            appendPod(key, component.dstFactor);
        }

        void appendKey(DeviceObjectKey &key, const BlendState &blend)
        {
            appendKey(key, blend.color);
            appendKey(key, blend.alpha);
        }

        void appendKey(DeviceObjectKey &key, const ColorTargetState &target)
        {
            appendPod(key, target.format);
            appendKey(key, target.blend);
            appendPod(key, target.blendEnabled);
            appendPod(key, target.writeMask);
            appendPod(key, target.pixelLocal);
        }

        void appendKey(DeviceObjectKey &key, const DepthStencilState &state)
        {
            appendPod(key, state.format);
            appendPod(key, state.depthTestEnabled);
            appendPod(key, state.depthWriteEnabled);
            appendPod(key, state.depthCompare);
            appendPod(key, state.depthBias);
            appendPod(key, state.depthBiasSlopeScale);
            appendPod(key, state.depthBiasClamp);
            appendPod(key, state.pixelLocal);
        }

        void appendKey(DeviceObjectKey &key, const PrimitiveState &state)
        {
            appendPod(key, state.topology);
            appendPod(key, state.stripIndexFormat);
            appendPod(key, state.frontFace);
            appendPod(key, state.cullMode);
        }

        void appendKey(DeviceObjectKey &key, const TessellationStageDescriptor &stage)
        {
            appendChild(key, stage.module);
            appendString(key, stage.entryPoint);
        }

        void appendKey(DeviceObjectKey &key, const TessellationState &state)
        {
            appendKey(key, state.control);
            appendKey(key, state.evaluation);
            appendPod(key, state.patchControlPoints);
            appendPod(key, state.partitionMode);
            appendPod(key, state.outputTopology);
            appendPod(key, state.maxTessellationFactor);
        }

        void appendKey(DeviceObjectKey &key, const VertexState &state)
        {
            appendChild(key, state.module);
            appendString(key, state.entryPoint);
            appendPod(key, static_cast<uint64_t>(state.buffers.size()));
            for (const VertexBufferLayout &buffer : state.buffers)
            {
                appendKey(key, buffer);
            }
        }

        void appendKey(DeviceObjectKey &key, const FragmentState &state)
        {
            appendChild(key, state.module);
            appendString(key, state.entryPoint);
            appendPod(key, static_cast<uint64_t>(state.targets.size()));
            for (const ColorTargetState &target : state.targets)
            {
                appendKey(key, target);
            }
        }

        void appendKey(DeviceObjectKey &key, const ComputeStageDescriptor &stage)
        {
            appendChild(key, stage.module);
            appendString(key, stage.entryPoint);
            appendPod(key, stage.workgroupX);
            appendPod(key, stage.workgroupY);
            appendPod(key, stage.workgroupZ);
        }

        void appendKey(DeviceObjectKey &key, const PixelLocalPassAttachmentAccess &access)
        {
            appendPod(key, access.colorReadMask);
            appendPod(key, access.colorWriteMask);
            appendPod(key, access.depthWrite);
        }

        void appendKey(DeviceObjectKey &key, const ArgumentBufferLayout &layout)
        {
            appendPod(key, layout.addressOffset);
            appendPod(key, layout.lengthOffset);
            appendPod(key, layout.type);
            appendPod(key, layout.visibility);
        }

        void appendKey(DeviceObjectKey &key, const ArgumentResourceLayout &layout)
        {
            appendPod(key, layout.offset);
            appendKey(key, layout.entry);
        }

        void appendKey(DeviceObjectKey &key, const ArgumentLayoutDescriptor &layout)
        {
            appendPod(key, layout.byteSize);
            appendPod(key, static_cast<uint64_t>(layout.buffers.size()));
            for (const ArgumentBufferLayout &buffer : layout.buffers)
            {
                appendKey(key, buffer);
            }
            appendPod(key, static_cast<uint64_t>(layout.resources.size()));
            for (const ArgumentResourceLayout &resource : layout.resources)
            {
                appendKey(key, resource);
            }
        }

        void appendKey(DeviceObjectKey &key, const ShaderModuleDescriptor &descriptor)
        {
            appendString(key, descriptor.code);
            appendPod(key, static_cast<uint64_t>(descriptor.spirv.size()));
            appendBytes(key, descriptor.spirv.data(), descriptor.spirv.size() * sizeof(uint32_t));
            appendPod(key, static_cast<uint64_t>(descriptor.metallib.size()));
            appendBytes(key, descriptor.metallib.data(), descriptor.metallib.size());
        }

        void appendKey(DeviceObjectKey &key, const BindGroupLayoutDescriptor &descriptor)
        {
            eastl::vector<BindGroupLayoutEntry> entries = descriptor.entries;
            eastl::sort(entries.begin(), entries.end(), [](const BindGroupLayoutEntry &lhs, const BindGroupLayoutEntry &rhs)
            {
                return lhs.binding < rhs.binding;
            });
            appendPod(key, static_cast<uint64_t>(entries.size()));
            for (const BindGroupLayoutEntry &entry : entries)
            {
                appendKey(key, entry);
            }
        }

        void appendKey(DeviceObjectKey &key, const PipelineLayoutDescriptor &descriptor)
        {
            appendPod(key, static_cast<uint64_t>(descriptor.bindGroupLayouts.size()));
            for (const BindGroupLayout &layout : descriptor.bindGroupLayouts)
            {
                appendChild(key, layout);
            }
            appendKey(key, descriptor.arguments);
        }

        void appendKey(DeviceObjectKey &key, const ComputePipelineDescriptor &descriptor)
        {
            appendChild(key, descriptor.layout);
            appendKey(key, descriptor.compute);
        }

        void appendKey(DeviceObjectKey &key, const RenderPipelineDescriptor &descriptor)
        {
            appendChild(key, descriptor.layout);
            appendKey(key, descriptor.vertex);
            appendPod(key, descriptor.tessellation.has_value());
            if (descriptor.tessellation.has_value())
            {
                appendKey(key, descriptor.tessellation.value());
            }
            appendKey(key, descriptor.primitive);
            appendKey(key, descriptor.depthStencil);
            appendKey(key, descriptor.fragment);
            appendKey(key, descriptor.pixelLocalAttachmentAccess);
        }

        /** Builds the canonical key for a descriptor and finalizes its hash. */
        template <typename Descriptor>
        DeviceObjectKey makeKey(const Descriptor &descriptor)
        {
            DeviceObjectKey key;
            appendKey(key, descriptor);
            Detail::XXH64State state;
            state.update(key.bytes.data(), key.bytes.size());
            key.hash = state.digest();
            return key;
        }
    } // namespace

    ShaderModule DeviceObjectCache::find(const ShaderModuleDescriptor &descriptor)
    {
        const DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return findInTable(mShaderModules, key);
    }

    ShaderModule DeviceObjectCache::insert(const ShaderModuleDescriptor &descriptor, ShaderModule created)
    {
        DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return insertIntoTable(mShaderModules, eastl::move(key), created);
    }

    BindGroupLayout DeviceObjectCache::find(const BindGroupLayoutDescriptor &descriptor)
    {
        const DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return findInTable(mBindGroupLayouts, key);
    }

    BindGroupLayout DeviceObjectCache::insert(const BindGroupLayoutDescriptor &descriptor, BindGroupLayout created)
    {
        DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return insertIntoTable(mBindGroupLayouts, eastl::move(key), created);
    }

    PipelineLayout DeviceObjectCache::find(const PipelineLayoutDescriptor &descriptor)
    {
        const DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return findInTable(mPipelineLayouts, key);
    }

    PipelineLayout DeviceObjectCache::insert(const PipelineLayoutDescriptor &descriptor, PipelineLayout created)
    {
        DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return insertIntoTable(mPipelineLayouts, eastl::move(key), created);
    }

    ComputePipeline DeviceObjectCache::find(const ComputePipelineDescriptor &descriptor)
    {
        const DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return findInTable(mComputePipelines, key);
    }

    ComputePipeline DeviceObjectCache::insert(const ComputePipelineDescriptor &descriptor, ComputePipeline created)
    {
        DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return insertIntoTable(mComputePipelines, eastl::move(key), created);
    }

    RenderPipeline DeviceObjectCache::find(const RenderPipelineDescriptor &descriptor)
    {
        const DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return findInTable(mRenderPipelines, key);
    }

    RenderPipeline DeviceObjectCache::insert(const RenderPipelineDescriptor &descriptor, RenderPipeline created)
    {
        DeviceObjectKey key = makeKey(descriptor);
        std::lock_guard<std::mutex> lock(mMutex);
        return insertIntoTable(mRenderPipelines, eastl::move(key), created);
    }

    void DeviceObjectCache::releaseUnreferenced()
    {
        eastl::vector<RenderPipeline> renderPipelines;
        eastl::vector<ComputePipeline> computePipelines;
        eastl::vector<PipelineLayout> pipelineLayouts;
        eastl::vector<BindGroupLayout> bindGroupLayouts;
        eastl::vector<ShaderModule> shaderModules;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            collectUnreferenced(mRenderPipelines, renderPipelines);
            collectUnreferenced(mComputePipelines, computePipelines);
            collectUnreferenced(mPipelineLayouts, pipelineLayouts);
            collectUnreferenced(mBindGroupLayouts, bindGroupLayouts);
            collectUnreferenced(mShaderModules, shaderModules);
        }
    }

    void DeviceObjectCache::clear()
    {
        Table<ShaderModule> shaderModules;
        Table<BindGroupLayout> bindGroupLayouts;
        Table<PipelineLayout> pipelineLayouts;
        Table<ComputePipeline> computePipelines;
        Table<RenderPipeline> renderPipelines;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRenderPipelines.swap(renderPipelines);
            mComputePipelines.swap(computePipelines);
            mPipelineLayouts.swap(pipelineLayouts);
            mBindGroupLayouts.swap(bindGroupLayouts);
            mShaderModules.swap(shaderModules);
        }
    }
} // namespace GVM::RHI::Private
