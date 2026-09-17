#ifndef UGLC_TEST_HOST_SAMPLER_RECORD_PRESERVES_BODY_HPP
#define UGLC_TEST_HOST_SAMPLER_RECORD_PRESERVES_BODY_HPP

#include "UGL.h"

using namespace UGL;

/** Provides a bind group whose only resource is a host-created sampler. */
struct HostSamplerRecordPreservesBodyBindGroup final : public IBindGroup
{
    /** Captures the sampler used to verify host record body preservation. */
    constructor(Sampler sampler [[Binding0]])
    {
    }
};

/** Verifies that ordinary host objects can store samplers without becoming shader-only shells. */
class HostSamplerRecordPreservesBodyNode final : public UObject
{
    Sampler cachedSampler;
    BindGroup<HostSamplerRecordPreservesBodyBindGroup> cachedBindGroup;

public:
    /** Creates a sampler, stores it on the host object, and builds a bind group from it. */
    constructor(Device device)
    {
        cachedSampler = device->createSampler({
            .label = "HostSamplerRecordPreservesBodySampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0,
            .lodMaxClamp = 1.0,
            .maxAnisotropy = 1
        });
        cachedBindGroup = device->createBindGroup<HostSamplerRecordPreservesBodyBindGroup>(cachedSampler);
    }

    /** Returns the bind group built by the preserved host constructor body. */
    BindGroup<HostSamplerRecordPreservesBodyBindGroup> getBindGroup() const
    {
        return cachedBindGroup;
    }
};

#endif
