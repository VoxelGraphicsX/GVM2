#include "VKDescriptorCacheUtils.hpp"

#include "VKBindingUtils.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKPipelineLayout.hpp"
#include "VKShaderModule.hpp"

#include "Private/RHIHashXXH64.hpp"

#include <EASTL/bit.h>
#include <EASTL/algorithm.h>
#include <EASTL/string_view.h>

namespace GVM::RHI::Vulkan::Detail
{
    namespace
    {
        using GVM::RHI::Detail::XXH64State;

        bool equalFloat(float lhs, float rhs)
        {
            return eastl::bit_cast<uint32_t>(lhs) == eastl::bit_cast<uint32_t>(rhs);
        }

        void hashShaderModuleHandle(XXH64State &state, const ShaderModule &shaderModule)
        {
            const bool hasShader = static_cast<bool>(shaderModule);
            state.updatePod(hasShader);
            if (!hasShader)
            {
                return;
            }

            const auto &descriptor = static_cast<const VKShaderModule *>(shaderModule.get())->getDescriptor();
            const uint64_t wordCount = static_cast<uint64_t>(descriptor.spirv.size());
            state.updatePod(wordCount);
            if (!descriptor.spirv.empty())
            {
                state.update(descriptor.spirv.data(), descriptor.spirv.size() * sizeof(uint32_t));
            }
        }

        bool equalShaderModuleHandle(const ShaderModule &lhs, const ShaderModule &rhs)
        {
            if (lhs.get() == rhs.get())
            {
                return true;
            }
            if (!lhs || !rhs)
            {
                return !lhs && !rhs;
            }
            return equalShaderModuleDescriptor(
                static_cast<const VKShaderModule *>(lhs.get())->getDescriptor(),
                static_cast<const VKShaderModule *>(rhs.get())->getDescriptor());
        }

        eastl::vector<BindGroupLayoutEntry> getSortedBindGroupLayoutEntries(const BindGroupLayoutDescriptor &descriptor)
        {
            eastl::vector<BindGroupLayoutEntry> entries = descriptor.entries;
            eastl::sort(entries.begin(), entries.end(), [](const BindGroupLayoutEntry &lhs, const BindGroupLayoutEntry &rhs)
            {
                return lhs.binding < rhs.binding;
            });
            return entries;
        }

        eastl::vector<BindGroupEntry> getSortedBindGroupEntries(const BindGroupDescriptor &descriptor)
        {
            eastl::vector<BindGroupEntry> entries = descriptor.entries;
            eastl::sort(entries.begin(), entries.end(), [](const BindGroupEntry &lhs, const BindGroupEntry &rhs)
            {
                return lhs.binding < rhs.binding;
            });
            return entries;
        }

        void hashBufferBindingLayout(XXH64State &state, const BufferBindingLayout &layout)
        {
            state.updateEnum(layout.type);
            state.updateEnum(layout.access);
        }

        bool equalBufferBindingLayout(const BufferBindingLayout &lhs, const BufferBindingLayout &rhs)
        {
            return lhs.type == rhs.type && lhs.access == rhs.access;
        }

        void hashSamplerBindingLayout(XXH64State &state, const SamplerBindingLayout &layout)
        {
            state.updateEnum(layout.type);
        }

        bool equalSamplerBindingLayout(const SamplerBindingLayout &lhs, const SamplerBindingLayout &rhs)
        {
            return lhs.type == rhs.type;
        }

        void hashTextureBindingLayout(XXH64State &state, const TextureBindingLayout &layout)
        {
            state.updateEnum(layout.sampleType);
            state.updateEnum(layout.viewDimension);
        }

        bool equalTextureBindingLayout(const TextureBindingLayout &lhs, const TextureBindingLayout &rhs)
        {
            return lhs.sampleType == rhs.sampleType && lhs.viewDimension == rhs.viewDimension;
        }

        void hashStorageTextureBindingLayout(XXH64State &state, const StorageTextureBindingLayout &layout)
        {
            state.updateEnum(layout.access);
            state.updateEnum(layout.format);
            state.updateEnum(layout.viewDimension);
        }

        bool equalStorageTextureBindingLayout(const StorageTextureBindingLayout &lhs, const StorageTextureBindingLayout &rhs)
        {
            return
                lhs.access == rhs.access &&
                lhs.format == rhs.format &&
                lhs.viewDimension == rhs.viewDimension;
        }

