#ifndef GVM_THREE_WEBGPU_INSTANCE_MESH_HPP
#define GVM_THREE_WEBGPU_INSTANCE_MESH_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one Suzanne vertex after CPU normal generation. */
struct WebgpuInstanceMeshVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the animated mesh transform and camera projection. */
struct WebgpuInstanceMeshObjectData
{
    float4x4 viewProjection;
    float4x4 model;
    float4 timeAndCount;
};

/** Stores one cubic-grid transform and stable instance ordinal. */
struct WebgpuInstanceMeshInstanceData
{
    float4 transformColumn0;
    float4 transformColumn1;
    float4 transformColumn2;
    float4 transformColumn3;
    float4 ordinal;
};

/** Stores the private MeshBasicNodeMaterial controls. */
struct WebgpuInstanceMeshMaterialData
{
    float4 colorAndOpacity;
};

/** Defines the sole Scene RenderSet for the animated Suzanne grid. */
struct WebgpuInstanceMeshSceneRenderSet : public IRenderSet
{
    /** Declares Suzanne geometry plus object, instance, and material components. */
    constructor(
        BufferComponent<WebgpuInstanceMeshVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuInstanceMeshObjectData> objects,
        BufferComponent<WebgpuInstanceMeshInstanceData> instances,
        BufferComponent<WebgpuInstanceMeshMaterialData> materials)
    {
    }
};

/** Carries world normals, stable instance identity, and entity identity. */
struct WebgpuInstanceMeshVertexOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    float3 rangeColor [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the final RGBA8 color and depth attachments. */
struct WebgpuInstanceMeshFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only attachment used by the screen Inspector pass. */
struct WebgpuInstanceMeshColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen coordinates into the screen Inspector pass. */
struct WebgpuInstanceMeshScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear working-space channel to the r185 output transfer. */
float webgpuInstanceMeshLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the Suzanne instance grid through RenderSet indexed-indirect metadata. */
class WebgpuInstanceMeshMainPass final : public IRenderClass
{
public:
    /** Configures the opaque double-sided MeshBasicNodeMaterial state. */
    constructor(RenderSet<WebgpuInstanceMeshSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the animated entity and per-instance transforms. */
    WebgpuInstanceMeshVertexOutput vertex(
        WebgpuInstanceMeshVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuInstanceMeshObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuInstanceMeshInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 instancePosition =
            instanceData.transformColumn0 * inputValue.position.x +
            instanceData.transformColumn1 * inputValue.position.y +
            instanceData.transformColumn2 * inputValue.position.z +
            instanceData.transformColumn3 * inputValue.position.w;
        const float4 worldPosition =
            mul(objectData.model, instancePosition);
        WebgpuInstanceMeshVertexOutput outputValue;
        outputValue.position =
            mul(objectData.viewProjection, worldPosition);
        const float4 instanceNormal =
            instanceData.transformColumn0 * inputValue.normal.x +
            instanceData.transformColumn1 * inputValue.normal.y +
            instanceData.transformColumn2 * inputValue.normal.z;
        const float4 transformedNormal =
            mul(objectData.model, instanceNormal);
        outputValue.worldNormal = normalize(
            float3(
                transformedNormal.x,
                transformedNormal.y,
                transformedNormal.z));
        outputValue.rangeColor = instanceData.ordinal.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Reproduces the animated normal/range-color TSL mix in linear space. */
    WebgpuInstanceMeshFrameBuffer fragment(
        WebgpuInstanceMeshVertexOutput inputValue)
    {
        const WebgpuInstanceMeshObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuInstanceMeshMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float blendWeight =
            sin(
                (objectData.timeAndCount.x * 0.1f + 0.75f) *
                6.28318530717958647692f) *
                0.5f +
            0.5f;
        const float3 linearColor = lerp(
            normalize(inputValue.worldNormal),
            inputValue.rangeColor,
            blendWeight) *
            materialData.colorAndOpacity.xyz;
        WebgpuInstanceMeshFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuInstanceMeshLinearToSrgb(linearColor.x),
                webgpuInstanceMeshLinearToSrgb(linearColor.y),
                webgpuInstanceMeshLinearToSrgb(linearColor.z)),
            half(materialData.colorAndOpacity.w));
        return frameBuffer;
    }
};

/** Composites the deterministic WebGPU Inspector bar without Scene geometry. */
class WebgpuInstanceMeshInspectorPass final : public IRenderClass
{
public:
    /** Configures source-alpha blending for the screen-only overlay. */
    constructor()
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without a Scene draw. */
    WebgpuInstanceMeshScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstanceMeshScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked rounded Inspector bar and drop shadow. */
    WebgpuInstanceMeshColorFrameBuffer fragment(
        WebgpuInstanceMeshScreenOutput inputValue)
    {
        const float2 pixel =
            float2(inputValue.uv.x * 800.0f, inputValue.uv.y * 500.0f);
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float cornerRadius =
            pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta =
            abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float roundedDistance =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) -
            cornerRadius;
        if (roundedDistance > 0.5f)
        {
            const float2 shadowCenter = float2(699.5f, 37.5f);
            const float2 shadowDelta =
                abs(pixel - shadowCenter) -
                (halfExtent - float2(cornerRadius));
            const float shadowDistance =
                length(max(shadowDelta, float2(0.0f))) +
                min(max(shadowDelta.x, shadowDelta.y), 0.0f) -
                cornerRadius;
            const float shadowAlpha =
                0.13f *
                exp(
                    -max(shadowDistance, 0.0f) *
                    max(shadowDistance, 0.0f) /
                    72.0f);
            if (shadowAlpha < 0.004f)
            {
                discard_fragment();
            }
            WebgpuInstanceMeshColorFrameBuffer shadowFrameBuffer;
            shadowFrameBuffer.color =
                half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadowFrameBuffer;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (pixel.x < 663.0f)
        {
            color =
                float3(23.1818f, 61.8182f, 85.7727f) / 255.0f;
            alpha = 0.88f;
        }
        if (roundedDistance > -1.0f)
        {
            color = float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuInstanceMeshColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated Scene RenderSet and deterministic readback attachments. */
class WebgpuInstanceMeshRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuInstanceMeshSceneRenderSet> sceneSet;
    RenderClass<WebgpuInstanceMeshMainPass> scenePass;
    RenderClass<WebgpuInstanceMeshInspectorPass> inspectorPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the one Scene RenderSet and its dedicated main pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuInstanceMeshSceneRenderSet>();
        scenePass =
            device->createRenderClass<WebgpuInstanceMeshMainPass>(sceneSet);
        inspectorPass =
            device->createRenderClass<WebgpuInstanceMeshInspectorPass>();
    }

    /** Allocates the single-sample Scene color and depth attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebgpuInstanceMeshRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebgpuInstanceMeshDepth32",
            width,
            height,
            1u);
    }

    /** Updates entity metadata and executes one RenderSet-only draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebgpuInstanceMeshFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue
            ->renderPass(
                "WebgpuInstanceMeshScene",
                frameBuffer,
                scenePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene RenderSet and readback attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
