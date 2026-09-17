#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_MATRIX_VECTOR_MUL_VULKAN_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_MATRIX_VECTOR_MUL_VULKAN_HPP

#include "UGL.h"

using namespace UGL;

/** Provides a vertex input that keeps the matrix multiplication operand dynamic. */
struct ExperimentalUGLIRMatrixVectorMulInput
{
    float4 pos [[Attribute0]];
};

/** Carries the transformed clip-space position to the fragment stage. */
struct ExperimentalUGLIRMatrixVectorMulOutput
{
    float4 pos [[Position]];
};

/** Writes a constant color so the fixture focuses on vertex matrix semantics. */
struct ExperimentalUGLIRMatrixVectorMulFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Exercises UGL matrix-vector multiplication through the experimental direct SPIR-V writer. */
class ExperimentalUGLIRMatrixVectorMulVulkanPass final : public IRenderClass
{
public:
    /** Creates the pass without external resources. */
    constructor()
    {
    }

private:
    /** Applies a non-symmetric matrix so transposed SPIR-V lowering is observable in disassembly. */
    ExperimentalUGLIRMatrixVectorMulOutput vertex(uint vid [[VertexID]], ExperimentalUGLIRMatrixVectorMulInput inputValue [[VertexInput0]])
    {
        const float4x4 transform = float4x4(1.0f, 2.0f, 3.0f, 4.0f,
                                            5.0f, 6.0f, 7.0f, 8.0f,
                                            9.0f, 10.0f, 11.0f, 12.0f,
                                            13.0f, 14.0f, 15.0f, 16.0f);

        ExperimentalUGLIRMatrixVectorMulOutput outputValue;
        outputValue.pos = mul(transform, inputValue.pos);
        return outputValue;
    }

    /** Returns a stable fragment payload for host artifact generation. */
    ExperimentalUGLIRMatrixVectorMulFrameBuffer fragment(ExperimentalUGLIRMatrixVectorMulOutput inputValue)
    {
        ExperimentalUGLIRMatrixVectorMulFrameBuffer frameBuffer;
        frameBuffer.color = half4(1.0f, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
