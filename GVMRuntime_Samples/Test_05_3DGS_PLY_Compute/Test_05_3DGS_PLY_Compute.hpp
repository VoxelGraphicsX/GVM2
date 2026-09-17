#ifndef TEST_05_3DGS_PLY_COMPUTE_HPP
#define TEST_05_3DGS_PLY_COMPUTE_HPP

#include "UGL.h"
using namespace UGL;

#include "GaussianSplattingDslShared/Camera.hpp"
#include "GaussianSplattingDslShared/CpuGaussianScene.hpp"
#include "GaussianSplattingDslShared/PresentQuad.hpp"
#include "SimpleCamera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace GsViewer
{
    static const uint TileSize = 16;
    static const uint Amd8RadixBucketCount = 256;
    static const uint Amd8RadixPassBits = 8;
    static const uint Amd8RadixPassCount = 4;
    static const uint Amd8WorkGroupSize = 256;
    static const uint Amd8ElementsPerThread = 32;
    static const uint Amd8PartitionElementCount = Amd8WorkGroupSize * Amd8ElementsPerThread;
    static const uint SortEntryScanWorkGroupSize = Amd8WorkGroupSize;
    // Stable scatter is organized around fixed logical tiles, not hardware wave width.
    static const uint Amd8ScatterTileSize = 32;
    static const uint Amd8ScatterTileCount = Amd8WorkGroupSize / Amd8ScatterTileSize;
    static const uint DuplicateWorkGroupSize = 128;
    static const uint TileRasterBatchSize = TileSize * TileSize;
    static const uint InvalidIndex = 0xffffffffu;
    static const uint InvalidTelemetryFrameTag = InvalidIndex;
    static const float MinVisibleAlpha = 1.0f / 255.0f;
    static const float ShC0 = 0.28209479177387814f;
    static const float ShC1 = 0.4886025119029199f;
    static const float ShC2_0 = 1.0925484305920792f;
    static const float ShC2_1 = -1.0925484305920792f;
    static const float ShC2_2 = 0.31539156525252005f;
    static const float ShC2_3 = -1.0925484305920792f;
    static const float ShC2_4 = 0.5462742152960396f;
    static const float ShC3_0 = -0.5900435899266435f;
    static const float ShC3_1 = 2.890611442640554f;
    static const float ShC3_2 = -0.4570457994644658f;
    static const float ShC3_3 = 0.3731763325901154f;
    static const float ShC3_4 = -0.4570457994644658f;
    static const float ShC3_5 = 1.445305721320277f;
    static const float ShC3_6 = -0.5900435899266435f;
    static const uint ShMaxDegree = 3u;
    static const uint ShMaxRestCoefficientCount = 45u;
    static const uint SafeMaxSortEntryCount = 1u << 24;
    static const uint BudgetReadbackBufferCount = 8;
    static const uint DuplicateEmitHeavySplatThreshold = 512u;
    static const uint DuplicateEmitChunkEntryCount = 256u;
    static const uint HeavyTileRangeThreshold = 1024u;
    static const uint HeavyTileSegmentEntryCount = TileRasterBatchSize;
    static const uint RasterAnalysisProjectedSplatCount = 0u;
    static const uint RasterAnalysisProjectedEntryCount = 1u;
    static const uint RasterAnalysisNonEmptyTileCount = 2u;
    static const uint RasterAnalysisLightTileCount = 3u;
    static const uint RasterAnalysisHeavyTileCount = 4u;
    static const uint RasterAnalysisHeavySegmentCount = 5u;
    static const uint RasterAnalysisMaxProjectedTileCount = 6u;
    static const uint RasterAnalysisMaxTileRangeLength = 7u;
    static const uint RasterAnalysisMaxHeavyTileSegmentCount = 8u;
    static const uint RasterAnalysisHeavyEmitTaskCount = 9u;
    static const uint RasterAnalysisCounterCount = 10u;

#include "GaussianSplattingStandardTelemetryLayouts.hpp"

    struct GaussianSplatGPU
    {
        float4 positionOpacity;
        float4 axis0;
        float4 axis1;
        float4 axis2;
        float4 baseColor;
        float4 shDc;
    };

#include "GaussianSplattingGpuLayouts.hpp"

    struct ProjectedGaussian
    {
        float4 centerRadiusDepth;
        float4 conicOpacity;
        float4 color;
        uint packedTileBoundsMin;
        uint packedTileBoundsMax;
        uint tileCount;
        uint entryOffset;
    };

    struct DuplicateEmitTask
    {
        uint splatIndex;
        uint localEntryBase;
        uint entryCount;
        uint pad;
    };

    struct TileRange
    {
        uint start;
        uint end;
    };

    struct LightTileTask
    {
        uint tileId;
        uint rangeStart;
        uint entryCount;
        uint pad;
    };

    struct HeavyTileDesc
    {
        uint tileId;
        uint rangeStart;
        uint rangeLength;
        uint segmentBase;
        uint segmentCount;
    };

    struct HeavySegmentTask
    {
        uint tileId;
        uint rangeStart;
        uint entryCount;
        uint summarySegmentIndex;
    };

    struct SegmentPixelSummary
    {
        half4 colorTransmission;
    };

    struct ViewerGlobals
    {
        uint4 imageInfo;
        uint4 sceneInfo;
        uint4 sortKeyInfo;
        float4 backgroundAndScale;
    };

#include "GaussianSplattingCoreBindGroups.hpp"
#include "GaussianSplattingRasterAnalysisBindGroup.hpp"

    struct DuplicateStateBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<uint> blockSums [[Binding0]], RWStructuredBuffer<uint> blockPrefix [[Binding1]]
        )
        {
        }
    };

    struct SceneBindGroup final : public IBindGroup
    {
        constructor(StructuredBuffer<GaussianSplatGPU> splats [[Binding0]])
        {
        }
    };

    struct ProjectedBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<ProjectedGaussian> projected [[Binding0]])
        {
        }
    };

    struct DuplicateEmitTaskBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<DuplicateEmitTask> duplicateEmitTasks [[Binding0]])
        {
        }
    };

    struct TileRangeBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<TileRange> tileRanges [[Binding0]])
        {
        }
    };

    struct LightTileTaskBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<LightTileTask> lightTileTasks [[Binding0]])
        {
        }
    };

    struct HeavyTileDescBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<HeavyTileDesc> heavyTileDescs [[Binding0]])
        {
        }
    };

    struct HeavySegmentTaskBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<HeavySegmentTask> heavySegmentTasks [[Binding0]])
        {
        }
    };

    struct SegmentSummaryBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<SegmentPixelSummary> summaries [[Binding0]])
        {
        }
    };

    inline float3 backgroundColor(const ViewerGlobals &globals)
    {
        return globals.backgroundAndScale.xyz;
    }

    inline float packedFloat4Component(float4 value, uint componentIndex)
    {
        if (componentIndex == 0u)
        {
            return value.x;
        }
        if (componentIndex == 1u)
        {
            return value.y;
        }
        if (componentIndex == 2u)
        {
            return value.z;
        }
        return value.w;
    }

    inline float packedShRestCoefficient(const GaussianShRestGPU &shRest, uint coefficientIndex)
    {
        if (coefficientIndex < 4u)
            return packedFloat4Component(shRest.shCoeff0, coefficientIndex);
        if (coefficientIndex < 8u)
            return packedFloat4Component(shRest.shCoeff1, coefficientIndex - 4u);
        if (coefficientIndex < 12u)
            return packedFloat4Component(shRest.shCoeff2, coefficientIndex - 8u);
        if (coefficientIndex < 16u)
            return packedFloat4Component(shRest.shCoeff3, coefficientIndex - 12u);
        if (coefficientIndex < 20u)
            return packedFloat4Component(shRest.shCoeff4, coefficientIndex - 16u);
        if (coefficientIndex < 24u)
            return packedFloat4Component(shRest.shCoeff5, coefficientIndex - 20u);
        if (coefficientIndex < 28u)
            return packedFloat4Component(shRest.shCoeff6, coefficientIndex - 24u);
        if (coefficientIndex < 32u)
            return packedFloat4Component(shRest.shCoeff7, coefficientIndex - 28u);
        if (coefficientIndex < 36u)
            return packedFloat4Component(shRest.shCoeff8, coefficientIndex - 32u);
        if (coefficientIndex < 40u)
            return packedFloat4Component(shRest.shCoeff9, coefficientIndex - 36u);
        if (coefficientIndex < 44u)
            return packedFloat4Component(shRest.shCoeff10, coefficientIndex - 40u);
        if (coefficientIndex < 48u)
            return packedFloat4Component(shRest.shCoeff11, coefficientIndex - 44u);
        return 0.0f;
    }

    inline float shRestChannelCoefficient(const GaussianShRestGPU &shRest, uint channelIndex, uint restTermIndex)
    {
        if (channelIndex >= 3u || restTermIndex >= 15u)
        {
            return 0.0f;
        }

        // Graphdeco save_ply flattens features_rest after transpose(1, 2),
        // so the persisted layout is channel-major:
        // [r.basis1 .. r.basis15, g.basis1 .. g.basis15, b.basis1 .. b.basis15].
        const uint packedIndex = channelIndex * 15u + restTermIndex;
        return packedShRestCoefficient(shRest, packedIndex);
    }

    inline float3 shRestTriplet(const GaussianShRestGPU &shRest, uint restTermIndex)
    {
        return float3(
            shRestChannelCoefficient(shRest, 0u, restTermIndex),
            shRestChannelCoefficient(shRest, 1u, restTermIndex),
            shRestChannelCoefficient(shRest, 2u, restTermIndex)
        );
    }

    inline float3 evaluateShColor(const GaussianSplatGPU &splat, const GaussianShRestGPU &shRest, float3 viewDirection)
    {
        float3 color = float3(0.5f) + splat.shDc.xyz * ShC0;
        const float x = viewDirection.x;
        const float y = viewDirection.y;
        const float z = viewDirection.z;
        color = color
            - ShC1 * y * shRestTriplet(shRest, 0u)
            + ShC1 * z * shRestTriplet(shRest, 1u)
            - ShC1 * x * shRestTriplet(shRest, 2u);

        const float xx = x * x;
        const float yy = y * y;
        const float zz = z * z;
        const float xy = x * y;
        const float yz = y * z;
        const float xz = x * z;

        color = color
            + ShC2_0 * xy * shRestTriplet(shRest, 3u)
            + ShC2_1 * yz * shRestTriplet(shRest, 4u)
            + ShC2_2 * (2.0f * zz - xx - yy) * shRestTriplet(shRest, 5u)
            + ShC2_3 * xz * shRestTriplet(shRest, 6u)
            + ShC2_4 * (xx - yy) * shRestTriplet(shRest, 7u);

        color = color
            + ShC3_0 * y * (3.0f * xx - yy) * shRestTriplet(shRest, 8u)
            + ShC3_1 * xy * z * shRestTriplet(shRest, 9u)
            + ShC3_2 * y * (4.0f * zz - xx - yy) * shRestTriplet(shRest, 10u)
            + ShC3_3 * z * (2.0f * zz - 3.0f * xx - 3.0f * yy) * shRestTriplet(shRest, 11u)
            + ShC3_4 * x * (4.0f * zz - xx - yy) * shRestTriplet(shRest, 12u)
            + ShC3_5 * z * (xx - yy) * shRestTriplet(shRest, 13u)
            + ShC3_6 * x * (xx - 3.0f * yy) * shRestTriplet(shRest, 14u);

        return max(color, float3(0.0f));
    }

    inline float ndcToPixel(float ndcCoordinate, uint resolution)
    {
        return ((ndcCoordinate + 1.0f) * float(resolution) - 1.0f) * 0.5f;
    }

    inline float2 projectCameraToPixel(float3 cameraPosition, const Camera &camera, const ViewerGlobals &globals)
    {
        const float4 clip = mul(camera.proj, float4(cameraPosition, 1.0f));
        const float inverseClipW = 1.0f / max(clip.w, 0.00001f);
        const float2 projectedNdc = clip.xy * inverseClipW;
        return float2(ndcToPixel(projectedNdc.x, globals.imageInfo.x), ndcToPixel(projectedNdc.y, globals.imageInfo.y));
    }

    inline float2 projectCameraAxisToPixelDelta(float3 cameraPosition, float3 cameraAxis, float focalX, float focalY)
    {
        const float inverseZ = 1.0f / max(cameraPosition.z, 0.00001f);
        const float inverseZSquared = inverseZ * inverseZ;
        return float2(focalX * (cameraAxis.x * inverseZ - cameraPosition.x * cameraAxis.z * inverseZSquared), focalY * (cameraAxis.y * inverseZ - cameraPosition.y * cameraAxis.z * inverseZSquared));
    }

    inline float maxEigenvalueSymmetric2x2(float xx, float xy, float yy)
    {
        const float trace = xx + yy;
        const float determinant = xx * yy - xy * xy;
        const float discriminant = max(trace * trace - 4.0f * determinant, 0.0f);
        return max(0.5f * (trace + sqrt(discriminant)), 0.0f);
    }

    inline float computeGaussianSupportRadius(float maxEigenvalue, const ViewerGlobals &globals)
    {
        if (maxEigenvalue <= 0.0f)
        {
            return 0.0f;
        }

        const float viewportDiagonal = sqrt(float(globals.imageInfo.x) * float(globals.imageInfo.x) + float(globals.imageInfo.y) * float(globals.imageInfo.y));
        return min(ceil(3.0f * sqrt(maxEigenvalue)), viewportDiagonal);
    }

    inline uint packDepthKey(float cameraDepth, const ViewerGlobals &globals)
    {
        return (asuint(max(cameraDepth, 0.000001f)) >> globals.sortKeyInfo.x) & globals.sortKeyInfo.z;
    }

    inline uint composeSortKey(uint tileKey, uint packedDepthKey, const ViewerGlobals &globals)
    {
        return (tileKey << globals.sortKeyInfo.y) | packedDepthKey;
    }

    inline uint unpackTileKey(uint sortKey, const ViewerGlobals &globals)
    {
        return sortKey >> globals.sortKeyInfo.y;
    }

    inline uint radixBucketForPass(const SortEntry &entry, uint passIndex)
    {
        return (entry.sortKey >> (passIndex * Amd8RadixPassBits)) & 0xffu;
    }

    inline uint clampActiveEntryCount(const ViewerGlobals &globals, uint rawActiveCount)
    {
        return min(rawActiveCount, globals.sceneInfo.y);
    }

    inline uint computeAmd8PartitionCount(uint activeCount)
    {
        return max((activeCount + Amd8PartitionElementCount - 1u) / Amd8PartitionElementCount, 1u);
    }

    inline uint packUint16x2(uint low, uint high)
    {
        return (low & 0xffffu) | ((high & 0xffffu) << 16u);
    }

    inline uint unpackLowUint16(uint packed)
    {
        return packed & 0xffffu;
    }

    inline uint unpackHighUint16(uint packed)
    {
        return packed >> 16u;
    }

    inline uint2 tileCoordinatesFromId(uint tileId, const ViewerGlobals &globals)
    {
        return uint2(tileId % globals.imageInfo.z, tileId / globals.imageInfo.z);
    }

    class [[LocalWorkGroupSize(128, 1, 1)]] ProjectGaussiansPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<CameraBindGroup> cameraBindGroup [[Slot0]], BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot1]], BindGroup<SceneBindGroup> sceneBindGroup [[Slot2]], BindGroup<SceneShBindGroup> sceneShBindGroup [[Slot3]], BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot4]], BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot5]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const Camera camera = cameraBindGroup->camBuffer->read();
            const uint splatIndex = threadID.x;
            if (splatIndex >= globals.sceneInfo.x)
            {
                return;
            }

            const GaussianSplatGPU splat = sceneBindGroup->splats[splatIndex];
            ProjectedGaussian projected;
            projected.centerRadiusDepth = float4(0.0f);
            projected.conicOpacity = float4(0.0f);
            projected.color = float4(splat.baseColor.xyz, 0.0f);
            projected.packedTileBoundsMin = 0u;
            projected.packedTileBoundsMax = 0u;
            projected.tileCount = 0u;
            projected.entryOffset = 0u;

            const float3 worldPosition = splat.positionOpacity.xyz;
            const float opacity = splat.positionOpacity.w;

            const float3 cameraPosition = mul(camera.view, float4(worldPosition, 1.0f)).xyz;
            if (cameraPosition.z <= 0.0001f)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            const float4 clip = mul(camera.proj, float4(cameraPosition, 1.0f));
            if (clip.w <= 0.00001f)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            const float depth = clip.z / clip.w;
            if (depth < -0.001f || depth > 1.001f)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            float3 covariancePosition = cameraPosition;
            const float tanHalfFovX = 1.0f / camera.proj[0][0];
            const float tanHalfFovY = 1.0f / camera.proj[1][1];
            const float clampedNormalizedX = clamp(covariancePosition.x / covariancePosition.z, -1.3f * tanHalfFovX, 1.3f * tanHalfFovX);
            const float clampedNormalizedY = clamp(covariancePosition.y / covariancePosition.z, -1.3f * tanHalfFovY, 1.3f * tanHalfFovY);
            covariancePosition.x = clampedNormalizedX * covariancePosition.z;
            covariancePosition.y = clampedNormalizedY * covariancePosition.z;

            const float3 cameraAxis0 = mul(camera.view, float4(splat.axis0.xyz, 0.0f)).xyz;
            const float3 cameraAxis1 = mul(camera.view, float4(splat.axis1.xyz, 0.0f)).xyz;
            const float3 cameraAxis2 = mul(camera.view, float4(splat.axis2.xyz, 0.0f)).xyz;

            const float2 centerPx = projectCameraToPixel(cameraPosition, camera, globals);
            const float focalX = camera.proj[0][0] * float(globals.imageInfo.x) * 0.5f;
            const float focalY = camera.proj[1][1] * float(globals.imageInfo.y) * 0.5f;

            const float2 projectedAxis0 = projectCameraAxisToPixelDelta(covariancePosition, cameraAxis0, focalX, focalY);
            const float2 projectedAxis1 = projectCameraAxisToPixelDelta(covariancePosition, cameraAxis1, focalX, focalY);
            const float2 projectedAxis2 = projectCameraAxisToPixelDelta(covariancePosition, cameraAxis2, focalX, focalY);

            const float covarianceXX = projectedAxis0.x * projectedAxis0.x + projectedAxis1.x * projectedAxis1.x + projectedAxis2.x * projectedAxis2.x;
            const float covarianceXY = projectedAxis0.x * projectedAxis0.y + projectedAxis1.x * projectedAxis1.y + projectedAxis2.x * projectedAxis2.y;
            const float covarianceYY = projectedAxis0.y * projectedAxis0.y + projectedAxis1.y * projectedAxis1.y + projectedAxis2.y * projectedAxis2.y;

            const float stabilizedXX = covarianceXX + 0.3f;
            const float stabilizedXY = covarianceXY;
            const float stabilizedYY = covarianceYY + 0.3f;
            const float determinant = stabilizedXX * stabilizedYY - stabilizedXY * stabilizedXY;
            if (determinant <= 0.000001f)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            const float alphaThresholdPower = log(clamp(MinVisibleAlpha / opacity, 0.000001f, 0.999999f));
            const float maxEigenvalue = maxEigenvalueSymmetric2x2(stabilizedXX, stabilizedXY, stabilizedYY);
            const float radius = computeGaussianSupportRadius(maxEigenvalue, globals);
            if (radius <= 0.0f)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            const int pixelMinX = (int)floor(centerPx.x - radius);
            const int pixelMinY = (int)floor(centerPx.y - radius);
            const int pixelMaxX = (int)ceil(centerPx.x + radius);
            const int pixelMaxY = (int)ceil(centerPx.y + radius);

            if (pixelMaxX < 0 || pixelMaxY < 0 || pixelMinX >= (int)globals.imageInfo.x || pixelMinY >= (int)globals.imageInfo.y)
            {
                projectedBindGroup->projected[splatIndex] = projected;
                return;
            }

            const int tileSize = (int)globals.sceneInfo.z;
            const uint tileMinX = (uint)max((int)floor((float)pixelMinX / (float)tileSize), 0);
            const uint tileMinY = (uint)max((int)floor((float)pixelMinY / (float)tileSize), 0);
            const uint tileMaxX = (uint)min((int)floor((float)pixelMaxX / (float)tileSize), (int)globals.imageInfo.z - 1);
            const uint tileMaxY = (uint)min((int)floor((float)pixelMaxY / (float)tileSize), (int)globals.imageInfo.w - 1);

            const float inverseDeterminant = 1.0f / determinant;
            const float conicXX = stabilizedYY * inverseDeterminant;
            const float conicXY = -stabilizedXY * inverseDeterminant;
            const float conicYY = stabilizedXX * inverseDeterminant;
            const uint tileCount = (tileMaxX - tileMinX + 1u) * (tileMaxY - tileMinY + 1u);
            atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedSplatCount], 1u);
            atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedEntryCount], tileCount);
            atomicMax(rasterAnalysisBindGroup->counters[RasterAnalysisMaxProjectedTileCount], tileCount);

            const GaussianShRestGPU shRest = sceneShBindGroup->shRest[splatIndex];
            const float3 cameraWorldPosition = camera.viewInv[3].xyz;
            const float3 rawViewDirection = worldPosition - cameraWorldPosition;
            const float directionLengthSquared = dot(rawViewDirection, rawViewDirection);
            float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
            if (directionLengthSquared > 1.0e-12f)
            {
                viewDirection = rawViewDirection * rsqrt(directionLengthSquared);
            }
            const float3 finalColor = evaluateShColor(splat, shRest, viewDirection);

            // Pack the per-splat depth key once so duplicate emit stays integer-only on the hot path.
            projected.centerRadiusDepth = float4(centerPx, radius, asfloat(packDepthKey(cameraPosition.z, globals)));
            projected.conicOpacity = float4(conicXX, conicXY, conicYY, opacity);
            projected.color = float4(finalColor, alphaThresholdPower);
            projected.packedTileBoundsMin = packUint16x2(tileMinX, tileMinY);
            projected.packedTileBoundsMax = packUint16x2(tileMaxX, tileMaxY);
            projected.tileCount = tileCount;
            projectedBindGroup->projected[splatIndex] = projected;
        }
    };

    class [[LocalWorkGroupSize(DuplicateWorkGroupSize, 1, 1)]] DuplicatePrefixPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]], BindGroup<DuplicateStateBindGroup> duplicateStateBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint splatIndex = threadID.x;

            GroupShared<uint> counts[DuplicateWorkGroupSize];
            GroupShared<uint> scanData[DuplicateWorkGroupSize];

            uint tileCount = 0u;
            if (splatIndex < globals.sceneInfo.x)
            {
                tileCount = projectedBindGroup->projected[splatIndex].tileCount;
            }

            counts[groupIndex] = tileCount;
            scanData[groupIndex] = tileCount;
            GroupMemoryBarrierWithGroupSync();

            for (uint offset = 1u; offset < DuplicateWorkGroupSize; offset <<= 1u)
            {
                const uint index = ((groupIndex + 1u) * offset * 2u) - 1u;
                if (index < DuplicateWorkGroupSize)
                {
                    scanData[index] = (uint)scanData[index] + (uint)scanData[index - offset];
                }
                GroupMemoryBarrierWithGroupSync();
            }

            if (groupIndex == DuplicateWorkGroupSize - 1u)
            {
                duplicateStateBindGroup->blockSums[groupID.x] = (uint)scanData[groupIndex];
                scanData[groupIndex] = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            for (uint offset = DuplicateWorkGroupSize >> 1u; offset > 0u; offset >>= 1u)
            {
                const uint index = ((groupIndex + 1u) * offset * 2u) - 1u;
                if (index < DuplicateWorkGroupSize)
                {
                    const uint leftIndex = index - offset;
                    const uint leftValue = (uint)scanData[leftIndex];
                    scanData[leftIndex] = scanData[index];
                    scanData[index] = (uint)scanData[index] + leftValue;
                }
                GroupMemoryBarrierWithGroupSync();
            }

            if (splatIndex < globals.sceneInfo.x)
            {
                projectedBindGroup->projected[splatIndex].entryOffset = scanData[groupIndex];
            }
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] DuplicateBlockPrefixPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<DuplicateStateBindGroup> duplicateStateBindGroup [[Slot1]], BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot2]], BindGroup<BudgetTelemetryBindGroup> budgetTelemetryBindGroup [[Slot3]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint blockCount = (globals.sceneInfo.x + DuplicateWorkGroupSize - 1u) / DuplicateWorkGroupSize;

            uint totalEntryCount = 0u;
            for (uint blockIndex = 0u; blockIndex < blockCount; ++blockIndex)
            {
                duplicateStateBindGroup->blockPrefix[blockIndex] = totalEntryCount;
                totalEntryCount += duplicateStateBindGroup->blockSums[blockIndex];
            }

            const uint clampedEntryCount = min(totalEntryCount, globals.sceneInfo.y);
            atomicStore(frameStateBindGroup->duplicateCounter[0], clampedEntryCount);
            budgetTelemetryBindGroup->telemetry[0].requiredEntryCount = totalEntryCount;
            budgetTelemetryBindGroup->telemetry[0].duplicateOverflowCount = totalEntryCount > clampedEntryCount ? (totalEntryCount - clampedEntryCount) : 0u;
            if (totalEntryCount > clampedEntryCount)
            {
                const uint overflowCount = totalEntryCount - clampedEntryCount;
                atomicAdd(frameStateBindGroup->overflowCounter[0], overflowCount);
                atomicAdd(debugCounterBindGroup->duplicateOverflowCounter[0], overflowCount);
            }
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] FinalizeBudgetTelemetryPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot0]], BindGroup<BudgetTelemetryBindGroup> budgetTelemetryBindGroup [[Slot1]], BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            budgetTelemetryBindGroup->telemetry[0].duplicateOverflowCount = atomicLoad(debugCounterBindGroup->duplicateOverflowCounter[0]);
            budgetTelemetryBindGroup->telemetry[0].scatterOverflowCount = atomicLoad(debugCounterBindGroup->scatterOverflowCounter[0]);
            budgetTelemetryBindGroup->telemetry[0].projectedSplatCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedSplatCount]);
            budgetTelemetryBindGroup->telemetry[0].projectedEntryCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedEntryCount]);
            budgetTelemetryBindGroup->telemetry[0].nonEmptyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisNonEmptyTileCount]);
            budgetTelemetryBindGroup->telemetry[0].lightTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisLightTileCount]);
            budgetTelemetryBindGroup->telemetry[0].heavyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyTileCount]);
            budgetTelemetryBindGroup->telemetry[0].heavySegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavySegmentCount]);
            budgetTelemetryBindGroup->telemetry[0].maxProjectedTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxProjectedTileCount]);
            budgetTelemetryBindGroup->telemetry[0].maxTileRangeLength = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxTileRangeLength]);
            budgetTelemetryBindGroup->telemetry[0].maxHeavyTileSegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxHeavyTileSegmentCount]);
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] BuildSortScanDispatchPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint workgroupCount = (activeCount + SortEntryScanWorkGroupSize - 1u) / SortEntryScanWorkGroupSize;
            entryDispatchBindGroup->entryDispatch[0].x = workgroupCount;
            entryDispatchBindGroup->entryDispatch[0].y = 1u;
            entryDispatchBindGroup->entryDispatch[0].z = 1u;
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] BuildDuplicateDispatchPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint workgroupCount = max((globals.sceneInfo.x + DuplicateWorkGroupSize - 1u) / DuplicateWorkGroupSize, 1u);
            entryDispatchBindGroup->entryDispatch[0].x = workgroupCount;
            entryDispatchBindGroup->entryDispatch[0].y = 1u;
            entryDispatchBindGroup->entryDispatch[0].z = 1u;
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] BuildAmd8DispatchPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint partitionCount = computeAmd8PartitionCount(activeCount);
            entryDispatchBindGroup->entryDispatch[0].x = partitionCount;
            entryDispatchBindGroup->entryDispatch[0].y = 1u;
            entryDispatchBindGroup->entryDispatch[0].z = 1u;
        }
    };

    class [[LocalWorkGroupSize(DuplicateWorkGroupSize, 1, 1)]] BuildDuplicateEmitTasksPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot2]],
                    BindGroup<DuplicateEmitTaskBindGroup> duplicateEmitTaskBindGroup [[Slot3]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint splatIndex = threadID.x;
            if (splatIndex >= globals.sceneInfo.x)
            {
                return;
            }

            const uint tileCount = projectedBindGroup->projected[splatIndex].tileCount;
            if (tileCount <= DuplicateEmitHeavySplatThreshold)
            {
                return;
            }

            const uint taskCount = (tileCount + DuplicateEmitChunkEntryCount - 1u) / DuplicateEmitChunkEntryCount;
            const uint taskBase = atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyEmitTaskCount], taskCount);
            for (uint taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            {
                DuplicateEmitTask task;
                task.splatIndex = splatIndex;
                task.localEntryBase = taskIndex * DuplicateEmitChunkEntryCount;
                task.entryCount = min(tileCount - task.localEntryBase, DuplicateEmitChunkEntryCount);
                task.pad = 0u;
                duplicateEmitTaskBindGroup->duplicateEmitTasks[taskBase + taskIndex] = task;
            }
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] BuildDuplicateEmitHeavyDispatchPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const uint taskCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyEmitTaskCount]);
            entryDispatchBindGroup->entryDispatch[0].x = max(taskCount, 1u);
            entryDispatchBindGroup->entryDispatch[0].y = 1u;
            entryDispatchBindGroup->entryDispatch[0].z = 1u;
        }
    };

    class [[LocalWorkGroupSize(DuplicateWorkGroupSize, 1, 1)]] DuplicateEmitPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]], BindGroup<DuplicateStateBindGroup> duplicateStateBindGroup [[Slot2]], BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot3]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint splatIndex = threadID.x;
            if (splatIndex >= globals.sceneInfo.x)
            {
                return;
            }

            const ProjectedGaussian projected = projectedBindGroup->projected[splatIndex];
            if (projected.tileCount == 0u || projected.tileCount > DuplicateEmitHeavySplatThreshold)
            {
                return;
            }

            const uint tileMinX = unpackLowUint16(projected.packedTileBoundsMin);
            const uint tileMinY = unpackHighUint16(projected.packedTileBoundsMin);
            const uint tileMaxX = unpackLowUint16(projected.packedTileBoundsMax);
            const uint tileMaxY = unpackHighUint16(projected.packedTileBoundsMax);

            const uint writeBase = duplicateStateBindGroup->blockPrefix[groupID.x] + projected.entryOffset;
            if (writeBase >= globals.sceneInfo.y)
            {
                return;
            }

            const uint packedDepthKey = asuint(projected.centerRadiusDepth.w);
            uint localEntryIndex = 0u;
            for (uint tileY = tileMinY; tileY <= tileMaxY; ++tileY)
            {
                for (uint tileX = tileMinX; tileX <= tileMaxX; ++tileX)
                {
                    const uint writeIndex = writeBase + localEntryIndex;
                    if (writeIndex >= globals.sceneInfo.y)
                    {
                        return;
                    }

                    SortEntry entry;
                    const uint tileKey = tileY * globals.imageInfo.z + tileX;
                    entry.sortKey = composeSortKey(tileKey, packedDepthKey, globals);
                    entry.splatIndex = splatIndex;
                    entryBindGroup->entries[writeIndex] = entry;
                    localEntryIndex += 1u;
                }
            }
        }
    };

    class [[LocalWorkGroupSize(DuplicateWorkGroupSize, 1, 1)]] DuplicateEmitHeavyPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]],
                    BindGroup<DuplicateStateBindGroup> duplicateStateBindGroup [[Slot2]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot3]],
                    BindGroup<DuplicateEmitTaskBindGroup> duplicateEmitTaskBindGroup [[Slot4]],
                    BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot5]]
        )
        {
        }

    private:
        void compute(uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            const uint taskIndex = groupID.x;
            const uint taskCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyEmitTaskCount]);
            if (taskIndex >= taskCount)
            {
                return;
            }

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const DuplicateEmitTask task = duplicateEmitTaskBindGroup->duplicateEmitTasks[taskIndex];
            const ProjectedGaussian projected = projectedBindGroup->projected[task.splatIndex];
            if (projected.tileCount == 0u)
            {
                return;
            }

            const uint tileMinX = unpackLowUint16(projected.packedTileBoundsMin);
            const uint tileMinY = unpackHighUint16(projected.packedTileBoundsMin);
            const uint tileMaxX = unpackLowUint16(projected.packedTileBoundsMax);
            const uint tileSpanX = tileMaxX - tileMinX + 1u;
            const uint writeBase = duplicateStateBindGroup->blockPrefix[task.splatIndex / DuplicateWorkGroupSize] + projected.entryOffset;
            if (writeBase >= globals.sceneInfo.y)
            {
                return;
            }

            const uint packedDepthKey = asuint(projected.centerRadiusDepth.w);
            for (uint taskEntryIndex = groupIndex; taskEntryIndex < task.entryCount; taskEntryIndex += DuplicateWorkGroupSize)
            {
                const uint localEntryIndex = task.localEntryBase + taskEntryIndex;
                const uint writeIndex = writeBase + localEntryIndex;
                if (writeIndex >= globals.sceneInfo.y)
                {
                    return;
                }

                const uint tileOffsetY = localEntryIndex / tileSpanX;
                const uint tileOffsetX = localEntryIndex - tileOffsetY * tileSpanX;
                const uint tileKey = (tileMinY + tileOffsetY) * globals.imageInfo.z + (tileMinX + tileOffsetX);

                SortEntry entry;
                entry.sortKey = composeSortKey(tileKey, packedDepthKey, globals);
                entry.splatIndex = task.splatIndex;
                entryBindGroup->entries[writeIndex] = entry;
            }
        }
    };

    class [[LocalWorkGroupSize(Amd8WorkGroupSize, 1, 1)]] Amd8PrefixPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> localHistogram[Amd8RadixBucketCount];

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint partitionCount = computeAmd8PartitionCount(activeCount);
            const uint partitionIndex = groupID.x;
            if (partitionIndex >= partitionCount)
            {
                return;
            }

            atomicStore(localHistogram[groupIndex], 0u);
            GroupMemoryBarrierWithGroupSync();

            const uint waveLaneIndex = WaveGetLaneIndex();
            const uint waveLaneCount = WaveGetLaneCount();
            const uint partitionBase = partitionIndex * Amd8PartitionElementCount;
            for (uint elementIndex = 0u; elementIndex < Amd8ElementsPerThread; ++elementIndex)
            {
                const uint entryIndex = partitionBase + elementIndex * Amd8WorkGroupSize + groupIndex;
                uint bucket = InvalidIndex;
                if (entryIndex < activeCount)
                {
                    const SortEntry entry = entryPairBindGroup->inputEntries[entryIndex];
                    bucket = radixBucketForPass(entry, globals.sceneInfo.w);
                }

                uint leaderLaneIndex = 0u;
                uint waveBucketCount = 0u;
                bool isBucketLeader = false;
                if (bucket < Amd8RadixBucketCount)
                {
                    bool foundLeader = false;
                    for (uint laneIndex = 0u; laneIndex < waveLaneCount; ++laneIndex)
                    {
                        const uint waveBucket = WaveReadLaneAt(bucket, laneIndex);
                        if (waveBucket == bucket)
                        {
                            if (!foundLeader)
                            {
                                leaderLaneIndex = laneIndex;
                                foundLeader = true;
                            }
                            waveBucketCount += 1u;
                        }
                    }
                    isBucketLeader = waveLaneIndex == leaderLaneIndex;
                }

                if (isBucketLeader)
                {
                    atomicAdd(localHistogram[bucket], waveBucketCount);
                }
            }
            GroupMemoryBarrierWithGroupSync();

            const uint localCount = atomicLoad(localHistogram[groupIndex]);
            const uint stateIndex = partitionIndex * Amd8RadixBucketCount + groupIndex;
            prefixDataBindGroup->prefixData[stateIndex].aggregate = localCount;
            prefixDataBindGroup->prefixData[stateIndex].prefix = 0u;
        }
    };

    class [[LocalWorkGroupSize(Amd8RadixBucketCount, 1, 1)]] Amd8ResolveOffsetsPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot1]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> bucketTotals[Amd8RadixBucketCount];

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint partitionCount = computeAmd8PartitionCount(activeCount);
            const uint bucketIndex = groupIndex;

            uint running = 0u;
            for (uint partitionIndex = 0u; partitionIndex < partitionCount; ++partitionIndex)
            {
                const uint stateIndex = partitionIndex * Amd8RadixBucketCount + bucketIndex;
                prefixDataBindGroup->prefixData[stateIndex].prefix = running;
                running += prefixDataBindGroup->prefixData[stateIndex].aggregate;
            }
            bucketTotals[bucketIndex] = running;
            GroupMemoryBarrierWithGroupSync();

            if (groupIndex == 0u)
            {
                uint scan = 0u;
                for (uint scanIndex = 0u; scanIndex < Amd8RadixBucketCount; ++scanIndex)
                {
                    const uint value = bucketTotals[scanIndex];
                    bucketTotals[scanIndex] = scan;
                    scan += value;
                }
            }
            GroupMemoryBarrierWithGroupSync();

            bucketBaseBindGroup->bucketBase[bucketIndex] = bucketTotals[bucketIndex];
        }
    };

    class [[LocalWorkGroupSize(Amd8WorkGroupSize, 1, 1)]] Amd8ScatterPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot3]], BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot4]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> localBuckets[Amd8WorkGroupSize];
            GroupShared<uint> blockBucketCarry[Amd8RadixBucketCount];
            GroupShared<uint> tileBucketCounts[Amd8ScatterTileCount * Amd8RadixBucketCount];

            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint partitionCount = computeAmd8PartitionCount(activeCount);
            const uint partitionIndex = groupID.x;
            if (partitionIndex >= partitionCount)
            {
                return;
            }

            for (uint bucketIndex = groupIndex; bucketIndex < Amd8RadixBucketCount; bucketIndex += Amd8WorkGroupSize)
            {
                blockBucketCarry[bucketIndex] = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            const uint tileIndex = groupIndex / Amd8ScatterTileSize;
            const uint tileLane = groupIndex % Amd8ScatterTileSize;
            const uint partitionBase = partitionIndex * Amd8PartitionElementCount;
            for (uint elementIndex = 0u; elementIndex < Amd8ElementsPerThread; ++elementIndex)
            {
                for (uint tileBucketIndex = groupIndex; tileBucketIndex < Amd8ScatterTileCount * Amd8RadixBucketCount; tileBucketIndex += Amd8WorkGroupSize)
                {
                    tileBucketCounts[tileBucketIndex] = 0u;
                }
                GroupMemoryBarrierWithGroupSync();

                const uint entryIndex = partitionBase + elementIndex * Amd8WorkGroupSize + groupIndex;
                uint bucket = InvalidIndex;
                SortEntry entry = {};
                if (entryIndex < activeCount)
                {
                    entry = entryPairBindGroup->inputEntries[entryIndex];
                    bucket = radixBucketForPass(entry, globals.sceneInfo.w);
                }

                localBuckets[groupIndex] = bucket;
                GroupMemoryBarrierWithGroupSync();

                if (tileLane == 0u)
                {
                    const uint tileBase = tileIndex * Amd8ScatterTileSize;
                    const uint tileCountBase = tileIndex * Amd8RadixBucketCount;
                    for (uint laneIndex = 0u; laneIndex < Amd8ScatterTileSize; ++laneIndex)
                    {
                        const uint tileBucket = localBuckets[tileBase + laneIndex];
                        if (tileBucket < Amd8RadixBucketCount)
                        {
                            tileBucketCounts[tileCountBase + tileBucket] =
                                tileBucketCounts[tileCountBase + tileBucket] + 1u;
                        }
                    }
                }
                GroupMemoryBarrierWithGroupSync();

                if (bucket < Amd8RadixBucketCount)
                {
                    uint localRank = blockBucketCarry[bucket];
                    const uint tileBase = tileIndex * Amd8ScatterTileSize;
                    for (uint laneIndex = 0u; laneIndex < tileLane; ++laneIndex)
                    {
                        if (localBuckets[tileBase + laneIndex] == bucket)
                        {
                            localRank += 1u;
                        }
                    }
                    for (uint previousTile = 0u; previousTile < tileIndex; ++previousTile)
                    {
                        localRank += tileBucketCounts[previousTile * Amd8RadixBucketCount + bucket];
                    }

                    const uint stateIndex = partitionIndex * Amd8RadixBucketCount + bucket;
                    const uint writeIndex = bucketBaseBindGroup->bucketBase[bucket] + prefixDataBindGroup->prefixData[stateIndex].prefix + localRank;
                    if (writeIndex >= globals.sceneInfo.y)
                    {
                        atomicAdd(frameStateBindGroup->overflowCounter[0], 1u);
                        atomicAdd(debugCounterBindGroup->scatterOverflowCounter[0], 1u);
                    }
                    else
                    {
                        entryPairBindGroup->outputEntries[writeIndex] = entry;
                    }
                }
                GroupMemoryBarrierWithGroupSync();

                if (groupIndex < Amd8RadixBucketCount)
                {
                    uint batchCount = 0u;
                    for (uint tile = 0u; tile < Amd8ScatterTileCount; ++tile)
                    {
                        batchCount += tileBucketCounts[tile * Amd8RadixBucketCount + groupIndex];
                    }
                    blockBucketCarry[groupIndex] = blockBucketCarry[groupIndex] + batchCount;
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    };

    class [[LocalWorkGroupSize(SortEntryScanWorkGroupSize, 1, 1)]] TileRangePass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot1]], BindGroup<TileRangeBindGroup> tileRangeBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint entryIndex = threadID.x;
            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            if (entryIndex >= activeCount)
            {
                return;
            }

            const uint tileCount = globals.imageInfo.z * globals.imageInfo.w;
            const uint tileId = unpackTileKey(entryBindGroup->entries[entryIndex].sortKey, globals);
            if (tileId >= tileCount)
            {
                return;
            }

            if (entryIndex == 0u || unpackTileKey(entryBindGroup->entries[entryIndex - 1u].sortKey, globals) != tileId)
            {
                tileRangeBindGroup->tileRanges[tileId].start = entryIndex;
            }

            if (entryIndex + 1u == activeCount || unpackTileKey(entryBindGroup->entries[entryIndex + 1u].sortKey, globals) != tileId)
            {
                tileRangeBindGroup->tileRanges[tileId].end = entryIndex + 1u;
            }
        }
    };

    class [[LocalWorkGroupSize(128, 1, 1)]] BuildTileTasksPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<TileRangeBindGroup> tileRangeBindGroup [[Slot1]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot2]],
                    BindGroup<LightTileTaskBindGroup> lightTileTaskBindGroup [[Slot3]],
                    BindGroup<HeavyTileDescBindGroup> heavyTileDescBindGroup [[Slot4]],
                    BindGroup<HeavySegmentTaskBindGroup> heavySegmentTaskBindGroup [[Slot5]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint tileId = threadID.x;
            const uint tileCount = globals.imageInfo.z * globals.imageInfo.w;
            if (tileId >= tileCount)
            {
                return;
            }

            const TileRange range = tileRangeBindGroup->tileRanges[tileId];
            if (range.start == InvalidIndex || range.end == InvalidIndex)
            {
                return;
            }

            const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
            const uint rangeStart = min(range.start, activeCount);
            const uint rangeEnd = min(range.end, activeCount);
            if (rangeStart >= rangeEnd)
            {
                return;
            }

            const uint rangeLength = rangeEnd - rangeStart;
            atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisNonEmptyTileCount], 1u);
            atomicMax(rasterAnalysisBindGroup->counters[RasterAnalysisMaxTileRangeLength], rangeLength);

            if (rangeLength > HeavyTileRangeThreshold)
            {
                const uint heavyTileIndex = atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyTileCount], 1u);
                const uint segmentCount = (rangeLength + HeavyTileSegmentEntryCount - 1u) / HeavyTileSegmentEntryCount;
                const uint segmentBase = atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisHeavySegmentCount], segmentCount);
                atomicMax(rasterAnalysisBindGroup->counters[RasterAnalysisMaxHeavyTileSegmentCount], segmentCount);

                HeavyTileDesc heavyTileDesc;
                heavyTileDesc.tileId = tileId;
                heavyTileDesc.rangeStart = rangeStart;
                heavyTileDesc.rangeLength = rangeLength;
                heavyTileDesc.segmentBase = segmentBase;
                heavyTileDesc.segmentCount = segmentCount;
                heavyTileDescBindGroup->heavyTileDescs[heavyTileIndex] = heavyTileDesc;

                for (uint segmentIndex = 0u; segmentIndex < segmentCount; ++segmentIndex)
                {
                    const uint segmentStart = rangeStart + segmentIndex * HeavyTileSegmentEntryCount;
                    const uint remainingCount = rangeEnd - segmentStart;

                    HeavySegmentTask heavySegmentTask;
                    heavySegmentTask.tileId = tileId;
                    heavySegmentTask.rangeStart = segmentStart;
                    heavySegmentTask.entryCount = min(remainingCount, HeavyTileSegmentEntryCount);
                    heavySegmentTask.summarySegmentIndex = segmentBase + segmentIndex;
                    heavySegmentTaskBindGroup->heavySegmentTasks[segmentBase + segmentIndex] = heavySegmentTask;
                }
            }
            else
            {
                const uint lightTileIndex = atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisLightTileCount], 1u);

                LightTileTask lightTileTask;
                lightTileTask.tileId = tileId;
                lightTileTask.rangeStart = rangeStart;
                lightTileTask.entryCount = rangeLength;
                lightTileTask.pad = 0u;
                lightTileTaskBindGroup->lightTileTasks[lightTileIndex] = lightTileTask;
            }
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] BuildTileTaskDispatchPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot0]],
                    BindGroup<EntryDispatchBindGroup> lightTileDispatchBindGroup [[Slot1]],
                    BindGroup<EntryDispatchBindGroup> heavySegmentDispatchBindGroup [[Slot2]],
                    BindGroup<EntryDispatchBindGroup> heavyComposeDispatchBindGroup [[Slot3]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x != 0u)
            {
                return;
            }

            const uint lightTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisLightTileCount]);
            const uint heavySegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavySegmentCount]);
            const uint heavyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyTileCount]);

            lightTileDispatchBindGroup->entryDispatch[0].x = max(lightTileCount, 1u);
            lightTileDispatchBindGroup->entryDispatch[0].y = 1u;
            lightTileDispatchBindGroup->entryDispatch[0].z = 1u;

            heavySegmentDispatchBindGroup->entryDispatch[0].x = max(heavySegmentCount, 1u);
            heavySegmentDispatchBindGroup->entryDispatch[0].y = 1u;
            heavySegmentDispatchBindGroup->entryDispatch[0].z = 1u;

            heavyComposeDispatchBindGroup->entryDispatch[0].x = max(heavyTileCount, 1u);
            heavyComposeDispatchBindGroup->entryDispatch[0].y = 1u;
            heavyComposeDispatchBindGroup->entryDispatch[0].z = 1u;
        }
    };

    class [[LocalWorkGroupSize(TileSize, TileSize, 1)]] LightTileRasterPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]],
                    BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot2]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot3]],
                    BindGroup<LightTileTaskBindGroup> lightTileTaskBindGroup [[Slot4]],
                    BindGroup<OutputBindGroup> outputBindGroup [[Slot5]]
        )
        {
        }

    private:
        void compute(uint3 groupThreadID [[GroupThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint lightTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisLightTileCount]);
            const uint taskIndex = groupID.x;
            if (taskIndex >= lightTileCount)
            {
                return;
            }

            const LightTileTask task = lightTileTaskBindGroup->lightTileTasks[taskIndex];
            const uint2 tileCoordinates = tileCoordinatesFromId(task.tileId, globals);
            const uint2 pixelCoordinate = uint2(tileCoordinates.x * TileSize + groupThreadID.x, tileCoordinates.y * TileSize + groupThreadID.y);
            const bool pixelInBounds = pixelCoordinate.x < globals.imageInfo.x && pixelCoordinate.y < globals.imageInfo.y;

            const float3 background = backgroundColor(globals);
            float3 accumColor = float3(0.0f);
            float transmission = 1.0f;
            const float2 pixelCenter = float2((float)pixelCoordinate.x, (float)pixelCoordinate.y);
            const uint rangeStart = task.rangeStart;
            const uint rangeEnd = task.rangeStart + task.entryCount;

            GroupShared<uint> sharedDoneFlags[TileRasterBatchSize];
            GroupShared<uint> sharedAllDone;
            GroupShared<float4> sharedCenterRadiusDepth[TileRasterBatchSize];
            GroupShared<float4> sharedConicOpacity[TileRasterBatchSize];
            GroupShared<float4> sharedColor[TileRasterBatchSize];

            for (uint batchStart = rangeStart; batchStart < rangeEnd; batchStart += TileRasterBatchSize)
            {
                const bool pixelDone = !pixelInBounds || transmission < 0.0001f;
                sharedDoneFlags[groupIndex] = pixelDone ? 1u : 0u;
                GroupMemoryBarrierWithGroupSync();

                if (groupIndex == 0u)
                {
                    uint allDone = 1u;
                    for (uint laneIndex = 0u; laneIndex < TileRasterBatchSize; ++laneIndex)
                    {
                        allDone = allDone & sharedDoneFlags[laneIndex];
                    }
                    sharedAllDone = allDone;
                }
                GroupMemoryBarrierWithGroupSync();

                if (sharedAllDone != 0u)
                {
                    break;
                }

                const uint batchCount = min(TileRasterBatchSize, rangeEnd - batchStart);
                const uint stagedEntryIndex = batchStart + groupIndex;
                if (groupIndex < batchCount && stagedEntryIndex < rangeEnd)
                {
                    const SortEntry entry = entryBindGroup->entries[stagedEntryIndex];
                    float4 stagedCenterRadiusDepth = float4(0.0f, 0.0f, -1.0f, 0.0f);
                    float4 stagedConicOpacity = float4(0.0f);
                    float4 stagedColor = float4(0.0f);
                    if (entry.splatIndex < globals.sceneInfo.x)
                    {
                        const ProjectedGaussian projected = projectedBindGroup->projected[entry.splatIndex];
                        stagedCenterRadiusDepth = projected.centerRadiusDepth;
                        stagedConicOpacity = projected.conicOpacity;
                        stagedColor = projected.color;
                    }
                    sharedCenterRadiusDepth[groupIndex] = stagedCenterRadiusDepth;
                    sharedConicOpacity[groupIndex] = stagedConicOpacity;
                    sharedColor[groupIndex] = stagedColor;
                }
                else
                {
                    sharedCenterRadiusDepth[groupIndex] = float4(0.0f, 0.0f, -1.0f, 0.0f);
                    sharedConicOpacity[groupIndex] = float4(0.0f);
                    sharedColor[groupIndex] = float4(0.0f);
                }
                GroupMemoryBarrierWithGroupSync();

                if (pixelInBounds && transmission >= 0.0001f)
                {
                    for (uint batchIndex = 0u; batchIndex < batchCount; ++batchIndex)
                    {
                        const float4 sharedCenter = sharedCenterRadiusDepth[batchIndex];
                        const float supportRadius = sharedCenter.z;
                        if (supportRadius <= 0.0f)
                        {
                            continue;
                        }

                        const float4 sharedConic = sharedConicOpacity[batchIndex];
                        const float4 sharedRgb = sharedColor[batchIndex];
                        const float2 delta = sharedCenter.xy - pixelCenter;

                        if (abs(delta.x) > supportRadius || abs(delta.y) > supportRadius)
                        {
                            continue;
                        }

                        const float power = -0.5f * (delta.x * delta.x * sharedConic.x + delta.y * delta.y * sharedConic.z) - delta.x * delta.y * sharedConic.y;
                        if (power > 0.0f || power < sharedRgb.w)
                        {
                            continue;
                        }

                        const float alpha = min(0.99f, sharedConic.w * exp(power));
                        if (alpha < MinVisibleAlpha)
                        {
                            continue;
                        }

                        accumColor += sharedRgb.xyz * (alpha * transmission);
                        transmission *= (1.0f - alpha);
                        if (transmission < 0.0001f)
                        {
                            break;
                        }
                    }
                }
                GroupMemoryBarrierWithGroupSync();
            }

            if (!pixelInBounds)
            {
                return;
            }

            const float3 finalColor = accumColor + background * transmission;
            outputBindGroup->outputTexture->write(pixelCoordinate, half4(finalColor, 1.0f));
        }
    };

    class [[LocalWorkGroupSize(TileSize, TileSize, 1)]] HeavyTileSegmentRasterPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<ProjectedBindGroup> projectedBindGroup [[Slot1]],
                    BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot2]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot3]],
                    BindGroup<HeavySegmentTaskBindGroup> heavySegmentTaskBindGroup [[Slot4]],
                    BindGroup<SegmentSummaryBindGroup> segmentSummaryBindGroup [[Slot5]]
        )
        {
        }

    private:
        void compute(uint3 groupThreadID [[GroupThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint heavySegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavySegmentCount]);
            const uint taskIndex = groupID.x;
            if (taskIndex >= heavySegmentCount)
            {
                return;
            }

            const HeavySegmentTask task = heavySegmentTaskBindGroup->heavySegmentTasks[taskIndex];
            const uint2 tileCoordinates = tileCoordinatesFromId(task.tileId, globals);
            const uint2 pixelCoordinate = uint2(tileCoordinates.x * TileSize + groupThreadID.x, tileCoordinates.y * TileSize + groupThreadID.y);
            const bool pixelInBounds = pixelCoordinate.x < globals.imageInfo.x && pixelCoordinate.y < globals.imageInfo.y;

            float3 accumColor = float3(0.0f);
            float transmission = 1.0f;
            const float2 pixelCenter = float2((float)pixelCoordinate.x, (float)pixelCoordinate.y);
            const uint rangeStart = task.rangeStart;
            const uint rangeEnd = task.rangeStart + task.entryCount;

            GroupShared<float4> sharedCenterRadiusDepth[TileRasterBatchSize];
            GroupShared<float4> sharedConicOpacity[TileRasterBatchSize];
            GroupShared<float4> sharedColor[TileRasterBatchSize];

            const uint stagedEntryIndex = rangeStart + groupIndex;
            if (groupIndex < task.entryCount && stagedEntryIndex < rangeEnd)
            {
                const SortEntry entry = entryBindGroup->entries[stagedEntryIndex];
                float4 stagedCenterRadiusDepth = float4(0.0f, 0.0f, -1.0f, 0.0f);
                float4 stagedConicOpacity = float4(0.0f);
                float4 stagedColor = float4(0.0f);
                if (entry.splatIndex < globals.sceneInfo.x)
                {
                    const ProjectedGaussian projected = projectedBindGroup->projected[entry.splatIndex];
                    stagedCenterRadiusDepth = projected.centerRadiusDepth;
                    stagedConicOpacity = projected.conicOpacity;
                    stagedColor = projected.color;
                }
                sharedCenterRadiusDepth[groupIndex] = stagedCenterRadiusDepth;
                sharedConicOpacity[groupIndex] = stagedConicOpacity;
                sharedColor[groupIndex] = stagedColor;
            }
            else
            {
                sharedCenterRadiusDepth[groupIndex] = float4(0.0f, 0.0f, -1.0f, 0.0f);
                sharedConicOpacity[groupIndex] = float4(0.0f);
                sharedColor[groupIndex] = float4(0.0f);
            }
            GroupMemoryBarrierWithGroupSync();

            if (pixelInBounds)
            {
                for (uint batchIndex = 0u; batchIndex < task.entryCount; ++batchIndex)
                {
                    const float4 sharedCenter = sharedCenterRadiusDepth[batchIndex];
                    const float supportRadius = sharedCenter.z;
                    if (supportRadius <= 0.0f)
                    {
                        continue;
                    }

                    const float4 sharedConic = sharedConicOpacity[batchIndex];
                    const float4 sharedRgb = sharedColor[batchIndex];
                    const float2 delta = sharedCenter.xy - pixelCenter;

                    if (abs(delta.x) > supportRadius || abs(delta.y) > supportRadius)
                    {
                        continue;
                    }

                    const float power = -0.5f * (delta.x * delta.x * sharedConic.x + delta.y * delta.y * sharedConic.z) - delta.x * delta.y * sharedConic.y;
                    if (power > 0.0f || power < sharedRgb.w)
                    {
                        continue;
                    }

                    const float alpha = min(0.99f, sharedConic.w * exp(power));
                    if (alpha < MinVisibleAlpha)
                    {
                        continue;
                    }

                    accumColor += sharedRgb.xyz * (alpha * transmission);
                    transmission *= (1.0f - alpha);
                    if (transmission < 0.0001f)
                    {
                        break;
                    }
                }
            }

            const uint summaryIndex = task.summarySegmentIndex * TileRasterBatchSize + groupIndex;
            segmentSummaryBindGroup->summaries[summaryIndex].colorTransmission = half4(accumColor, transmission);
        }
    };

    class [[LocalWorkGroupSize(TileSize, TileSize, 1)]] HeavyTileComposePass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot1]],
                    BindGroup<HeavyTileDescBindGroup> heavyTileDescBindGroup [[Slot2]],
                    BindGroup<SegmentSummaryBindGroup> segmentSummaryBindGroup [[Slot3]],
                    BindGroup<OutputBindGroup> outputBindGroup [[Slot4]]
        )
        {
        }

    private:
        void compute(uint3 groupThreadID [[GroupThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const uint heavyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyTileCount]);
            const uint taskIndex = groupID.x;
            if (taskIndex >= heavyTileCount)
            {
                return;
            }

            const HeavyTileDesc heavyTileDesc = heavyTileDescBindGroup->heavyTileDescs[taskIndex];
            const uint2 tileCoordinates = tileCoordinatesFromId(heavyTileDesc.tileId, globals);
            const uint2 pixelCoordinate = uint2(tileCoordinates.x * TileSize + groupThreadID.x, tileCoordinates.y * TileSize + groupThreadID.y);
            if (pixelCoordinate.x >= globals.imageInfo.x || pixelCoordinate.y >= globals.imageInfo.y)
            {
                return;
            }

            float3 accumColor = float3(0.0f);
            float transmission = 1.0f;
            for (uint segmentIndex = 0u; segmentIndex < heavyTileDesc.segmentCount; ++segmentIndex)
            {
                const uint summaryIndex = (heavyTileDesc.segmentBase + segmentIndex) * TileRasterBatchSize + groupIndex;
                const float4 segmentSummary = float4(segmentSummaryBindGroup->summaries[summaryIndex].colorTransmission);
                accumColor += transmission * segmentSummary.xyz;
                transmission *= segmentSummary.w;
                if (transmission < 0.0001f)
                {
                    break;
                }
            }

            const float3 finalColor = accumColor + backgroundColor(globals) * transmission;
            outputBindGroup->outputTexture->write(pixelCoordinate, half4(finalColor, 1.0f));
        }
    };

    class [[LocalWorkGroupSize(8, 8, 1)]] DebugOverlayPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot0]], BindGroup<OutputBindGroup> outputBindGroup [[Slot1]])
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x >= 12u || threadID.y >= 12u)
            {
                return;
            }

            float3 debugColor = float3(0.0f);
            if (atomicLoad(debugCounterBindGroup->duplicateOverflowCounter[0]) > 0u)
            {
                debugColor += float3(1.0f, 0.0f, 1.0f);
            }
            if (atomicLoad(debugCounterBindGroup->scatterOverflowCounter[0]) > 0u)
            {
                debugColor += float3(0.0f, 1.0f, 1.0f);
            }
            if (debugColor.x <= 0.0f && debugColor.y <= 0.0f && debugColor.z <= 0.0f)
            {
                return;
            }

            outputBindGroup->outputTexture->write(threadID.xy, half4(saturate(debugColor), 1.0f));
        }
    };

    class [[LocalWorkGroupSize(8, 8, 1)]] ClearOutputPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<OutputBindGroup> outputBindGroup [[Slot1]])
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            if (threadID.x >= globals.imageInfo.x || threadID.y >= globals.imageInfo.y)
            {
                return;
            }

            outputBindGroup->outputTexture->write(threadID.xy, half4(backgroundColor(globals), 1.0f));
        }
    };
} // namespace GsViewer

