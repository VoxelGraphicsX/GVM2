#ifndef GVM_THREE_WEBGPU_COMPUTE_POINTS_HPP
#define GVM_THREE_WEBGPU_COMPUTE_POINTS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputePointsCount = 300000u;

/** Stores one triangle-expanded single-pixel point corner. */
struct WebgpuComputePointsVertex
{
    float4 corner [[Attribute0]];
};

/** Stores the orthographic viewport used by the point expansion. */
struct WebgpuComputePointsObjectData
{
    float4 viewport;
};

/** Stores one stable instance ordinal required by the Scene RenderSet. */
struct WebgpuComputePointsInstanceData
{
    float4 ordinal;
};

/** Stores the private point material controls. */
struct WebgpuComputePointsMaterialData
{
    float4 opacity;
};

/** Defines the sole Scene RenderSet for the 300,000 computed points. */
struct WebgpuComputePointsSceneRenderSet : public IRenderSet
{
    /** Declares triangle geometry plus object, instance, and material components. */
    constructor(
        BufferComponent<WebgpuComputePointsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuComputePointsObjectData> objects,
        BufferComponent<WebgpuComputePointsInstanceData> instances,
        BufferComponent<WebgpuComputePointsMaterialData> materials)
    {
    }
};

/** Stores pointer and orthographic bounds for the update Compute pass. */
struct WebgpuComputePointsControls
{
    float4 pointerAndLimit;
    float4 inspectorEnabled;
};

/** Binds computed position/velocity state and immutable frame controls. */
struct WebgpuComputePointsComputeResources final : public IBindGroup
{
    /** Declares the current public writable structured-buffer contract. */
    constructor(
        RWStructuredBuffer<float4> state [[Binding0]],
        UniformBuffer<WebgpuComputePointsControls> controls [[Binding1]])
    {
    }
};

/** Binds computed point state to the RenderSet vertex stage. */
struct WebgpuComputePointsDrawResources final : public IBindGroup
{
    /** Exposes the same state buffer read-only during drawing. */
    constructor(StructuredBuffer<float4> state [[Binding0]])
    {
    }
};

/** Binds the frame-dependent Inspector visibility flag. */
struct WebgpuComputePointsInspectorResources final : public IBindGroup
{
    /** Shares the immutable controls buffer with the screen pass. */
    constructor(
        UniformBuffer<WebgpuComputePointsControls> controls [[Binding0]])
    {
    }
};

/** Converts one linear point channel to the r185 output transfer. */
float webgpuComputePointsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Updates position and velocity with the exact r185 bounds and pointer rules. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputePointsUpdatePass final
    : public IComputeClass
{
public:
    /** Binds the one writable point-state buffer. */
    constructor(BindGroup<WebgpuComputePointsComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Advances one point and clamps or clears it at deterministic boundaries. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= WebgpuComputePointsCount) return;
        float4 state = resources->state[index];
        if (state.z == 0.0f && state.w == 0.0f)
        {
            const float particleIndex = float(index);
            const float randomAngle =
                particleIndex * 0.005f *
                6.28318530717958647692f;
            const float randomSpeed =
                particleIndex * 0.00000001f + 0.0000001f;
            state.z = sin(randomAngle) * randomSpeed;
            state.w = cos(randomAngle) * randomSpeed;
        }
        float2 position = state.xy + state.zw;
        const float2 limit = resources->controls->pointerAndLimit.zw;
        if (abs(position.x) >= limit.x) state.z = -state.z;
        if (abs(position.y) >= limit.y) state.w = -state.w;
        position = clamp(position, -limit, limit);
        const float2 pointer = resources->controls->pointerAndLimit.xy;
        if (length(pointer - position) <= 0.1f)
        {
            position = float2(0.0f);
        }
        resources->state[index] =
            float4(position, state.z, state.w);
    }
};

