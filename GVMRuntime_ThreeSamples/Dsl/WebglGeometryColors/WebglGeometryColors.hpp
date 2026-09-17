#pragma once

#include "UGL.h"
#include "WebglGeometryColorsData.hpp"

// Keep generated Legacy and Experimental shards synchronized with the explicit pass state below.

using namespace UGL;

/** Defines the one RenderSet that owns all nine logical renderables. */
struct WebglGeometryColorsSceneRenderSet : public IRenderSet
{
    /** Declares the unified geometry, entity data, and shadow texture pool. */
    constructor(
        BufferComponent<WebglGeometryColorsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryColorsObjectData> objects,
        BufferComponent<WebglGeometryColorsInstanceData> instances,
        BufferComponent<WebglGeometryColorsMaterialData> materials,
        (TextureComponent<half4, 4u> textures))
    {
    }
};

/** Binds the DSL-generated shadow texture and its linear sampler. */
struct WebglGeometryColorsResources final : public IBindGroup
{
    /** Declares a clamped linear sampler for the generated radial texture. */
    constructor(Texture2D<float4> shadowTexture [[Binding0]],
                Sampler shadowSampler [[Binding1]])
    {
    }
};

/** Binds the writable radial-gradient texture used by the screen compute pass. */
struct WebglGeometryColorsShadowComputeResources final : public IBindGroup
{
    /** Declares the one ordinary single-sample storage texture. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> shadowTexture [[Binding0]])
    {
    }
};

/** Generates the r185 CanvasTexture radial gradient entirely in DSL Compute. */
class [[LocalWorkGroupSize(8, 8, 1)]]
WebglGeometryColorsShadowTextureComputePass final : public IComputeClass
{
public:
    /** Binds the writable 128-by-128 shadow target. */
    constructor(BindGroup<WebglGeometryColorsShadowComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Writes the black-alpha radial gradient used by the CanvasTexture shadow. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        if (dispatchThreadID.x >= 128u || dispatchThreadID.y >= 128u)
            return;
        const float2 delta = (float2(dispatchThreadID.xy) + 0.5f - float2(64.0f)) / 64.0f;
        const float distance = min(length(delta), 1.0f);
        // The upstream CanvasTexture is an opaque grayscale radial image:
        // the center is RGB(210) and the edge reaches RGB(255).  It is not
        // an alpha mask; MeshBasicMaterial samples the color directly.
        const float shade = (210.0f + 45.0f * distance) / 255.0f;
        resources->shadowTexture->write(
            dispatchThreadID.xy,
            half4(half3(shade), half(1.0f)));
    }
};

/** Carries the transformed union attributes and RenderSet identity. */
struct WebglGeometryColorsVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float3 color [[Attribute2]];
    float2 uv [[Attribute3]];
    float4 linePixels [[Attribute4]];
    uint entityID [[Attribute5]];
};

