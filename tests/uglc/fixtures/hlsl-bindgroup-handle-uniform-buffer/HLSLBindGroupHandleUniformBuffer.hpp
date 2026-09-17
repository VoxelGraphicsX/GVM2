#ifndef UGLC_TEST_HLSL_BINDGROUP_HANDLE_UNIFORM_BUFFER_HPP
#define UGLC_TEST_HLSL_BINDGROUP_HANDLE_UNIFORM_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores uniform values read through a BindGroup handle-backed behavior object. */
struct BindGroupHandleUniformParams
{
    uint4 packedValues;
};

/** Provides a uniform buffer and writable output storage for BindGroup handle lowering. */
struct BindGroupHandleUniformBindGroup final : public IBindGroup
{
    /** Creates the bind group with a uniform buffer and output storage buffer. */
    constructor(UniformBuffer<BindGroupHandleUniformParams> params [[Binding0]],
                RWStructuredBuffer<uint> outputValues [[Binding1]])
    {
    }
};

/** Stores a BindGroup handle that contains a UniformBuffer resource. */
template <class BindGroupType>
struct UniformBindGroupHandleAccess
{
    /** Stores the bind group selected by the compute pass. */
    BindGroup<BindGroupType> storedBindGroup;

    /** Initializes the behavior object with the compute pass bind group handle. */
    void init(BindGroup<BindGroupType> bindGroup)
    {
        storedBindGroup = bindGroup;
    }

    /** Reads uniform data through the stored bind group handle and returns a computed value. */
    uint readUniformValue(uint index)
    {
        const BindGroupHandleUniformParams paramsValue = storedBindGroup->params->read();
        return paramsValue.packedValues.x + paramsValue.packedValues.y + index;
    }

    /** Writes a value through the stored bind group handle. */
    void writeOutput(uint index, uint value)
    {
        storedBindGroup->outputValues[index] = value;
    }
};

/** Verifies that HLSL BindGroup handle structs preserve UniformBuffer access through behavior methods. */
class [[LocalWorkGroupSize(1, 1, 1)]] HLSLBindGroupHandleUniformBufferPass final : public IComputeClass
{
public:
    /** Captures the bind group used by the compute pass. */
    constructor(BindGroup<BindGroupHandleUniformBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads a uniform value through a stored BindGroup handle and writes it to storage. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        UniformBindGroupHandleAccess<BindGroupHandleUniformBindGroup> handleAccess;
        handleAccess.init(bindGroup);
        handleAccess.writeOutput(index, handleAccess.readUniformValue(index));
    }
};

#endif
