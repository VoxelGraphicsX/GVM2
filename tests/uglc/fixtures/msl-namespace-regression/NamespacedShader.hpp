#ifndef UGLC_TEST_NAMESPACED_SHADER_HPP
#define UGLC_TEST_NAMESPACED_SHADER_HPP

#include "UGL.h"

using namespace UGL;

namespace RefHelpers
{
    namespace Inner
    {
        struct LocalPayload
        {
            float4 color;
        };

        inline float4 EncodeColor(float value)
        {
            return float4(value, value * 0.5f, 0.25f, 1.0f);
        }

        template <typename T>
        inline T identityValue(T value)
        {
            return value;
        }
    } // namespace Inner
} // namespace RefHelpers

struct NamespacedVertexInput
{
    float4 pos [[Attribute0]];
    float value [[Attribute1]];
};

struct NamespacedVertexOutput
{
    float4 pos [[Position]];
    float value [[Attribute0]];
};

struct NamespacedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class NamespacedPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    NamespacedVertexOutput vertex(uint vid [[VertexID]], NamespacedVertexInput inputValue [[VertexInput0]])
    {
        NamespacedVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.value = inputValue.value;
        return outputValue;
    }

    NamespacedFrameBuffer fragment(NamespacedVertexOutput inputValue)
    {
        RefHelpers::Inner::LocalPayload payload;
        payload.color = RefHelpers::Inner::EncodeColor(inputValue.value);
        payload = RefHelpers::Inner::identityValue(payload);

        NamespacedFrameBuffer frameBuffer;
        frameBuffer.color = half4(payload.color);
        return frameBuffer;
    }
};

#endif
