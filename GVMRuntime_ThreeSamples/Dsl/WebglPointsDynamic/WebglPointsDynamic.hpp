#ifndef GVM_THREE_WEBGL_POINTS_DYNAMIC_HPP
#define GVM_THREE_WEBGL_POINTS_DYNAMIC_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one OBJ point and one screen-space billboard corner. */
struct WebglPointsDynamicVertex
{
    float4 position [[Attribute0]];
    float4 cornerAndPadding [[Attribute1]];
    float4 color [[Attribute2]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct WebglPointsDynamicVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
    float fogAmount [[Attribute2]];
    float2 corner [[Attribute3]];
};

/** Stores one entity camera transform, point size, and material index. */
struct WebglPointsDynamicObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewportPointSizeAndFog;
    uint4 materialAndFlags;
};

/** Stores one per-instance transform and tint selected by the entity builtin. */
struct WebglPointsDynamicInstanceData
{
    float4 tint;
};

/** Stores one material color and the private semantic mode selected by C++. */
struct WebglPointsDynamicMaterialData
{
    float4 baseColor;
};

/** Stores fixed viewport, film, and bloom controls for the private composer chain. */
struct WebglPointsDynamicScreenUniforms
{
    float4 viewportAndTime;
    float4 bloomAndFilm;
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglPointsDynamicSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, and material storage. */
    constructor(BufferComponent<WebglPointsDynamicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebglPointsDynamicObjectData> objects,
                BufferComponent<WebglPointsDynamicInstanceData> instances,
                BufferComponent<WebglPointsDynamicMaterialData> materials)
    {
    }
};

/** Defines the linear scene attachment and depth used for deterministic capture. */
struct WebglPointsDynamicFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one depth-free linear screen attachment. */
struct WebglPointsDynamicLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final display-encoded capture attachment. */
struct WebglPointsDynamicOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen UV coordinates through the private screen passes. */
struct WebglPointsDynamicScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Binds one sampled screen input and the fixed composer controls. */
struct WebglPointsDynamicScreenResources final : public IBindGroup
{
    /** Declares the input texture, linear sampler, and uniform controls. */
    constructor(
        Texture2D<float4> inputTexture [[Binding0]],
        Sampler linearSampler [[Binding1]],
        UniformBuffer<WebglPointsDynamicScreenUniforms> uniforms [[Binding2]])
    {
    }
};

/** Binds the original scene and blurred bloom texture for composition. */
struct WebglPointsDynamicBloomCombineResources final : public IBindGroup
{
    /** Declares both sampled textures, sampler, and bloom strength. */
    constructor(
        Texture2D<float4> sceneTexture [[Binding0]],
        Texture2D<float4> bloomTexture [[Binding1]],
        Sampler linearSampler [[Binding2]],
        UniformBuffer<WebglPointsDynamicScreenUniforms> uniforms [[Binding3]])
    {
    }
};