        void hashBindGroupLayoutEntry(XXH64State &state, const BindGroupLayoutEntry &entry)
        {
            state.updatePod(entry.binding);
            state.updatePod(entry.maxCount);
            state.updatePod(entry.visibility);
            hashBufferBindingLayout(state, entry.buffer);
            hashSamplerBindingLayout(state, entry.sampler);
            hashTextureBindingLayout(state, entry.texture);
            hashStorageTextureBindingLayout(state, entry.storageTexture);
        }

        bool equalBindGroupLayoutEntry(const BindGroupLayoutEntry &lhs, const BindGroupLayoutEntry &rhs)
        {
            return
                lhs.binding == rhs.binding &&
                lhs.maxCount == rhs.maxCount &&
                lhs.visibility == rhs.visibility &&
                equalBufferBindingLayout(lhs.buffer, rhs.buffer) &&
                equalSamplerBindingLayout(lhs.sampler, rhs.sampler) &&
                equalTextureBindingLayout(lhs.texture, rhs.texture) &&
                equalStorageTextureBindingLayout(lhs.storageTexture, rhs.storageTexture);
        }

        void hashBindGroupLayoutHandle(XXH64State &state, const BindGroupLayout &layout)
        {
            const bool hasLayout = static_cast<bool>(layout);
            state.updatePod(hasLayout);
            if (!hasLayout)
            {
                return;
            }

            const auto &descriptor = static_cast<const VKBindGroupLayout *>(layout.get())->getDescriptor();
            state.updatePod(hashBindGroupLayoutDescriptor(descriptor));
        }

        bool equalBindGroupLayoutHandle(const BindGroupLayout &lhs, const BindGroupLayout &rhs)
        {
            if (lhs.get() == rhs.get())
            {
                return true;
            }
            if (!lhs || !rhs)
            {
                return !lhs && !rhs;
            }
            return equalBindGroupLayoutDescriptor(
                static_cast<const VKBindGroupLayout *>(lhs.get())->getDescriptor(),
                static_cast<const VKBindGroupLayout *>(rhs.get())->getDescriptor());
        }

        void hashBufferRange(XXH64State &state, const BufferRange &range)
        {
            const uintptr_t handleValue = reinterpret_cast<uintptr_t>(range.buffer.get());
            state.updatePod(handleValue);
            state.updatePod(range.offset);
            state.updatePod(range.size);
        }

        bool equalBufferRange(const BufferRange &lhs, const BufferRange &rhs)
        {
            return lhs.buffer.get() == rhs.buffer.get() && lhs.offset == rhs.offset && lhs.size == rhs.size;
        }

        void hashBindGroupEntry(XXH64State &state, const BindGroupEntry &entry)
        {
            state.updatePod(entry.binding);
            const auto entryKind = Detail::getBindGroupEntryKind(entry);
            state.updateEnum(entryKind);

            switch (entryKind)
            {
            case Detail::BindGroupEntryKind::Buffer:
            {
                const uint64_t count = static_cast<uint64_t>(entry.buffer.size());
                state.updatePod(count);
                for (const BufferRange &bufferRange : entry.buffer)
                {
                    hashBufferRange(state, bufferRange);
                }
                break;
            }
            case Detail::BindGroupEntryKind::Sampler:
            {
                const uint64_t count = static_cast<uint64_t>(entry.sampler.size());
                state.updatePod(count);
                for (const Sampler &sampler : entry.sampler)
                {
                    const uintptr_t handleValue = reinterpret_cast<uintptr_t>(sampler.get());
                    state.updatePod(handleValue);
                }
                break;
            }
            case Detail::BindGroupEntryKind::TextureView:
            {
                const uint64_t count = static_cast<uint64_t>(entry.textureView.size());
                state.updatePod(count);
                for (const TextureView &view : entry.textureView)
                {
                    const uintptr_t handleValue = reinterpret_cast<uintptr_t>(view.get());
                    state.updatePod(handleValue);
                }
                break;
            }
            default:
                break;
            }
        }

