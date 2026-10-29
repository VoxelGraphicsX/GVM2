#ifndef UGLC_TEST_INVALID_RENDER_CLIP_DISTANCE_HPP
#define UGLC_TEST_INVALID_RENDER_CLIP_DISTANCE_HPP

#include "UGL.h"
using namespace UGL;

/** Exercises the portable hardware clip-plane limit before shader emission. */
class InvalidRenderClipDistancePass final : public IRenderClass
{
    struct VertexOutput
    {
        float4 position [[Position]];
        // Deliberately exceed the documented portable limit of eight by one plane.
        [[ClipDistance]] float planes[9];
    };

    struct FrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    constructor()
    {
    }

    VertexOutput vertex(uint vertexIndex [[VertexID]])
    {
        VertexOutput output;
        output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
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
