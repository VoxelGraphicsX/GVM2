#ifndef UGLC_TEST_INVALID_SHADER_RESOURCE_BEHAVIOR_STRUCTURED_BUFFER_HPP
#define UGLC_TEST_INVALID_SHADER_RESOURCE_BEHAVIOR_STRUCTURED_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores a shader resource handle and therefore must remain a behavior record, not a buffer element layout. */
struct InvalidShaderResourceBehaviorStructuredBufferElement
{
    Texture2D<float4> texture;
    uint tag;

    /** Returns a plain value so the record looks like a behavior object instead of a passive data layout. */
    uint readTag()
    {
        return tag;
    }
};

/** Attempts to use a shader-resource behavior record as a StructuredBuffer element type. */
struct InvalidShaderResourceBehaviorStructuredBufferBindGroup final : public IBindGroup
{
    /** Creates the invalid bind group whose payload element hides a texture handle. */
    constructor(StructuredBuffer<InvalidShaderResourceBehaviorStructuredBufferElement> elements [[Binding0]],
                RWStructuredBuffer<uint> outputValues [[Binding1]])
    {
    }
};

/** Binds the invalid layout so UGLC validates the StructuredBuffer element type. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidShaderResourceBehaviorStructuredBufferPass final : public IComputeClass
{
public:
    /** Captures the invalid bind group used by the compute pass. */
    constructor(BindGroup<InvalidShaderResourceBehaviorStructuredBufferBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a trivial value; the fixture should fail before this body reaches backend emission. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->outputValues[threadID.x] = 1u;
    }
};

#endif
