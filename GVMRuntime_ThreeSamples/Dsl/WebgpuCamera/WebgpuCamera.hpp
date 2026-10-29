#pragma once

#include "WebgpuCameraData.hpp"

#include "UGL.h"

using namespace UGL;

/** Stores both CPU-resolved model-view matrices and per-pass visibility flags. */
struct WebgpuCameraObjectData
{
    float4x4 leftModelView;
    float4x4 rightModelView;
    float4 visibility;
};

/** Stores the mandatory non-instanced component record. */
struct WebgpuCameraInstanceData
{
    float4 reserved;
};

/** Stores the semantic material phase for one camera-scene entity. */
struct WebgpuCameraMaterialData
{
    float4 phase;
};

/** Defines the unique six-entity RenderSet owned by the logical Scene. */
struct WebgpuCameraSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and mandatory entity components. */
    constructor(
        BufferComponent<WebgpuCameraVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuCameraObjectData> objects,
        BufferComponent<WebgpuCameraInstanceData> instances,
        BufferComponent<WebgpuCameraMaterialData> materials)
    {
    }
};

/** Stores one half-frame camera transform and viewport convention. */
struct WebgpuCameraPassUniforms
{
    float4x4 viewProjection;
    float4 viewportAndPass;
};

/** Binds the deterministic camera state for one Scene submission. */
struct WebgpuCameraPassResources final : public IBindGroup
{
    /** Declares the immutable target-frame camera uniform. */
    constructor(UniformBuffer<WebgpuCameraPassUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries discrete r185 basic-material color and primitive phase. */
struct WebgpuCameraRasterOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float kind [[Attribute1]];
    float visible [[Attribute2]];
    float4 lineScreen [[Attribute3]];
};

/** Defines the shared single-sample output and depth attachments. */
struct WebgpuCameraFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Projects and expands one semantic line, point, or clear-domain vertex. */
WebgpuCameraRasterOutput webgpuCameraProjectVertex(
    IN RenderSet<WebgpuCameraSceneRenderSet> sceneSet,
    IN BindGroup<WebgpuCameraPassResources> resources,
    WebgpuCameraVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebgpuCameraObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebgpuCameraInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebgpuCameraMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const float kind = inputValue.colorAndKind.w;
    const float rightPass = resources->uniforms->viewportAndPass.z;
    WebgpuCameraRasterOutput outputValue;
    outputValue.color = inputValue.colorAndKind.xyz +
        (instanceData.reserved.xyz + materialData.phase.xyz) * 0.0f;
    outputValue.kind = kind;
    outputValue.visible = rightPass > 0.5f
        ? objectData.visibility.y
        : objectData.visibility.x;
    outputValue.lineScreen = float4(0.0f);

    if (kind > 1.5f)
    {
        const float2 corner = inputValue.expansion.xy;
        outputValue.position = float4(
            corner.x * 0.5f + resources->uniforms->viewportAndPass.x,
            corner.y,
            0.999999f,
            1.0f);
        outputValue.color = rightPass > 0.5f
            ? float3(17.0f / 255.0f)
            : float3(0.0f);
        outputValue.visible = 1.0f;
        return outputValue;
    }

    float4 view0;
    float4 view1;
    if (rightPass > 0.5f)
    {
        view0 = mul(objectData.rightModelView, inputValue.endpoint0);
        view1 = mul(objectData.rightModelView, inputValue.endpoint1);
    }
    else
    {
        view0 = mul(objectData.leftModelView, inputValue.endpoint0);
        view1 = mul(objectData.leftModelView, inputValue.endpoint1);
    }
    float4 clip0;
    float4 clip1;
    if (rightPass < 0.5f && objectData.visibility.z > 0.5f)
    {
        clip0 = inputValue.leftClipEndpoint0;
        clip1 = inputValue.leftClipEndpoint1;
    }
    else
    {
        clip0 = mul(resources->uniforms->viewProjection, view0);
        clip1 = mul(resources->uniforms->viewProjection, view1);
    }
    if (kind < 0.5f)
    {
        const float nearClipEpsilon =
            resources->uniforms->viewportAndPass.w > 0.5f
            ? 0.0005f
            : 0.0f;
        if (clip0.z < nearClipEpsilon && clip1.z < nearClipEpsilon)
        {
            outputValue.visible = 0.0f;
        }
        else if (clip0.z < nearClipEpsilon)
        {
            clip0 = lerp(
                clip0, clip1,
                (nearClipEpsilon - clip0.z) /
                    (clip1.z - clip0.z));
        }
        else if (clip1.z < nearClipEpsilon)
        {
            clip1 = lerp(
                clip1, clip0,
                (nearClipEpsilon - clip1.z) /
                    (clip0.z - clip1.z));
        }
        const float farDistance0 = clip0.w - clip0.z;
        const float farDistance1 = clip1.w - clip1.z;
        if (farDistance0 < 0.0f && farDistance1 < 0.0f)
        {
            outputValue.visible = 0.0f;
        }
        else if (farDistance0 < 0.0f)
        {
            clip0 = lerp(
                clip0, clip1,
                -farDistance0 / (farDistance1 - farDistance0));
        }
        else if (farDistance1 < 0.0f)
        {
            clip1 = lerp(
                clip1, clip0,
                -farDistance1 / ((clip0.w - clip0.z) - farDistance1));
        }
    }
    const float2 fullNdc0 = float2(
        clip0.x / clip0.w * 0.5f + resources->uniforms->viewportAndPass.x,
        clip0.y / clip0.w);
    const float2 fullNdc1 = float2(
        clip1.x / clip1.w * 0.5f + resources->uniforms->viewportAndPass.x,
        clip1.y / clip1.w);
    const float rawScreenX0 = (fullNdc0.x * 0.5f + 0.5f) * 800.0f;
    const float rawScreenY0 = (fullNdc0.y * 0.5f + 0.5f) * 500.0f;
    const float rawScreenX1 = (fullNdc1.x * 0.5f + 0.5f) * 800.0f;
    const float rawScreenY1 = (fullNdc1.y * 0.5f + 0.5f) * 500.0f;
    const float screenX0 = rawScreenX0;
    const float screenY0 = rawScreenY0;
    const float screenX1 = rawScreenX1;
    const float screenY1 = rawScreenY1;
    outputValue.lineScreen = float4(
        screenX0,
        screenY0,
        screenX1,
        screenY1);
    float4 clipPosition = inputValue.expansion.x < 0.5f ? clip0 : clip1;
    clipPosition.x = clipPosition.x * 0.5f +
        resources->uniforms->viewportAndPass.x * clipPosition.w;

    if (kind > 0.5f && kind < 1.5f)
    {
        if (clip0.x < -clip0.w || clip0.x > clip0.w ||
            clip0.y < -clip0.w || clip0.y > clip0.w ||
            clip0.z < 0.0f || clip0.z > clip0.w)
        {
            outputValue.visible = 0.0f;
        }
        clipPosition.x += inputValue.expansion.y * clipPosition.w / 800.0f;
        clipPosition.y += inputValue.expansion.z * clipPosition.w / 500.0f;
        outputValue.position = clipPosition;
        return outputValue;
    }
    outputValue.position = clipPosition;
    return outputValue;
}

/** Draws the active camera into the left half using the Scene's sole Set. */
class WebgpuCameraActiveCameraPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and left-camera state. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex through the active camera. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits one discrete line, point, or background color. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x >= 400.0f)
            discard_fragment();
        if (inputValue.kind < 1.5f || inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws native one-pixel line entities for the active camera. */
class WebgpuCameraActiveLinePass final : public IRenderClass
{
public:
    /** Binds the unique Set and selects native LineList rasterization. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex without triangle expansion. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits only line entities; non-line Set entities are rejected. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x >= 400.0f ||
            inputValue.kind >= 0.5f || inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws triangle-expanded one-pixel point entities for the active camera. */
class WebgpuCameraActivePointPass final : public IRenderClass
{
public:
    /** Binds the unique Set and selects the normalized triangle topology. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one point with deterministic screen-space quad expansion. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits only point entities; background and lines are rejected. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x >= 400.0f ||
            inputValue.kind < 0.5f || inputValue.kind >= 1.5f ||
            inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the observer camera into the right half using the same Scene Set. */
class WebgpuCameraObserverCameraPass final : public IRenderClass
{
public:
    /** Binds the same unique Scene Set and observer-camera state. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex through the observer camera. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits one discrete line, point, or background color. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x < 400.0f)
            discard_fragment();
        if (inputValue.kind < 1.5f || inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws native one-pixel line entities for the observer camera. */
class WebgpuCameraObserverLinePass final : public IRenderClass
{
public:
    /** Binds the unique Set and selects native LineList rasterization. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex without triangle expansion. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits only line entities; non-line Set entities are rejected. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x < 400.0f ||
            inputValue.kind >= 0.5f || inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws triangle-expanded one-pixel point entities for the observer camera. */
class WebgpuCameraObserverPointPass final : public IRenderClass
{
public:
    /** Binds the unique Set and selects the normalized triangle topology. */
    constructor(
        RenderSet<WebgpuCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one point with deterministic screen-space quad expansion. */
    WebgpuCameraRasterOutput vertex(
        WebgpuCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webgpuCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits only point entities; background and lines are rejected. */
    WebgpuCameraFrameBuffer fragment(WebgpuCameraRasterOutput inputValue)
    {
        if (inputValue.position.x < 400.0f ||
            inputValue.kind < 0.5f || inputValue.kind >= 1.5f ||
            inputValue.visible < 0.5f)
            discard_fragment();
        WebgpuCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated two-view Scene pipeline with one ordinary sample. */
class WebgpuCameraRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuCameraSceneRenderSet> sceneSet;
    Buffer<WebgpuCameraPassUniforms, BufferUsage<Uniform, CopyDst>> leftUniforms;
    Buffer<WebgpuCameraPassUniforms, BufferUsage<Uniform, CopyDst>> rightUniforms;
    BindGroup<WebgpuCameraPassResources> leftResources;
    BindGroup<WebgpuCameraPassResources> rightResources;
    RenderClass<WebgpuCameraActiveCameraPass> leftTrianglePass;
    RenderClass<WebgpuCameraActiveLinePass> leftLinePass;
    RenderClass<WebgpuCameraActivePointPass> leftPointPass;
    RenderClass<WebgpuCameraObserverCameraPass> rightTrianglePass;
    RenderClass<WebgpuCameraObserverLinePass> rightLinePass;
    RenderClass<WebgpuCameraObserverPointPass> rightPointPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and two immutable pass-uniform buffers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuCameraSceneRenderSet>();
        leftUniforms = device->createBuffer("WebgpuCameraLeftUniforms", 1u);
        rightUniforms = device->createBuffer("WebgpuCameraRightUniforms", 1u);
    }

    /** Allocates the ordinary single-sample final color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebgpuCameraOutput", width, height, 1u);
        sceneDepth = device->createTexture("WebgpuCameraDepth", width, height, 1u);
    }

    /** Uploads both target-frame camera states and creates the two Scene passes. */
    void configureCameras(
        float4x4 leftViewProjection,
        float4x4 rightViewProjection,
        float nearClipEpsilonMode)
    {
        WebgpuCameraPassUniforms leftState;
        leftState.viewProjection = leftViewProjection;
        leftState.viewportAndPass = float4(
            -0.5f, 0.5f, 0.0f,
            leftViewProjection[3][3] < 0.5f
                ? nearClipEpsilonMode
                : 0.0f);
        WebgpuCameraPassUniforms rightState;
        rightState.viewProjection = rightViewProjection;
        rightState.viewportAndPass = float4(0.5f, 0.5f, 1.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(BufferRange(leftUniforms), &leftState, sizeof(leftState))
            ->writeBuffer(BufferRange(rightUniforms), &rightState, sizeof(rightState))
            ->submit();
        leftResources = device->createBindGroup<WebgpuCameraPassResources>(leftUniforms);
        rightResources = device->createBindGroup<WebgpuCameraPassResources>(rightUniforms);
        leftTrianglePass = device->createRenderClass<WebgpuCameraActiveCameraPass>(
            sceneSet, leftResources);
        leftLinePass = device->createRenderClass<WebgpuCameraActiveLinePass>(
            sceneSet, leftResources);
        leftPointPass = device->createRenderClass<WebgpuCameraActivePointPass>(
            sceneSet, leftResources);
        rightTrianglePass = device->createRenderClass<WebgpuCameraObserverCameraPass>(
            sceneSet, rightResources);
        rightLinePass = device->createRenderClass<WebgpuCameraObserverLinePass>(
            sceneSet, rightResources);
        rightPointPass = device->createRenderClass<WebgpuCameraObserverPointPass>(
            sceneSet, rightResources);
    }

    /** Submits both Scene passes against the same unique RenderSet. */
    void render() override
    {
        sceneSet->update();
        WebgpuCameraFrameBuffer leftClearFrame;
        leftClearFrame.color = outputColor->createView();
        leftClearFrame.color.loadOp = LoadOp::Clear;
        leftClearFrame.color.storeOp = StoreOp::Store;
        leftClearFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        leftClearFrame.depth = sceneDepth->createView();
        leftClearFrame.depth.depthLoadOp = LoadOp::Clear;
        leftClearFrame.depth.depthStoreOp = StoreOp::Store;
        leftClearFrame.depth.depthClearValue = 1.0f;
        WebgpuCameraFrameBuffer leftLineFrame;
        leftLineFrame.color = outputColor->createView();
        leftLineFrame.color.loadOp = LoadOp::Load;
        leftLineFrame.color.storeOp = StoreOp::Store;
        leftLineFrame.depth = sceneDepth->createView();
        leftLineFrame.depth.depthLoadOp = LoadOp::Load;
        leftLineFrame.depth.depthStoreOp = StoreOp::Store;
        WebgpuCameraFrameBuffer leftPointFrame;
        leftPointFrame.color = outputColor->createView();
        leftPointFrame.color.loadOp = LoadOp::Load;
        leftPointFrame.color.storeOp = StoreOp::Store;
        leftPointFrame.depth = sceneDepth->createView();
        leftPointFrame.depth.depthLoadOp = LoadOp::Load;
        leftPointFrame.depth.depthStoreOp = StoreOp::Store;
        WebgpuCameraFrameBuffer rightTriangleFrame;
        rightTriangleFrame.color = outputColor->createView();
        rightTriangleFrame.color.loadOp = LoadOp::Load;
        rightTriangleFrame.color.storeOp = StoreOp::Store;
        rightTriangleFrame.depth = sceneDepth->createView();
        rightTriangleFrame.depth.depthLoadOp = LoadOp::Load;
        rightTriangleFrame.depth.depthStoreOp = StoreOp::Store;
        WebgpuCameraFrameBuffer rightLineFrame;
        rightLineFrame.color = outputColor->createView();
        rightLineFrame.color.loadOp = LoadOp::Load;
        rightLineFrame.color.storeOp = StoreOp::Store;
        rightLineFrame.depth = sceneDepth->createView();
        rightLineFrame.depth.depthLoadOp = LoadOp::Load;
        rightLineFrame.depth.depthStoreOp = StoreOp::Store;
        WebgpuCameraFrameBuffer rightPointFrame;
        rightPointFrame.color = outputColor->createView();
        rightPointFrame.color.loadOp = LoadOp::Load;
        rightPointFrame.color.storeOp = StoreOp::Store;
        rightPointFrame.depth = sceneDepth->createView();
        rightPointFrame.depth.depthLoadOp = LoadOp::Load;
        rightPointFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuCameraActiveCamera", leftClearFrame, leftTrianglePass())
            ->renderPass("WebgpuCameraActiveLine", leftLineFrame, leftLinePass())
            ->renderPass("WebgpuCameraActivePoint", leftPointFrame, leftPointPass())
            ->renderPass("WebgpuCameraObserverCamera", rightTriangleFrame, rightTrianglePass())
            ->renderPass("WebgpuCameraObserverLine", rightLineFrame, rightLinePass())
            ->renderPass("WebgpuCameraObserverPoint", rightPointFrame, rightPointPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and dedicated pass resources. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(leftUniforms);
        device->freeBuffer(rightUniforms);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};
