#ifndef GVM_THREE_WEBGPU_SKINNING_POINTS_HPP
#define GVM_THREE_WEBGPU_SKINNING_POINTS_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuSkinningPointsCount = 16340u;

/** Stores one corner of the triangle-expanded point billboard. */
struct WebgpuSkinningPointsVertex
{
    float4 cornerAndUv [[Attribute0]];
};

/** Stores the frozen camera transform and output dimensions. */
struct WebgpuSkinningPointsObjectData
{
    float4 viewProjectionColumn0;
    float4 viewProjectionColumn1;
    float4 viewProjectionColumn2;
    float4 viewProjectionColumn3;
    float4 viewport;
};

/** Stores one stable source-vertex ordinal for each point instance. */
struct WebgpuSkinningPointsInstanceData
{
    float4 ordinal;
};

/** Stores the two r185 speed-gradient colors. */
struct WebgpuSkinningPointsMaterialData
{
    float4 slowColor;
    float4 fastColor;
};

/** Defines the only Scene RenderSet used by the expanded point cloud. */
struct WebgpuSkinningPointsSceneRenderSet : public IRenderSet
{
    /** Declares quad geometry and the per-object, instance, and material data. */
    constructor(
        BufferComponent<WebgpuSkinningPointsVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuSkinningPointsObjectData> objects,
        BufferComponent<WebgpuSkinningPointsInstanceData> instances,
        BufferComponent<WebgpuSkinningPointsMaterialData> materials)
    {
    }
};

/** Binds CPU-evaluated skin targets and GPU-owned point state. */
struct WebgpuSkinningPointsComputeResources final : public IBindGroup
{
    /** Declares current targets plus writable position and speed state. */
    constructor(
        StructuredBuffer<float4> targets [[Binding0]],
        RWStructuredBuffer<float4> positions [[Binding1]],
        RWStructuredBuffer<float4> speeds [[Binding2]])
    {
    }
};

/** Exposes the current target and prior point state to the draw pass. */
struct WebgpuSkinningPointsDrawResources final : public IBindGroup
{
    /** Declares current targets and the previous-frame point positions. */
    constructor(
        StructuredBuffer<float4> targets [[Binding0]],
        StructuredBuffer<float4> positions [[Binding1]])
    {
    }
};

/** Updates every visible point from the current skinned target. */
class [[LocalWorkGroupSize(64, 1, 1)]]
    WebgpuSkinningPointsUpdatePass final : public IComputeClass
{
public:
    /** Binds the private point-state resources. */
    constructor(
        BindGroup<WebgpuSkinningPointsComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Computes one point's world-space displacement and new position. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint pointIndex = dispatchThreadID.x;
        if (pointIndex >= WebgpuSkinningPointsCount) return;
        const float4 target = resources->targets[pointIndex];
        const float4 previous = resources->positions[pointIndex];
        resources->speeds[pointIndex] =
            float4(target.xyz - previous.xyz, 0.0f);
        resources->positions[pointIndex] = target;
    }
};