        bool equalBindGroupEntry(const BindGroupEntry &lhs, const BindGroupEntry &rhs)
        {
            const auto lhsKind = Detail::getBindGroupEntryKind(lhs);
            const auto rhsKind = Detail::getBindGroupEntryKind(rhs);
            if (lhs.binding != rhs.binding || lhsKind != rhsKind)
            {
                return false;
            }

            switch (lhsKind)
            {
            case Detail::BindGroupEntryKind::Buffer:
                if (lhs.buffer.size() != rhs.buffer.size())
                {
                    return false;
                }
                for (size_t index = 0; index < lhs.buffer.size(); ++index)
                {
                    if (!equalBufferRange(lhs.buffer[index], rhs.buffer[index]))
                    {
                        return false;
                    }
                }
                return true;

            case Detail::BindGroupEntryKind::Sampler:
                if (lhs.sampler.size() != rhs.sampler.size())
                {
                    return false;
                }
                for (size_t index = 0; index < lhs.sampler.size(); ++index)
                {
                    if (lhs.sampler[index].get() != rhs.sampler[index].get())
                    {
                        return false;
                    }
                }
                return true;

            case Detail::BindGroupEntryKind::TextureView:
                if (lhs.textureView.size() != rhs.textureView.size())
                {
                    return false;
                }
                for (size_t index = 0; index < lhs.textureView.size(); ++index)
                {
                    if (lhs.textureView[index].get() != rhs.textureView[index].get())
                    {
                        return false;
                    }
                }
                return true;

            default:
                return lhsKind == Detail::BindGroupEntryKind::Undefined;
            }
        }

        void hashVertexAttribute(XXH64State &state, const VertexAttribute &attribute)
        {
            state.updateEnum(attribute.format);
            state.updatePod(attribute.offset);
            state.updatePod(attribute.shaderLocation);
        }

        bool equalVertexAttribute(const VertexAttribute &lhs, const VertexAttribute &rhs)
        {
            return
                lhs.format == rhs.format &&
                lhs.offset == rhs.offset &&
                lhs.shaderLocation == rhs.shaderLocation;
        }

        void hashVertexBufferLayout(XXH64State &state, const VertexBufferLayout &layout)
        {
            state.updatePod(layout.arrayStride);
            state.updateEnum(layout.stepMode);
            const uint64_t attributeCount = static_cast<uint64_t>(layout.attributes.size());
            state.updatePod(attributeCount);
            for (const VertexAttribute &attribute : layout.attributes)
            {
                hashVertexAttribute(state, attribute);
            }
        }

        bool equalVertexBufferLayout(const VertexBufferLayout &lhs, const VertexBufferLayout &rhs)
        {
            if (lhs.arrayStride != rhs.arrayStride || lhs.stepMode != rhs.stepMode || lhs.attributes.size() != rhs.attributes.size())
            {
                return false;
            }
            for (size_t index = 0; index < lhs.attributes.size(); ++index)
            {
                if (!equalVertexAttribute(lhs.attributes[index], rhs.attributes[index]))
                {
                    return false;
                }
            }
            return true;
        }

        void hashBlendComponent(XXH64State &state, const BlendComponent &component)
        {
            state.updateEnum(component.operation);
            state.updateEnum(component.srcFactor);
            state.updateEnum(component.dstFactor);
        }

        bool equalBlendComponent(const BlendComponent &lhs, const BlendComponent &rhs)
        {
            return
                lhs.operation == rhs.operation &&
                lhs.srcFactor == rhs.srcFactor &&
                lhs.dstFactor == rhs.dstFactor;
        }

        void hashBlendState(XXH64State &state, const BlendState &blend)
        {
            hashBlendComponent(state, blend.color);
            hashBlendComponent(state, blend.alpha);
        }

        bool equalBlendState(const BlendState &lhs, const BlendState &rhs)
        {
            return equalBlendComponent(lhs.color, rhs.color) && equalBlendComponent(lhs.alpha, rhs.alpha);
        }

        void hashColorTargetState(XXH64State &state, const ColorTargetState &target)
        {
            state.updateEnum(target.format);
            hashBlendState(state, target.blend);
            state.updatePod(target.blendEnabled);
            state.updatePod(target.writeMask);
            state.updatePod(target.pixelLocal);
        }

        bool equalColorTargetState(const ColorTargetState &lhs, const ColorTargetState &rhs)
        {
            return
                lhs.format == rhs.format &&
                equalBlendState(lhs.blend, rhs.blend) &&
                lhs.blendEnabled == rhs.blendEnabled &&
                lhs.writeMask == rhs.writeMask &&
                lhs.pixelLocal == rhs.pixelLocal;
        }

        void hashDepthStencilState(XXH64State &state, const DepthStencilState &descriptor)
        {
            state.updateEnum(descriptor.format);
            state.updatePod(descriptor.depthTestEnabled);
            state.updatePod(descriptor.depthWriteEnabled);
            state.updateEnum(descriptor.depthCompare);
            state.updatePod(descriptor.depthBias);
            state.updateFloat(descriptor.depthBiasSlopeScale);
            state.updateFloat(descriptor.depthBiasClamp);
            state.updatePod(descriptor.pixelLocal);
        }

