#pragma once

#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Private
{
    /** Validates that a snapshot's value, buffer, and resource counts match the pipeline's Args layout. */
    void validateArgumentSnapshotShape(const ArgumentLayoutDescriptor &layout, const ArgumentsDescriptor &descriptor);

    /** Validates declared usage, four-byte alignment, and storage bounds for one Args buffer range; returns its resolved byte length. */
    uint64_t validateArgumentBufferRange(const ArgumentBufferLayout &field, BufferUsageFlags bufferUsage,
        uint64_t storageSize, const BufferRange &range);

    /** Validates that an Args sampler field carries a live sampler with the declared comparison mode. */
    void validateArgumentSampler(const BindGroupLayoutEntry &entry, const ArgumentResource &resource,
        bool samplerLive, bool comparisonSampler);

    /** Validates that an Args texture field carries a live view matching the declared dimension, usage, and format. */
    void validateArgumentTextureView(const BindGroupLayoutEntry &entry, const ArgumentResource &resource,
        bool viewLive, TextureViewDimension viewDimension, TextureUsageFlags textureUsage, TextureFormat viewFormat);

    /** Validates four-byte alignment and storage bounds for one bind-group buffer range; returns its resolved byte length. */
    uint64_t validateBindGroupBufferRange(uint64_t storageSize, const BufferRange &range);

    /** Throws std::logic_error when a pipeline declares Args but no snapshot was bound since the last pipeline change. */
    void requireArgumentSnapshot(bool pipelineDeclaresArguments, bool snapshotBound);

} // namespace GVM::RHI::Private
