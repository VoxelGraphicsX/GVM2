#ifndef REPLAY_ATTACHMENT_HAZARD_HPP
#define REPLAY_ATTACHMENT_HAZARD_HPP

#include "UGL.h"

#include <EASTL/vector.h>
#include <cstdio>

using namespace UGL;

namespace ReplayAttachmentHazard
{
    static const uint ConsumerModeReadOnly = 0u;
    static const uint ConsumerModeTerrainOnly = 1u;
    static const uint ConsumerModeTerrainGrass = 2u;

    /// Carries per-frame sample controls used by the replay attachment hazard shaders.
    struct ReplayAttachmentHazardParams
    {
        uint2 resolution;
        uint consumerMode;
        uint frameIndex;
    };

    /// Matches GVM's non-indexed indirect draw command layout for grass-like visibility stress draws.
    struct HazardIndirectRenderCommand
    {
        uint vertexCount;
        uint instanceCount;
        uint firstVertex;
        uint firstInstance;
    };

    /// Stores the visibility payload written by the base and grass-like visibility passes.
    struct VisibilityFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA32Uint> visibility;
        DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
    };

    /// Stores visibility plus replay metadata when the pass participates in replay payload writes.
    struct ReplayVisibilityFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA32Uint> visibility;
        ColorAttachment<TextureFormat::RGBA32Uint> replayPayload;
        DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
    };

    /// Stores the pixel-local consumer scratch value and the final sample output.
    struct PixelLocalConsumerFrame final : public IFrameBuffer
    {
        PixelLocalColorAttachment<TextureFormat::RGBA8Unorm, PixelLocalAccess::ReadWrite, PixelLocalStorage::Transient,
                                  PixelLocalLoad::DontCare, PixelLocalStore::Discard>
            scratch;
        ColorAttachment<TextureFormat::RGBA8Unorm> lightingResult;
    };

    /// Stores the non-pixel-local consumer output.
    struct PresentFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    /// Binds the visibility, depth, replay, and uniform resources consumed by the hazard readback passes.
    struct ReplayAttachmentHazardConsumerBindGroup final : public IBindGroup
    {
        constructor(Texture2D<TextureFormat::RGBA32Uint> visibilityBuffer [[Binding0]],
                    Texture2D<TextureFormat::Depth32Float> depthTexture [[Binding1]],
                    Texture2D<TextureFormat::RGBA32Uint> replayPayloadBuffer [[Binding2]],
                    UniformBuffer<ReplayAttachmentHazardParams> params [[Binding3]])
        {
        }
    };

    /// Shared vertex output for procedural full-screen and grass-like draws.
    struct HazardVertexOutput
    {
        float4 position [[Position]];
        float2 uv [[Attribute0]];
        uint instanceID [[Attribute1]];
    };

    inline float2 hazardFullScreenUv(uint vertexID)
    {
        return float2((vertexID << 1u) & 2u, vertexID & 2u);
    }

    inline uint4 makeVisibilityValue(uint surfaceKind, uint primitiveID, uint payload0, uint payload1)
    {
        return uint4(surfaceKind, primitiveID, payload0, payload1);
    }

    inline uint4 sampleReplayPayload(
        IN BindGroup<ReplayAttachmentHazardConsumerBindGroup> consumerBindGroup,
        uint2 pixelCoord)
    {
        return uint4(consumerBindGroup->replayPayloadBuffer->read(pixelCoord).xyzw);
    }

    inline uint4 sampleVisibilityPayload(
        IN BindGroup<ReplayAttachmentHazardConsumerBindGroup> consumerBindGroup,
        uint2 pixelCoord)
    {
        return uint4(consumerBindGroup->visibilityBuffer->read(pixelCoord).xyzw);
    }

    inline float resolveHazardChecksum(uint4 visibilityValue, uint4 replayValue, float depthValue, uint consumerMode)
    {
        float checksum = float((visibilityValue.x ^ visibilityValue.y ^ visibilityValue.z ^ visibilityValue.w) & 255u) / 255.0f;
        checksum += float((replayValue.x + replayValue.y + replayValue.z + replayValue.w) & 255u) / 510.0f;
        checksum += saturate(depthValue) * 0.25f;

        if (consumerMode == ConsumerModeTerrainOnly)
        {
            checksum += float(((replayValue.x * 17u) ^ (replayValue.y * 13u)) & 127u) / 512.0f;
        }
        else if (consumerMode == ConsumerModeTerrainGrass)
        {
            const bool grassPixel = visibilityValue.x == 2u;
            checksum += grassPixel ? float(((visibilityValue.y * 3u) + replayValue.z) & 255u) / 384.0f
                                   : float(((replayValue.x * 19u) + replayValue.w) & 255u) / 448.0f;
        }

        return frac(checksum);
    }

    /// Writes a full-screen terrain-like visibility pass and always writes replay metadata.
    class BaseVisibilityPass final : public IRenderClass
    {
    public:
        constructor()
        {
            setDepthWriteEnabled(true);
            setDepthCompareFunction(CompareFunction::LessEqual);
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]])
        {
            const float2 uv = hazardFullScreenUv(vertexID);
            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.72f, 1.0f);
            output.uv = uv;
            output.instanceID = 0u;
            return output;
        }

        ReplayVisibilityFrameBuffer fragment(HazardVertexOutput input, uint primitiveID [[PrimitiveID]])
        {
            ReplayVisibilityFrameBuffer output;
            const uint2 encodedUv = uint2(input.uv * 65535.0f);
            output.visibility = makeVisibilityValue(1u, primitiveID, encodedUv.x, encodedUv.y);
            output.replayPayload = uint4(encodedUv.x, encodedUv.y, primitiveID, 0xBACE0001u);
            return output;
        }
    };

    /// Writes grass-like visibility without binding or touching the replay payload attachment.
    class GrassVisibilityPass final : public IRenderClass
    {
    public:
        constructor()
        {
            setDepthWriteEnabled(true);
            setDepthCompareFunction(CompareFunction::LessEqual);
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]], uint instanceID [[InstanceID]])
        {
            const uint localVertex = vertexID % 6u;
            const float2 corner = float2(
                (localVertex == 1u || localVertex == 2u || localVertex == 4u) ? 1.0f : 0.0f,
                (localVertex == 2u || localVertex == 4u || localVertex == 5u) ? 1.0f : 0.0f);

            const uint gridX = 256u;
            const uint gridY = 144u;
            const uint cellX = instanceID % gridX;
            const uint cellY = (instanceID / gridX) % gridY;
            const float2 cellUv = (float2(cellX, cellY) + corner) / float2(gridX, gridY);
            const float2 jitter = float2(float((instanceID * 17u) & 15u), float((instanceID * 31u) & 15u)) / float2(4096.0f, 4096.0f);
            const float2 uv = clamp(cellUv + jitter, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.50f, 1.0f);
            output.uv = uv;
            output.instanceID = instanceID;
            return output;
        }

        VisibilityFrameBuffer fragment(HazardVertexOutput input, uint primitiveID [[PrimitiveID]])
        {
            VisibilityFrameBuffer output;
            const uint2 encodedUv = uint2(input.uv * 65535.0f);
            output.visibility = makeVisibilityValue(2u, primitiveID, input.instanceID, encodedUv.x ^ encodedUv.y);
            return output;
        }
    };

    /// Writes grass-like visibility and explicitly writes the replay payload attachment.
    class GrassReplayVisibilityPass final : public IRenderClass
    {
    public:
        constructor()
        {
            setDepthWriteEnabled(true);
            setDepthCompareFunction(CompareFunction::LessEqual);
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]], uint instanceID [[InstanceID]])
        {
            const uint localVertex = vertexID % 6u;
            const float2 corner = float2(
                (localVertex == 1u || localVertex == 2u || localVertex == 4u) ? 1.0f : 0.0f,
                (localVertex == 2u || localVertex == 4u || localVertex == 5u) ? 1.0f : 0.0f);

            const uint gridX = 256u;
            const uint gridY = 144u;
            const uint cellX = instanceID % gridX;
            const uint cellY = (instanceID / gridX) % gridY;
            const float2 cellUv = (float2(cellX, cellY) + corner) / float2(gridX, gridY);
            const float2 jitter = float2(float((instanceID * 17u) & 15u), float((instanceID * 31u) & 15u)) / float2(4096.0f, 4096.0f);
            const float2 uv = clamp(cellUv + jitter, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.50f, 1.0f);
            output.uv = uv;
            output.instanceID = instanceID;
            return output;
        }

        ReplayVisibilityFrameBuffer fragment(HazardVertexOutput input, uint primitiveID [[PrimitiveID]])
        {
            ReplayVisibilityFrameBuffer output;
            const uint2 encodedUv = uint2(input.uv * 65535.0f);
            output.visibility = makeVisibilityValue(2u, primitiveID, input.instanceID, encodedUv.x ^ encodedUv.y);
            output.replayPayload = uint4(encodedUv.x, encodedUv.y, input.instanceID, 0x6A551001u);
            return output;
        }
    };

    /// Draws the grass-like overlay while declaring fragment barycentrics and omitting the replay attachment.
    class GrassVisibilityBaryPass final : public IRenderClass
    {
    public:
        constructor()
        {
            setDepthWriteEnabled(true);
            setDepthCompareFunction(CompareFunction::LessEqual);
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]], uint instanceID [[InstanceID]])
        {
            const uint localVertex = vertexID % 6u;
            const float2 corner = float2(
                (localVertex == 1u || localVertex == 2u || localVertex == 4u) ? 1.0f : 0.0f,
                (localVertex == 2u || localVertex == 4u || localVertex == 5u) ? 1.0f : 0.0f);

            const uint gridX = 256u;
            const uint gridY = 144u;
            const uint cellX = instanceID % gridX;
            const uint cellY = (instanceID / gridX) % gridY;
            const float2 cellUv = (float2(cellX, cellY) + corner) / float2(gridX, gridY);
            const float2 jitter = float2(float((instanceID * 17u) & 15u), float((instanceID * 31u) & 15u)) / float2(4096.0f, 4096.0f);
            const float2 uv = clamp(cellUv + jitter, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.50f, 1.0f);
            output.uv = uv;
            output.instanceID = instanceID;
            return output;
        }

        VisibilityFrameBuffer fragment(
            HazardVertexOutput input,
            uint primitiveID [[PrimitiveID]],
            float3 bary [[Barycentrics]])
        {
            VisibilityFrameBuffer output;
            const uint baryMix =
                (uint(saturate(bary.x) * 255.0f) << 16u) |
                (uint(saturate(bary.y) * 255.0f) << 8u) |
                uint(saturate(bary.z) * 255.0f);
            output.visibility = makeVisibilityValue(2u, primitiveID ^ baryMix, input.instanceID, baryMix);
            return output;
        }
    };

    /// Draws the grass-like overlay while declaring fragment barycentrics and updating replay payload.
    class GrassReplayVisibilityBaryPass final : public IRenderClass
    {
    public:
        constructor()
        {
            setDepthWriteEnabled(true);
            setDepthCompareFunction(CompareFunction::LessEqual);
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]], uint instanceID [[InstanceID]])
        {
            const uint localVertex = vertexID % 6u;
            const float2 corner = float2(
                (localVertex == 1u || localVertex == 2u || localVertex == 4u) ? 1.0f : 0.0f,
                (localVertex == 2u || localVertex == 4u || localVertex == 5u) ? 1.0f : 0.0f);

            const uint gridX = 256u;
            const uint gridY = 144u;
            const uint cellX = instanceID % gridX;
            const uint cellY = (instanceID / gridX) % gridY;
            const float2 cellUv = (float2(cellX, cellY) + corner) / float2(gridX, gridY);
            const float2 jitter = float2(float((instanceID * 17u) & 15u), float((instanceID * 31u) & 15u)) / float2(4096.0f, 4096.0f);
            const float2 uv = clamp(cellUv + jitter, float2(0.0f, 0.0f), float2(1.0f, 1.0f));

            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.50f, 1.0f);
            output.uv = uv;
            output.instanceID = instanceID;
            return output;
        }

        ReplayVisibilityFrameBuffer fragment(
            HazardVertexOutput input,
            uint primitiveID [[PrimitiveID]],
            float3 bary [[Barycentrics]])
        {
            ReplayVisibilityFrameBuffer output;
            const uint baryMix =
                (uint(saturate(bary.x) * 255.0f) << 16u) |
                (uint(saturate(bary.y) * 255.0f) << 8u) |
                uint(saturate(bary.z) * 255.0f);
            output.visibility = makeVisibilityValue(2u, primitiveID ^ baryMix, input.instanceID, baryMix);
            output.replayPayload = uint4(
                uint(saturate(input.uv.x) * 65535.0f),
                uint(saturate(input.uv.y) * 65535.0f),
                baryMix,
                input.instanceID);
            return output;
        }
    };

    /// Reads visibility, depth, and replay payload as a pixel-local resolve subpass.
    class PixelLocalConsumerResolvePass final : public IPixelLocalRenderClass
    {
    public:
        constructor(BindGroup<ReplayAttachmentHazardConsumerBindGroup> consumerBindGroup [[Slot0]])
        {
        }

    private:
        PixelLocalConsumerFrame pixel(float4 pixelPosition [[PixelCoord]])
        {
            PixelLocalConsumerFrame output;
            const uint2 pixelCoord = uint2(pixelPosition.xy);
            const ReplayAttachmentHazardParams params = consumerBindGroup->params->read();
            if (pixelCoord.x >= params.resolution.x || pixelCoord.y >= params.resolution.y)
            {
                output.scratch = half4(0.0f, 0.0f, 0.0f, 1.0f);
                return output;
            }

            const uint4 visibilityValue = sampleVisibilityPayload(consumerBindGroup, pixelCoord);
            const uint4 replayValue = sampleReplayPayload(consumerBindGroup, pixelCoord);
            const float depthValue = consumerBindGroup->depthTexture->read(pixelCoord).x;
            const float checksum = resolveHazardChecksum(visibilityValue, replayValue, depthValue, params.consumerMode);
            output.scratch = half4(checksum, float(visibilityValue.x) / 4.0f, float(replayValue.w & 255u) / 255.0f, 1.0f);
            return output;
        }
    };

    /// Reads the pixel-local scratch value and writes the final diagnostic color.
    class PixelLocalConsumerLightingPass final : public IPixelLocalRenderClass
    {
    public:
        constructor()
        {
        }

    private:
        PixelLocalConsumerFrame pixel(PixelLocalConsumerFrame input [[PixelLocalInput]], float4 pixelPosition [[PixelCoord]])
        {
            PixelLocalConsumerFrame output;
            const half4 scratchValue = input.scratch.read();
            output.lightingResult = scratchValue;
            return output;
        }
    };

    /// Reads visibility, depth, and replay payload in an ordinary render pass for non-pixel-local comparison.
    class TextureConsumerPass final : public IRenderClass
    {
    public:
        constructor(BindGroup<ReplayAttachmentHazardConsumerBindGroup> consumerBindGroup [[Slot0]])
        {
        }

    private:
        HazardVertexOutput vertex(uint vertexID [[VertexID]])
        {
            const float2 uv = hazardFullScreenUv(vertexID);
            HazardVertexOutput output = {};
            output.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
            output.uv = uv;
            output.instanceID = 0u;
            return output;
        }

        PresentFrameBuffer fragment(HazardVertexOutput input)
        {
            PresentFrameBuffer output;
            const ReplayAttachmentHazardParams params = consumerBindGroup->params->read();
            const uint2 pixelCoord = min(uint2(input.uv * float2(params.resolution)), params.resolution - uint2(1u, 1u));
            if (pixelCoord.x >= params.resolution.x || pixelCoord.y >= params.resolution.y)
            {
                output.color = half4(0.0f, 0.0f, 0.0f, 1.0f);
                return output;
            }

            const uint4 visibilityValue = sampleVisibilityPayload(consumerBindGroup, pixelCoord);
            const uint4 replayValue = sampleReplayPayload(consumerBindGroup, pixelCoord);
            const float depthValue = consumerBindGroup->depthTexture->read(pixelCoord).x;
            const float checksum = resolveHazardChecksum(visibilityValue, replayValue, depthValue, params.consumerMode);
            output.color = half4(checksum, float(visibilityValue.x) / 4.0f, float(replayValue.w & 255u) / 255.0f, 1.0f);
            return output;
        }
    };

    /// Owns the minimal GVMRuntime replay attachment hazard renderer used by the sample executable.
    class HazardRenderer : public UGL::AbstractRenderer
    {
        Device device;
        Swapchain swapchain;

        RenderClass<BaseVisibilityPass> baseVisibilityPass;
        RenderClass<GrassVisibilityPass> grassVisibilityPass;
        RenderClass<GrassReplayVisibilityPass> grassReplayVisibilityPass;
        RenderClass<GrassVisibilityBaryPass> grassVisibilityBaryPass;
        RenderClass<GrassReplayVisibilityBaryPass> grassReplayVisibilityBaryPass;
        RenderClass<TextureConsumerPass> textureConsumerPass;
        RenderClass<PixelLocalConsumerResolvePass> pixelLocalResolvePass;
        RenderClass<PixelLocalConsumerLightingPass> pixelLocalLightingPass;

        BindGroup<ReplayAttachmentHazardConsumerBindGroup> consumerBindGroup;

        Buffer<ReplayAttachmentHazardParams, BufferUsage<Uniform, CopyDst>> paramsBuffer;
        Buffer<HazardIndirectRenderCommand, BufferUsage<CopyDst, Indirect>> grassIndirectBuffer;
        Texture<TextureFormat::RGBA32Uint, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> visibilityBuffer;
        Texture<TextureFormat::RGBA32Uint, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> replayPayloadBuffer;
        Texture<TextureFormat::Depth32Float, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> depthBuffer;
        Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, PixelLocalAttachment>, TextureDimension::e2D> pixelLocalScratchTexture;
        Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, RenderAttachment>, TextureDimension::e2D> outputTexture;
        GpuTimestampFrameProfiler timestampProfiler;
        eastl::vector<uint64_t> timestampRawResults;

        uint width = 2560u;
        uint height = 1440u;
        uint frameIndex = 0u;
        uint timestampPrintInterval = 60u;

        [[Export]] int consumerMode = int(ConsumerModeTerrainGrass);
        [[Export]] int grassEnabled = 1;
        [[Export]] int grassWritesReplay = 0;
        [[Export]] int pixelLocalEnabled = 1;
        [[Export]] int grassInstanceCount = 178791;
        [[Export]] int grassVertexCount = 39;
        [[Export]] int grassDrawIndirect = 1;
        [[Export]] int grassBaryEnabled = 1;

    public:
        /// Initializes all textures, bind groups, and shader classes for one hazard sample run.
        void init(Device inputDevice, Swapchain inputSwapchain, uint inputWidth, uint inputHeight)
        {
            device = inputDevice;
            swapchain = inputSwapchain;
            width = max(inputWidth, 1u);
            height = max(inputHeight, 1u);

            visibilityBuffer = device->createTexture("ReplayHazard.Visibility", width, height, 1);
            replayPayloadBuffer = device->createTexture("ReplayHazard.ReplayPayload", width, height, 1);
            depthBuffer = device->createTexture("ReplayHazard.Depth", width, height, 1);
            pixelLocalScratchTexture = device->createTexture("ReplayHazard.PixelLocalScratch", width, height, 1);
            outputTexture = device->createTexture("ReplayHazard.Output", width, height, 1);
            paramsBuffer = device->createBuffer("ReplayHazard.Params", 1);
            grassIndirectBuffer = device->createBuffer("ReplayHazard.GrassIndirect", 1);

            consumerBindGroup = device->createBindGroup<ReplayAttachmentHazardConsumerBindGroup>(
                visibilityBuffer->createView(),
                depthBuffer->createView(),
                replayPayloadBuffer->createView(),
                paramsBuffer);

            baseVisibilityPass = device->createRenderClass<BaseVisibilityPass>();
            grassVisibilityPass = device->createRenderClass<GrassVisibilityPass>();
            grassReplayVisibilityPass = device->createRenderClass<GrassReplayVisibilityPass>();
            grassVisibilityBaryPass = device->createRenderClass<GrassVisibilityBaryPass>();
            grassReplayVisibilityBaryPass = device->createRenderClass<GrassReplayVisibilityBaryPass>();
            textureConsumerPass = device->createRenderClass<TextureConsumerPass>(consumerBindGroup);
            pixelLocalResolvePass = device->createRenderClass<PixelLocalConsumerResolvePass>(consumerBindGroup);
            pixelLocalLightingPass = device->createRenderClass<PixelLocalConsumerLightingPass>();
            timestampProfiler = device->createTimestampFrameProfiler(8u, "ReplayHazard.TimestampProfiler");
        }

        /// Renders one hazard sample frame and presents the output texture.
        void render()
        {
            timestampProfiler.reset();
            ReplayAttachmentHazardParams params;
            params.resolution = uint2(width, height);
            params.consumerMode = uint(max(consumerMode, 0));
            params.frameIndex = frameIndex++;

            eastl::vector<ReplayAttachmentHazardParams> paramsData = {params};
            device->graphicsQueue(0)->writeBuffer(BufferRange(paramsBuffer), paramsData.data(), sizeof(ReplayAttachmentHazardParams));

            const uint grassVertexCountU = uint(max(grassVertexCount, 0));
            const uint grassInstanceCountU = uint(max(grassInstanceCount, 0));
            if (grassEnabled != 0 && grassDrawIndirect != 0)
            {
                HazardIndirectRenderCommand command;
                command.vertexCount = grassVertexCountU;
                command.instanceCount = grassInstanceCountU;
                command.firstVertex = 0u;
                command.firstInstance = 0u;
                eastl::vector<HazardIndirectRenderCommand> commandData = {command};
                device->graphicsQueue(0)->writeBuffer(
                    BufferRange(grassIndirectBuffer),
                    commandData.data(),
                    sizeof(HazardIndirectRenderCommand));
            }

            ReplayVisibilityFrameBuffer baseFrameBuffer;
            baseFrameBuffer.visibility = visibilityBuffer->createView();
            baseFrameBuffer.visibility.loadOp = LoadOp::Clear;
            baseFrameBuffer.visibility.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
            baseFrameBuffer.visibility.storeOp = StoreOp::Store;
            baseFrameBuffer.replayPayload = replayPayloadBuffer->createView();
            baseFrameBuffer.replayPayload.loadOp = LoadOp::Clear;
            baseFrameBuffer.replayPayload.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
            baseFrameBuffer.replayPayload.storeOp = StoreOp::Store;
            baseFrameBuffer.depthStencil = depthBuffer->createView();
            baseFrameBuffer.depthStencil.depthLoadOp = LoadOp::Clear;
            baseFrameBuffer.depthStencil.depthStoreOp = StoreOp::Store;
            baseFrameBuffer.depthStencil.depthClearValue = 1.0f;

            const GpuTimestampFrameProfiler::Scope baseScope = timestampProfiler.writePass("BaseVisibilityPass");
            device->graphicsQueue(0)->renderPass("BaseVisibilityPass", baseFrameBuffer, baseScope, baseVisibilityPass(3u, 1u, 0u, 0u));

            if (grassEnabled != 0)
            {
                if (grassWritesReplay != 0)
                {
                    ReplayVisibilityFrameBuffer grassFrameBuffer;
                    grassFrameBuffer.visibility = visibilityBuffer->createView();
                    grassFrameBuffer.visibility.loadOp = LoadOp::Load;
                    grassFrameBuffer.visibility.storeOp = StoreOp::Store;
                    grassFrameBuffer.replayPayload = replayPayloadBuffer->createView();
                    grassFrameBuffer.replayPayload.loadOp = LoadOp::Load;
                    grassFrameBuffer.replayPayload.storeOp = StoreOp::Store;
                    grassFrameBuffer.depthStencil = depthBuffer->createView();
                    grassFrameBuffer.depthStencil.depthLoadOp = LoadOp::Load;
                    grassFrameBuffer.depthStencil.depthStoreOp = StoreOp::Store;
                    const GpuTimestampFrameProfiler::Scope grassScope = timestampProfiler.writePass("GrassVisibilityPass.WriteReplay");
                    if (grassDrawIndirect != 0)
                    {
                        if (grassBaryEnabled != 0)
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.WriteReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassReplayVisibilityBaryPass->drawIndirect(grassIndirectBuffer, 1u, 0u));
                        }
                        else
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.WriteReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassReplayVisibilityPass->drawIndirect(grassIndirectBuffer, 1u, 0u));
                        }
                    }
                    else
                    {
                        if (grassBaryEnabled != 0)
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.WriteReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassReplayVisibilityBaryPass(grassVertexCountU, grassInstanceCountU, 0u, 0u));
                        }
                        else
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.WriteReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassReplayVisibilityPass(grassVertexCountU, grassInstanceCountU, 0u, 0u));
                        }
                    }
                }
                else
                {
                    VisibilityFrameBuffer grassFrameBuffer;
                    grassFrameBuffer.visibility = visibilityBuffer->createView();
                    grassFrameBuffer.visibility.loadOp = LoadOp::Load;
                    grassFrameBuffer.visibility.storeOp = StoreOp::Store;
                    grassFrameBuffer.depthStencil = depthBuffer->createView();
                    grassFrameBuffer.depthStencil.depthLoadOp = LoadOp::Load;
                    grassFrameBuffer.depthStencil.depthStoreOp = StoreOp::Store;
                    const GpuTimestampFrameProfiler::Scope grassScope = timestampProfiler.writePass("GrassVisibilityPass.OmitReplay");
                    if (grassDrawIndirect != 0)
                    {
                        if (grassBaryEnabled != 0)
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.OmitReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassVisibilityBaryPass->drawIndirect(grassIndirectBuffer, 1u, 0u));
                        }
                        else
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.OmitReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassVisibilityPass->drawIndirect(grassIndirectBuffer, 1u, 0u));
                        }
                    }
                    else
                    {
                        if (grassBaryEnabled != 0)
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.OmitReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassVisibilityBaryPass(grassVertexCountU, grassInstanceCountU, 0u, 0u));
                        }
                        else
                        {
                            device->graphicsQueue(0)->renderPass(
                                "GrassVisibilityPass.OmitReplay",
                                grassFrameBuffer,
                                grassScope,
                                grassVisibilityPass(grassVertexCountU, grassInstanceCountU, 0u, 0u));
                        }
                    }
                }
            }

            if (pixelLocalEnabled != 0)
            {
                PixelLocalConsumerFrame consumerFrame;
                consumerFrame.scratch = pixelLocalScratchTexture->createView();
                consumerFrame.lightingResult = outputTexture->createView();
                consumerFrame.lightingResult.loadOp = LoadOp::Clear;
                consumerFrame.lightingResult.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
                consumerFrame.lightingResult.storeOp = StoreOp::Store;
                const GpuTimestampFrameProfiler::Scope consumerScope = timestampProfiler.writePass("PixelLocalConsumerPass");
                device->graphicsQueue(0)->renderPass(
                    "PixelLocalConsumerPass",
                    consumerFrame,
                    consumerScope,
                    pixelLocalPass(pixelLocalResolvePass(), nextPixelLocalPass(), pixelLocalLightingPass()));
            }
            else
            {
                PresentFrameBuffer consumerFrame;
                consumerFrame.color = outputTexture->createView();
                consumerFrame.color.loadOp = LoadOp::Clear;
                consumerFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
                consumerFrame.color.storeOp = StoreOp::Store;
                const GpuTimestampFrameProfiler::Scope consumerScope = timestampProfiler.writePass("TextureConsumerPass");
                device->graphicsQueue(0)->renderPass("TextureConsumerPass", consumerFrame, consumerScope, textureConsumerPass(3u, 1u, 0u, 0u));
            }

            auto nextTextureStatus = swapchain->queryNextTexture();
            device->graphicsQueue(0)->renderToSwapchain(nextTextureStatus, outputTexture)->resolveTimestampProfiler(timestampProfiler)->submit();
            printTimestampResultsIfNeeded();
            swapchain->present();
        }

        /// Prints GPU timestamp scope durations at a fixed interval without writing any external files.
        void printTimestampResultsIfNeeded()
        {
            if (timestampPrintInterval == 0u || (frameIndex % timestampPrintInterval) != 0u)
            {
                return;
            }

            const uint usedQueryCount = timestampProfiler.getUsedQueryCount();
            if (usedQueryCount == 0u)
            {
                return;
            }

            timestampRawResults.resize(usedQueryCount);
            device->graphicsQueue(0)->readBuffer(
                BufferRange(timestampProfiler.getResolveBuffer(), 0u, uint64_t(usedQueryCount) * sizeof(uint64_t)),
                timestampRawResults.data(),
                uint64_t(usedQueryCount) * sizeof(uint64_t))->submit();

            const eastl::vector<GpuTimestampFrameProfiler::Scope> scopes = timestampProfiler.getScopes();
            printf("[replay-hazard-gpu]");
            for (uint i = 0u; i < uint(scopes.size()); ++i)
            {
                const GpuTimestampFrameProfiler::Scope scope = scopes[i];
                const uint beginIndex = scope.timestampWrites.beginningOfPassWriteIndex;
                const uint endIndex = scope.timestampWrites.endOfPassWriteIndex;
                if (beginIndex >= usedQueryCount || endIndex >= usedQueryCount)
                {
                    continue;
                }
                const uint64_t beginValue = timestampRawResults[beginIndex];
                const uint64_t endValue = timestampRawResults[endIndex];
                const uint64_t delta = endValue >= beginValue ? (endValue - beginValue) : 0u;
                const double durationMs = double(delta) * timestampProfiler.getSupport().tickPeriodNs * 0.000001;
                printf(" scope%u=%.3fms", i, durationMs);
            }
            printf("\n");
        }
    };
} // namespace ReplayAttachmentHazard

#endif // REPLAY_ATTACHMENT_HAZARD_HPP
