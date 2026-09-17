#ifndef UGLC_TEST_RENDER_VERTEX_ONLY_HPP
#define UGLC_TEST_RENDER_VERTEX_ONLY_HPP

#include "UGL.h"

using namespace UGL;

struct RenderVertexOnlyInput
{
    float4 pos [[Attribute0]];
};

struct RenderVertexOnlyOutput
{
    float4 pos [[Position]];
};

class RenderVertexOnlyPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    RenderVertexOnlyOutput vertex(uint vid [[VertexID]], RenderVertexOnlyInput inputValue [[VertexInput0]])
    {
        RenderVertexOnlyOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }
};

#endif
