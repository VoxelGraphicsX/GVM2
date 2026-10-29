#pragma once

#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Private
{
    /** Backend-created compute shader module for the shared multi-region copy kernel. */
    struct BufferCopyMultipleRegionShader
    {
        ShaderModule module;
        const char *entryPoint = nullptr;
    };

    /**
     * Backend-resolved inputs of one copyBufferToBufferMultipleRegion call. Usage flags and the CPU view
     * come from the backend because BufferImpl does not expose them publicly.
     */
    struct BufferCopyMultipleRegionInputs
    {
        Buffer source;
        Buffer destination;
        Buffer regions;
        BufferUsageFlags sourceUsage = 0;
        BufferUsageFlags destinationUsage = 0;
        BufferUsageFlags regionsUsage = 0;
        const BufferCopyRegion *cpuRegions = nullptr;
        uint32_t regionCount = 0;
    };

    /**
     * Copies BufferCopyRegion lists through a compute kernel using only public RHI objects, so every
     * backend shares one implementation.
     *
     * Shader contract:
     * - Bind group 0 has three bindings: binding 0 the source as a read-only storage buffer, binding 1 the
     *   destination as a read-write storage buffer, and binding 2 the regions as a read-only storage buffer.
     * - Each region occupies three uint32 values: srcOffset, dstOffset, and size, all in bytes.
     * - The workgroup size is 64x1x1. The workgroup index is the region index and the workgroup's threads
     *   copy that region's 4-byte words cooperatively.
     */
    class BufferCopyMultipleRegionExecutor
    {
    public:
        /**
         * Builds the bind group layout, pipeline layout, and compute pipeline for the shared kernel.
         * The shader module must implement the contract documented on this class.
         */
        void init(DeviceImpl &device, const BufferCopyMultipleRegionShader &shader);

        /** Validates one call on the CPU; call it before obtaining the encoder so a rejected call leaves no queue state behind. */
        void validate(const BufferCopyMultipleRegionInputs &inputs) const;

        /**
         * Records one bind group and dispatch per chunk of at most 65472 regions into the encoder; the
         * inputs must already have passed validate(). Chunking keeps every dispatch below the guaranteed
         * 65535-workgroup limit, and 65472 * 12 bytes stays a multiple of 256 so every chunk range
         * satisfies the maximum minStorageBufferOffsetAlignment.
         */
        void encode(ComputePassEncoderImpl &encoder, const BufferCopyMultipleRegionInputs &inputs);

    private:
        DeviceImpl *mDevice = nullptr;
        BufferCopyMultipleRegionShader mShader = {};
        ComputePipeline mPipeline;
        BindGroupLayout mBindGroupLayout;
        PipelineLayout mPipelineLayout;
    };
} // namespace GVM::RHI::Private
