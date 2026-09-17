#ifndef UGLC_TEST_INIT_LIST_HELPER_DEPENDENCY_HPP
#define UGLC_TEST_INIT_LIST_HELPER_DEPENDENCY_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the writable output buffer used by the init-list dependency regression pass. */
struct InitListHelperDependencyBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Carries the projection matrix used by helper calls inside initializer lists. */
struct InitListHelperDependencyProjection
{
    float4x4 matrix;
};

/** Projects one point so initializer-list traversal must collect this helper call. */
inline float4 initListMakeClip(float3 position, InitListHelperDependencyProjection projection)
{
    return mul(projection.matrix, float4(position, 1.0f));
}

/** Converts one projected point so nested initializer-list traversal must collect this helper call. */
inline float3 initListMakeScreen(uint2 size, float4 clipPosition)
{
    const float safeW = max(abs(clipPosition.w), 0.0001f);
    const float3 ndc = clipPosition.xyz / float3(safeW);
    return float3((ndc.xy * 0.5f + 0.5f) * float2(size), ndc.z);
}

/** Uses array initializer lists whose elements call helpers that must be entry-reachable. */
inline uint initListMeasureTriangle(uint2 size, InitListHelperDependencyProjection projection)
{
    const float4 clipPositions[3] = {
        initListMakeClip(float3(0.0f, 0.0f, 0.0f), projection),
        initListMakeClip(float3(1.0f, 0.0f, 0.0f), projection),
        initListMakeClip(float3(0.0f, 1.0f, 0.0f), projection),
    };
    const float3 screenPositions[3] = {
        initListMakeScreen(size, clipPositions[0]),
        initListMakeScreen(size, clipPositions[1]),
        initListMakeScreen(size, clipPositions[2]),
    };
    return uint(screenPositions[0].x + screenPositions[1].y + screenPositions[2].z);
}

/** Verifies helper calls inside initializer lists are collected before shader backend emission. */
class [[LocalWorkGroupSize(1, 1, 1)]] InitListHelperDependencyPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<InitListHelperDependencyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a value produced by a helper that contains nested initializer-list helper calls. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            InitListHelperDependencyProjection projection;
            projection.matrix = float4x4(1.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, 1.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f, 1.0f);
            bindGroup->values[0] = initListMeasureTriangle(uint2(64u, 64u), projection);
        }
    }
};

#endif
