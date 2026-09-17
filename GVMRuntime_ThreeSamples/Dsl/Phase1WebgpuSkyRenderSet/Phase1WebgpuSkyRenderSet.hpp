#ifndef GVM_THREE_WEBGPU_SKY_RENDER_SET_HPP
#define GVM_THREE_WEBGPU_SKY_RENDER_SET_HPP

#include "Phase1SkyShared.hpp"

/** Defines the unique RenderSet for the WebGPU sky Scene. */
struct WebgpuSkySceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and mandatory object, instance, and material components. */
    constructor(
        BufferComponent<Phase1SkyVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<Phase1SkyObjectData> objects,
        BufferComponent<Phase1SkyInstanceData> instances,
        BufferComponent<Phase1SkyMaterialData> materials)
    {
    }
};

/** Binds the deterministic atmospheric and camera state. */
struct WebgpuSkyFrameResources final : public IBindGroup
{
    /** Declares the immutable frame resource layout. */
    constructor(
        UniformBuffer<Phase1SkyFrameData> frame [[Binding0]])
    {
    }
};

/** Defines the final deterministic WebGPU sky target. */
struct WebgpuSkyFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws the WebGPU sky through the Scene-owned RenderSet indirect path. */
class WebgpuSkyScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and the sky frame controls. */
    constructor(
        RenderSet<WebgpuSkySceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSkyFrameResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the RenderSet-owned fullscreen sky geometry and preserves entity identity. */
    Phase1SkyVertexOutput vertex(
        Phase1SkyVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const Phase1SkyObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const Phase1SkyInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const Phase1SkyMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        Phase1SkyVertexOutput outputValue;
        outputValue.position =
            inputValue.position +
            float4(
                objectData.phase.y +
                    instanceData.value.x +
                    materialData.value.x,
                0.0f,
                0.0f,
                0.0f);
        outputValue.uv =
            inputValue.position.xy * 0.5f + float2(0.5f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the complete r185 WebGPU sky atmosphere and output transform. */
    WebgpuSkyFrameBuffer fragment(Phase1SkyVertexOutput inputValue)
    {
        const float3 direction =
            phase1SkyCameraRay(
                inputValue.uv,
                resources->frame->cameraRight,
                resources->frame->cameraUp,
                resources->frame->cameraForward,
                resources->frame->viewportAndTime);
        const float3 linearColor =
            phase1SkyAtmosphere(
                direction,
                resources->frame->atmosphere,
                resources->frame->clouds,
                resources->frame->sunAndExposure,
                resources->frame->viewportAndTime);
        const float3 encoded =
            phase1SkyOutput(linearColor, resources->frame->clouds.w);
        WebgpuSkyFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Carries fullscreen coordinates into the WebGPU Inspector overlay pass. */
struct WebgpuSkyInspectorVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Composites the deterministic minimized Inspector bar captured by r185. */
class WebgpuSkyInspectorPass final : public IRenderClass
{
public:
    /** Binds the frame state used to enable the initial-frame Inspector overlay. */
    constructor(BindGroup<WebgpuSkyFrameResources> resources [[Slot0]])
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
    /** Emits one fullscreen triangle for the screen-only Inspector pass. */
    WebgpuSkyInspectorVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuSkyInspectorVertexOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the minimized Inspector bar without affecting later captures. */
    WebgpuSkyFrameBuffer fragment(
        WebgpuSkyInspectorVertexOutput inputValue)
    {
        if (resources->frame->viewportAndTime.w < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel =
            float2(
                inputValue.uv.x * resources->frame->viewportAndTime.x,
                inputValue.uv.y *
                    resources->frame->viewportAndTime.y);
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
            WebgpuSkyFrameBuffer shadowFrameBuffer;
            shadowFrameBuffer.color =
                half4(
                    half3(float3(0.0f)),
                    half(shadowAlpha));
            return shadowFrameBuffer;
        }
        float3 color = float3(30.0f, 30.0f, 36.0f) / 255.0f;
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
            color = float3(46.1f, 46.1f, 55.8f) / 255.0f;
            alpha = 0.899f;
        }
        alpha *=
            clamp(
                0.5f - roundedDistance,
                0.0f,
                1.0f);
        WebgpuSkyFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGPU sky RenderSet, atmospheric pass, and Inspector pass. */
class Phase1WebgpuSkyRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSkySceneRenderSet> sceneSet;
    Buffer<Phase1SkyFrameData, BufferUsage<Uniform, CopyDst>>
        frameBuffer;
    BindGroup<WebgpuSkyFrameResources> frameResources;
    RenderClass<WebgpuSkyScenePass> skyPass;
    RenderClass<WebgpuSkyInspectorPass> inspectorPass;
    Texture<
        TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
        TextureDimension::e2D>
        outputColor;
    Phase1SkyFrameData frameData;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and dedicated WebGPU sky resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuSkySceneRenderSet>();
        frameBuffer =
            device->createBuffer("WebgpuSkyFrameData", 1u);
        frameResources =
            device->createBindGroup<WebgpuSkyFrameResources>(
                frameBuffer);
        skyPass =
            device->createRenderClass<WebgpuSkyScenePass>(
                sceneSet,
                frameResources);
        inspectorPass =
            device->createRenderClass<WebgpuSkyInspectorPass>(
                frameResources);
    }

    /** Allocates the fixed-size final color target requested by the Three host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor =
            device->createTexture(
                "WebgpuSkyOutput",
                width,
                height,
                1u);
    }

    /** Uploads one canonical sky scenario without pipeline-specific branches. */
    void configureSky(
        float4 atmosphere,
        float4 clouds,
        float4 sunAndExposure,
        float4 cameraRight,
        float4 cameraUp,
        float4 cameraForward,
        float4 viewportAndTime)
    {
        frameData.atmosphere = atmosphere;
        frameData.clouds = clouds;
        frameData.sunAndExposure = sunAndExposure;
        frameData.cameraRight = cameraRight;
        frameData.cameraUp = cameraUp;
        frameData.cameraForward = cameraForward;
        frameData.viewportAndTime = viewportAndTime;
        frameData.viewportAndTime.x = float(readbackWidth);
        frameData.viewportAndTime.y = float(readbackHeight);
        graphicsQueue
            ->writeBuffer(
                BufferRange(frameBuffer),
                &frameData,
                sizeof(frameData))
            ->submit();
    }

    /** Updates and draws the unique RenderSet, then composites the screen-only Inspector. */
    void render() override
    {
        sceneSet->update();
        WebgpuSkyFrameBuffer frameBufferValue;
        frameBufferValue.color = outputColor->createView();
        frameBufferValue.color.loadOp = LoadOp::Clear;
        frameBufferValue.color.storeOp = StoreOp::Store;
        frameBufferValue.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        WebgpuSkyFrameBuffer inspectorFrameBuffer;
        inspectorFrameBuffer.color = outputColor->createView();
        inspectorFrameBuffer.color.loadOp = LoadOp::Load;
        inspectorFrameBuffer.color.storeOp = StoreOp::Store;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuSkyScene",
                frameBufferValue,
                skyPass())
            ->renderPass(
                "WebgpuSkyInspector",
                inspectorFrameBuffer,
                inspectorPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final color target for deterministic readback. */
    Texture<
        TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
        TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured deterministic readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured deterministic readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases every dedicated WebGPU sky resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeTexture(outputColor);
    }
};

#endif