/** Emits an EffectComposer-compatible fullscreen triangle. */
WebglPointsDynamicScreenOutput webglPointsDynamicFullscreenVertex(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglPointsDynamicScreenOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Converts one linear channel with the Three.js OutputPass transfer function. */
float webglPointsDynamicLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/**
 * Draws all OBJ-derived points through the Scene RenderSet indexed-indirect path.
 * Point primitives are expanded to triangle billboards in the vertex stage so
 * the frozen DSL surface does not depend on a PointSize builtin.
 */
class WebglPointsDynamicMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<WebglPointsDynamicSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves per-entity and per-instance data through the RenderEntity builtins. */
    WebglPointsDynamicVertexOutput vertex(
        WebglPointsDynamicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPointsDynamicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglPointsDynamicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglPointsDynamicVertexOutput outputValue;
        const float4 viewPosition = mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(objectData.modelViewProjection, inputValue.position);
        const float2 viewport = objectData.viewportPointSizeAndFog.xy;
        const float perspectiveScale = objectData.viewportPointSizeAndFog.z;
        const float pointSize = max(
            perspectiveScale / max(-viewPosition.z, 1.0f),
            1.0f);
        const float2 corner = inputValue.cornerAndPadding.xy;
        // Reconstruct the deterministic aliased point window used by the
        // WebGL backend before expanding it to a triangle quad. Native point
        // rasterization rounds the coverage edge to a 1/16-pixel grid and
        // anchors the center to a 1/256-pixel grid; raw clip-space expansion
        // otherwise shifts the dense floor lattice by a pixel.
        const float2 pointCenter =
            (clipPosition.xy / clipPosition.w + float2(1.0f)) *
            (viewport * 0.5f);
        const float2 topPointCenter =
            float2(pointCenter.x, viewport.y - pointCenter.y);
        const float2 halfPointSize = float2(pointSize * 0.5f);
        const float2 roundedCoverageLow = floor(
            (topPointCenter - halfPointSize) * 16.0f + float2(0.5f)) / 16.0f;
        const float2 roundedCoverageHigh = floor(
            (topPointCenter + halfPointSize) * 16.0f + float2(0.5f)) / 16.0f;
        const float2 coverageSize =
            roundedCoverageHigh - roundedCoverageLow;
        const float2 coverageEdge = floor(
            float2(
                topPointCenter.x - halfPointSize.x,
                topPointCenter.y -
                    (coverageSize.y - halfPointSize.y)) * 256.0f +
            float2(0.5f, 0.75f)) / 256.0f;
        const float2 coverageCenter = coverageEdge + coverageSize * 0.5f;
        clipPosition.x =
            (coverageCenter.x / (viewport.x * 0.5f) - 1.0f) * clipPosition.w;
        clipPosition.y =
            ((viewport.y - coverageCenter.y) / (viewport.y * 0.5f) - 1.0f) *
            clipPosition.w;
        clipPosition.xy += corner *
            (coverageSize / (viewport * 0.5f)) * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = clipPosition;
        outputValue.color = inputValue.color * instanceData.tint;
        outputValue.entityID = renderEntityID;
        const float fogDensity = objectData.viewportPointSizeAndFog.w;
        outputValue.fogAmount = 1.0f - exp(-max(-viewPosition.z, 0.0f) * max(-viewPosition.z, 0.0f) * fogDensity * fogDensity);
        outputValue.corner = corner;
        return outputValue;
    }

    /** Applies the point material and fog in the linear composer domain. */
    WebglPointsDynamicFrameBuffer fragment(
        WebglPointsDynamicVertexOutput inputValue)
    {
        const WebglPointsDynamicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglPointsDynamicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID, objectData.materialAndFlags.x);
        const float3 linearColor = saturate(
            inputValue.color.xyz * materialData.baseColor.xyz);
        const float3 fogColor = float3(0.0f, 0.000303526984f, 0.001214107934f);
        const float3 foggedColor = lerp(linearColor, fogColor, saturate(inputValue.fogAmount));
        WebglPointsDynamicFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(foggedColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies one fixed 25-tap Gaussian blur pass used by r185 BloomPass. */
class WebglPointsDynamicBloomBlurPass final : public IRenderClass
{
public:
    /** Binds one scene or intermediate texture and the blur increment. */
    constructor(BindGroup<WebglPointsDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPointsDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPointsDynamicFullscreenVertex(vertexID);
    }

    /** Samples a bounded horizontal or vertical Gaussian kernel. */
    WebglPointsDynamicLinearFrameBuffer fragment(
        WebglPointsDynamicScreenOutput inputValue)
    {
        const float2 increment = float2(
            resources->uniforms->bloomAndFilm.z,
            resources->uniforms->bloomAndFilm.w);
        float4 color = float4(0.0f);
        for (int tap = -12; tap <= 12; ++tap)
        {
            const float offset = float(tap);
            const float weight = exp(-(offset * offset) / 32.0f) /
                10.009172595445069f;
            color += resources->inputTexture->sample(
                resources->linearSampler, inputValue.uv + increment * offset) * weight;
        }
        WebglPointsDynamicLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Adds the blurred bloom texture back to the original scene. */
class WebglPointsDynamicBloomCombinePass final : public IRenderClass
{
public:
    /** Binds the original scene and the two-direction blur result. */
    constructor(BindGroup<WebglPointsDynamicBloomCombineResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPointsDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPointsDynamicFullscreenVertex(vertexID);
    }

    /** Reproduces BloomPass strength 0.75 additive composition. */
    WebglPointsDynamicLinearFrameBuffer fragment(
        WebglPointsDynamicScreenOutput inputValue)
    {
        const float4 scene = resources->sceneTexture->sample(
            resources->linearSampler, inputValue.uv);
        const float4 bloom = resources->bloomTexture->sample(
            resources->linearSampler, inputValue.uv);
        WebglPointsDynamicLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(scene + bloom * resources->uniforms->bloomAndFilm.x);
        return frameBuffer;
    }
};

/** Applies the deterministic FilmPass noise to the composer buffer. */
class WebglPointsDynamicFilmPass final : public IRenderClass
{
public:
    /** Binds the composer buffer and film intensity. */
    constructor(BindGroup<WebglPointsDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPointsDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPointsDynamicFullscreenVertex(vertexID);
    }

    /** Reproduces FilmShader's spatial noise and intensity blend. */
    WebglPointsDynamicLinearFrameBuffer fragment(
        WebglPointsDynamicScreenOutput inputValue)
    {
        const float4 color = resources->inputTexture->sample(
            resources->linearSampler, inputValue.uv);
        const float2 noiseUv = frac(
            inputValue.uv + resources->uniforms->viewportAndTime.z);
        // Keep the shader's deterministic spatial grain function in the
        // same floating-point form used by the existing sample capture.
        // Three's shared shader common chunk folds the dot product by PI
        // before evaluating sin().  This is observable in the FilmPass grain
        // and must be retained for the locked r185 postprocess clock.
        const float randomArgument = fmod(
            dot(noiseUv, float2(12.9898f, 78.233f)),
            3.14159265358979323846f);
        const float noise = frac(sin(randomArgument) * 43758.5453f);
        const float intensity = resources->uniforms->bloomAndFilm.y;
        const float3 baseColor = float3(color.x, color.y, color.z);
        const float3 filmed = baseColor + baseColor * clamp(
            0.1f + noise, 0.0f, 1.0f);
        WebglPointsDynamicLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(baseColor + (filmed - baseColor) * intensity),
            half(color.w));
        return frameBuffer;
    }
};

/** Applies the r185 FocusShader neighborhood blur in the screen domain. */
class WebglPointsDynamicFocusPass final : public IRenderClass
{
public:
    /** Binds the film buffer and viewport controls. */
    constructor(BindGroup<WebglPointsDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPointsDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPointsDynamicFullscreenVertex(vertexID);
    }

    /** Reproduces the exact seven-direction r185 FocusShader kernel. */
    WebglPointsDynamicLinearFrameBuffer fragment(
        WebglPointsDynamicScreenOutput inputValue)
    {
        const float2 vin = (inputValue.uv - float2(0.5f)) * 1.4f;
        const float sampleDistance = dot(vin, vin) * 2.0f;
        const float focusScale =
            (0.00125f * 100.0f + sampleDistance) * 0.94f * 4.0f;
        const float2 sampleSize = float2(
            focusScale / resources->uniforms->viewportAndTime.x,
            focusScale / resources->uniforms->viewportAndTime.y);
        float4 color = resources->inputTexture->sample(
            resources->linearSampler, inputValue.uv);
        float4 sum = color;
        float4 sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(0.111964f, 0.993712f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(0.846724f, 0.532032f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(0.943883f, -0.330279f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(0.330279f, -0.943883f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(-0.532032f, -0.846724f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(-0.993712f, -0.111964f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        sampleValue = resources->inputTexture->sample(
            resources->linearSampler,
            inputValue.uv + float2(-0.707107f, 0.707107f) * sampleSize);
        sum += sampleValue;
        if (sampleValue.z < color.z) color = sampleValue;
        color = color * 2.0f - sum * 0.125f;
        color = lerp(
            color,
            sum * 0.125f,
            1.0f - sampleDistance * 0.5f);
        const float3 focused =
            color.xyz * color.xyz * 0.95f + color.xyz;
        WebglPointsDynamicLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(focused), half(1.0f));
        return frameBuffer;
    }
};

/** Converts the final focus result into the display sRGB target. */
class WebglPointsDynamicOutputPass final : public IRenderClass
{
public:
    /** Binds the final focus texture. */
    constructor(BindGroup<WebglPointsDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPointsDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPointsDynamicFullscreenVertex(vertexID);
    }

    /** Applies OutputPass's linear-to-sRGB conversion. */
    WebglPointsDynamicOutputFrameBuffer fragment(
        WebglPointsDynamicScreenOutput inputValue)
    {
        const float4 color = resources->inputTexture->sample(
            resources->linearSampler, inputValue.uv);
        WebglPointsDynamicOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPointsDynamicLinearToSrgb(color.x)),
            half(webglPointsDynamicLinearToSrgb(color.y)),
            half(webglPointsDynamicLinearToSrgb(color.z)),
            half(color.w));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet and its single-sample composer chain. */
class WebglPointsDynamicRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPointsDynamicSceneRenderSet> sceneSet;
    RenderClass<WebglPointsDynamicMainPass> scenePass;
    Buffer<WebglPointsDynamicScreenUniforms, BufferUsage<Uniform, CopyDst>> screenUniformBuffer;
    Buffer<WebglPointsDynamicScreenUniforms, BufferUsage<Uniform, CopyDst>> bloomYUniformBuffer;
    Sampler linearSampler;
    RenderClass<WebglPointsDynamicBloomBlurPass> bloomXPass;
    RenderClass<WebglPointsDynamicBloomBlurPass> bloomYPass;
    RenderClass<WebglPointsDynamicBloomCombinePass> bloomCombinePass;
    RenderClass<WebglPointsDynamicFilmPass> filmPass;
    RenderClass<WebglPointsDynamicFocusPass> focusPass;
    RenderClass<WebglPointsDynamicOutputPass> outputPass;
    BindGroup<WebglPointsDynamicScreenResources> bloomXResources;
    BindGroup<WebglPointsDynamicScreenResources> bloomYResources;
    BindGroup<WebglPointsDynamicBloomCombineResources> bloomCombineResources;
    BindGroup<WebglPointsDynamicScreenResources> filmResources;
    BindGroup<WebglPointsDynamicScreenResources> focusResources;
    BindGroup<WebglPointsDynamicScreenResources> outputResources;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneColor;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        bloomX;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        bloomY;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        bloomCombined;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        filmColor;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        focusColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        sceneDepth;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates this shard's RenderSet and its generated Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglPointsDynamicSceneRenderSet>();
        scenePass = device->createRenderClass<WebglPointsDynamicMainPass>(sceneSet);
        screenUniformBuffer = device->createBuffer("WebglPointsDynamicScreenUniforms", 1u);
        bloomYUniformBuffer = device->createBuffer("WebglPointsDynamicBloomYUniforms", 1u);
        SamplerDescriptor samplerDescriptor;
        samplerDescriptor.addressModeU = AddressMode::ClampToEdge;
        samplerDescriptor.addressModeV = AddressMode::ClampToEdge;
        samplerDescriptor.addressModeW = AddressMode::ClampToEdge;
        samplerDescriptor.magFilter = FilterMode::Linear;
        samplerDescriptor.minFilter = FilterMode::Linear;
        samplerDescriptor.mipmapFilter = MipmapFilterMode::Linear;
        samplerDescriptor.lodMinClamp = 0.0f;
        samplerDescriptor.lodMaxClamp = 0.0f;
        samplerDescriptor.compare = CompareFunction::Undefined;
        samplerDescriptor.maxAnisotropy = 1u;
        linearSampler = device->createSampler(samplerDescriptor);
    }

    /** Recreates the explicit single-sample capture attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneColor = device->createTexture("WebglPointsDynamicSceneColor", width, height, 1u);
        bloomX = device->createTexture("WebglPointsDynamicBloomX", width, height, 1u);
        bloomY = device->createTexture("WebglPointsDynamicBloomY", width, height, 1u);
        bloomCombined = device->createTexture("WebglPointsDynamicBloomCombined", width, height, 1u);
        filmColor = device->createTexture("WebglPointsDynamicFilm", width, height, 1u);
        focusColor = device->createTexture("WebglPointsDynamicFocus", width, height, 1u);
        outputColor = device->createTexture("WebglPointsDynamicOutput", width, height, 1u);
        sceneDepth = device->createTexture("WebglPointsDynamicDepth", width, height, 1u);
        const WebglPointsDynamicScreenUniforms horizontalUniforms = {
            float4(float(width), float(height), 0.0f, 0.0f),
            float4(0.75f, 0.5f, 1.0f / 512.0f, 0.0f)};
        const WebglPointsDynamicScreenUniforms verticalUniforms = {
            float4(float(width), float(height), 0.01f, 0.0f),
            float4(0.75f, 0.5f, 0.0f, 1.0f / 512.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(screenUniformBuffer),
                &horizontalUniforms,
                sizeof(horizontalUniforms))
            ->writeBuffer(
                BufferRange(bloomYUniformBuffer),
                &verticalUniforms,
                sizeof(verticalUniforms))
            ->submit();
        bloomXResources = device->createBindGroup<WebglPointsDynamicScreenResources>(
            sceneColor->createView(), linearSampler, screenUniformBuffer);
        bloomYResources = device->createBindGroup<WebglPointsDynamicScreenResources>(
            bloomX->createView(), linearSampler, bloomYUniformBuffer);
        bloomCombineResources = device->createBindGroup<WebglPointsDynamicBloomCombineResources>(
            sceneColor->createView(), bloomY->createView(), linearSampler, screenUniformBuffer);
        filmResources = device->createBindGroup<WebglPointsDynamicScreenResources>(
            bloomCombined->createView(), linearSampler, screenUniformBuffer);
        focusResources = device->createBindGroup<WebglPointsDynamicScreenResources>(
            filmColor->createView(), linearSampler, screenUniformBuffer);
        outputResources = device->createBindGroup<WebglPointsDynamicScreenResources>(
            focusColor->createView(), linearSampler, screenUniformBuffer);
        bloomXPass = device->createRenderClass<WebglPointsDynamicBloomBlurPass>(bloomXResources);
        bloomYPass = device->createRenderClass<WebglPointsDynamicBloomBlurPass>(bloomYResources);
        bloomCombinePass = device->createRenderClass<WebglPointsDynamicBloomCombinePass>(bloomCombineResources);
        filmPass = device->createRenderClass<WebglPointsDynamicFilmPass>(filmResources);
        focusPass = device->createRenderClass<WebglPointsDynamicFocusPass>(focusResources);
        outputPass = device->createRenderClass<WebglPointsDynamicOutputPass>(outputResources);
    }

    /** Updates the deterministic FilmPass time before the next single-sample frame. */
    void updateFilmTime(float filmTime)
    {
        const WebglPointsDynamicScreenUniforms uniforms = {
            float4(float(readbackWidth), float(readbackHeight), filmTime, 0.0f),
            float4(0.75f, 0.5f, 1.0f / 512.0f, 0.0f)};
        graphicsQueue->writeBuffer(
            BufferRange(screenUniformBuffer), &uniforms, sizeof(uniforms))->submit();
    }

    /** Applies pending RenderSet commands and submits the complete screen chain. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglPointsDynamicFrameBuffer frameBuffer;
        frameBuffer.color = sceneColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.000303526984, 0.001214107934, 1.0};
        frameBuffer.depth = sceneDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglPointsDynamicLinearFrameBuffer bloomXFrame;
        bloomXFrame.color = bloomX->createView();
        bloomXFrame.color.loadOp = LoadOp::Clear;
        bloomXFrame.color.storeOp = StoreOp::Store;
        WebglPointsDynamicLinearFrameBuffer bloomYFrame;
        bloomYFrame.color = bloomY->createView();
        bloomYFrame.color.loadOp = LoadOp::Clear;
        bloomYFrame.color.storeOp = StoreOp::Store;
        WebglPointsDynamicLinearFrameBuffer combineFrame;
        combineFrame.color = bloomCombined->createView();
        combineFrame.color.loadOp = LoadOp::Clear;
        combineFrame.color.storeOp = StoreOp::Store;
        WebglPointsDynamicLinearFrameBuffer filmFrame;
        filmFrame.color = filmColor->createView();
        filmFrame.color.loadOp = LoadOp::Clear;
        filmFrame.color.storeOp = StoreOp::Store;
        WebglPointsDynamicLinearFrameBuffer focusFrame;
        focusFrame.color = focusColor->createView();
        focusFrame.color.loadOp = LoadOp::Clear;
        focusFrame.color.storeOp = StoreOp::Store;
        WebglPointsDynamicOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        graphicsQueue
            ->renderPass("WebglPointsDynamicScene", frameBuffer, scenePass())
            ->renderPass("WebglPointsDynamicBloomX", bloomXFrame, bloomXPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPointsDynamicBloomY", bloomYFrame, bloomYPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPointsDynamicBloomCombine", combineFrame, bloomCombinePass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPointsDynamicFilm", filmFrame, filmPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPointsDynamicFocus", focusFrame, focusPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPointsDynamicOutput", outputFrame, outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the explicit capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicit capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the single-sample attachments and RenderSet after capture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(screenUniformBuffer);
        device->freeBuffer(bloomYUniformBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(bloomX);
        device->freeTexture(bloomY);
        device->freeTexture(bloomCombined);
        device->freeTexture(filmColor);
        device->freeTexture(focusColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#undef WebglPointsDynamicRenderer
#undef WebglPointsDynamicOutputPass
#undef WebglPointsDynamicFocusPass
#undef WebglPointsDynamicFilmPass
#undef WebglPointsDynamicBloomCombinePass
#undef WebglPointsDynamicBloomBlurPass
#undef WebglPointsDynamicBloomCombineResources
#undef WebglPointsDynamicScreenResources
#undef WebglPointsDynamicOutputFrameBuffer
#undef WebglPointsDynamicLinearFrameBuffer
#undef WebglPointsDynamicFrameBuffer
#undef WebglPointsDynamicMainPass
#undef WebglPointsDynamicSceneRenderSet
#undef THREE_BASIC_JOIN

#endif
