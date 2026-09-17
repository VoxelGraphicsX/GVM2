#ifndef UGLC_TEST_SHADER_STATIC_VARIANT_COMPUTE_HPP
#define UGLC_TEST_SHADER_STATIC_VARIANT_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

/// Provides the storage buffer resource shared by the compute shader static variant specializations.
struct StaticVariantComputeBindGroup final : public IBindGroup
{
    /// Creates the compute bind group with the writable value buffer used by the fixture.
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

template <uint WorkGroupSize>
/// Exercises compute shader static variant materialization through a constexpr workgroup-size NTTP.
class [[LocalWorkGroupSize(WorkGroupSize, 1, 1)]] StaticVariantComputePass final : public IComputeClass
{
public:
    /// Creates the compute pass specialization with its concrete bind group layout.
    constructor(BindGroup<StaticVariantComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /// Applies a variant-specific increment and validates that if constexpr emits only the selected branch.
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        if constexpr (WorkGroupSize == 8u)
        {
            bindGroup->values[index] = bindGroup->values[index] + 1u;
        }
        else
        {
            bindGroup->values[index] = bindGroup->values[index] + 2u;
        }
    }
};

using StaticVariantCompute8 = StaticVariantComputePass<8u>;
using StaticVariantCompute16 = StaticVariantComputePass<16u>;

/// Hosts two concrete compute class specializations so UGLC emits separate workgroup-size variants.
class ShaderStaticVariantComputeRenderer final : public AbstractRenderer
{
    Device device;
    Buffer<uint, BufferUsage<Storage, CopyDst>> values;
    BindGroup<StaticVariantComputeBindGroup> bindGroup;
    ComputeClass<StaticVariantCompute8> compute8;
    ComputeClass<StaticVariantCompute16> compute16;

public:
    /// Creates the shared storage buffer, bind group, and compute class wrappers used by the fixture.
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        values = device->createBuffer("StaticVariantComputeValues", 128);
        bindGroup = device->createBindGroup<StaticVariantComputeBindGroup>(values);
        compute8 = device->createComputeClass<StaticVariantCompute8>(bindGroup);
        compute16 = device->createComputeClass<StaticVariantCompute16>(bindGroup);
    }

    /// Leaves rendering empty because this fixture validates generated compute code only.
    void render() override
    {
    }

    /// Leaves teardown empty because the fixture owns only reference-counted DSL resources.
    void destroy() override
    {
    }
};

#endif