class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;

    Buffer<Camera, BufferUsage<Uniform, CopyDst>> cameraBuffer;
    Buffer<GsViewer::ViewerGlobals, BufferUsage<Uniform, CopyDst>> viewerGlobalsBuffer;
    std::vector<Buffer<GsViewer::ViewerGlobals, BufferUsage<Uniform, CopyDst>>> sortPassGlobalsBuffers;
    Buffer<uint, BufferUsage<Storage, CopyDst>> duplicateCounterBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> overflowCounterBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> duplicateOverflowCounterBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> scatterOverflowCounterBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> rasterAnalysisCounterBuffer;
    Buffer<GsViewer::BudgetTelemetry, BufferUsage<Storage, CopyDst, CopySrc>> budgetTelemetryBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> duplicateDispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> duplicateEmitHeavyDispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> amd8DispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> sortScanDispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> lightTileDispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> heavySegmentDispatchIndirectBuffer;
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> heavyComposeDispatchIndirectBuffer;
    std::array<Buffer<GsViewer::BudgetTelemetry, BufferUsage<CopyDst, MapRead>>, GsViewer::BudgetReadbackBufferCount> budgetTelemetryReadbackBuffers;
    Buffer<uint, BufferUsage<Storage, CopyDst>> duplicateBlockSumBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> duplicateBlockPrefixBuffer;
    Buffer<GsViewer::PrefixData, BufferUsage<Storage, CopyDst>> amd8PrefixDataBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> amd8BucketBaseBuffer;

    Buffer<GsViewer::GaussianSplatGPU, BufferUsage<Storage, CopyDst>> gaussianBuffer;
    Buffer<GsViewer::GaussianShRestGPU, BufferUsage<Storage, CopyDst>> gaussianShRestBuffer;
    Buffer<GsViewer::ProjectedGaussian, BufferUsage<Storage, CopyDst>> projectedBuffer;
    Buffer<GsViewer::SortEntry, BufferUsage<Storage, CopyDst>> sortPingBuffer;
    Buffer<GsViewer::SortEntry, BufferUsage<Storage, CopyDst>> sortPongBuffer;
    Buffer<GsViewer::DuplicateEmitTask, BufferUsage<Storage, CopyDst>> duplicateEmitTaskBuffer;
    Buffer<GsViewer::TileRange, BufferUsage<Storage, CopyDst>> tileRangeBuffer;
    Buffer<GsViewer::LightTileTask, BufferUsage<Storage, CopyDst>> lightTileTaskBuffer;
    Buffer<GsViewer::HeavyTileDesc, BufferUsage<Storage, CopyDst>> heavyTileDescBuffer;
    Buffer<GsViewer::HeavySegmentTask, BufferUsage<Storage, CopyDst>> heavySegmentTaskBuffer;
    Buffer<GsViewer::SegmentPixelSummary, BufferUsage<Storage, CopyDst>> segmentSummaryBuffer;

    BindGroup<CameraBindGroup> cameraBindGroup;
    BindGroup<GsViewer::FrameStateBindGroup> frameStateBindGroup;
    std::vector<BindGroup<GsViewer::FrameStateBindGroup>> sortPassFrameStateBindGroups;
    BindGroup<GsViewer::SceneBindGroup> sceneBindGroup;
    BindGroup<GsViewer::SceneShBindGroup> sceneShBindGroup;
    BindGroup<GsViewer::ProjectedBindGroup> projectedBindGroup;
    BindGroup<GsViewer::DuplicateStateBindGroup> duplicateStateBindGroup;
    BindGroup<GsViewer::DebugCounterBindGroup> debugCounterBindGroup;
    BindGroup<GsViewer::RasterAnalysisBindGroup> rasterAnalysisBindGroup;
    BindGroup<GsViewer::BudgetTelemetryBindGroup> budgetTelemetryBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> duplicateDispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> duplicateEmitHeavyDispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> amd8DispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> sortScanDispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> lightTileDispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> heavySegmentDispatchBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> heavyComposeDispatchBindGroup;
    BindGroup<GsViewer::EntryBufferBindGroup> sortPingReadBindGroup;
    BindGroup<GsViewer::EntryBufferBindGroup> sortPongReadBindGroup;
    BindGroup<GsViewer::DuplicateEmitTaskBindGroup> duplicateEmitTaskBindGroup;
    BindGroup<GsViewer::EntryPairBindGroup> sortPingToPongBindGroup;
    BindGroup<GsViewer::EntryPairBindGroup> sortPongToPingBindGroup;
    BindGroup<GsViewer::TileRangeBindGroup> tileRangeBindGroup;
    BindGroup<GsViewer::LightTileTaskBindGroup> lightTileTaskBindGroup;
    BindGroup<GsViewer::HeavyTileDescBindGroup> heavyTileDescBindGroup;
    BindGroup<GsViewer::HeavySegmentTaskBindGroup> heavySegmentTaskBindGroup;
    BindGroup<GsViewer::SegmentSummaryBindGroup> segmentSummaryBindGroup;
    BindGroup<GsViewer::PrefixDataBindGroup> amd8PrefixDataBindGroup;
    BindGroup<GsViewer::BucketBaseBindGroup> amd8BucketBaseBindGroup;
    BindGroup<GsViewer::OutputBindGroup> outputBindGroup;
    BindGroup<PresentQuadBindGroup> presentBindGroup;

    ComputeClass<GsViewer::ProjectGaussiansPass> projectPass;
    ComputeClass<GsViewer::DuplicatePrefixPass> duplicatePrefixPass;
    ComputeClass<GsViewer::DuplicateBlockPrefixPass> duplicateBlockPrefixPass;
    ComputeClass<GsViewer::BuildDuplicateEmitTasksPass> buildDuplicateEmitTasksPass;
    ComputeClass<GsViewer::BuildDuplicateEmitHeavyDispatchPass> buildDuplicateEmitHeavyDispatchPass;
    ComputeClass<GsViewer::DuplicateEmitPass> duplicateEmitPass;
    ComputeClass<GsViewer::DuplicateEmitHeavyPass> duplicateEmitHeavyPass;
    ComputeClass<GsViewer::BuildDuplicateDispatchPass> buildDuplicateDispatchPass;
    ComputeClass<GsViewer::BuildAmd8DispatchPass> buildAmd8DispatchPass;
    ComputeClass<GsViewer::FinalizeBudgetTelemetryPass> finalizeBudgetTelemetryPass;
    ComputeClass<GsViewer::BuildSortScanDispatchPass> buildSortScanDispatchPass;
    ComputeClass<GsViewer::Amd8PrefixPass> amd8PrefixPass;
    ComputeClass<GsViewer::Amd8ResolveOffsetsPass> amd8ResolveOffsetsPass;
    ComputeClass<GsViewer::Amd8ScatterPass> amd8ScatterPass;
    ComputeClass<GsViewer::TileRangePass> tileRangePass;
    ComputeClass<GsViewer::BuildTileTasksPass> buildTileTasksPass;
    ComputeClass<GsViewer::BuildTileTaskDispatchPass> buildTileTaskDispatchPass;
    ComputeClass<GsViewer::LightTileRasterPass> lightTileRasterPass;
    ComputeClass<GsViewer::HeavyTileSegmentRasterPass> heavyTileSegmentRasterPass;
    ComputeClass<GsViewer::HeavyTileComposePass> heavyTileComposePass;
    ComputeClass<GsViewer::DebugOverlayPass> debugOverlayPass;
    ComputeClass<GsViewer::ClearOutputPass> clearOutputPass;
    RenderClass<PresentQuad> presentQuad;

    Texture<UGL::TextureFormat::RGBA16Float, TextureUsage<StorageBinding, TextureBinding>, TextureDimension::e2D> outputTexture;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> presentTexture;
    Sampler sampler;
    SimpleCamera cameraController;
    Camera camera;

    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t tileCountX = 80;
    uint32_t tileCountY = 45;
    uint32_t sceneSplatCount = 0;
    uint32_t sceneShDegree = 0u;
    uint32_t maxSortEntryCount = GsViewer::SafeMaxSortEntryCount;
    uint32_t maxAmd8PartitionCount = 1u;
    uint32_t maxDuplicateEmitTaskCount = 1u;
    uint32_t maxHeavySegmentTaskCount = 1u;
    bool sceneReady = false;
    float sceneAverageAxisRadius = 0.0f;
    float sceneMaxAxisRadius = 0.0f;
    float sceneBoundsDiagonal = 1.0f;
    float3 sceneCenter = float3(0.0f, 0.0f, 0.0f);
    uint64_t submittedFrameCount = 0u;
    uint32_t lastConsumedTelemetryFrameTag = GsViewer::InvalidTelemetryFrameTag;
    GsViewer::BudgetTelemetry latestBudgetTelemetry = {};
    bool hasLatestBudgetTelemetry = false;

    float backgroundR = 0.05f;
    float backgroundG = 0.07f;
    float backgroundB = 0.1f;

    int lastMouseMoveX = -1;
    int lastMouseMoveY = -1;
    [[Export]] int MouseLeftClick = 0;
    [[Export]] int mouseMoveX = 0;
    [[Export]] int mouseMoveY = 0;
    [[Export]] int cameraMove = 0;
    [[Export]] double durationTicks = 0;

