#ifndef GVM_THREE_RENDER_SET_PHASE0_HPP
#define GVM_THREE_RENDER_SET_PHASE0_HPP

#include "UGL.h"

using namespace UGL;

static const uint RenderSetPhase0MaxTextures = 8u;

/** Stores one normalized triangle-list vertex for the Phase 0 scene RenderSet. */
struct RenderSetPhase0Vertex
{
    float4 position [[Attribute0]];
    float4 texCoord [[Attribute1]];
};

/** Stores the transform and material selection shared by every instance of one entity. */
struct RenderSetPhase0ObjectData
{
    float4 offsetAndScale;
    uint4 materialAndReserved;
};

/** Stores the transform and tint selected by a local RenderEntity instance index. */
struct RenderSetPhase0InstanceData
{
    float4 offsetAndScale;
    float4 tint;
};

/** Stores one material color selected through the entity material component. */
struct RenderSetPhase0MaterialData
{
    float4 baseColor;
};

/** Defines the only scene RenderSet used by every geometry draw in the Phase 0 fixture. */
struct RenderSetPhase0SceneSet : public IRenderSet
{
    /** Declares the consolidated geometry, object, instance, material, and texture components. */
    constructor(BufferComponent<RenderSetPhase0Vertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<RenderSetPhase0ObjectData> objects,
                BufferComponent<RenderSetPhase0InstanceData> instances,
                BufferComponent<RenderSetPhase0MaterialData> materials,
                (TextureComponent<half4, RenderSetPhase0MaxTextures> albedo))
    {
    }
};

/** Carries entity-resolved scene values from the vertex stage into the fragment stage. */
struct RenderSetPhase0VertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    float2 identityCheck [[Attribute1]];
    float boundsSafetyFailure [[Attribute2]];
};

/** Defines the deterministic RGBA8 target used for readback and presentation. */
struct RenderSetPhase0FrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws every scene entity through the RenderSet-only indexed-indirect entry point. */
class RenderSetPhase0ScenePass final : public IRenderClass
{
public:
    /** Binds the scene's unique RenderSet to the only geometry pass. */
    constructor(RenderSet<RenderSetPhase0SceneSet> sceneSet [[Slot0]])
    {
    }

private:
    /** Resolves per-entity and per-instance components using the RenderEntity builtins. */
    RenderSetPhase0VertexOutput vertex(RenderSetPhase0Vertex inputValue [[VertexInput0]],
                                       uint renderEntityID [[RenderEntityID]],
                                       uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const RenderSetPhase0ObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const RenderSetPhase0InstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const RenderSetPhase0MaterialData materialData = sceneSet->materials->get(renderEntityID, objectData.materialAndReserved.x);
        const bool invalidObjectAccepted = sceneSet->objects->checkValid(0xffffffffu);
        const RenderSetPhase0ObjectData clampedObjectData = sceneSet->objects->get(0xffffffffu, 0xffffffffu);
        auto albedoTexture = sceneSet->albedo->get(renderEntityID, 0u);
        const float4 albedoValue = float4(albedoTexture->read(uint2(0u, 0u), 0u));

        const float2 objectPosition = inputValue.position.xy * objectData.offsetAndScale.zw + objectData.offsetAndScale.xy;
        const float2 instancePosition = objectPosition * instanceData.offsetAndScale.zw + instanceData.offsetAndScale.xy;

        RenderSetPhase0VertexOutput outputValue;
        outputValue.position = float4(instancePosition, inputValue.position.z, inputValue.position.w);
        outputValue.color = materialData.baseColor * instanceData.tint * albedoValue;
        outputValue.identityCheck = float2(float(renderEntityID + 1u) / 16.0f,
                                           float(renderEntityInstanceID + 1u) / 16.0f);
        const bool clampedReadOutsideFixtureRange = clampedObjectData.offsetAndScale.z < -1.0f ||
                                                    clampedObjectData.offsetAndScale.z > 1.0f;
        outputValue.boundsSafetyFailure = (invalidObjectAccepted || clampedReadOutsideFixtureRange) ? 1.0f : 0.0f;
        return outputValue;
    }

    /** Writes the component-derived color while preserving a visible entity/instance ABI check. */
    RenderSetPhase0FrameBuffer fragment(RenderSetPhase0VertexOutput inputValue)
    {
        RenderSetPhase0FrameBuffer frameBuffer;
        if (inputValue.boundsSafetyFailure > 0.5f)
        {
            frameBuffer.color = half4(half(1.0f), half(0.0f), half(1.0f), half(1.0f));
            return frameBuffer;
        }
        frameBuffer.color = half4(
            half3(inputValue.color.xyz + float3(inputValue.identityCheck, 0.0f) * 0.01f),
            half(inputValue.color.w));
        return frameBuffer;
    }
};

/** Owns the single scene RenderSet and submits its deterministic RGBA8 frame. */
class RenderSetPhase0Renderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<RenderSetPhase0SceneSet> sceneSet;
    RenderClass<RenderSetPhase0ScenePass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        presentTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique scene RenderSet, its pass, and the deterministic readback target. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<RenderSetPhase0SceneSet>();
        scenePass = device->createRenderClass<RenderSetPhase0ScenePass>(sceneSet);

        presentTexture = device->createTexture("RenderSetPhase0RGBA8",
                                               readbackWidth,
                                               readbackHeight,
                                               1u);
    }

    /** Recreates the deterministic offscreen target at the explicit host-requested extent. */
    void configureOutput(uint width, uint height)
    {
        device->freeTexture(presentTexture);
        readbackWidth = width;
        readbackHeight = height;
        presentTexture = device->createTexture("RenderSetPhase0RGBA8",
                                               readbackWidth,
                                               readbackHeight,
                                               1u);
    }

    /** Applies pending entity commands, draws through RenderSet indirect metadata, and presents. */
    void render() override
    {
        sceneSet->update();

        auto nextTexture = swapchain->queryNextTexture();
        RenderSetPhase0FrameBuffer frameBuffer;
        frameBuffer.color = presentTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.015, 0.02, 0.03, 1.0};

        graphicsQueue->renderPass("RenderSetPhase0Scene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, presentTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final RGBA8 target for test-only GPU readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return presentTexture;
    }

    /** Returns the current RGBA8 readback width selected from the swapchain. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the current RGBA8 readback height selected from the swapchain. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the generated RenderSet and target resources after the host stops rendering. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(presentTexture);
    }
};

#endif