/** Carries point-local coordinates, speed, and entity identity. */
struct WebgpuSkinningPointsVertexOutput
{
    float4 position [[Position]];
    float2 pointCoordinate [[Attribute0]];
    float3 speed [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample point-cloud color and depth attachments. */
struct WebgpuSkinningPointsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel to display-encoded sRGB. */
float webgpuSkinningPointsLinearToSrgb(float value)
{
    const float bounded = clamp(value, 0.0f, 1.0f);
    return bounded <= 0.0031308f
        ? bounded * 12.92f
        : pow(bounded, 0.4166666666666667f) * 1.055f - 0.055f;
}

/** Draws 16,340 triangle-expanded points through the unique Scene Set. */
class WebgpuSkinningPointsMainPass final : public IRenderClass
{
public:
    /** Binds exactly one RenderSet and the GPU-computed point state. */
    constructor(
        RenderSet<WebgpuSkinningPointsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuSkinningPointsDrawResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one state element into a non-attenuated screen-space quad. */
    WebgpuSkinningPointsVertexOutput vertex(
        WebgpuSkinningPointsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuSkinningPointsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuSkinningPointsInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const uint pointIndex = uint(instanceData.ordinal.x);
        const float4 worldPosition = resources->targets[pointIndex];
        const float3 speed =
            worldPosition.xyz - resources->positions[pointIndex].xyz;
        float4 clipPosition =
            objectData.viewProjectionColumn0 * worldPosition.x +
            objectData.viewProjectionColumn1 * worldPosition.y +
            objectData.viewProjectionColumn2 * worldPosition.z +
            objectData.viewProjectionColumn3;
        clipPosition.y = -clipPosition.y;
        const float pointSize =
            min(exp(length(speed)), 5.0f) * 5.0f + 1.0f;
        clipPosition.xy += inputValue.cornerAndUv.xy * pointSize *
            float2(2.0f / objectData.viewport.x,
                   2.0f / objectData.viewport.y) * clipPosition.w;
        WebgpuSkinningPointsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.pointCoordinate = inputValue.cornerAndUv.zw;
        outputValue.speed = speed;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies circle coverage, alpha test, and the speed color gradient. */
    WebgpuSkinningPointsFrameBuffer fragment(
        WebgpuSkinningPointsVertexOutput inputValue)
    {
        const float2 centered =
            inputValue.pointCoordinate * 2.0f - float2(1.0f);
        if (dot(centered, centered) > 1.0f) discard_fragment();
        const WebgpuSkinningPointsMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 blendWeight = inputValue.speed * 0.6f;
        const float3 linearColor =
            material.slowColor.xyz * (float3(1.0f) - blendWeight) +
            material.fastColor.xyz * blendWeight;
        WebgpuSkinningPointsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuSkinningPointsLinearToSrgb(linearColor.x)),
            half(webgpuSkinningPointsLinearToSrgb(linearColor.y)),
            half(webgpuSkinningPointsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the point Compute resources and the only Scene RenderSet. */
class WebgpuSkinningPointsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSkinningPointsSceneRenderSet> sceneSet;
    Buffer<float4, BufferUsage<Storage, CopyDst>> targetBuffer;
    Buffer<float4, BufferUsage<Storage, CopyDst>> positionBuffer;
    Buffer<float4, BufferUsage<Storage, CopyDst>> speedBuffer;
    BindGroup<WebgpuSkinningPointsComputeResources> computeResources;
    BindGroup<WebgpuSkinningPointsDrawResources> drawResources;
    ComputeClass<WebgpuSkinningPointsUpdatePass> updatePass;
    RenderClass<WebgpuSkinningPointsMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates point-state buffers, the Compute pass, and unique Scene Set. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuSkinningPointsSceneRenderSet>();
        targetBuffer = device->createBuffer(
            "WebgpuSkinningPointsTargets", WebgpuSkinningPointsCount);
        positionBuffer = device->createBuffer(
            "WebgpuSkinningPointsPositions", WebgpuSkinningPointsCount);
        speedBuffer = device->createBuffer(
            "WebgpuSkinningPointsSpeeds", WebgpuSkinningPointsCount);
        computeResources =
            device->createBindGroup<WebgpuSkinningPointsComputeResources>(
                targetBuffer, positionBuffer, speedBuffer);
        drawResources =
            device->createBindGroup<WebgpuSkinningPointsDrawResources>(
                targetBuffer, positionBuffer);
        updatePass =
            device->createComputeClass<WebgpuSkinningPointsUpdatePass>(
                computeResources);
        mainPass =
            device->createRenderClass<WebgpuSkinningPointsMainPass>(
                sceneSet, drawResources);
    }

    /** Allocates the fixed single-sample RGBA8 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebgpuSkinningPointsOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuSkinningPointsDepth", width, height, 1u);
    }

    /** Returns the target buffer used by deterministic host uploads. */
    Buffer<float4, BufferUsage<Storage, CopyDst>>
    getTargetBufferHandle() const
    {
        return targetBuffer;
    }

    /** Returns the position buffer initialized before the first dispatch. */
    Buffer<float4, BufferUsage<Storage, CopyDst>>
    getPositionBufferHandle() const
    {
        return positionBuffer;
    }

    /** Returns the speed buffer initialized before the first dispatch. */
    Buffer<float4, BufferUsage<Storage, CopyDst>>
    getSpeedBufferHandle() const
    {
        return speedBuffer;
    }

    /** Draws current targets before Compute advances persistent point state. */
    void render() override
    {
        sceneSet->update();
        WebgpuSkinningPointsFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            17.0 / 255.0, 17.0 / 255.0, 17.0 / 255.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Discard;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuSkinningPointsMain", frameBuffer, mainPass())
            ->computePass(
                "WebgpuSkinningPointsUpdate",
                updatePass(WebgpuSkinningPointsCount, 1u, 1u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final texture for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return readbackWidth; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return readbackHeight; }

    /** Releases the Scene Set and all private GPU resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
        device->freeBuffer(targetBuffer);
        device->freeBuffer(positionBuffer);
        device->freeBuffer(speedBuffer);
    }
};

#endif