public:
    void init(Device device, Swapchain swapchain)
    {
        this->device = device;
        this->swapchain = swapchain;

        cameraController.create(CameraType::FPS, float3(0.0f, 0.0f, 3.0f));

        sampler = device->createSampler({
            .label = "GsViewerSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1,
        });

        cameraBuffer = device->createBuffer("GsCameraBuffer", 1);
        viewerGlobalsBuffer = device->createBuffer("GsViewerGlobalsBuffer", 1);
        sortPassGlobalsBuffers.resize(GsViewer::Amd8RadixPassCount);
        duplicateCounterBuffer = device->createBuffer("GsDuplicateCounterBuffer", 1);
        overflowCounterBuffer = device->createBuffer("GsOverflowCounterBuffer", 1);
        duplicateOverflowCounterBuffer = device->createBuffer("GsDuplicateOverflowCounterBuffer", 1);
        scatterOverflowCounterBuffer = device->createBuffer("GsScatterOverflowCounterBuffer", 1);
        rasterAnalysisCounterBuffer = device->createBuffer("GsRasterAnalysisCounterBuffer", GsViewer::RasterAnalysisCounterCount);
        budgetTelemetryBuffer = device->createBuffer("GsBudgetTelemetryBuffer", 1);
        duplicateDispatchIndirectBuffer = device->createBuffer("GsDuplicateDispatchIndirectBuffer", 1);
        duplicateEmitHeavyDispatchIndirectBuffer = device->createBuffer("GsDuplicateEmitHeavyDispatchIndirectBuffer", 1);
        amd8DispatchIndirectBuffer = device->createBuffer("GsAmd8DispatchIndirectBuffer", 1);
        sortScanDispatchIndirectBuffer = device->createBuffer("GsSortScanDispatchIndirectBuffer", 1);
        lightTileDispatchIndirectBuffer = device->createBuffer("GsLightTileDispatchIndirectBuffer", 1);
        heavySegmentDispatchIndirectBuffer = device->createBuffer("GsHeavySegmentDispatchIndirectBuffer", 1);
        heavyComposeDispatchIndirectBuffer = device->createBuffer("GsHeavyComposeDispatchIndirectBuffer", 1);

        cameraBindGroup = device->createBindGroup<CameraBindGroup>(cameraBuffer);
        frameStateBindGroup = device->createBindGroup<GsViewer::FrameStateBindGroup>(viewerGlobalsBuffer, duplicateCounterBuffer, overflowCounterBuffer);
        debugCounterBindGroup = device->createBindGroup<GsViewer::DebugCounterBindGroup>(duplicateOverflowCounterBuffer, scatterOverflowCounterBuffer);
        rasterAnalysisBindGroup = device->createBindGroup<GsViewer::RasterAnalysisBindGroup>(rasterAnalysisCounterBuffer);
        budgetTelemetryBindGroup = device->createBindGroup<GsViewer::BudgetTelemetryBindGroup>(budgetTelemetryBuffer);
        duplicateDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(duplicateDispatchIndirectBuffer);
        duplicateEmitHeavyDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(duplicateEmitHeavyDispatchIndirectBuffer);
        amd8DispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(amd8DispatchIndirectBuffer);
        sortScanDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(sortScanDispatchIndirectBuffer);
        lightTileDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(lightTileDispatchIndirectBuffer);
        heavySegmentDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(heavySegmentDispatchIndirectBuffer);
        heavyComposeDispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(heavyComposeDispatchIndirectBuffer);
        sortPassFrameStateBindGroups.resize(GsViewer::Amd8RadixPassCount);
        for (uint32_t passIndex = 0u; passIndex < GsViewer::Amd8RadixPassCount; ++passIndex)
        {
            sortPassGlobalsBuffers[passIndex] = device->createBuffer("GsViewerGlobalsSortPassBuffer", 1);
            sortPassFrameStateBindGroups[passIndex] = device->createBindGroup<GsViewer::FrameStateBindGroup>(sortPassGlobalsBuffers[passIndex], duplicateCounterBuffer, overflowCounterBuffer);
        }

        for (uint32_t readbackIndex = 0u; readbackIndex < GsViewer::BudgetReadbackBufferCount; ++readbackIndex)
        {
            budgetTelemetryReadbackBuffers[readbackIndex] = device->createBuffer("GsBudgetTelemetryReadbackBuffer", 1);
            budgetTelemetryReadbackBuffers[readbackIndex]->map();
            GsViewer::BudgetTelemetry invalidTelemetry = {};
            invalidTelemetry.frameTag = GsViewer::InvalidTelemetryFrameTag;
            if (budgetTelemetryReadbackBuffers[readbackIndex]->getMappedRange(0u, sizeof(GsViewer::BudgetTelemetry)) == 0)
            {
                budgetTelemetryReadbackBuffers[readbackIndex]->unmap();
                throw std::runtime_error("GsViewer budget telemetry readback buffer failed to expose a writable mapped range during initialization.");
            }
            std::memcpy(budgetTelemetryReadbackBuffers[readbackIndex]->getMappedRange(0u, sizeof(GsViewer::BudgetTelemetry)), &invalidTelemetry, sizeof(GsViewer::BudgetTelemetry));
            budgetTelemetryReadbackBuffers[readbackIndex]->unmap();
        }

        resize(width, height);
    }

    void resize(uint32_t newWidth, uint32_t newHeight)
    {
        width = std::max(newWidth, 1u);
        height = std::max(newHeight, 1u);
        tileCountX = (width + GsViewer::TileSize - 1u) / GsViewer::TileSize;
        tileCountY = (height + GsViewer::TileSize - 1u) / GsViewer::TileSize;

        outputTexture = device->createTexture("GsOutputTexture", width, height, 1);
        presentTexture = device->createTexture("GsPresentTexture", width, height, 1);
        outputBindGroup = device->createBindGroup<GsViewer::OutputBindGroup>(outputTexture->createView());
        presentBindGroup = device->createBindGroup<PresentQuadBindGroup>(outputTexture->createView(), sampler);
        presentQuad = device->createRenderClass<PresentQuad>(presentBindGroup);
        clearOutputPass = device->createComputeClass<GsViewer::ClearOutputPass>(frameStateBindGroup, outputBindGroup);

        camera.proj = PerspectiveLH(45.0f * 3.14159265359f / 180.0f, float(width) / float(height), 0.1f, 2048.0f);
        camera.projInv = inverse(camera.proj);

        if (sceneReady)
        {
            updateSortEntryBudget();
            rebuildSortResources();
        }
    }

    void uploadScene(const CpuGaussianScene &scene)
    {
        sceneSplatCount = (uint32_t)scene.splats.size();
        sceneShDegree = GsViewer::ShMaxDegree;
        sceneReady = sceneSplatCount > 0u;
        if (!sceneReady)
        {
            return;
        }

        std::vector<GsViewer::GaussianSplatGPU> uploadData;
        uploadData.resize(scene.splats.size());
        std::vector<GsViewer::GaussianShRestGPU> shUploadData;
        shUploadData.resize(scene.splats.size());

        double axisRadiusSum = 0.0;
        float axisRadiusMax = 0.0f;

        for (uint32_t splatIndex = 0u; splatIndex < sceneSplatCount; ++splatIndex)
        {
            const CpuGaussianSplat &source = scene.splats[splatIndex];
            GsViewer::GaussianSplatGPU destination;
            destination.positionOpacity = float4(source.position[0], source.position[1], source.position[2], source.opacity);
            destination.axis0 = float4(source.axis0[0], source.axis0[1], source.axis0[2], 0.0f);
            destination.axis1 = float4(source.axis1[0], source.axis1[1], source.axis1[2], 0.0f);
            destination.axis2 = float4(source.axis2[0], source.axis2[1], source.axis2[2], 0.0f);
            destination.baseColor = float4(source.color[0], source.color[1], source.color[2], 1.0f);
            destination.shDc = float4(source.shDc[0], source.shDc[1], source.shDc[2], 0.0f);
            GsViewer::GaussianShRestGPU shDestination = {};
            for (uint32_t coefficientIndex = 0u; coefficientIndex < GsViewer::ShMaxRestCoefficientCount; ++coefficientIndex)
            {
                const float coefficientValue = source.shRest[coefficientIndex];
                switch (coefficientIndex / 4u)
                {
                case 0u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff0.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff0.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff0.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff0.w = coefficientValue;
                        break;
                    }
                    break;
                case 1u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff1.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff1.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff1.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff1.w = coefficientValue;
                        break;
                    }
                    break;
                case 2u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff2.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff2.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff2.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff2.w = coefficientValue;
                        break;
                    }
                    break;
                case 3u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff3.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff3.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff3.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff3.w = coefficientValue;
                        break;
                    }
                    break;
                case 4u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff4.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff4.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff4.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff4.w = coefficientValue;
                        break;
                    }
                    break;
                case 5u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff5.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff5.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff5.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff5.w = coefficientValue;
                        break;
                    }
                    break;
                case 6u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff6.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff6.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff6.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff6.w = coefficientValue;
                        break;
                    }
                    break;
                case 7u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff7.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff7.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff7.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff7.w = coefficientValue;
                        break;
                    }
                    break;
                case 8u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff8.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff8.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff8.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff8.w = coefficientValue;
                        break;
                    }
                    break;
                case 9u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff9.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff9.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff9.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff9.w = coefficientValue;
                        break;
                    }
                    break;
                case 10u:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff10.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff10.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff10.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff10.w = coefficientValue;
                        break;
                    }
                    break;
                default:
                    switch (coefficientIndex % 4u)
                    {
                    case 0u:
                        shDestination.shCoeff11.x = coefficientValue;
                        break;
                    case 1u:
                        shDestination.shCoeff11.y = coefficientValue;
                        break;
                    case 2u:
                        shDestination.shCoeff11.z = coefficientValue;
                        break;
                    default:
                        shDestination.shCoeff11.w = coefficientValue;
                        break;
                    }
                    break;
                }
            }
            shUploadData[splatIndex] = shDestination;
            uploadData[splatIndex] = destination;

            const float axis0Length = sqrt(source.axis0[0] * source.axis0[0] + source.axis0[1] * source.axis0[1] + source.axis0[2] * source.axis0[2]);
            const float axis1Length = sqrt(source.axis1[0] * source.axis1[0] + source.axis1[1] * source.axis1[1] + source.axis1[2] * source.axis1[2]);
            const float axis2Length = sqrt(source.axis2[0] * source.axis2[0] + source.axis2[1] * source.axis2[1] + source.axis2[2] * source.axis2[2]);
            const float axisRadius = std::max(axis0Length, std::max(axis1Length, axis2Length));
            axisRadiusSum += axisRadius;
            axisRadiusMax = std::max(axisRadiusMax, axisRadius);
        }

        const float sceneExtentX = scene.boundsMax[0] - scene.boundsMin[0];
        const float sceneExtentY = scene.boundsMax[1] - scene.boundsMin[1];
        const float sceneExtentZ = scene.boundsMax[2] - scene.boundsMin[2];
        sceneBoundsDiagonal = sqrt(sceneExtentX * sceneExtentX + sceneExtentY * sceneExtentY + sceneExtentZ * sceneExtentZ);
        sceneBoundsDiagonal = std::max(sceneBoundsDiagonal, 0.001f);
        sceneAverageAxisRadius = float(axisRadiusSum / std::max<uint32_t>(sceneSplatCount, 1u));
        sceneMaxAxisRadius = axisRadiusMax;
        const uint64_t estimatedEntries = estimateSortEntryBudgetRaw();
        updateSortEntryBudget();
        if (estimatedEntries > uint64_t(GsViewer::SafeMaxSortEntryCount))
        {
            std::fprintf(stderr, "[3dgs-viewer] warning: sort entry budget capped. estimated=%llu capped=%u. Visual truncation is likely when many splats expand across tiles.\n", (unsigned long long)estimatedEntries, maxSortEntryCount);
            std::fflush(stderr);
        }

        gaussianBuffer = device->createBuffer("GsGaussianBuffer", sceneSplatCount);
        gaussianShRestBuffer = device->createBuffer("GsGaussianShRestBuffer", sceneSplatCount);
        projectedBuffer = device->createBuffer("GsProjectedBuffer", sceneSplatCount);
        const uint32_t duplicateBlockCount = std::max((sceneSplatCount + GsViewer::DuplicateWorkGroupSize - 1u) / GsViewer::DuplicateWorkGroupSize, 1u);
        duplicateBlockSumBuffer = device->createBuffer("GsDuplicateBlockSumBuffer", duplicateBlockCount);
        duplicateBlockPrefixBuffer = device->createBuffer("GsDuplicateBlockPrefixBuffer", duplicateBlockCount);

        sceneBindGroup = device->createBindGroup<GsViewer::SceneBindGroup>(gaussianBuffer);
        sceneShBindGroup = device->createBindGroup<GsViewer::SceneShBindGroup>(gaussianShRestBuffer);
        projectedBindGroup = device->createBindGroup<GsViewer::ProjectedBindGroup>(projectedBuffer);
        duplicateStateBindGroup = device->createBindGroup<GsViewer::DuplicateStateBindGroup>(duplicateBlockSumBuffer, duplicateBlockPrefixBuffer);

        projectPass = device->createComputeClass<GsViewer::ProjectGaussiansPass>(cameraBindGroup, frameStateBindGroup, sceneBindGroup, sceneShBindGroup, projectedBindGroup, rasterAnalysisBindGroup);
        duplicatePrefixPass = device->createComputeClass<GsViewer::DuplicatePrefixPass>(frameStateBindGroup, projectedBindGroup, duplicateStateBindGroup);
        duplicateBlockPrefixPass = device->createComputeClass<GsViewer::DuplicateBlockPrefixPass>(frameStateBindGroup, duplicateStateBindGroup, debugCounterBindGroup, budgetTelemetryBindGroup);
        buildDuplicateDispatchPass = device->createComputeClass<GsViewer::BuildDuplicateDispatchPass>(frameStateBindGroup, duplicateDispatchBindGroup);
        buildAmd8DispatchPass = device->createComputeClass<GsViewer::BuildAmd8DispatchPass>(frameStateBindGroup, amd8DispatchBindGroup);
        finalizeBudgetTelemetryPass = device->createComputeClass<GsViewer::FinalizeBudgetTelemetryPass>(debugCounterBindGroup, budgetTelemetryBindGroup, rasterAnalysisBindGroup);
        buildSortScanDispatchPass = device->createComputeClass<GsViewer::BuildSortScanDispatchPass>(frameStateBindGroup, sortScanDispatchBindGroup);
        rebuildSortResources();

        device->graphicsQueue(0)->writeBuffer(BufferRange(gaussianBuffer), uploadData.data(), sizeof(GsViewer::GaussianSplatGPU) * uploadData.size());
        device->graphicsQueue(0)->writeBuffer(BufferRange(gaussianShRestBuffer), shUploadData.data(), sizeof(GsViewer::GaussianShRestGPU) * shUploadData.size());

        const float extentX = scene.boundsMax[0] - scene.boundsMin[0];
        const float extentY = scene.boundsMax[1] - scene.boundsMin[1];
        const float extentZ = scene.boundsMax[2] - scene.boundsMin[2];
        const float fallbackCenterX = (scene.boundsMin[0] + scene.boundsMax[0]) * 0.5f;
        const float fallbackCenterY = (scene.boundsMin[1] + scene.boundsMax[1]) * 0.5f;
        const float fallbackCenterZ = (scene.boundsMin[2] + scene.boundsMax[2]) * 0.5f;
        const float fallbackRadius = std::max(std::max(extentX, extentY), extentZ) * 0.5f + 1.0f;

        const float focusCenterX = scene.hasFocusHint ? scene.focusCenter[0] : fallbackCenterX;
        const float focusCenterY = scene.hasFocusHint ? scene.focusCenter[1] : fallbackCenterY;
        const float focusCenterZ = scene.hasFocusHint ? scene.focusCenter[2] : fallbackCenterZ;
        const float focusRadius = scene.hasFocusHint ? std::max(scene.focusRadius, 0.25f) : fallbackRadius;
        sceneCenter = float3(focusCenterX, focusCenterY, focusCenterZ);

        if (scene.hasSuggestedCamera)
        {
            float3 suggestedForward = float3(scene.suggestedCameraForward[0], scene.suggestedCameraForward[1], scene.suggestedCameraForward[2]);
            const float suggestedForwardLengthSquared = dot(suggestedForward, suggestedForward);
            if (suggestedForwardLengthSquared > 1.0e-12f)
            {
                suggestedForward *= rsqrt(suggestedForwardLengthSquared);
            }
            else
            {
                suggestedForward = float3(0.0f, 0.0f, 1.0f);
            }

            const float suggestedYaw = atan2(suggestedForward.z, suggestedForward.x) * 180.0f / 3.14159265359f;
            const float suggestedPitch = asin(clamp(suggestedForward.y, -1.0f, 1.0f)) * 180.0f / 3.14159265359f;
            cameraController.create(
                CameraType::FPS,
                float3(scene.suggestedCameraPosition[0], scene.suggestedCameraPosition[1], scene.suggestedCameraPosition[2]),
                float3(0.0f, 1.0f, 0.0f),
                suggestedYaw,
                suggestedPitch
            );
        }
        else
        {
            cameraController.create(CameraType::FPS, float3(focusCenterX, focusCenterY, focusCenterZ + focusRadius * 2.5f));
        }
    }

