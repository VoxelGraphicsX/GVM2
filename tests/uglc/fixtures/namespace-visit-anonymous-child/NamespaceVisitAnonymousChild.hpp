#ifndef UGLC_TEST_NAMESPACE_VISIT_ANONYMOUS_CHILD_HPP
#define UGLC_TEST_NAMESPACE_VISIT_ANONYMOUS_CHILD_HPP

#include "UGL.h"

namespace NamespaceVisitCase
{
    namespace
    {
        inline unsigned int HiddenSeed()
        {
            return 1u;
        }
    } // namespace

    struct NamespaceVisitVertexInput
    {
        UGL::float4 pos [[Attribute0]];
    };

    struct NamespaceVisitVertexOutput
    {
        UGL::float4 pos [[Position]];
        float value [[Attribute0]];
    };

    struct NamespaceVisitFrameBuffer final : public UGL::IFrameBuffer
    {
        UGL::ColorAttachment<UGL::TextureFormat::RGBA8Unorm> color;
    };

    class NamespaceVisitPass final : public UGL::IRenderClass
    {
    public:
        constructor()
        {
        }

    private:
        NamespaceVisitVertexOutput vertex(unsigned int vid [[VertexID]], NamespaceVisitVertexInput inputValue [[VertexInput0]])
        {
            NamespaceVisitVertexOutput outputValue;
            outputValue.pos = inputValue.pos;
            outputValue.value = float(HiddenSeed());
            return outputValue;
        }

        NamespaceVisitFrameBuffer fragment(NamespaceVisitVertexOutput inputValue)
        {
            NamespaceVisitFrameBuffer frameBuffer;
            frameBuffer.color = UGL::half4(inputValue.value, 0.0f, 0.0f, 1.0f);
            return frameBuffer;
        }
    };
} // namespace NamespaceVisitCase

#endif
