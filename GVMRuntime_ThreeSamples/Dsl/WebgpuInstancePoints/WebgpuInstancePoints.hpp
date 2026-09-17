#ifndef GVM_THREE_WEBGPU_INSTANCE_POINTS_HPP
#define GVM_THREE_WEBGPU_INSTANCE_POINTS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuInstancePointsCount = 256u;

/** Stores one corner of the triangle-expanded point sprite. */
struct WebgpuInstancePointsVertex
{
    float4 cornerAndUv [[Attribute0]];
};

/** Stores both camera matrices, viewport size, time, and GUI widths. */
struct WebgpuInstancePointsObjectData
{
    float4x4 mainViewProjection;
    float4x4 insetViewProjection;
    float4 viewportAndTime;
    float4 widthsAndPulse;
};

/** Stores one Hilbert spline position and its linear HSL color. */
struct WebgpuInstancePointsInstanceData
{
    float4 position;
    float4 color;
};

/** Stores opacity and circular-coverage controls. */
struct WebgpuInstancePointsMaterialData
{
    float4 opacityAndCoverage;
};

/** Defines the sole Scene RenderSet shared by both point viewports. */
struct WebgpuInstancePointsSceneRenderSet : public IRenderSet
{
    /** Declares expanded sprite geometry plus point instance components. */
    constructor(
        BufferComponent<WebgpuInstancePointsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuInstancePointsObjectData> objects,
        BufferComponent<WebgpuInstancePointsInstanceData> instances,
        BufferComponent<WebgpuInstancePointsMaterialData> materials)
    {
    }
};

/** Stores Compute controls for the animated point widths. */
struct WebgpuInstancePointsComputeData
{
    float4 timeWidthsAndPulse;
};

/** Binds writable point sizes and the immutable target-frame controls. */
struct WebgpuInstancePointsComputeResources final : public IBindGroup
{
    /** Declares current public structured-buffer Compute resources. */
    constructor(
        RWStructuredBuffer<float4> sizes [[Binding0]],
        UniformBuffer<WebgpuInstancePointsComputeData> data [[Binding1]])
    {
    }
};

/** Binds computed point widths for both Scene passes. */
struct WebgpuInstancePointsDrawResources final : public IBindGroup
{
    /** Declares the read-only size state produced by the Compute pass. */
    constructor(
        StructuredBuffer<float4> sizes [[Binding0]])
    {
    }
};

/** Updates one deterministic point width per dispatch thread. */
class [[LocalWorkGroupSize(64, 1, 1)]]
WebgpuInstancePointsSizeComputePass final : public IComputeClass
{
public:
    /** Binds the writable size state and target-frame controls. */
    constructor(
        BindGroup<WebgpuInstancePointsComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Reproduces the TSL sine pulse for one instance index. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint instanceID = dispatchThreadID.x;
        if (instanceID >= WebgpuInstancePointsCount) return;
        const float timeValue =
            resources->data->timeWidthsAndPulse.x;
        const float minimumWidth =
            resources->data->timeWidthsAndPulse.y;
        const float maximumWidth =
            resources->data->timeWidthsAndPulse.z;
        const float pulseSpeed =
            resources->data->timeWidthsAndPulse.w;
        const float sizeFactor =
            sin((timeValue + float(instanceID)) * pulseSpeed) *
                0.5f +
            0.5f;
        resources->sizes[instanceID] = float4(
            lerp(minimumWidth, maximumWidth, sizeFactor),
            0.0f,
            0.0f,
            0.0f);
    }
};

