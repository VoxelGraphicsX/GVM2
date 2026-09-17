#ifndef TEST_06_3DGS_PLY_COMPUTE_SORT_GRAPHICS_HPP
#define TEST_06_3DGS_PLY_COMPUTE_SORT_GRAPHICS_HPP

#include "UGL.h"
using namespace UGL;

#include "GaussianSplattingDslShared/Camera.hpp"
#include "GaussianSplattingDslShared/CpuGaussianScene.hpp"
#include "SimpleCamera.hpp"

#include <algorithm>
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
    static const float DefaultSortEntryFrustumDilation = 0.3f;
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
    static const float ShLodDegree0MaxRadiusPx = 1.5f;
    static const float ShLodDegree1MaxRadiusPx = 3.0f;
    static const float ShLodDegree2MaxRadiusPx = 6.0f;
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

#include "GaussianSplattingRegularHotLayouts.hpp"
#include "GaussianSplattingGpuLayouts.hpp"
#include "GaussianSplattingBillboardLayouts.hpp"
#include "GaussianSplattingStandardViewerGlobals.hpp"

#include "GaussianSplattingCoreBindGroups.hpp"
#include "GaussianSplattingRasterAnalysisBindGroup.hpp"
#include "GaussianSplattingGraphicsBindGroups.hpp"