private:
    uint64_t estimateSortEntryBudgetRaw() const
    {
        const uint64_t tileCount = std::max<uint64_t>(uint64_t(tileCountX) * uint64_t(tileCountY), 1u);
        const float averageRadiusRatio = sceneAverageAxisRadius / sceneBoundsDiagonal;
        const float maxRadiusRatio = sceneMaxAxisRadius / sceneBoundsDiagonal;

        uint64_t entriesPerSplat = 64ull;
        entriesPerSplat += std::min<uint64_t>(uint64_t(tileCount * averageRadiusRatio * 24.0f), 128ull);
        entriesPerSplat += std::min<uint64_t>(uint64_t(tileCount * maxRadiusRatio * 12.0f), 128ull);
        entriesPerSplat = std::clamp<uint64_t>(entriesPerSplat, 64ull, std::min<uint64_t>(tileCount, 256ull));

        return std::max<uint64_t>(sceneSplatCount * entriesPerSplat, 1ull << 19);
    }

    void updateSortEntryBudget()
    {
        maxSortEntryCount = uint32_t(std::min<uint64_t>(estimateSortEntryBudgetRaw(), uint64_t(GsViewer::SafeMaxSortEntryCount)));
    }

    uint32_t computeExpandedSortBudget(uint32_t requiredEntryCount, bool hadOverflow) const
    {
        const uint64_t staticBudget = estimateSortEntryBudgetRaw();
        const uint64_t requiredHeadroom = hadOverflow ? std::max<uint64_t>(uint64_t(requiredEntryCount) / 2u, 1ull << 18) : std::max<uint64_t>(uint64_t(requiredEntryCount) / 4u, 1ull << 16);
        const uint64_t requiredWithHeadroom = uint64_t(requiredEntryCount) + requiredHeadroom;
        uint64_t desiredBudget = std::max(staticBudget, requiredWithHeadroom);
        if (hadOverflow && desiredBudget <= maxSortEntryCount)
        {
            const uint64_t overflowDrivenGrowth = std::max<uint64_t>(uint64_t(maxSortEntryCount) * 2u, uint64_t(maxSortEntryCount) + (1ull << 19));
            desiredBudget = std::max(desiredBudget, overflowDrivenGrowth);
        }
        return uint32_t(std::min<uint64_t>(desiredBudget, uint64_t(GsViewer::SafeMaxSortEntryCount)));
    }

    void maybeGrowSortBudgetFromTelemetry()
    {
        if (!sceneReady || submittedFrameCount < GsViewer::BudgetReadbackBufferCount)
        {
            return;
        }

        const uint32_t newestSafeFrameTag = uint32_t((submittedFrameCount - GsViewer::BudgetReadbackBufferCount) & 0xffffffffu);
        bool foundTelemetry = false;
        GsViewer::BudgetTelemetry telemetry = {};
        uint32_t bestFrameTag = GsViewer::InvalidTelemetryFrameTag;

        for (uint32_t readbackIndex = 0u; readbackIndex < GsViewer::BudgetReadbackBufferCount; ++readbackIndex)
        {
            GsViewer::BudgetTelemetry candidate = {};
            budgetTelemetryReadbackBuffers[readbackIndex]->map();
            if (budgetTelemetryReadbackBuffers[readbackIndex]->getConstMappedRange(0u, sizeof(GsViewer::BudgetTelemetry)) == 0)
            {
                budgetTelemetryReadbackBuffers[readbackIndex]->unmap();
                throw std::runtime_error("GsViewer budget telemetry readback buffer failed to expose a readable mapped range during telemetry consumption.");
            }
            std::memcpy(&candidate, budgetTelemetryReadbackBuffers[readbackIndex]->getConstMappedRange(0u, sizeof(GsViewer::BudgetTelemetry)), sizeof(GsViewer::BudgetTelemetry));
            budgetTelemetryReadbackBuffers[readbackIndex]->unmap();

            if (candidate.frameTag == GsViewer::InvalidTelemetryFrameTag)
            {
                continue;
            }

            if (candidate.frameTag > newestSafeFrameTag)
            {
                continue;
            }

            if (lastConsumedTelemetryFrameTag != GsViewer::InvalidTelemetryFrameTag && candidate.frameTag <= lastConsumedTelemetryFrameTag)
            {
                continue;
            }

            if (!foundTelemetry || candidate.frameTag > bestFrameTag)
            {
                telemetry = candidate;
                bestFrameTag = candidate.frameTag;
                foundTelemetry = true;
            }
        }

        if (!foundTelemetry)
        {
            return;
        }

        latestBudgetTelemetry = telemetry;
        hasLatestBudgetTelemetry = true;

        const bool hadOverflow = telemetry.duplicateOverflowCount > 0u || telemetry.scatterOverflowCount > 0u;
        const uint32_t desiredBudget = computeExpandedSortBudget(telemetry.requiredEntryCount, hadOverflow);
        lastConsumedTelemetryFrameTag = telemetry.frameTag;
        if (desiredBudget <= maxSortEntryCount)
        {
            return;
        }

        maxSortEntryCount = desiredBudget;
        rebuildSortResources();
    }

    uint32_t computeMaxHeavySegmentTaskCount() const
    {
        const uint32_t tileCount = std::max(tileCountX * tileCountY, 1u);
        const uint32_t segmentCountFromEntries = std::max((maxSortEntryCount + GsViewer::HeavyTileSegmentEntryCount - 1u) / GsViewer::HeavyTileSegmentEntryCount, 1u);
        return std::max(tileCount + segmentCountFromEntries, 1u);
    }

    uint32_t computeMaxDuplicateEmitTaskCount() const
    {
        const uint32_t chunkTaskCount = std::max((maxSortEntryCount + GsViewer::DuplicateEmitChunkEntryCount - 1u) / GsViewer::DuplicateEmitChunkEntryCount, 1u);
        return std::max(sceneSplatCount + chunkTaskCount, 1u);
    }

    void rebuildSortResources()
    {
        sortPingBuffer = device->createBuffer("GsSortPingBuffer", maxSortEntryCount);
        sortPongBuffer = device->createBuffer("GsSortPongBuffer", maxSortEntryCount);
        maxDuplicateEmitTaskCount = computeMaxDuplicateEmitTaskCount();
        duplicateEmitTaskBuffer = device->createBuffer("GsDuplicateEmitTaskBuffer", maxDuplicateEmitTaskCount);
        tileRangeBuffer = device->createBuffer("GsTileRangeBuffer", std::max(tileCountX * tileCountY, 1u));
        maxAmd8PartitionCount = std::max((maxSortEntryCount + GsViewer::Amd8PartitionElementCount - 1u) / GsViewer::Amd8PartitionElementCount, 1u);
        maxHeavySegmentTaskCount = computeMaxHeavySegmentTaskCount();
        amd8BucketBaseBuffer = device->createBuffer("GsAmd8BucketBaseBuffer", GsViewer::Amd8RadixBucketCount);
        amd8PrefixDataBuffer = device->createBuffer("GsAmd8PrefixDataBuffer", maxAmd8PartitionCount * GsViewer::Amd8RadixBucketCount);
        lightTileTaskBuffer = device->createBuffer("GsLightTileTaskBuffer", std::max(tileCountX * tileCountY, 1u));
        heavyTileDescBuffer = device->createBuffer("GsHeavyTileDescBuffer", std::max(tileCountX * tileCountY, 1u));
        heavySegmentTaskBuffer = device->createBuffer("GsHeavySegmentTaskBuffer", maxHeavySegmentTaskCount);
        segmentSummaryBuffer = device->createBuffer("GsSegmentSummaryBuffer", uint64_t(maxHeavySegmentTaskCount) * uint64_t(GsViewer::TileRasterBatchSize));

        amd8BucketBaseBindGroup = device->createBindGroup<GsViewer::BucketBaseBindGroup>(amd8BucketBaseBuffer);
        amd8PrefixDataBindGroup = device->createBindGroup<GsViewer::PrefixDataBindGroup>(amd8PrefixDataBuffer);
        sortPingReadBindGroup = device->createBindGroup<GsViewer::EntryBufferBindGroup>(sortPingBuffer);
        sortPongReadBindGroup = device->createBindGroup<GsViewer::EntryBufferBindGroup>(sortPongBuffer);
        duplicateEmitTaskBindGroup = device->createBindGroup<GsViewer::DuplicateEmitTaskBindGroup>(duplicateEmitTaskBuffer);
        sortPingToPongBindGroup = device->createBindGroup<GsViewer::EntryPairBindGroup>(sortPingBuffer, sortPongBuffer);
        sortPongToPingBindGroup = device->createBindGroup<GsViewer::EntryPairBindGroup>(sortPongBuffer, sortPingBuffer);
        tileRangeBindGroup = device->createBindGroup<GsViewer::TileRangeBindGroup>(tileRangeBuffer);
        lightTileTaskBindGroup = device->createBindGroup<GsViewer::LightTileTaskBindGroup>(lightTileTaskBuffer);
        heavyTileDescBindGroup = device->createBindGroup<GsViewer::HeavyTileDescBindGroup>(heavyTileDescBuffer);
        heavySegmentTaskBindGroup = device->createBindGroup<GsViewer::HeavySegmentTaskBindGroup>(heavySegmentTaskBuffer);
        segmentSummaryBindGroup = device->createBindGroup<GsViewer::SegmentSummaryBindGroup>(segmentSummaryBuffer);

        buildDuplicateEmitTasksPass = device->createComputeClass<GsViewer::BuildDuplicateEmitTasksPass>(frameStateBindGroup, projectedBindGroup, rasterAnalysisBindGroup, duplicateEmitTaskBindGroup);
        buildDuplicateEmitHeavyDispatchPass = device->createComputeClass<GsViewer::BuildDuplicateEmitHeavyDispatchPass>(rasterAnalysisBindGroup, duplicateEmitHeavyDispatchBindGroup);
        duplicateEmitPass = device->createComputeClass<GsViewer::DuplicateEmitPass>(frameStateBindGroup, projectedBindGroup, duplicateStateBindGroup, sortPingReadBindGroup);
        duplicateEmitHeavyPass = device->createComputeClass<GsViewer::DuplicateEmitHeavyPass>(frameStateBindGroup, projectedBindGroup, duplicateStateBindGroup, rasterAnalysisBindGroup, duplicateEmitTaskBindGroup, sortPingReadBindGroup);
        amd8PrefixPass = device->createComputeClass<GsViewer::Amd8PrefixPass>(sortPassFrameStateBindGroups[0], sortPingToPongBindGroup, amd8PrefixDataBindGroup);
        amd8ResolveOffsetsPass = device->createComputeClass<GsViewer::Amd8ResolveOffsetsPass>(sortPassFrameStateBindGroups[0], amd8PrefixDataBindGroup, amd8BucketBaseBindGroup);
        amd8ScatterPass = device->createComputeClass<GsViewer::Amd8ScatterPass>(sortPassFrameStateBindGroups[0], sortPingToPongBindGroup, amd8PrefixDataBindGroup, amd8BucketBaseBindGroup, debugCounterBindGroup);
        tileRangePass = device->createComputeClass<GsViewer::TileRangePass>(frameStateBindGroup, sortPingReadBindGroup, tileRangeBindGroup);
        buildTileTasksPass = device->createComputeClass<GsViewer::BuildTileTasksPass>(frameStateBindGroup, tileRangeBindGroup, rasterAnalysisBindGroup, lightTileTaskBindGroup, heavyTileDescBindGroup, heavySegmentTaskBindGroup);
        buildTileTaskDispatchPass = device->createComputeClass<GsViewer::BuildTileTaskDispatchPass>(rasterAnalysisBindGroup, lightTileDispatchBindGroup, heavySegmentDispatchBindGroup, heavyComposeDispatchBindGroup);
        lightTileRasterPass = device->createComputeClass<GsViewer::LightTileRasterPass>(frameStateBindGroup, projectedBindGroup, sortPingReadBindGroup, rasterAnalysisBindGroup, lightTileTaskBindGroup, outputBindGroup);
        heavyTileSegmentRasterPass = device->createComputeClass<GsViewer::HeavyTileSegmentRasterPass>(frameStateBindGroup, projectedBindGroup, sortPingReadBindGroup, rasterAnalysisBindGroup, heavySegmentTaskBindGroup, segmentSummaryBindGroup);
        heavyTileComposePass = device->createComputeClass<GsViewer::HeavyTileComposePass>(frameStateBindGroup, rasterAnalysisBindGroup, heavyTileDescBindGroup, segmentSummaryBindGroup, outputBindGroup);
        debugOverlayPass = device->createComputeClass<GsViewer::DebugOverlayPass>(debugCounterBindGroup, outputBindGroup);
    }

    uint32_t getActiveSortPassCount() const
    {
        return GsViewer::Amd8RadixPassCount;
    }

    GsViewer::ViewerGlobals buildSortPassGlobals(uint32_t sortPassIndex) const
    {
        GsViewer::ViewerGlobals globals;
        uint32_t maxTileKey = std::max(tileCountX * tileCountY, 1u) - 1u;
        uint32_t tileKeyBitCount = 0u;
        while (maxTileKey > 0u)
        {
            tileKeyBitCount += 1u;
            maxTileKey >>= 1u;
        }
        tileKeyBitCount = std::max(tileKeyBitCount, 1u);
        const uint32_t depthKeyBitCount = 32u - tileKeyBitCount;
        const uint32_t depthKeyMask = depthKeyBitCount >= 32u ? 0xffffffffu : ((1u << depthKeyBitCount) - 1u);
        globals.imageInfo = uint4(width, height, tileCountX, tileCountY);
        globals.sceneInfo = uint4(sceneSplatCount, maxSortEntryCount, GsViewer::TileSize, sortPassIndex);
        globals.sortKeyInfo = uint4(tileKeyBitCount, depthKeyBitCount, depthKeyMask, sceneShDegree);
        globals.backgroundAndScale = float4(backgroundR, backgroundG, backgroundB, 1.0f);
        return globals;
    }