        bool equalDepthStencilState(const DepthStencilState &lhs, const DepthStencilState &rhs)
        {
            return
                lhs.format == rhs.format &&
                lhs.depthTestEnabled == rhs.depthTestEnabled &&
                lhs.depthWriteEnabled == rhs.depthWriteEnabled &&
                lhs.depthCompare == rhs.depthCompare &&
                lhs.depthBias == rhs.depthBias &&
                equalFloat(lhs.depthBiasSlopeScale, rhs.depthBiasSlopeScale) &&
                equalFloat(lhs.depthBiasClamp, rhs.depthBiasClamp) &&
                lhs.pixelLocal == rhs.pixelLocal;
        }

        void hashPixelLocalPassAttachmentAccess(XXH64State &state, const PixelLocalPassAttachmentAccess &access)
        {
            state.updatePod(access.colorReadMask);
            state.updatePod(access.colorWriteMask);
            state.updatePod(access.depthWrite);
        }

        void hashPrimitiveState(XXH64State &state, const PrimitiveState &descriptor)
        {
            state.updateEnum(descriptor.topology);
            state.updateEnum(descriptor.stripIndexFormat);
            state.updateEnum(descriptor.frontFace);
            state.updateEnum(descriptor.cullMode);
        }

        bool equalPrimitiveState(const PrimitiveState &lhs, const PrimitiveState &rhs)
        {
            return
                lhs.topology == rhs.topology &&
                lhs.stripIndexFormat == rhs.stripIndexFormat &&
                lhs.frontFace == rhs.frontFace &&
                lhs.cullMode == rhs.cullMode;
        }

        void hashVertexState(XXH64State &state, const VertexState &descriptor)
        {
            hashShaderModuleHandle(state, descriptor.module);
            state.updateString(eastl::string_view(descriptor.entryPoint.data(), descriptor.entryPoint.size()));

            const uint64_t bufferCount = static_cast<uint64_t>(descriptor.buffers.size());
            state.updatePod(bufferCount);
            for (const VertexBufferLayout &buffer : descriptor.buffers)
            {
                hashVertexBufferLayout(state, buffer);
            }
        }

        bool equalVertexState(const VertexState &lhs, const VertexState &rhs)
        {
            if (!equalShaderModuleHandle(lhs.module, rhs.module) ||
                lhs.entryPoint != rhs.entryPoint ||
                lhs.buffers.size() != rhs.buffers.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.buffers.size(); ++index)
            {
                if (!equalVertexBufferLayout(lhs.buffers[index], rhs.buffers[index]))
                {
                    return false;
                }
            }
            return true;
        }

        void hashTessellationStageDescriptor(XXH64State &state, const TessellationStageDescriptor &descriptor)
        {
            hashShaderModuleHandle(state, descriptor.module);
            state.updateString(eastl::string_view(descriptor.entryPoint.data(), descriptor.entryPoint.size()));
        }

        bool equalTessellationStageDescriptor(const TessellationStageDescriptor &lhs, const TessellationStageDescriptor &rhs)
        {
            return equalShaderModuleHandle(lhs.module, rhs.module) && lhs.entryPoint == rhs.entryPoint;
        }

        void hashTessellationState(XXH64State &state, const TessellationState &descriptor)
        {
            hashTessellationStageDescriptor(state, descriptor.control);
            hashTessellationStageDescriptor(state, descriptor.evaluation);
            state.updatePod(descriptor.patchControlPoints);
            state.updateEnum(descriptor.partitionMode);
            state.updateEnum(descriptor.outputTopology);
            state.updatePod(descriptor.maxTessellationFactor);
        }

        bool equalTessellationState(const TessellationState &lhs, const TessellationState &rhs)
        {
            return
                equalTessellationStageDescriptor(lhs.control, rhs.control) &&
                equalTessellationStageDescriptor(lhs.evaluation, rhs.evaluation) &&
                lhs.patchControlPoints == rhs.patchControlPoints &&
                lhs.partitionMode == rhs.partitionMode &&
                lhs.outputTopology == rhs.outputTopology &&
                lhs.maxTessellationFactor == rhs.maxTessellationFactor;
        }