/** Carries point color and entity identity to the fragment stage. */
struct WebgpuComputePointsVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the final color and depth attachments. */
struct WebgpuComputePointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only attachment for the Inspector screen pass. */
struct WebgpuComputePointsColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen coordinates into the Inspector screen pass. */
struct WebgpuComputePointsScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Binds the single-sample Scene texture for the output copy pass. */
struct WebgpuComputePointsResolveResources final : public IBindGroup
{
    /** Declares the Scene texture and linear sampler. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Draws all points through one RenderSet entity and 300,000 instances. */
class WebgpuComputePointsMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and computed state buffer. */
    constructor(
        RenderSet<WebgpuComputePointsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuComputePointsDrawResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Expands one computed point into a deterministic single-pixel quad. */
    WebgpuComputePointsVertexOutput vertex(
        WebgpuComputePointsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuComputePointsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuComputePointsInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 state = resources->state[renderEntityInstanceID];
        WebgpuComputePointsVertexOutput outputValue;
        outputValue.position = float4(
            float2(state.x, -state.y) +
                inputValue.corner.xy *
                float2(2.0f / objectData.viewport.x,
                       2.0f / objectData.viewport.y),
            0.0f,
            1.0f);
        outputValue.color =
            float3(state.x + 1.0f, state.y + 1.0f, 1.0f);
        outputValue.entityID =
            renderEntityID + uint(instanceData.ordinal.x * 0.0f);
        return outputValue;
    }

    /** Writes the unlit point color used by PointsNodeMaterial. */
    WebgpuComputePointsFrameBuffer fragment(
        WebgpuComputePointsVertexOutput inputValue)
    {
        const WebgpuComputePointsMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuComputePointsFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(
                half3(
                    webgpuComputePointsLinearToSrgb(inputValue.color.x),
                    webgpuComputePointsLinearToSrgb(inputValue.color.y),
                    webgpuComputePointsLinearToSrgb(inputValue.color.z)),
                half(material.opacity.x));
        return frameBuffer;
    }
};

/** Copies the single-sample Scene target to the final host-sized attachment. */
class WebgpuComputePointsResolvePass final : public IRenderClass
{
public:
    /** Binds only the single-sample Scene color resources. */
    constructor(
        BindGroup<WebgpuComputePointsResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuComputePointsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads the one centered Scene sample without antialias emulation. */
    WebgpuComputePointsColorFrameBuffer fragment(
        WebgpuComputePointsScreenOutput inputValue)
    {
        WebgpuComputePointsColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->sceneColor->sample(
            resources->sceneSampler,
            inputValue.uv));
        return frameBuffer;
    }
};

/** Applies the native point-raster convention after the RGBA8 resolve. */
class WebgpuComputePointsRasterConventionPass final : public IRenderClass
{
public:
    /** Binds the resolved point image and its linear sampler. */
    constructor(
        BindGroup<WebgpuComputePointsResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuComputePointsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Copies the single-sample point image without antialias emulation. */
    WebgpuComputePointsColorFrameBuffer fragment(
        WebgpuComputePointsScreenOutput inputValue)
    {
        WebgpuComputePointsColorFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(
                resources->sceneColor->sample(
                    resources->sceneSampler,
                    inputValue.uv));
        return frameBuffer;
    }
};

/** Composites the deterministic WebGPU Inspector bar after Scene drawing. */
class WebgpuComputePointsInspectorPass final : public IRenderClass
{
public:
    /** Configures source-alpha blending for the screen-only overlay. */
    constructor(
        BindGroup<WebgpuComputePointsInspectorResources> resources [[Slot0]])
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
    /** Emits one fullscreen triangle without Scene geometry. */
    WebgpuComputePointsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputePointsScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked rounded Inspector bar and shadow. */
    WebgpuComputePointsColorFrameBuffer fragment(
        WebgpuComputePointsScreenOutput inputValue)
    {
        if (resources->controls->inspectorEnabled.x < 0.5f)
        {
            discard_fragment();
        }
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
            if (shadowAlpha < 0.004f) discard_fragment();
            WebgpuComputePointsColorFrameBuffer shadowFrameBuffer;
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
        WebgpuComputePointsColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(alpha));
        return frameBuffer;
    }
};

/** Owns the dedicated Compute state and sole Scene RenderSet. */
class WebgpuComputePointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuComputePointsSceneRenderSet> sceneSet;
    Buffer<float4, BufferUsage<Storage, CopyDst>> stateBuffer;
    Buffer<WebgpuComputePointsControls, BufferUsage<Uniform, CopyDst>>
        controlsBuffer;
    BindGroup<WebgpuComputePointsComputeResources> computeResources;
    BindGroup<WebgpuComputePointsDrawResources> drawResources;
    BindGroup<WebgpuComputePointsInspectorResources> inspectorResources;
    ComputeClass<WebgpuComputePointsUpdatePass> computePass;
    RenderClass<WebgpuComputePointsMainPass> mainPass;
    Sampler resolveSampler;
    BindGroup<WebgpuComputePointsResolveResources> resolveResources;
    RenderClass<WebgpuComputePointsResolvePass> resolvePass;
    BindGroup<WebgpuComputePointsResolveResources>
        rasterConventionResources;
    RenderClass<WebgpuComputePointsRasterConventionPass>
        rasterConventionPass;
    RenderClass<WebgpuComputePointsInspectorPass> inspectorPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> resolvedTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the state buffers, Compute pass, RenderSet, and draw pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuComputePointsSceneRenderSet>();
        stateBuffer = device->createBuffer(
            "WebgpuComputePointsState",
            WebgpuComputePointsCount);
        controlsBuffer = device->createBuffer(
            "WebgpuComputePointsControls",
            1u);
        computeResources =
            device->createBindGroup<WebgpuComputePointsComputeResources>(
                stateBuffer,
                controlsBuffer);
        drawResources =
            device->createBindGroup<WebgpuComputePointsDrawResources>(
                stateBuffer);
        inspectorResources =
            device->createBindGroup<WebgpuComputePointsInspectorResources>(
                controlsBuffer);
        computePass =
            device->createComputeClass<WebgpuComputePointsUpdatePass>(
                computeResources);
        mainPass =
            device->createRenderClass<WebgpuComputePointsMainPass>(
                sceneSet,
                drawResources);
        inspectorPass =
            device->createRenderClass<WebgpuComputePointsInspectorPass>(
                inspectorResources);
        resolveSampler = device->createSampler({
            .label = "WebgpuComputePointsResolveSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates host-sized final color and depth attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebgpuComputePointsSceneRGBA8",
            width,
            height,
            1u);
        resolvedTexture = device->createTexture(
            "WebgpuComputePointsResolvedRGBA8",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebgpuComputePointsRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebgpuComputePointsDepth32",
            width,
            height,
            1u);
        resolveResources =
            device->createBindGroup<WebgpuComputePointsResolveResources>(
                sceneTexture->createView(),
                resolveSampler);
        resolvePass =
            device->createRenderClass<WebgpuComputePointsResolvePass>(
                resolveResources);
        rasterConventionResources =
            device->createBindGroup<WebgpuComputePointsResolveResources>(
                resolvedTexture->createView(),
                resolveSampler);
        rasterConventionPass =
            device->createRenderClass<
                WebgpuComputePointsRasterConventionPass>(
                rasterConventionResources);
    }