/** Carries point UV, color, entity identity, and computed width. */
struct WebgpuInstancePointsVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float3 color [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the shared RGBA8 color and depth attachments. */
struct WebgpuInstancePointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only attachment for inset background composition. */
struct WebgpuInstancePointsColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds the 4x Scene color used by the deterministic resolve pass. */
struct WebgpuInstancePointsResolveResources final : public IBindGroup
{
    /** Declares the single-sample color and its output sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Converts one linear channel to the r185 output transfer. */
float webgpuInstancePointsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the common main/inset point vertex path. */
WebgpuInstancePointsVertexOutput webgpuInstancePointsVertex(
    WebgpuInstancePointsVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID,
    RenderSet<WebgpuInstancePointsSceneRenderSet> sceneSet IN,
    BindGroup<WebgpuInstancePointsDrawResources> drawResources IN,
    bool inset)
{
    const WebgpuInstancePointsObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebgpuInstancePointsInstanceData instanceData =
        sceneSet->instances->get(
            renderEntityID,
            renderEntityInstanceID);
    const float4 clipPosition = inset
        ? mul(objectData.insetViewProjection, instanceData.position)
        : mul(objectData.mainViewProjection, instanceData.position);
    const float sizePixels =
        drawResources->sizes[renderEntityInstanceID].x;
    const float renderedSizePixels = sizePixels;
    const float2 viewport = inset
        ? float2(125.0f, 125.0f)
        : float2(
            objectData.viewportAndTime.x,
            objectData.viewportAndTime.y);
    float4 expanded = clipPosition;
    expanded.xy +=
        inputValue.cornerAndUv.xy *
        renderedSizePixels *
        2.0f /
        viewport *
        clipPosition.w;
    if (inset)
    {
        expanded.x =
            expanded.x * 0.15625f -
            expanded.w * 0.79375f;
        expanded.y =
            expanded.y * 0.25f +
            expanded.w * 0.67f;
    }
    WebgpuInstancePointsVertexOutput outputValue;
    outputValue.position = expanded;
    outputValue.uv = inputValue.cornerAndUv.zw;
    outputValue.color =
        instanceData.color.xyz *
        clamp(
            sizePixels / objectData.widthsAndPulse.y,
            0.0f,
            1.0f);
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Draws the full-window Hilbert point view through the Scene RenderSet. */
class WebgpuInstancePointsMainPass final : public IRenderClass
{
public:
    /** Configures transparent circular sprite blending. */
    constructor(
        RenderSet<WebgpuInstancePointsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuInstancePointsDrawResources> drawResources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one nonattenuated point in full-window pixel units. */
    WebgpuInstancePointsVertexOutput vertex(
        WebgpuInstancePointsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuInstancePointsVertex(
            inputValue,
            renderEntityID,
            renderEntityInstanceID,
            sceneSet,
            drawResources,
            false);
    }

    /** Applies the single-sample hard circle and linear-to-sRGB output. */
    WebgpuInstancePointsFrameBuffer fragment(
        WebgpuInstancePointsVertexOutput inputValue)
    {
        const float2 centered =
            inputValue.uv * 2.0f - 1.0f;
        const float radialDistanceSquared =
            dot(centered, centered);
        const float coverage =
            radialDistanceSquared > 1.0f ? 0.0f : 1.0f;
        const WebgpuInstancePointsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuInstancePointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.x)),
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.y)),
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.z)),
            half(coverage * materialData.opacityAndCoverage.x));
        return frameBuffer;
    }
};

/** Draws the same Scene RenderSet into the deterministic inset region. */
class WebgpuInstancePointsInsetPass final : public IRenderClass
{
public:
    /** Configures the same point material while preserving the main color. */
    constructor(
        RenderSet<WebgpuInstancePointsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuInstancePointsDrawResources> drawResources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one point in 125x125 inset pixel units. */
    WebgpuInstancePointsVertexOutput vertex(
        WebgpuInstancePointsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuInstancePointsVertex(
            inputValue,
            renderEntityID,
            renderEntityInstanceID,
            sceneSet,
            drawResources,
            true);
    }

    /** Applies the inset single-sample hard circle and output transfer. */
    WebgpuInstancePointsFrameBuffer fragment(
        WebgpuInstancePointsVertexOutput inputValue)
    {
        const float2 centered =
            inputValue.uv * 2.0f - 1.0f;
        const float radialDistanceSquared =
            dot(centered, centered);
        const float coverage =
            radialDistanceSquared > 1.0f ? 0.0f : 1.0f;
        const WebgpuInstancePointsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuInstancePointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.x)),
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.y)),
            half(webgpuInstancePointsLinearToSrgb(inputValue.color.z)),
            half(coverage * materialData.opacityAndCoverage.x));
        return frameBuffer;
    }
};