        void hashFragmentState(XXH64State &state, const FragmentState &descriptor)
        {
            hashShaderModuleHandle(state, descriptor.module);
            state.updateString(eastl::string_view(descriptor.entryPoint.data(), descriptor.entryPoint.size()));

            const uint64_t targetCount = static_cast<uint64_t>(descriptor.targets.size());
            state.updatePod(targetCount);
            for (const ColorTargetState &target : descriptor.targets)
            {
                hashColorTargetState(state, target);
            }
        }

        bool equalFragmentState(const FragmentState &lhs, const FragmentState &rhs)
        {
            if (!equalShaderModuleHandle(lhs.module, rhs.module) ||
                lhs.entryPoint != rhs.entryPoint ||
                lhs.targets.size() != rhs.targets.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.targets.size(); ++index)
            {
                if (!equalColorTargetState(lhs.targets[index], rhs.targets[index]))
                {
                    return false;
                }
            }
            return true;
        }

        void hashComputeStageDescriptor(XXH64State &state, const ComputeStageDescriptor &descriptor)
        {
            hashShaderModuleHandle(state, descriptor.module);
            state.updateString(eastl::string_view(descriptor.entryPoint.data(), descriptor.entryPoint.size()));
            state.updatePod(descriptor.workgroupX);
            state.updatePod(descriptor.workgroupY);
            state.updatePod(descriptor.workgroupZ);
        }

        bool equalComputeStageDescriptor(const ComputeStageDescriptor &lhs, const ComputeStageDescriptor &rhs)
        {
            return
                equalShaderModuleHandle(lhs.module, rhs.module) &&
                lhs.entryPoint == rhs.entryPoint &&
                lhs.workgroupX == rhs.workgroupX &&
                lhs.workgroupY == rhs.workgroupY &&
                lhs.workgroupZ == rhs.workgroupZ;
        }
    } // namespace

    uint64_t hashShaderModuleDescriptor(const ShaderModuleDescriptor &descriptor)
    {
        XXH64State state;
        const uint64_t wordCount = static_cast<uint64_t>(descriptor.spirv.size());
        state.updatePod(wordCount);
        if (!descriptor.spirv.empty())
        {
            state.update(descriptor.spirv.data(), descriptor.spirv.size() * sizeof(uint32_t));
        }
        return state.digest();
    }

    bool equalShaderModuleDescriptor(const ShaderModuleDescriptor &lhs, const ShaderModuleDescriptor &rhs)
    {
        return lhs.spirv == rhs.spirv;
    }

    uint64_t hashBindGroupLayoutDescriptor(const BindGroupLayoutDescriptor &descriptor)
    {
        XXH64State state;
        const eastl::vector<BindGroupLayoutEntry> entries = getSortedBindGroupLayoutEntries(descriptor);
        const uint64_t entryCount = static_cast<uint64_t>(entries.size());
        state.updatePod(entryCount);
        for (const BindGroupLayoutEntry &entry : entries)
        {
            hashBindGroupLayoutEntry(state, entry);
        }
        return state.digest();
    }

    bool equalBindGroupLayoutDescriptor(const BindGroupLayoutDescriptor &lhs, const BindGroupLayoutDescriptor &rhs)
    {
        const eastl::vector<BindGroupLayoutEntry> lhsEntries = getSortedBindGroupLayoutEntries(lhs);
        const eastl::vector<BindGroupLayoutEntry> rhsEntries = getSortedBindGroupLayoutEntries(rhs);
        if (lhsEntries.size() != rhsEntries.size())
        {
            return false;
        }

        for (size_t index = 0; index < lhsEntries.size(); ++index)
        {
            if (!equalBindGroupLayoutEntry(lhsEntries[index], rhsEntries[index]))
            {
                return false;
            }
        }
        return true;
    }

    uint64_t hashPipelineLayoutDescriptor(const PipelineLayoutDescriptor &descriptor)
    {
        XXH64State state;
        const uint64_t layoutCount = static_cast<uint64_t>(descriptor.bindGroupLayouts.size());
        state.updatePod(layoutCount);
        for (const BindGroupLayout &layout : descriptor.bindGroupLayouts)
        {
            hashBindGroupLayoutHandle(state, layout);
        }
        return state.digest();
    }

    bool equalPipelineLayoutDescriptor(const PipelineLayoutDescriptor &lhs, const PipelineLayoutDescriptor &rhs)
    {
        if (lhs.bindGroupLayouts.size() != rhs.bindGroupLayouts.size())
        {
            return false;
        }

        for (size_t index = 0; index < lhs.bindGroupLayouts.size(); ++index)
        {
            if (!equalBindGroupLayoutHandle(lhs.bindGroupLayouts[index], rhs.bindGroupLayouts[index]))
            {
                return false;
            }
        }
        return true;
    }

