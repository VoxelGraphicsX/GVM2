#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_READ_IN_RENDER_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_READ_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalReadRenderVertexOutput
{
    float4 position [[Position]];
};

struct InvalidPixelLocalReadRenderFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;
};

class InvalidPixelLocalReadRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalReadRenderVertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidPixelLocalReadRenderVertexOutput outputValue;
        outputValue.position = vertexID == 0u ? float4(-1.0f, -1.0f, 0.5f, 1.0f)
                                              : vertexID == 1u ? float4(3.0f, -1.0f, 0.5f, 1.0f)
                                                               : float4(-1.0f, 3.0f, 0.5f, 1.0f);
        return outputValue;
    }

    InvalidPixelLocalReadRenderFrame fragment(InvalidPixelLocalReadRenderVertexOutput)
    {
        InvalidPixelLocalReadRenderFrame outputValue;
        half4 albedoValue = outputValue.albedo.read();
        outputValue.albedo = albedoValue;
        return outputValue;
    }
};

#endif
