#ifndef UGLC_TEST_INVALID_RENDER_MISSING_VERTEX_HPP
#define UGLC_TEST_INVALID_RENDER_MISSING_VERTEX_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderMissingVertexFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidRenderMissingVertexPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidRenderMissingVertexFrameBuffer fragment()
    {
        InvalidRenderMissingVertexFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

#endif
