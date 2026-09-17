#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_INPUT_IN_RENDER_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_INPUT_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalInputRenderVertexOutput
{
    float4 position [[Position]];
};

struct InvalidPixelLocalInputRenderFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;
};

class InvalidPixelLocalInputRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalInputRenderVertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidPixelLocalInputRenderVertexOutput outputValue;
        outputValue.position = vertexID == 0u ? float4(-1.0f, -1.0f, 0.5f, 1.0f)
                                              : vertexID == 1u ? float4(3.0f, -1.0f, 0.5f, 1.0f)
                                                               : float4(-1.0f, 3.0f, 0.5f, 1.0f);
        return outputValue;
    }

    InvalidPixelLocalInputRenderFrame fragment(InvalidPixelLocalInputRenderVertexOutput,
                                               InvalidPixelLocalInputRenderFrame inputValue [[PixelLocalInput]])
    {
        InvalidPixelLocalInputRenderFrame outputValue;
        outputValue.albedo = half4(1.0f, 0.0f, 0.0f, 1.0f);
        return outputValue;
    }
};

#endif
