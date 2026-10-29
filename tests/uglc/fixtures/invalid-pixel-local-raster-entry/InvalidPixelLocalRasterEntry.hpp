#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_RASTER_ENTRY_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_RASTER_ENTRY_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalRasterVertexOutput
{
    float4 position [[Position]];
};

struct InvalidPixelLocalRasterFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;
};

class InvalidPixelLocalRasterPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalRasterVertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidPixelLocalRasterVertexOutput outputValue;
        outputValue.position = vertexID == 0u ? float4(-1.0f, -1.0f, 0.5f, 1.0f)
                                              : vertexID == 1u ? float4(3.0f, -1.0f, 0.5f, 1.0f)
                                                               : float4(-1.0f, 3.0f, 0.5f, 1.0f);
        return outputValue;
    }

    InvalidPixelLocalRasterFrame fragment(InvalidPixelLocalRasterVertexOutput)
    {
        InvalidPixelLocalRasterFrame outputValue;
        outputValue.albedo = half4(1.0f, 0.0f, 0.0f, 1.0f);
        return outputValue;
    }
};

#endif