    uint64_t hashBindGroupDescriptor(const BindGroupDescriptor &descriptor)
    {
        XXH64State state;
        hashBindGroupLayoutHandle(state, descriptor.layout);

        const eastl::vector<BindGroupEntry> entries = getSortedBindGroupEntries(descriptor);
        const uint64_t entryCount = static_cast<uint64_t>(entries.size());
        state.updatePod(entryCount);
        for (const BindGroupEntry &entry : entries)
        {
            hashBindGroupEntry(state, entry);
        }
        return state.digest();
    }

    uint64_t hashComputePipelineDescriptor(const ComputePipelineDescriptor &descriptor)
    {
        XXH64State state;
        const bool hasLayout = static_cast<bool>(descriptor.layout);
        state.updatePod(hasLayout);
        if (hasLayout)
        {
            state.updatePod(hashPipelineLayoutDescriptor(static_cast<const VKPipelineLayout *>(descriptor.layout.get())->getDescriptor()));
        }
        hashComputeStageDescriptor(state, descriptor.compute);
        return state.digest();
    }

    bool equalComputePipelineDescriptor(const ComputePipelineDescriptor &lhs, const ComputePipelineDescriptor &rhs)
    {
        if (lhs.layout.get() == rhs.layout.get())
        {
            return equalComputeStageDescriptor(lhs.compute, rhs.compute);
        }

        if (!lhs.layout || !rhs.layout)
        {
            return !lhs.layout && !rhs.layout && equalComputeStageDescriptor(lhs.compute, rhs.compute);
        }

        return
            equalPipelineLayoutDescriptor(
                static_cast<const VKPipelineLayout *>(lhs.layout.get())->getDescriptor(),
                static_cast<const VKPipelineLayout *>(rhs.layout.get())->getDescriptor()) &&
            equalComputeStageDescriptor(lhs.compute, rhs.compute);
    }

    uint64_t hashRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor)
    {
        XXH64State state;
        const bool hasLayout = static_cast<bool>(descriptor.layout);
        state.updatePod(hasLayout);
        if (hasLayout)
        {
            state.updatePod(hashPipelineLayoutDescriptor(static_cast<const VKPipelineLayout *>(descriptor.layout.get())->getDescriptor()));
        }

        hashVertexState(state, descriptor.vertex);
        const bool hasTessellation = descriptor.tessellation.has_value();
        state.updatePod(hasTessellation);
        if (hasTessellation)
        {
            hashTessellationState(state, descriptor.tessellation.value());
        }
        hashPrimitiveState(state, descriptor.primitive);
        hashDepthStencilState(state, descriptor.depthStencil);
        hashFragmentState(state, descriptor.fragment);
        hashPixelLocalPassAttachmentAccess(state, descriptor.pixelLocalAttachmentAccess);
        return state.digest();
    }

    bool equalRenderPipelineDescriptor(const RenderPipelineDescriptor &lhs, const RenderPipelineDescriptor &rhs)
    {
        if (lhs.layout.get() == rhs.layout.get())
        {
            // Fast path below still validates the rest of the descriptor state.
        }
        else
        {
            if (!lhs.layout || !rhs.layout)
            {
                return false;
            }
            if (!equalPipelineLayoutDescriptor(
                    static_cast<const VKPipelineLayout *>(lhs.layout.get())->getDescriptor(),
                    static_cast<const VKPipelineLayout *>(rhs.layout.get())->getDescriptor()))
            {
                return false;
            }
        }

        if (!equalVertexState(lhs.vertex, rhs.vertex) ||
            lhs.tessellation.has_value() != rhs.tessellation.has_value())
        {
            return false;
        }
        if (lhs.tessellation.has_value() && !equalTessellationState(lhs.tessellation.value(), rhs.tessellation.value()))
        {
            return false;
        }

        return
            equalPrimitiveState(lhs.primitive, rhs.primitive) &&
            equalDepthStencilState(lhs.depthStencil, rhs.depthStencil) &&
            equalFragmentState(lhs.fragment, rhs.fragment) &&
            lhs.pixelLocalAttachmentAccess == rhs.pixelLocalAttachmentAccess;
    }
} // namespace GVM::RHI::Vulkan::Detail
