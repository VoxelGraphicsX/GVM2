#ifndef UGLC_TEST_HLSL_MATRIX_SUBSCRIPT_METAL_SEMANTICS_HPP
#define UGLC_TEST_HLSL_MATRIX_SUBSCRIPT_METAL_SEMANTICS_HPP

#include "UGL.h"

using namespace UGL;

struct MatrixSubscriptMetalSemanticsBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<float> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] MatrixSubscriptMetalSemanticsPass final : public IComputeClass
{
public:
    constructor(BindGroup<MatrixSubscriptMetalSemanticsBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        float4x4 projection = float4x4(float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 2.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.1f, 0.0f));

        projection[3][2] = 0.25f;
        projection[2][3] = 0.75f;

        float nearPlane = projection[3][2];
        float wScale = projection[2][3];
        float4 projectionColumn = projection[3];

        bindGroup->values[threadID.x] = nearPlane + wScale + projectionColumn.z;
    }
};

#endif
