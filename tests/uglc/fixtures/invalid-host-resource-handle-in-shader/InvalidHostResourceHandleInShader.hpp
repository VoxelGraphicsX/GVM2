#ifndef UGLC_TEST_INVALID_HOST_RESOURCE_HANDLE_IN_SHADER_HPP
#define UGLC_TEST_INVALID_HOST_RESOURCE_HANDLE_IN_SHADER_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the shader-visible storage buffer used by the invalid compute pass. */
struct InvalidHostResourceHandleInShaderBindGroup final : public IBindGroup
{
    /** Creates the bind group used by the invalid shader pass. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Stores host-only resource handles that cannot be declared or passed inside shader code. */
struct InvalidHostResourceHandleInShaderPack
{
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> texture;
    TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> textureView;
};

/** Accepts a host-only resource pack so shader calls exercise the DSL diagnostic path. */
inline uint readInvalidHostResourcePack(InvalidHostResourceHandleInShaderPack pack)
{
    return 1u;
}

/** Attempts to use a host-only resource record from shader code. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidHostResourceHandleInShaderPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output buffer. */
    constructor(BindGroup<InvalidHostResourceHandleInShaderBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Declares and passes a host-only resource pack inside shader code, which is invalid. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        InvalidHostResourceHandleInShaderPack pack;
        bindGroup->values[threadID.x] = readInvalidHostResourcePack(pack);
    }
};

#endif