public:
    /**
     * Returns the camera matrices used by the most recent render submission.
     *
     * Use this for diagnostics captures only; it does not force GPU work to complete.
     */
    Camera getCurrentCamera() const
    {
        return camera;
    }

    bool getLatestBudgetTelemetry(GsViewer::BudgetTelemetry &telemetry) const
    {
        if (!hasLatestBudgetTelemetry)
        {
            return false;
        }

        telemetry = latestBudgetTelemetry;
        return true;
    }

    void render()
    {
        if (lastMouseMoveX == -1)
        {
            lastMouseMoveX = mouseMoveX;
            lastMouseMoveY = mouseMoveY;
        }
        else if (MouseLeftClick > 0)
        {
            cameraController.processMouseMovement(float(mouseMoveX - lastMouseMoveX), float(mouseMoveY - lastMouseMoveY), true);
        }

        lastMouseMoveX = mouseMoveX;
        lastMouseMoveY = mouseMoveY;
        cameraController.processKeyboard(cameraMove, (float)durationTicks);
        cameraController.update();

        camera.view = cameraController.getViewMatrix();
        camera.viewInv = inverse(camera.view);

        maybeGrowSortBudgetFromTelemetry();

        auto frameQueue = device->graphicsQueue(0);
            frameQueue->writeBuffer(BufferRange(cameraBuffer), &camera, sizeof(Camera));

        if (!sceneReady)
        {
            const GsViewer::ViewerGlobals globals = buildSortPassGlobals(0u);
            frameQueue->writeBuffer(BufferRange(viewerGlobalsBuffer), &globals, sizeof(globals))->computePass("GsClearOutput", clearOutputPass(width, height, 1));
        }
        else
        {
            const GsViewer::ViewerGlobals globals = buildSortPassGlobals(0u);
            GsViewer::BudgetTelemetry budgetTelemetry = {};
            budgetTelemetry.frameTag = uint32_t(submittedFrameCount & 0xffffffffu);
            const uint32_t activeSortPassCount = getActiveSortPassCount();
            frameQueue->writeBuffer(BufferRange(viewerGlobalsBuffer), &globals, sizeof(globals))
                ->writeBuffer(BufferRange(budgetTelemetryBuffer), &budgetTelemetry, sizeof(budgetTelemetry))
                ->fillBuffer(BufferRange(duplicateCounterBuffer), 0u)
                ->fillBuffer(BufferRange(overflowCounterBuffer), 0u)
                ->fillBuffer(BufferRange(duplicateOverflowCounterBuffer), 0u)
                ->fillBuffer(BufferRange(scatterOverflowCounterBuffer), 0u)
                ->fillBuffer(BufferRange(rasterAnalysisCounterBuffer), 0u)
                ->fillBuffer(BufferRange(duplicateBlockSumBuffer), 0u)
                ->fillBuffer(BufferRange(duplicateBlockPrefixBuffer), 0u)
                ->fillBuffer(BufferRange(tileRangeBuffer), 0xffffffffu);

            for (uint32_t passIndex = 0u; passIndex < activeSortPassCount; ++passIndex)
            {
                const GsViewer::ViewerGlobals sortPassGlobals = buildSortPassGlobals(passIndex);
                frameQueue->writeBuffer(BufferRange(sortPassGlobalsBuffers[passIndex]), &sortPassGlobals, sizeof(sortPassGlobals));
            }

            frameQueue->computePass("GsClearOutput", clearOutputPass(width, height, 1));
            frameQueue->computePass("GsProject", projectPass(sceneSplatCount, 1, 1));

            std::vector<ComputePassTaskDescriptor> preSortTasks;
            preSortTasks.reserve(9u);
            preSortTasks.push_back(buildDuplicateDispatchPass(1, 1, 1));
            preSortTasks.push_back(duplicatePrefixPass(duplicateDispatchIndirectBuffer));
            preSortTasks.push_back(duplicateBlockPrefixPass(1, 1, 1));
            preSortTasks.push_back(buildDuplicateEmitTasksPass(sceneSplatCount, 1, 1));
            preSortTasks.push_back(buildDuplicateEmitHeavyDispatchPass(1, 1, 1));
            preSortTasks.push_back(duplicateEmitPass(duplicateDispatchIndirectBuffer));
            preSortTasks.push_back(duplicateEmitHeavyPass(duplicateEmitHeavyDispatchIndirectBuffer));
            preSortTasks.push_back(buildAmd8DispatchPass(1, 1, 1));
            preSortTasks.push_back(buildSortScanDispatchPass(1, 1, 1));
            frameQueue->computePass("GsPreSort", preSortTasks);

            std::vector<ComputePassTaskDescriptor> sortTasks;
            sortTasks.reserve(1u + activeSortPassCount * 15u);
            for (uint32_t passIndex = 0; passIndex < activeSortPassCount; ++passIndex)
            {
                const bool pingToPong = (passIndex & 1u) == 0u;
                auto currentPairBindGroup = pingToPong ? sortPingToPongBindGroup : sortPongToPingBindGroup;
                auto currentFrameStateBindGroup = sortPassFrameStateBindGroups[passIndex];

                sortTasks.push_back(amd8PrefixPass->setBindGroup(0u, currentFrameStateBindGroup));
                sortTasks.push_back(amd8PrefixPass->setBindGroup(1u, currentPairBindGroup));
                sortTasks.push_back(amd8PrefixPass->setBindGroup(2u, amd8PrefixDataBindGroup));
                sortTasks.push_back(amd8PrefixPass(amd8DispatchIndirectBuffer));

                sortTasks.push_back(amd8ResolveOffsetsPass->setBindGroup(0u, currentFrameStateBindGroup));
                sortTasks.push_back(amd8ResolveOffsetsPass->setBindGroup(1u, amd8PrefixDataBindGroup));
                sortTasks.push_back(amd8ResolveOffsetsPass->setBindGroup(2u, amd8BucketBaseBindGroup));
                sortTasks.push_back(amd8ResolveOffsetsPass(GsViewer::Amd8RadixBucketCount, 1, 1));

                sortTasks.push_back(amd8ScatterPass->setBindGroup(0u, currentFrameStateBindGroup));
                sortTasks.push_back(amd8ScatterPass->setBindGroup(1u, currentPairBindGroup));
                sortTasks.push_back(amd8ScatterPass->setBindGroup(2u, amd8PrefixDataBindGroup));
                sortTasks.push_back(amd8ScatterPass->setBindGroup(3u, amd8BucketBaseBindGroup));
                sortTasks.push_back(amd8ScatterPass->setBindGroup(4u, debugCounterBindGroup));
                sortTasks.push_back(amd8ScatterPass(amd8DispatchIndirectBuffer));
            }
            sortTasks.push_back(tileRangePass(sortScanDispatchIndirectBuffer));
            frameQueue->computePass("GsSort", sortTasks);

            std::vector<ComputePassTaskDescriptor> tileTaskBuildTasks;
            tileTaskBuildTasks.reserve(2u);
            tileTaskBuildTasks.push_back(buildTileTasksPass(tileCountX * tileCountY, 1, 1));
            tileTaskBuildTasks.push_back(buildTileTaskDispatchPass(1, 1, 1));
            frameQueue->computePass("GsBuildTileTasks", tileTaskBuildTasks);

            std::vector<ComputePassTaskDescriptor> rasterTasks;
            rasterTasks.reserve(3u);
            rasterTasks.push_back(lightTileRasterPass(lightTileDispatchIndirectBuffer));
            rasterTasks.push_back(heavyTileSegmentRasterPass(heavySegmentDispatchIndirectBuffer));
            rasterTasks.push_back(heavyTileComposePass(heavyComposeDispatchIndirectBuffer));
            frameQueue->computePass("GsRaster", rasterTasks);
            frameQueue->computePass("GsDebugOverlay", debugOverlayPass(12u, 12u, 1));
            frameQueue->computePass("GsFinalizeTelemetry", finalizeBudgetTelemetryPass(1, 1, 1));

            const uint32_t writebackSlot = uint32_t(submittedFrameCount % GsViewer::BudgetReadbackBufferCount);
            frameQueue->blitPass("GsBudgetTelemetryReadback", copyBufferToBuffer(budgetTelemetryBuffer, 0u, budgetTelemetryReadbackBuffers[writebackSlot], 0u, sizeof(GsViewer::BudgetTelemetry)));
        }

        auto nextTextureStatus = swapchain->queryNextTexture();
        PresentQuadFrameBuffer quadFrameBuffer;
        quadFrameBuffer.color = presentTexture->createView();
        quadFrameBuffer.color.loadOp = LoadOp::Clear;
        quadFrameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        quadFrameBuffer.color.storeOp = StoreOp::Store;
        frameQueue->renderPass("GsPresentPreparePass", quadFrameBuffer, presentQuad(3, 1, 0, 0))
            ->renderToSwapchain(nextTextureStatus, presentTexture)
            ->submit();
        swapchain->present();
        submittedFrameCount += 1u;
    }
};

#endif
