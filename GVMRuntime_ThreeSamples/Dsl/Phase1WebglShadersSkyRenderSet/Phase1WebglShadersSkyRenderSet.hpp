#ifndef GVM_THREE_WEBGL_SHADERS_SKY_RENDER_SET_HPP
#define GVM_THREE_WEBGL_SHADERS_SKY_RENDER_SET_HPP

#include "Phase1SkyShared.hpp"

/** Defines the unique RenderSet for the WebGL sky and expanded GridHelper Scene. */
struct WebglShadersSkySceneRenderSet : public IRenderSet
{
    /** Declares consolidated sky/grid geometry and mandatory Scene components. */
    constructor(
        BufferComponent<Phase1SkyVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<Phase1SkyObjectData> objects,
        BufferComponent<Phase1SkyInstanceData> instances,
        BufferComponent<Phase1SkyMaterialData> materials)
    {
    }
};

/** Binds the deterministic WebGL sky atmospheric and camera state. */
struct WebglShadersSkyFrameResources final : public IBindGroup
{
    /** Declares the immutable frame resource layout. */
    constructor(
        UniformBuffer<Phase1SkyFrameData> frame [[Binding0]])
    {
    }
};

/** Defines the final deterministic WebGL sky target. */
struct WebglShadersSkyFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws the sky and expanded GridHelper through one Scene RenderSet. */
class WebglShadersSkyScenePass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and enables GridHelper alpha composition. */
    constructor(
        RenderSet<WebglShadersSkySceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglShadersSkyFrameResources> resources [[Slot1]])
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
        setDepthWriteEnabled(false);
    }

private:
    /** Emits consolidated sky/grid clip-space geometry and preserves entity identity. */
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

    /** Evaluates either the complete atmosphere or the expanded GridHelper phase. */
    WebglShadersSkyFrameBuffer fragment(
        Phase1SkyVertexOutput inputValue)
    {
        const Phase1SkyObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        WebglShadersSkyFrameBuffer frameBuffer;
        if (objectData.phase.x > 0.5f)
        {
            frameBuffer.color =
                half4(
                    half3(float3(1.0f)),
                    half(1.0f));
            return frameBuffer;
        }
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
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated WebGL sky RenderSet and single Scene pass. */
class Phase1WebglShadersSkyRenderSetRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]]
    RenderSet<WebglShadersSkySceneRenderSet> sceneSet;
    Buffer<Phase1SkyFrameData, BufferUsage<Uniform, CopyDst>>
        frameBuffer;
    BindGroup<WebglShadersSkyFrameResources> frameResources;
    RenderClass<WebglShadersSkyScenePass> skyPass;
    Texture<
        TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
        TextureDimension::e2D>
        outputColor;
    Phase1SkyFrameData frameData;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the unique Scene RenderSet and dedicated WebGL sky resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglShadersSkySceneRenderSet>();
        frameBuffer =
            device->createBuffer(
                "WebglShadersSkyFrameData",
                1u);
        frameResources =
            device->createBindGroup<
                WebglShadersSkyFrameResources>(
                frameBuffer);
        skyPass =
            device->createRenderClass<
                WebglShadersSkyScenePass>(
                sceneSet,
                frameResources);
    }

    /** Allocates the fixed-size final color target requested by the Three host. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor =
            device->createTexture(
                "WebglShadersSkyOutput",
                width,
                height,
                1u);
    }

    /** Uploads one canonical WebGL sky scenario without compiler-specific branches. */
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

    /** Updates and draws the unique Scene RenderSet through indexed indirect metadata. */
    void render() override
    {
        sceneSet->update();
        WebglShadersSkyFrameBuffer frameBufferValue;
        frameBufferValue.color = outputColor->createView();
        frameBufferValue.color.loadOp = LoadOp::Clear;
        frameBufferValue.color.storeOp = StoreOp::Store;
        frameBufferValue.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglShadersSkyScene",
                frameBufferValue,
                skyPass())
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

    /** Releases every dedicated WebGL sky resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(frameBuffer);
        device->freeTexture(outputColor);
    }
};

#endif
