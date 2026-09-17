#pragma once

/**
 * Stores the frame-wide camera, output, sort, and projection constants used by the standard 3DGS render pipeline.
 *
 * Include this header inside namespace GsViewer after the sample constants are declared and before
 * GaussianSplattingCoreBindGroups.hpp, because FrameStateBindGroup binds this type by name.
 */
struct ViewerGlobals
{
    uint4 imageInfo;
    uint4 sceneInfo;
    uint4 sortKeyInfo;
    float4 backgroundAndScale;
    float4 projectionInfo;
    float4 frameMathInfo;
    float4 sceneWorldRow0;
    float4 sceneWorldRow1;
    float4 sceneWorldRow2;
    float4 sceneWorldRow3;
};
