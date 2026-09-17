#ifndef UGLC_TEST_DO_STMT_MACRO_REGRESSION_HPP
#define UGLC_TEST_DO_STMT_MACRO_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

#define UGLC_DO_STMT_INCREMENT(localValue) \
    do                                     \
    {                                      \
        localValue = localValue + 1;       \
    } while (0)

struct DoStmtMacroRegressionVertexOutput
{
    float4 pos [[Position]];
};

struct DoStmtMacroRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class DoStmtMacroRegressionPass final : public IRenderClass
{
public:
    constructor()
    {
        uint localCounter = 0;
        UGLC_DO_STMT_INCREMENT(localCounter);
    }

private:
    DoStmtMacroRegressionVertexOutput vertex(uint vid [[VertexID]])
    {
        DoStmtMacroRegressionVertexOutput outputValue;
        if (vid == 0)
        {
            outputValue.pos = float4(-0.5f, -0.5f, 0.0f, 1.0f);
        }
        else if (vid == 1)
        {
            outputValue.pos = float4(0.0f, 0.5f, 0.0f, 1.0f);
        }
        else
        {
            outputValue.pos = float4(0.5f, -0.5f, 0.0f, 1.0f);
        }
        return outputValue;
    }

    DoStmtMacroRegressionFrameBuffer fragment(DoStmtMacroRegressionVertexOutput inputValue)
    {
        (void)inputValue;
        DoStmtMacroRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(1.0f, 0.25f, 0.25f, 1.0f);
        return frameBuffer;
    }
};

#endif
