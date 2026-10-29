#ifndef UGLC_TEST_TEMPORARY_STRING_LABEL_REGRESSION_HPP
#define UGLC_TEST_TEMPORARY_STRING_LABEL_REGRESSION_HPP

#include "UGL.h"
#include <EASTL/string.h>

using namespace UGL;

struct TemporaryStringLabelRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

struct TemporaryStringLabelRegressionVertexOutput
{
    float4 pos [[Position]];
};

struct TemporaryStringLabelRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class TemporaryStringLabelRegressionPass final : public IRenderClass
{
public:
    constructor()
    {
        TextureViewDescriptor viewDescriptor{};
        uint mipOffset = 3u;
        viewDescriptor.label = (eastl::string("Hiz_") + eastl::to_string(mipOffset)).c_str();
    }

private:
    TemporaryStringLabelRegressionVertexOutput vertex(uint vid [[VertexID]],
                                                      TemporaryStringLabelRegressionVertexInput inputValue [[VertexInput0]])
    {
        TemporaryStringLabelRegressionVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    TemporaryStringLabelRegressionFrameBuffer fragment(TemporaryStringLabelRegressionVertexOutput inputValue)
    {
        TemporaryStringLabelRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
