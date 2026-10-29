#ifndef UGLC_TEST_RENDER_CLIP_DISTANCE_HPP
#define UGLC_TEST_RENDER_CLIP_DISTANCE_HPP

#include "UGL.h"
using namespace UGL;

/** Tests rasterizer clipping independently of fragment discard and user varyings. */
class RenderClipDistancePass final : public IRenderClass
{
    struct VertexOutput
    {
        float4 position [[Position]];
        // A two-dimensional rectangle has left/right and bottom/top clipping planes.
        [[ClipDistance]] float planes[4];
    };

    struct FrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

  public:
    constructor()
    {
    }

  private:
    VertexOutput vertex(uint vertexIndex [[VertexID]])
    {
        VertexOutput output;
        // Oversized NDC triangle (-1,-1),(-1,3),(3,-1), at midrange depth 0.5.
        output.position = float4(vertexIndex == 2u ? 3.0f : -1.0f,
            vertexIndex == 1u ? 3.0f : -1.0f, 0.5f, 1.0f);
        // Array order: left, right, bottom, top; zero is the retained half-space boundary.
        output.planes[0] = output.position.w + output.position.x;
        output.planes[1] = output.position.w - output.position.x;
        output.planes[2] = output.position.w + output.position.y;
        output.planes[3] = output.position.w - output.position.y;
        return output;
    }

    FrameBuffer fragment(VertexOutput input)
    {
        FrameBuffer output;
        output.color = half4(1.0f);
        return output;
    }
};

#endif