    /** Uploads immutable scenario controls before the first Compute dispatch. */
    void configureControls(float4 pointerAndLimit, float inspectorEnabled)
    {
        WebgpuComputePointsControls controls;
        controls.pointerAndLimit = pointerAndLimit;
        controls.inspectorEnabled =
            float4(inspectorEnabled, 0.0f, 0.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(controlsBuffer),
                &controls,
                sizeof(controls))
            ->submit();
    }

    /** Advances all points and draws the sole RenderSet entity. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebgpuComputePointsFrameBuffer frameBuffer;
        frameBuffer.color = sceneTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebgpuComputePointsColorFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = resolvedTexture->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuComputePointsColorFrameBuffer rasterConventionFrameBuffer;
        rasterConventionFrameBuffer.color =
            outputTexture->createView();
        rasterConventionFrameBuffer.color.loadOp = LoadOp::Clear;
        rasterConventionFrameBuffer.color.storeOp = StoreOp::Store;
        rasterConventionFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->computePass(
                "WebgpuComputePointsUpdate",
                computePass(WebgpuComputePointsCount, 1u, 1u))
            ->renderPass(
                "WebgpuComputePointsScene",
                frameBuffer,
                mainPass())
            ->renderPass(
                "WebgpuComputePointsResolve",
                outputFrameBuffer,
                resolvePass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuComputePointsRasterConvention",
                rasterConventionFrameBuffer,
                rasterConventionPass(3u, 1u, 0u, 0u))
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

    /** Releases the sole Scene RenderSet and its attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