/** Carries fullscreen coordinates for the inset background. */
struct WebgpuInstancePointsScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Draws the #222 inset background before the second Scene pass. */
class WebgpuInstancePointsInsetBackgroundPass final : public IRenderClass
{
public:
    /** Configures an opaque screen-space background pass. */
    constructor()
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuInstancePointsScreenOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstancePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Retains only the 125x125 inset rectangle with its locked color. */
    WebgpuInstancePointsColorFrameBuffer fragment(
        WebgpuInstancePointsScreenOutput inputValue)
    {
        const float2 pixel =
            float2(inputValue.uv.x * 800.0f,
                   inputValue.uv.y * 500.0f);
        if (pixel.x < 20.0f || pixel.x >= 145.0f ||
            pixel.y < 355.0f || pixel.y >= 480.0f)
        {
            discard_fragment();
        }
        WebgpuInstancePointsColorFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(0.1333333333f), half(1.0f));
        return frameBuffer;
    }
};

/** Composites the deterministic minimized WebGPU Inspector bar. */
class WebgpuInstancePointsInspectorPass final : public IRenderClass
{
public:
    /** Configures alpha blending for the screen-only Inspector overlay. */
    constructor()
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor =
            BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuInstancePointsScreenOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstancePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked rounded Inspector bar and its shadow. */
    WebgpuInstancePointsColorFrameBuffer fragment(
        WebgpuInstancePointsScreenOutput inputValue)
    {
        const float2 pixel =
            float2(
                inputValue.uv.x * 800.0f,
                inputValue.uv.y * 500.0f);
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
            const float2 shadowCenter =
                float2(699.5f, 37.5f);
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
            WebgpuInstancePointsColorFrameBuffer shadowFrameBuffer;
            shadowFrameBuffer.color =
                half4(half3(float3(0.0f)), half(shadowAlpha));
            return shadowFrameBuffer;
        }
        float3 color =
            float3(30.0f, 30.0f, 36.0f) / 255.0f;
        float alpha = 0.85f;
        if (pixel.x < 663.0f)
        {
            color =
                float3(23.1818f, 61.8182f, 85.7727f) /
                255.0f;
            alpha = 0.88f;
        }
        if (roundedDistance > -1.0f)
        {
            color =
                float3(46.1f, 46.1f, 55.8f) /
                255.0f;
            alpha = 0.899f;
        }
        alpha *= clamp(0.5f - roundedDistance, 0.0f, 1.0f);
        WebgpuInstancePointsColorFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Copies the single-sample Scene and screen-pass target into the final output. */
class WebgpuInstancePointsResolvePass final : public IRenderClass
{
public:
    /** Binds only the single-sample screen-space color. */
    constructor(
        BindGroup<WebgpuInstancePointsResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuInstancePointsScreenOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuInstancePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads the one centered Scene sample without antialias emulation. */
    WebgpuInstancePointsColorFrameBuffer fragment(
        WebgpuInstancePointsScreenOutput inputValue)
    {
        WebgpuInstancePointsColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->sceneColor->sample(
            resources->sceneSampler,
            inputValue.uv));
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet, one Compute pass, both viewports, and Inspector composition. */
class WebgpuInstancePointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuInstancePointsSceneRenderSet> sceneSet;
    Buffer<float4,
           BufferUsage<Storage, CopyDst>> sizeBuffer;
    Buffer<WebgpuInstancePointsComputeData,
           BufferUsage<Uniform, CopyDst>> computeDataBuffer;
    BindGroup<WebgpuInstancePointsComputeResources> computeResources;
    BindGroup<WebgpuInstancePointsDrawResources> drawResources;
    ComputeClass<WebgpuInstancePointsSizeComputePass> sizeComputePass;
    RenderClass<WebgpuInstancePointsMainPass> mainPass;
    RenderClass<WebgpuInstancePointsInsetBackgroundPass> insetBackgroundPass;
    RenderClass<WebgpuInstancePointsInsetPass> insetPass;
    RenderClass<WebgpuInstancePointsInspectorPass> inspectorPass;
    Sampler resolveSampler;
    BindGroup<WebgpuInstancePointsResolveResources> resolveResources;
    RenderClass<WebgpuInstancePointsResolvePass> resolvePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> mainDepthTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> insetDepthTexture;
    WebgpuInstancePointsComputeData computeData;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the Scene RenderSet, size Compute state, and three dedicated passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuInstancePointsSceneRenderSet>();
        sizeBuffer = device->createBuffer(
            "WebgpuInstancePointsSizes",
            WebgpuInstancePointsCount);
        computeDataBuffer = device->createBuffer(
            "WebgpuInstancePointsComputeData",
            1u);
        computeResources =
            device->createBindGroup<WebgpuInstancePointsComputeResources>(
                sizeBuffer,
                computeDataBuffer);
        drawResources =
            device->createBindGroup<WebgpuInstancePointsDrawResources>(
                sizeBuffer);
        sizeComputePass =
            device->createComputeClass<WebgpuInstancePointsSizeComputePass>(
                computeResources);
        mainPass =
            device->createRenderClass<WebgpuInstancePointsMainPass>(
                sceneSet,
                drawResources);
        insetBackgroundPass =
            device->createRenderClass<
                WebgpuInstancePointsInsetBackgroundPass>();
        insetPass =
            device->createRenderClass<WebgpuInstancePointsInsetPass>(
                sceneSet,
                drawResources);
        inspectorPass =
            device->createRenderClass<WebgpuInstancePointsInspectorPass>();
        resolveSampler = device->createSampler({
            .label = "WebgpuInstancePointsResolveSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates final color plus independent main and inset depth attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebgpuInstancePointsSceneRGBA8",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebgpuInstancePointsOutputRGBA8",
            width,
            height,
            1u);
        mainDepthTexture = device->createTexture(
            "WebgpuInstancePointsMainDepth32",
            width,
            height,
            1u);
        insetDepthTexture = device->createTexture(
            "WebgpuInstancePointsInsetDepth32",
            width,
            height,
            1u);
        resolveResources =
            device->createBindGroup<
                WebgpuInstancePointsResolveResources>(
                sceneTexture->createView(),
                resolveSampler);
        resolvePass =
            device->createRenderClass<
                WebgpuInstancePointsResolvePass>(
                resolveResources);
    }

    /** Uploads immutable target-frame controls consumed by the DSL Compute pass. */
    void configureCompute(
        float timeValue,
        float minimumWidth,
        float maximumWidth,
        float pulseSpeed)
    {
        computeData.timeWidthsAndPulse = float4(
            timeValue,
            minimumWidth,
            maximumWidth,
            pulseSpeed);
        graphicsQueue
            ->writeBuffer(
                BufferRange(computeDataBuffer),
                &computeData,
                sizeof(computeData))
            ->submit();
    }

    /** Computes sizes and draws main, inset background, and inset Scene passes. */
    void render() override
    {
        sceneSet->update();
        WebgpuInstancePointsFrameBuffer mainFrameBuffer;
        mainFrameBuffer.color = sceneTexture->createView();
        mainFrameBuffer.color.loadOp = LoadOp::Clear;
        mainFrameBuffer.color.storeOp = StoreOp::Store;
        mainFrameBuffer.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        mainFrameBuffer.depth = mainDepthTexture->createView();
        mainFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        mainFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        mainFrameBuffer.depth.depthClearValue = 1.0f;

        WebgpuInstancePointsColorFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = sceneTexture->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Load;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;

        WebgpuInstancePointsFrameBuffer insetFrameBuffer;
        insetFrameBuffer.color = sceneTexture->createView();
        insetFrameBuffer.color.loadOp = LoadOp::Load;
        insetFrameBuffer.color.storeOp = StoreOp::Store;
        insetFrameBuffer.depth = insetDepthTexture->createView();
        insetFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        insetFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        insetFrameBuffer.depth.depthClearValue = 1.0f;

        WebgpuInstancePointsColorFrameBuffer resolveFrameBuffer;
        resolveFrameBuffer.color = outputTexture->createView();
        resolveFrameBuffer.color.loadOp = LoadOp::Clear;
        resolveFrameBuffer.color.storeOp = StoreOp::Store;
        resolveFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};

        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->computePass(
                "WebgpuInstancePointsSizeCompute",
                sizeComputePass(WebgpuInstancePointsCount, 1u, 1u))
            ->renderPass(
                "WebgpuInstancePointsMain",
                mainFrameBuffer,
                mainPass())
            ->renderPass(
                "WebgpuInstancePointsInsetBackground",
                backgroundFrameBuffer,
                insetBackgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuInstancePointsInset",
                insetFrameBuffer,
                insetPass())
            ->renderPass(
                "WebgpuInstancePointsResolve",
                resolveFrameBuffer,
                resolvePass(3u, 1u, 0u, 0u))
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

    /** Releases the Scene RenderSet and all dedicated attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(mainDepthTexture);
        device->freeTexture(insetDepthTexture);
    }
};

#endif