#include "GaussianSplattingDslMath.hpp"

    class [[LocalWorkGroupSize(128, 1, 1)]] ProjectGaussiansPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<CameraBindGroup> cameraBindGroup [[Slot0]],
                    BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot1]],
                    BindGroup<SceneHotBindGroup> sceneHotBindGroup [[Slot2]],
                    BindGroup<SceneShBindGroup> sceneShBindGroup [[Slot3]],
                    BindGroup<ProjectedRenderBindGroup> projectedRenderBindGroup [[Slot4]],
                    BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot5]],
                    BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot6]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
        {
            const ViewerGlobals globals = frameStateBindGroup->globals->read();
            const Camera camera = cameraBindGroup->camBuffer->read();

            const uint splatIndex = threadID.x;
            uint localVisibleFlag = 0u;
            ProjectedGaussianRender renderProjected = {};
            SortEntry entry = {};

            if (splatIndex < globals.sceneInfo.x)
            {
                const GaussianSplatHotGPU splat = sceneHotBindGroup->splats[splatIndex];

                const float3 worldPosition = splat.positionOpacity.xyz;
                const float opacity = splat.positionOpacity.w;
                if (opacity > MinVisibleAlpha)
                {
                    const float3 cameraPosition = mul(camera.view, float4(worldPosition, 1.0f)).xyz;
                    if (cameraPosition.z > 0.0001f)
                    {
                        const float4 clip = mul(camera.proj, float4(cameraPosition, 1.0f));
                        if (clip.w > 0.00001f)
                        {
                            const float depth = clip.z / clip.w;
                            if (depth >= -0.001f && depth <= 1.001f)
                            {
                                float3 covariancePosition = cameraPosition;
                                const float tanHalfFovX = globals.projectionInfo.z;
                                const float tanHalfFovY = globals.projectionInfo.w;
                                const float clampedNormalizedX = clamp(covariancePosition.x / covariancePosition.z, -1.3f * tanHalfFovX, 1.3f * tanHalfFovX);
                                const float clampedNormalizedY = clamp(covariancePosition.y / covariancePosition.z, -1.3f * tanHalfFovY, 1.3f * tanHalfFovY);
                                covariancePosition.x = clampedNormalizedX * covariancePosition.z;
                                covariancePosition.y = clampedNormalizedY * covariancePosition.z;

                                const float3 cameraAxis0 = mul(camera.view, float4(splat.axis0.xyz, 0.0f)).xyz;
                                const float3 cameraAxis1 = mul(camera.view, float4(splat.axis1.xyz, 0.0f)).xyz;
                                const float3 cameraAxis2 = mul(camera.view, float4(splat.axis2.xyz, 0.0f)).xyz;

                                const float2 centerPx = projectClipToPixel(clip, globals);
                                const float focalX = globals.projectionInfo.x;
                                const float focalY = globals.projectionInfo.y;

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
                                if (determinant > 0.000001f)
                                {
                                    const float alphaThresholdPower = log(clamp(MinVisibleAlpha / opacity, 0.000001f, 0.999999f));
                                    const float supportScale = computeGaussianSupportScale(alphaThresholdPower);
                                    const float maxEigenvalue = maxEigenvalueSymmetric2x2(stabilizedXX, stabilizedXY, stabilizedYY);
                                    const float minEigenvalue = max(stabilizedXX + stabilizedYY - maxEigenvalue, 0.0f);
                                    if (maxEigenvalue > 0.0f && supportScale > 0.0f)
                                    {
                                        const float viewportDiagonal = globals.frameMathInfo.x;
                                        const float2 majorDirection = principalEigenvectorSymmetric2x2(stabilizedXX, stabilizedXY, stabilizedYY, maxEigenvalue);
                                        const float2 minorDirection = float2(-majorDirection.y, majorDirection.x);
                                        const float majorRadius = min(max(supportScale * sqrt(maxEigenvalue), 0.5f), viewportDiagonal);
                                        const float minorRadius = min(max(supportScale * sqrt(minEigenvalue), 0.5f), viewportDiagonal);
                                        const float2 majorAxis = majorDirection * majorRadius;
                                        const float2 minorAxis = minorDirection * minorRadius;
                                        const float2 quadHalfExtent = abs(majorAxis) + abs(minorAxis);
                                        const float aabbSupportRadius = max(quadHalfExtent.x, quadHalfExtent.y);
                                        if (aabbSupportRadius > 0.0f)
                                        {
                                            const int pixelMinX = (int)floor(centerPx.x - quadHalfExtent.x);
                                            const int pixelMinY = (int)floor(centerPx.y - quadHalfExtent.y);
                                            const int pixelMaxX = (int)ceil(centerPx.x + quadHalfExtent.x);
                                            const int pixelMaxY = (int)ceil(centerPx.y + quadHalfExtent.y);
                                            if (!(pixelMaxX < 0 || pixelMaxY < 0 || pixelMinX >= (int)globals.imageInfo.x || pixelMinY >= (int)globals.imageInfo.y))
                                            {
                                                const uint shDegree = chooseShLodDegree(min(globals.sortKeyInfo.w, ShMaxDegree), aabbSupportRadius);
                                                float3 finalColor = evaluateShColorDc(splat);
                                                if (shDegree > 0u)
                                                {
                                                    const GaussianShRestGPU shRest = sceneShBindGroup->shRest[splatIndex];
                                                    const float3 cameraWorldPosition = camera.viewInv[3].xyz;
                                                    const float3 rawViewDirection = worldPosition - cameraWorldPosition;
                                                    const float directionLengthSquared = dot(rawViewDirection, rawViewDirection);
                                                    float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
                                                    if (directionLengthSquared > 1.0e-12f)
                                                    {
                                                        viewDirection = rawViewDirection * rsqrt(directionLengthSquared);
                                                    }
                                                    finalColor = evaluateShColor(splat, shRest, viewDirection, shDegree);
                                                }

                                                localVisibleFlag = 1u;
                                                renderProjected.centerOpacitySupportScale = float4(centerPx, opacity, supportScale);
                                                renderProjected.color = float4(finalColor, 0.0f);
                                                renderProjected.quadAxis01 = float4(majorAxis, minorAxis);
                                                entry.sortKey = reversePackedDepthKeyForGraphics(packDepthKey(cameraPosition.z, globals), globals);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Compact visible splats inside the current wave and reserve global space once per non-empty wave.
            const bool visible = localVisibleFlag > 0u;
            const uint waveLaneIndex = WaveGetLaneIndex();
            const bool isWaveLeader = waveLaneIndex == 0u;
            const uint waveVisibleCount = WaveActiveCountBits(visible);
            const uint waveVisibleOffset = WavePrefixCountBits(visible);

            uint waveVisibleBase = 0u;
            uint waveWriteCount = 0u;
            if (isWaveLeader && waveVisibleCount > 0u)
            {
                const uint reservedBase = atomicAdd(frameStateBindGroup->duplicateCounter[0], waveVisibleCount);
                waveVisibleBase = reservedBase;
                if (waveVisibleBase < globals.sceneInfo.y)
                {
                    waveWriteCount = min(waveVisibleCount, globals.sceneInfo.y - waveVisibleBase);
                }

                const uint overflowCount = waveVisibleCount - waveWriteCount;
                if (overflowCount > 0u)
                {
                    atomicAdd(frameStateBindGroup->overflowCounter[0], overflowCount);
                }

                atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedSplatCount], waveVisibleCount);
                atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedEntryCount], waveVisibleCount);
                atomicMax(rasterAnalysisBindGroup->counters[RasterAnalysisMaxProjectedTileCount], 1u);
            }

            waveVisibleBase = WaveReadLaneAt(waveVisibleBase, 0u);
            waveWriteCount = WaveReadLaneAt(waveWriteCount, 0u);

            if (visible)
            {
                if (waveVisibleOffset < waveWriteCount)
                {
                    const uint visibleIndex = waveVisibleBase + waveVisibleOffset;
                    entry.splatIndex = visibleIndex;
                    projectedRenderBindGroup->projected[visibleIndex] = renderProjected;
                    entryBindGroup->entries[visibleIndex] = entry;
                }
            }
        }
    };

#include "GaussianSplattingVisibleSortEntriesPass.hpp"

#include "GaussianSplattingDuplicateProjectDispatchPass.hpp"

#include "GaussianSplattingStandardTelemetryPass.hpp"

#include "GaussianSplattingAmd8SortPasses.hpp"

#include "GaussianSplattingBillboardRenderPass.hpp"

#include "GaussianSplattingOutputPasses.hpp"
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
    Buffer<GsViewer::DispatchIndirectCommand, BufferUsage<Storage, CopyDst, Indirect>> amd8DispatchIndirectBuffer;
    Buffer<GsViewer::GaussianBillboardRenderPassIndirectRenderCommand, BufferUsage<Storage, CopyDst, CopySrc, Indirect>> gaussianRenderIndirectBuffer;
    std::array<Buffer<GsViewer::BudgetTelemetry, BufferUsage<CopyDst, MapRead>>, GsViewer::BudgetReadbackBufferCount> budgetTelemetryReadbackBuffers;
    Buffer<GsViewer::PrefixData, BufferUsage<Storage, CopyDst>> amd8PrefixDataBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> amd8BucketBaseBuffer;

    Buffer<GsViewer::GaussianSplatHotGPU, BufferUsage<Storage, CopyDst>> gaussianHotBuffer;
    Buffer<GsViewer::GaussianSplatColdGPU, BufferUsage<Storage, CopyDst>> gaussianColdBuffer;
    Buffer<GsViewer::GaussianShRestGPU, BufferUsage<Storage, CopyDst>> gaussianShRestBuffer;
    Buffer<GsViewer::ProjectedGaussianRender, BufferUsage<Storage, CopyDst>> visibleProjectedBuffer;
    Buffer<GsViewer::SortEntry, BufferUsage<Storage, CopyDst>> sortPingBuffer;
    Buffer<GsViewer::SortEntry, BufferUsage<Storage, CopyDst>> sortPongBuffer;

    BindGroup<CameraBindGroup> cameraBindGroup;
    BindGroup<GsViewer::ViewerGlobalsBindGroup> viewerGlobalsBindGroup;
    BindGroup<GsViewer::FrameStateBindGroup> frameStateBindGroup;
    std::vector<BindGroup<GsViewer::FrameStateBindGroup>> sortPassFrameStateBindGroups;
    BindGroup<GsViewer::SceneHotBindGroup> sceneHotBindGroup;
    BindGroup<GsViewer::SceneColdBindGroup> sceneColdBindGroup;
    BindGroup<GsViewer::SceneShBindGroup> sceneShBindGroup;
    BindGroup<GsViewer::ProjectedRenderBindGroup> visibleProjectedBindGroup;
    BindGroup<GsViewer::ProjectedRenderReadBindGroup> visibleProjectedReadBindGroup;
    BindGroup<GsViewer::DebugCounterBindGroup> debugCounterBindGroup;
    BindGroup<GsViewer::RasterAnalysisBindGroup> rasterAnalysisBindGroup;
    BindGroup<GsViewer::BudgetTelemetryBindGroup> budgetTelemetryBindGroup;
    BindGroup<GsViewer::EntryDispatchBindGroup> amd8DispatchBindGroup;
    BindGroup<GsViewer::RenderIndirectBindGroup> gaussianRenderIndirectBindGroup;
    BindGroup<GsViewer::EntryBufferBindGroup> sortPingReadBindGroup;
    BindGroup<GsViewer::EntryBufferBindGroup> sortPongReadBindGroup;
    BindGroup<GsViewer::EntryReadBindGroup> sortPingEntryReadBindGroup;
    BindGroup<GsViewer::EntryPairBindGroup> sortPingToPongBindGroup;
    BindGroup<GsViewer::EntryPairBindGroup> sortPongToPingBindGroup;
    BindGroup<GsViewer::PrefixDataBindGroup> amd8PrefixDataBindGroup;
    BindGroup<GsViewer::BucketBaseBindGroup> amd8BucketBaseBindGroup;
    BindGroup<GsViewer::OutputBindGroup> outputBindGroup;
    ComputeClass<GsViewer::ProjectGaussiansPass> projectGaussiansPass;
    ComputeClass<GsViewer::BuildAmd8DispatchPass> buildAmd8DispatchPass;
    ComputeClass<GsViewer::FinalizeBudgetTelemetryPass> finalizeBudgetTelemetryPass;
    ComputeClass<GsViewer::BuildGaussianRenderIndirectPass> buildGaussianRenderIndirectPass;
    ComputeClass<GsViewer::Amd8PrefixPass> amd8PrefixPass;
    ComputeClass<GsViewer::Amd8ResolveOffsetsPass> amd8ResolveOffsetsPass;
    ComputeClass<GsViewer::Amd8ScatterPass> amd8ScatterPass;
    ComputeClass<GsViewer::DebugOverlayPass> debugOverlayPass;
    ComputeClass<GsViewer::ClearOutputPass> clearOutputPass;
    RenderClass<GsViewer::GaussianBillboardRenderPass> gaussianBillboardRenderPass;
    Texture<UGL::TextureFormat::RGBA16Float, TextureUsage<CopySrc, StorageBinding, RenderAttachment, TextureBinding>, TextureDimension::e2D> outputTexture;
    Texture<UGL::TextureFormat::PreferredSwapchain, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> swapchainStagingTexture;
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
    float sortEntryFrustumDilation = GsViewer::DefaultSortEntryFrustumDilation;

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

        cameraBuffer = device->createBuffer("GsCameraBuffer", 1);
        viewerGlobalsBuffer = device->createBuffer("GsViewerGlobalsBuffer", 1);
        sortPassGlobalsBuffers.resize(GsViewer::Amd8RadixPassCount);
        duplicateCounterBuffer = device->createBuffer("GsDuplicateCounterBuffer", 1);
        overflowCounterBuffer = device->createBuffer("GsOverflowCounterBuffer", 1);
        duplicateOverflowCounterBuffer = device->createBuffer("GsDuplicateOverflowCounterBuffer", 1);
        scatterOverflowCounterBuffer = device->createBuffer("GsScatterOverflowCounterBuffer", 1);
        rasterAnalysisCounterBuffer = device->createBuffer("GsRasterAnalysisCounterBuffer", GsViewer::RasterAnalysisCounterCount);
        budgetTelemetryBuffer = device->createBuffer("GsBudgetTelemetryBuffer", 1);
        amd8DispatchIndirectBuffer = device->createBuffer("GsAmd8DispatchIndirectBuffer", 1);
        gaussianRenderIndirectBuffer = device->createBuffer("GsGaussianRenderIndirectBuffer", 1);

        cameraBindGroup = device->createBindGroup<CameraBindGroup>(cameraBuffer);
        viewerGlobalsBindGroup = device->createBindGroup<GsViewer::ViewerGlobalsBindGroup>(viewerGlobalsBuffer);
        frameStateBindGroup = device->createBindGroup<GsViewer::FrameStateBindGroup>(viewerGlobalsBuffer, duplicateCounterBuffer, overflowCounterBuffer);
        debugCounterBindGroup = device->createBindGroup<GsViewer::DebugCounterBindGroup>(duplicateOverflowCounterBuffer, scatterOverflowCounterBuffer);
        rasterAnalysisBindGroup = device->createBindGroup<GsViewer::RasterAnalysisBindGroup>(rasterAnalysisCounterBuffer);
        budgetTelemetryBindGroup = device->createBindGroup<GsViewer::BudgetTelemetryBindGroup>(budgetTelemetryBuffer);
        amd8DispatchBindGroup = device->createBindGroup<GsViewer::EntryDispatchBindGroup>(amd8DispatchIndirectBuffer);
        gaussianRenderIndirectBindGroup = device->createBindGroup<GsViewer::RenderIndirectBindGroup>(gaussianRenderIndirectBuffer);
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
        outputBindGroup = device->createBindGroup<GsViewer::OutputBindGroup>(outputTexture->createView());
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
        sceneShDegree = std::min<uint32_t>(scene.shDegree, GsViewer::ShMaxDegree);
        sceneReady = sceneSplatCount > 0u;
        if (!sceneReady)
        {
            return;
        }

        std::vector<GsViewer::GaussianSplatHotGPU> hotUploadData;
        hotUploadData.resize(scene.splats.size());
        std::vector<GsViewer::GaussianSplatColdGPU> coldUploadData;
        coldUploadData.resize(scene.splats.size());
        std::vector<GsViewer::GaussianShRestGPU> shUploadData;
        shUploadData.resize(scene.splats.size());

        double axisRadiusSum = 0.0;
        float axisRadiusMax = 0.0f;

        for (uint32_t splatIndex = 0u; splatIndex < sceneSplatCount; ++splatIndex)
        {
            const CpuGaussianSplat &source = scene.splats[splatIndex];
            GsViewer::GaussianSplatHotGPU hotDestination;
            hotDestination.positionOpacity = float4(source.position[0], source.position[1], source.position[2], source.opacity);
            hotDestination.axis0 = float4(source.axis0[0], source.axis0[1], source.axis0[2], 0.0f);
            hotDestination.axis1 = float4(source.axis1[0], source.axis1[1], source.axis1[2], 0.0f);
            hotDestination.axis2 = float4(source.axis2[0], source.axis2[1], source.axis2[2], 0.0f);
            hotDestination.shDc = float4(source.shDc[0], source.shDc[1], source.shDc[2], 0.0f);
            GsViewer::GaussianSplatColdGPU coldDestination;
            coldDestination.baseColor = float4(source.color[0], source.color[1], source.color[2], 1.0f);
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
            hotUploadData[splatIndex] = hotDestination;
            coldUploadData[splatIndex] = coldDestination;

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
            std::fprintf(stderr, "[3dgs-viewer] warning: visible-entry budget capped. estimated=%llu capped=%u. Visual truncation is likely when visible splats exceed the budget.\n", (unsigned long long)estimatedEntries, maxSortEntryCount);
            std::fflush(stderr);
        }

        gaussianHotBuffer = device->createBuffer("GsGaussianHotBuffer", sceneSplatCount);
        gaussianColdBuffer = device->createBuffer("GsGaussianColdBuffer", sceneSplatCount);
        gaussianShRestBuffer = device->createBuffer("GsGaussianShRestBuffer", sceneSplatCount);

        sceneHotBindGroup = device->createBindGroup<GsViewer::SceneHotBindGroup>(gaussianHotBuffer);
        sceneColdBindGroup = device->createBindGroup<GsViewer::SceneColdBindGroup>(gaussianColdBuffer);
        sceneShBindGroup = device->createBindGroup<GsViewer::SceneShBindGroup>(gaussianShRestBuffer);

        buildAmd8DispatchPass = device->createComputeClass<GsViewer::BuildAmd8DispatchPass>(frameStateBindGroup, amd8DispatchBindGroup);
        finalizeBudgetTelemetryPass = device->createComputeClass<GsViewer::FinalizeBudgetTelemetryPass>(debugCounterBindGroup, budgetTelemetryBindGroup, rasterAnalysisBindGroup);
        buildGaussianRenderIndirectPass = device->createComputeClass<GsViewer::BuildGaussianRenderIndirectPass>(frameStateBindGroup, gaussianRenderIndirectBindGroup);
        rebuildSortResources();

        device->graphicsQueue(0)->writeBuffer(BufferRange(gaussianHotBuffer), hotUploadData.data(), sizeof(GsViewer::GaussianSplatHotGPU) * hotUploadData.size());
        device->graphicsQueue(0)->writeBuffer(BufferRange(gaussianColdBuffer), coldUploadData.data(), sizeof(GsViewer::GaussianSplatColdGPU) * coldUploadData.size());
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
        return std::max<uint64_t>(sceneSplatCount, 1ull << 19);
    }

    void updateSortEntryBudget()
    {
        maxSortEntryCount = uint32_t(std::min<uint64_t>(estimateSortEntryBudgetRaw(), uint64_t(GsViewer::SafeMaxSortEntryCount)));
    }

    uint32_t computeExpandedSortBudget(uint32_t requiredEntryCount, bool hadOverflow) const
    {
        (void)requiredEntryCount;
        (void)hadOverflow;
        const uint64_t desiredBudget = estimateSortEntryBudgetRaw();
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
        lastConsumedTelemetryFrameTag = telemetry.frameTag;
    }

    void rebuildSortResources()
    {
        visibleProjectedBuffer = device->createBuffer("GsVisibleProjectedBuffer", maxSortEntryCount);
        sortPingBuffer = device->createBuffer("GsSortPingBuffer", maxSortEntryCount);
        sortPongBuffer = device->createBuffer("GsSortPongBuffer", maxSortEntryCount);
        maxAmd8PartitionCount = std::max((maxSortEntryCount + GsViewer::Amd8PartitionElementCount - 1u) / GsViewer::Amd8PartitionElementCount, 1u);
        amd8BucketBaseBuffer = device->createBuffer("GsAmd8BucketBaseBuffer", GsViewer::Amd8RadixBucketCount);
        amd8PrefixDataBuffer = device->createBuffer("GsAmd8PrefixDataBuffer", maxAmd8PartitionCount * GsViewer::Amd8RadixBucketCount);

        amd8BucketBaseBindGroup = device->createBindGroup<GsViewer::BucketBaseBindGroup>(amd8BucketBaseBuffer);
        amd8PrefixDataBindGroup = device->createBindGroup<GsViewer::PrefixDataBindGroup>(amd8PrefixDataBuffer);
        visibleProjectedBindGroup = device->createBindGroup<GsViewer::ProjectedRenderBindGroup>(visibleProjectedBuffer);
        visibleProjectedReadBindGroup = device->createBindGroup<GsViewer::ProjectedRenderReadBindGroup>(visibleProjectedBuffer);
        sortPingReadBindGroup = device->createBindGroup<GsViewer::EntryBufferBindGroup>(sortPingBuffer);
        sortPongReadBindGroup = device->createBindGroup<GsViewer::EntryBufferBindGroup>(sortPongBuffer);
        sortPingEntryReadBindGroup = device->createBindGroup<GsViewer::EntryReadBindGroup>(sortPingBuffer);
        sortPingToPongBindGroup = device->createBindGroup<GsViewer::EntryPairBindGroup>(sortPingBuffer, sortPongBuffer);
        sortPongToPingBindGroup = device->createBindGroup<GsViewer::EntryPairBindGroup>(sortPongBuffer, sortPingBuffer);

        if (sceneReady)
        {
            projectGaussiansPass = device->createComputeClass<GsViewer::ProjectGaussiansPass>(cameraBindGroup, frameStateBindGroup, sceneHotBindGroup, sceneShBindGroup, visibleProjectedBindGroup, sortPingReadBindGroup, rasterAnalysisBindGroup);
        }
        amd8PrefixPass = device->createComputeClass<GsViewer::Amd8PrefixPass>(sortPassFrameStateBindGroups[0], sortPingToPongBindGroup, amd8PrefixDataBindGroup);
        amd8ResolveOffsetsPass = device->createComputeClass<GsViewer::Amd8ResolveOffsetsPass>(sortPassFrameStateBindGroups[0], amd8PrefixDataBindGroup, amd8BucketBaseBindGroup);
        amd8ScatterPass = device->createComputeClass<GsViewer::Amd8ScatterPass>(sortPassFrameStateBindGroups[0], sortPingToPongBindGroup, amd8PrefixDataBindGroup, amd8BucketBaseBindGroup, debugCounterBindGroup);
        debugOverlayPass = device->createComputeClass<GsViewer::DebugOverlayPass>(debugCounterBindGroup, outputBindGroup);
        gaussianBillboardRenderPass = device->createRenderClass<GsViewer::GaussianBillboardRenderPass>(viewerGlobalsBindGroup, visibleProjectedReadBindGroup, sortPingEntryReadBindGroup);
    }

    uint32_t getActiveSortPassCount() const
    {
        return GsViewer::Amd8RadixPassCount;
    }

    GsViewer::ViewerGlobals buildSortPassGlobals(uint32_t sortPassIndex) const
    {
        GsViewer::ViewerGlobals globals;
        globals.imageInfo = uint4(width, height, tileCountX, tileCountY);
        globals.sceneInfo = uint4(sceneSplatCount, maxSortEntryCount, GsViewer::TileSize, sortPassIndex);
        // test06 no longer reserves sort-key bits for tile routing; the active pipeline is pure back-to-front depth sort.
        globals.sortKeyInfo = uint4(0u, 32u, 0xffffffffu, sceneShDegree);
        globals.backgroundAndScale = float4(backgroundR, backgroundG, backgroundB, sortEntryFrustumDilation);
        globals.projectionInfo = float4(
            camera.proj[0][0] * float(width) * 0.5f,
            camera.proj[1][1] * float(height) * 0.5f,
            1.0f / camera.proj[0][0],
            1.0f / camera.proj[1][1]
        );
        globals.frameMathInfo = float4(
            sqrt(float(width) * float(width) + float(height) * float(height)),
            0.0f,
            0.0f,
            0.0f
        );
        return globals;
    }

public:
    bool getLatestBudgetTelemetry(GsViewer::BudgetTelemetry &telemetry) const
    {
        if (!hasLatestBudgetTelemetry)
        {
            return false;
        }

        telemetry = latestBudgetTelemetry;
        return true;
    }

    Texture<UGL::TextureFormat::RGBA16Float, TextureUsage<CopySrc, StorageBinding, RenderAttachment, TextureBinding>, TextureDimension::e2D> getOutputTextureHandle() const
    {
        return outputTexture;
    }

    Texture<UGL::TextureFormat::PreferredSwapchain, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> getSwapchainStagingTextureHandle() const
    {
        return swapchainStagingTexture;
    }

    Buffer<GsViewer::GaussianBillboardRenderPassIndirectRenderCommand, BufferUsage<Storage, CopyDst, CopySrc, Indirect>> getGaussianRenderIndirectBufferHandle() const
    {
        return gaussianRenderIndirectBuffer;
    }

    Camera getCurrentCamera() const
    {
        return camera;
    }

    uint32_t getOutputTextureWidth() const
    {
        return width;
    }

    uint32_t getOutputTextureHeight() const
    {
        return height;
    }

    void setSortEntryFrustumDilation(float value)
    {
        sortEntryFrustumDilation = std::max(value, 0.0f);
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
                ->fillBuffer(BufferRange(rasterAnalysisCounterBuffer), 0u);

            for (uint32_t passIndex = 0u; passIndex < activeSortPassCount; ++passIndex)
            {
                const GsViewer::ViewerGlobals sortPassGlobals = buildSortPassGlobals(passIndex);
                frameQueue->writeBuffer(BufferRange(sortPassGlobalsBuffers[passIndex]), &sortPassGlobals, sizeof(sortPassGlobals));
            }

            frameQueue->computePass("GsProjectGaussians", projectGaussiansPass(sceneSplatCount, 1, 1));
            frameQueue->computePass("GsBuildAmd8Dispatch", buildAmd8DispatchPass(1, 1, 1));

            std::vector<ComputePassTaskDescriptor> sortTasks;
            sortTasks.reserve(activeSortPassCount * 15u);
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
            frameQueue->computePass("GsSort", sortTasks);
            frameQueue->computePass("GsBuildRenderIndirect", buildGaussianRenderIndirectPass(1, 1, 1));

            GsViewer::GaussianRasterFrameBuffer gaussianFrameBuffer;
            gaussianFrameBuffer.color = outputTexture->createView();
            gaussianFrameBuffer.color.loadOp = LoadOp::Clear;
            gaussianFrameBuffer.color.clearValue = {backgroundR, backgroundG, backgroundB, 1.0f};
            gaussianFrameBuffer.color.storeOp = StoreOp::Store;
            frameQueue->renderPass(
                "GsGaussianBillboardPass",
                gaussianFrameBuffer,
                gaussianBillboardRenderPass->drawIndirect(gaussianRenderIndirectBuffer, 1u, 0u)
            );
            frameQueue->computePass("GsDebugOverlay", debugOverlayPass(12u, 12u, 1));
            frameQueue->computePass("GsFinalizeTelemetry", finalizeBudgetTelemetryPass(1, 1, 1));

            const uint32_t writebackSlot = uint32_t(submittedFrameCount % GsViewer::BudgetReadbackBufferCount);
            frameQueue->blitPass("GsBudgetTelemetryReadback", copyBufferToBuffer(budgetTelemetryBuffer, 0u, budgetTelemetryReadbackBuffers[writebackSlot], 0u, sizeof(GsViewer::BudgetTelemetry)));
        }

        auto nextTextureStatus = swapchain->queryNextTexture();
        swapchainStagingTexture = nextTextureStatus.texture;
        frameQueue->renderToSwapchain(nextTextureStatus, outputTexture)->submit();
        swapchain->present();
        submittedFrameCount += 1u;
    }
};

#endif
