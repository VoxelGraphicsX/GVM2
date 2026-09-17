#ifndef UGLC_TEST_RWTEXTURE2DARRAY_WRITE_LOWERING_HPP
#define UGLC_TEST_RWTEXTURE2DARRAY_WRITE_LOWERING_HPP

#include "UGL.h"

using namespace UGL;

/**
 * Binds a writable 2D texture array for backend lowering validation.
 * The fixture intentionally uses a scalar half format so each write argument has a distinct type and role.
 */
struct RWTexture2DArrayWriteBindGroup final : public IBindGroup
{
    /** Declares the writable texture array consumed by the compute pass. */
    constructor(RWTexture2DArray<TextureFormat::R16Float> targetArray [[Binding0]])
    {
    }
};

/**
 * Emits direct and dispatch-derived RWTexture2DArray writes.
 * The generated HLSL and MSL must preserve the DSL argument order as coordinate, layer, then value.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] RWTexture2DArrayWritePass final : public IComputeClass
{
public:
    /** Stores the bind group used by the generated compute class. */
    constructor(BindGroup<RWTexture2DArrayWriteBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes stable values to a texture array so the backend member-call lowering can be asserted. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->targetArray->write(uint2(3u, 5u), 2u, half(0.75f));
        bindGroup->targetArray->write(threadID.xy, threadID.z, half(0.25f));
    }
};

#endif