/** Defines the ordinary single-sample color and depth attachments. */
struct WebglGeometryColorsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear channel to the browser's sRGB canvas encoding. */
float webglGeometryColorsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Projects the packed solid, shadow, or wireframe vertex. */
WebglGeometryColorsVertexOutput webglGeometryColorsTransformVertex(
    IN RenderSet<WebglGeometryColorsSceneRenderSet> sceneSet,
    WebglGeometryColorsVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglGeometryColorsObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGeometryColorsMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const WebglGeometryColorsInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    WebglGeometryColorsVertexOutput outputValue;
    float4 clipPosition;
    float3 viewPosition;
    float3 viewNormal;
    float2 uv = inputValue.uvOrCorner.xy;
    float4 linePixels = float4(0.0f);
    if (materialData.baseColorAndPhase.w > 1.5f)
    {
        const float4 startView = mul(objectData.modelView,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 endView = mul(objectData.modelView,
            inputValue.normalOrEnd + float4(instanceData.reserved.xyz, 0.0f));
        const float4 startClip = mul(objectData.modelViewProjection,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 endClip = mul(objectData.modelViewProjection,
            inputValue.normalOrEnd + float4(instanceData.reserved.xyz, 0.0f));
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const bool useEnd = inputValue.uvOrCorner.x > 0.5f;
        clipPosition = useEnd ? endClip : startClip;
        const float2 lineDirectionPixels =
            (endNdc - startNdc) * objectData.viewport.xy;
        const float2 lineNormal = normalize(float2(
            -lineDirectionPixels.y, lineDirectionPixels.x));
        // uvOrCorner.y is -1/+1 on the two strip sides.  A half-pixel
        // displacement on each side reproduces WebGL's one-pixel line.
        clipPosition.xy += lineNormal * inputValue.uvOrCorner.y /
            objectData.viewport.xy * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        viewPosition = useEnd ? endView.xyz : startView.xyz;
        viewNormal = float3(0.0f);
        linePixels = float4(
            (startNdc.x + 1.0f) * objectData.viewport.x * 0.5f,
            (1.0f - startNdc.y) * objectData.viewport.y * 0.5f,
            (endNdc.x + 1.0f) * objectData.viewport.x * 0.5f,
            (1.0f - endNdc.y) * objectData.viewport.y * 0.5f);
    }
    else
    {
        const float4 localPosition = inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        const float4 view = mul(objectData.modelView, localPosition);
        clipPosition = mul(objectData.modelViewProjection, localPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        viewPosition = view.xyz;
        const float3 transformedNormal = mul(objectData.modelView,
            float4(inputValue.normalOrEnd.xyz, 0.0f)).xyz;
        viewNormal = normalize(transformedNormal);
    }
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewPosition;
    outputValue.viewNormal = viewNormal;
    outputValue.color = inputValue.color.xyz;
    outputValue.uv = uv;
    outputValue.linePixels = linePixels;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws the nine objects through the Scene's one indexed-indirect RenderSet. */
class WebglGeometryColorsOpaquePass final : public IRenderClass
{
public:
    /** Enables opaque depth semantics while preserving transparent shadow/wire phases. */
    constructor(
        RenderSet<WebglGeometryColorsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryColorsResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Resolves RenderSet entities and forwards all interpolated attributes. */
    WebglGeometryColorsVertexOutput vertex(
        WebglGeometryColorsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryColorsTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Applies MeshPhong vertex colors and rejects the separately drawn shadow phase. */
    WebglGeometryColorsFrameBuffer fragment(
        WebglGeometryColorsVertexOutput inputValue)
    {
        const WebglGeometryColorsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float phase = materialData.baseColorAndPhase.w;
        if (phase > 0.5f)
        {
            discard_fragment();
        }
        const WebglGeometryColorsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        // MeshPhongMaterial with flatShading computes the face normal from
        // screen-space derivatives of the view-space position.  Use the same
        // derivative path instead of interpolating a CPU normal so the
        // perspective projection and raster winding match Three.js exactly.
        const float3 normal = normalize(cross(
            ddx(inputValue.viewPosition), ddy(inputValue.viewPosition)));
        const float3 lightVector = objectData.lightViewAndIntensity.xyz;
        const float3 lightDirection = normalize(lightVector);
        const float dotNL = max(dot(normal, lightDirection), 0.0f);
        const float irradiance = dotNL * 3.0f;
        const float diffuse = irradiance * 0.3183098861837907f;
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float dotNH = saturate(dot(normal, halfDirection));
        const float dotVH = saturate(dot(viewDirection, halfDirection));
        const float fresnel = exp2((-5.55473f * dotVH - 6.98316f) * dotVH);
        // MeshPhongMaterial defaults to sRGB #111111, shininess=30; this
        // example overrides shininess to zero, which makes the Blinn-Phong
        // lobe a broad 1/pi term while retaining the default specular color.
        const float3 specularColor = float3(0.005605391f);
        const float3 fresnelColor = specularColor * (1.0f - fresnel) + float3(fresnel);
        // Three.js clamps MeshPhongMaterial.shininess to 1e-4 before
        // uploading the uniform, even when the example requests zero.
        const float specularDistribution = 0.3183098861837907f *
            (0.0001f * 0.5f + 1.0f) * pow(dotNH, 0.0001f);
        const float3 specular = irradiance * fresnelColor *
            (0.25f * specularDistribution);
        const float3 linearColor = inputValue.color * diffuse + specular;
        WebglGeometryColorsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglGeometryColorsLinearToSrgb(linearColor.x)),
            half(webglGeometryColorsLinearToSrgb(linearColor.y)),
            half(webglGeometryColorsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the three CanvasTexture shadow planes through the same Scene Set. */
class WebglGeometryColorsShadowPass final : public IRenderClass
{
public:
    /** Keeps the shadow plane visible regardless of its winding direction. */
    constructor(
        RenderSet<WebglGeometryColorsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryColorsResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Reuses the unified geometry transform for shadow entities. */
    WebglGeometryColorsVertexOutput vertex(
        WebglGeometryColorsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryColorsTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Applies the generated radial alpha texture only to shadow entities. */
    WebglGeometryColorsFrameBuffer fragment(
        WebglGeometryColorsVertexOutput inputValue)
    {
        const WebglGeometryColorsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (materialData.baseColorAndPhase.w <= 0.5f ||
            materialData.baseColorAndPhase.w > 1.5f)
        {
            discard_fragment();
        }
        // Evaluate the same CanvasTexture radial gradient in the fragment
        // stage.  This keeps the observable texture semantics deterministic
        // even on backends where a just-written storage texture is not yet
        // visible to a following render pass in the same submission.
        const float2 delta = inputValue.uv - float2(0.5f);
        const float distance = min(length(delta * 2.0f), 1.0f);
        const float shade = (210.0f + 45.0f * distance) / 255.0f;
        WebglGeometryColorsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(shade), half(shade), half(shade), half(1.0f));
        return frameBuffer;
    }
};

/** Draws only the transparent wireframe phase through the same Scene Set. */
class WebglGeometryColorsTransparentWireframePass final : public IRenderClass
{
public:
    /** Preserves MeshBasicMaterial's default depth-write ordering for wireframe. */
    constructor(RenderSet<WebglGeometryColorsSceneRenderSet> sceneSet [[Slot0]])
    {
        // The CPU expands each edge to a deterministic one-pixel strip; no
        // backend-specific line-width state is required.
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Reuses the geometry-colors transform and RenderEntity builtins. */
    WebglGeometryColorsVertexOutput vertex(
        WebglGeometryColorsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        WebglGeometryColorsVertexOutput outputValue =
            webglGeometryColorsTransformVertex(
                sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
        // Preserve the projected depth for wireframe entities.  The upstream
        // MeshBasicMaterial child is rendered after the solid mesh but still
        // depth-tested, so forcing every edge to the front would incorrectly
        // expose hidden back-facing edges.
        return outputValue;
    }

    /** Emits the native one-pixel wire segment and rejects other phases. */
    WebglGeometryColorsFrameBuffer fragment(
        WebglGeometryColorsVertexOutput inputValue)
    {
        const WebglGeometryColorsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        // Keep the pass's entity filtering in the vertex path.  A RenderSet
        // indexed-indirect invocation can evaluate all entities, so the
        // phase test is intentionally retained here for non-wire entities.
        if (materialData.baseColorAndPhase.w <= 1.5f)
            discard_fragment();
        // The r185 example uses the default opaque black wire material.  The
        // transparent flag controls sorting, not a reduced material alpha.
        WebglGeometryColorsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated Scene RenderSet, sampler, and ordinary single-sample target. */
class WebglGeometryColorsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryColorsSceneRenderSet> sceneSet;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> shadowTexture;
    Sampler shadowSampler;
    BindGroup<WebglGeometryColorsResources> resources;
    BindGroup<WebglGeometryColorsShadowComputeResources> shadowComputeResources;
    ComputeClass<WebglGeometryColorsShadowTextureComputePass> shadowComputePass;
    RenderClass<WebglGeometryColorsShadowPass> shadowPass;
    RenderClass<WebglGeometryColorsOpaquePass> scenePass;
    RenderClass<WebglGeometryColorsTransparentWireframePass> wirePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and fixed single-sample sampler state. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglGeometryColorsSceneRenderSet>();
        shadowTexture = device->createTexture("WebglGeometryColorsShadowTexture", 128u, 128u, 1u);
        shadowSampler = device->createSampler({
            .label = "WebglGeometryColorsShadowSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 1u,
        });
        resources = device->createBindGroup<WebglGeometryColorsResources>(
            shadowTexture->createView(), shadowSampler);
        shadowComputeResources = device->createBindGroup<WebglGeometryColorsShadowComputeResources>(
            shadowTexture->createView());
        shadowComputePass = device->createComputeClass<WebglGeometryColorsShadowTextureComputePass>(
            shadowComputeResources);
        shadowPass = device->createRenderClass<WebglGeometryColorsShadowPass>(sceneSet, resources);
        scenePass = device->createRenderClass<WebglGeometryColorsOpaquePass>(sceneSet, resources);
        wirePass = device->createRenderClass<WebglGeometryColorsTransparentWireframePass>(sceneSet);
    }

    /** Allocates the ordinary 800x500 RGBA8/depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglGeometryColorsOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglGeometryColorsDepth", width, height, 1u);
    }

    /** Submits the single Scene pass through RenderSet indexed-indirect drawing. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryColorsFrameBuffer shadowFrameBuffer;
        shadowFrameBuffer.color = outputColor->createView();
        shadowFrameBuffer.color.loadOp = LoadOp::Clear;
        shadowFrameBuffer.color.storeOp = StoreOp::Store;
        shadowFrameBuffer.color.clearValue = {1.0, 1.0, 1.0, 1.0};
        shadowFrameBuffer.depth = outputDepth->createView();
        shadowFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        shadowFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        shadowFrameBuffer.depth.depthClearValue = 1.0f;
        WebglGeometryColorsFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Load;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Load;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        WebglGeometryColorsFrameBuffer wireFrameBuffer;
        wireFrameBuffer.color = outputColor->createView();
        wireFrameBuffer.color.loadOp = LoadOp::Load;
        wireFrameBuffer.color.storeOp = StoreOp::Store;
        wireFrameBuffer.depth = outputDepth->createView();
        wireFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        wireFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->computePass("WebglGeometryColorsShadowTextureCompute", shadowComputePass(16u, 16u, 1u))
            ->renderPass("WebglGeometryColorsShadow", shadowFrameBuffer, shadowPass())
            ->renderPass("WebglGeometryColorsOpaque", frameBuffer, scenePass())
            ->renderPass("WebglGeometryColorsTransparentWireframe", wireFrameBuffer, wirePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned color target used by the host readback. */
    auto getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and explicit output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(shadowTexture);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};
