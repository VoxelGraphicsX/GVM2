#ifndef COMPOSITE_HPP
#define COMPOSITE_HPP

#include "UGL.h"
using namespace UGL;
#include "Camera.hpp"
#include "CheckerBoardBackground.hpp"
#include "GBuffer.hpp"

class [[LocalWorkGroupSize(8, 8, 1)]] Composite final : public IComputeClass
{
public:
    constructor(BindGroup<CameraBindGroup> camBindGroup [[Slot0]], BindGroup<CheckBoardGBufferBindGroup> checkerBoardGBufferBindGroup [[Slot1]], BindGroup<GBufferBindGroup> mainGBufferBindGroup [[Slot2]]
    )
    {
    }
    void compute(uint3 ThreadID [[DispatchThreadID]])
    {
        uint2 resolution;
        checkerBoardGBufferBindGroup->albedoTexture->getDimensions(resolution.x, resolution.y);
        if (ThreadID.x >= resolution.x || ThreadID.y >= resolution.y)
        {
            return;
        }

        float4 mainGBufferAlbedo = float4(mainGBufferBindGroup->albedoTexture->read(ThreadID.xy));
        float mainGBufferDepth = mainGBufferBindGroup->depthTexture->read(ThreadID.xy).x;

        float4 checkerBoardAlbedo = float4(checkerBoardGBufferBindGroup->albedoTexture->read(ThreadID.xy));
        float checkerBoardDepth = checkerBoardGBufferBindGroup->depthTexture->read(ThreadID.xy).x;

        float4 finalAlbedo;
        // float4 finalAlbedo = mainGBufferDepth > checkerBoardDepth ? mainGBufferAlbedo : checkerBoardAlbedo;
        // finalAlbedo = mainGBufferAlbedo.w == 0.f ? checkerBoardAlbedo : mainGBufferAlbedo;
        if (mainGBufferDepth > checkerBoardDepth)
        {
            finalAlbedo = mainGBufferAlbedo;
            if (mainGBufferAlbedo.w == 0.f)
            {
                finalAlbedo = checkerBoardAlbedo;
            }
        }
        else
        {
            finalAlbedo = checkerBoardAlbedo;
        }
        float finalDepth = min(mainGBufferDepth, checkerBoardDepth);

        checkerBoardGBufferBindGroup->albedoTexture->write(ThreadID.xy, half4(finalAlbedo));
        checkerBoardGBufferBindGroup->depthTexture->write(ThreadID.xy, finalDepth);
    }
};

#endif // COMPOSITE_HPP
