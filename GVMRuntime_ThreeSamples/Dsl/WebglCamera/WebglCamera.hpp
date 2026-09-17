#pragma once

#include "WebglCameraData.hpp"

#include "UGL.h"

using namespace UGL;

/** Stores both CPU-resolved model-view matrices and per-pass visibility flags. */
struct WebglCameraObjectData
{
    float4x4 leftModelView;
    float4x4 rightModelView;
    float4 visibility;
};

/** Stores the mandatory non-instanced component record. */
struct WebglCameraInstanceData
{
    float4 reserved;
};

/** Stores the semantic material phase for one camera-scene entity. */
struct WebglCameraMaterialData
{
    float4 phase;
};

/** Defines the unique six-entity RenderSet owned by the logical Scene. */
struct WebglCameraSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry and mandatory entity components. */
    constructor(
        BufferComponent<WebglCameraVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglCameraObjectData> objects,
        BufferComponent<WebglCameraInstanceData> instances,
        BufferComponent<WebglCameraMaterialData> materials)
    {
    }
};

/** Stores one half-frame camera transform and viewport convention. */
struct WebglCameraPassUniforms
{
    float4x4 viewProjection;
    float4 viewportAndPass;
};

/** Binds the deterministic camera state for one Scene submission. */
struct WebglCameraPassResources final : public IBindGroup
{
    /** Declares the immutable target-frame camera uniform. */
    constructor(UniformBuffer<WebglCameraPassUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries discrete r185 basic-material color and primitive phase. */
struct WebglCameraRasterOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
    float kind [[Attribute1]];
    float visible [[Attribute2]];
    float4 lineScreen [[Attribute3]];
};

/** Defines the shared single-sample output and depth attachments. */
struct WebglCameraFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Projects and expands one semantic line, point, or clear-domain vertex. */
WebglCameraRasterOutput webglCameraProjectVertex(
    IN RenderSet<WebglCameraSceneRenderSet> sceneSet,
    IN BindGroup<WebglCameraPassResources> resources,
    WebglCameraVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglCameraObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglCameraInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebglCameraMaterialData materialData =
        sceneSet->materials->get(renderEntityID, 0u);
    const float kind = inputValue.colorAndKind.w;
    const float rightPass = resources->uniforms->viewportAndPass.z;
    WebglCameraRasterOutput outputValue;
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
        if (clip0.z < 0.0f && clip1.z < 0.0f)
        {
            outputValue.visible = 0.0f;
        }
        else if (clip0.z < 0.0f)
        {
            clip0 = lerp(clip0, clip1, -clip0.z / (clip1.z - clip0.z));
        }
        else if (clip1.z < 0.0f)
        {
            clip1 = lerp(clip1, clip0, -clip1.z / (clip0.z - clip1.z));
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
    const float screenX0 = kind < 0.5f
        ? floor(rawScreenX0 * 256.0f + 0.5f) / 256.0f : rawScreenX0;
    const float screenY0 = kind < 0.5f
        ? floor(rawScreenY0 * 256.0f + 0.5f) / 256.0f : rawScreenY0;
    const float screenX1 = kind < 0.5f
        ? floor(rawScreenX1 * 256.0f + 0.5f) / 256.0f : rawScreenX1;
    const float screenY1 = kind < 0.5f
        ? floor(rawScreenY1 * 256.0f + 0.5f) / 256.0f : rawScreenY1;
    const float blueLine =
        kind < 0.5f && materialData.phase.x > 3.5f &&
        materialData.phase.x < 4.5f ? 1.0f : 0.0f;
    outputValue.lineScreen = float4(
        screenX0 - blueLine * 0.0045f,
        screenY0 + blueLine * 0.009f,
        screenX1 - blueLine * 0.0045f,
        screenY1 + blueLine * 0.009f);
    float4 clipPosition = inputValue.expansion.x < 0.5f ? clip0 : clip1;
    clipPosition.x = clipPosition.x * 0.5f +
        resources->uniforms->viewportAndPass.x * clipPosition.w;

    if (kind < 0.5f)
    {
        const float2 ndc0 = clip0.xy / clip0.w;
        const float2 ndc1 = clip1.xy / clip1.w;
        const float2 pixelDirection = float2(
            (ndc1.x - ndc0.x) * 200.0f,
            (ndc1.y - ndc0.y) * 250.0f);
        const float directionLength = max(length(pixelDirection), 0.000001f);
        const float2 perpendicular =
            float2(-pixelDirection.y, pixelDirection.x) / directionLength;
        clipPosition.x += perpendicular.x * inputValue.expansion.y *
            clipPosition.w / 500.0f;
        clipPosition.y += perpendicular.y * inputValue.expansion.y *
            clipPosition.w / 312.5f;
    }
    else
    {
        float pointPixelX = floor(outputValue.lineScreen.x);
        float pointPixelY = floor(outputValue.lineScreen.y);
        if (rightPass > 0.5f && frac(outputValue.lineScreen.x) < 0.002f)
            pointPixelX -= 1.0f;
        if (rightPass > 0.5f && frac(outputValue.lineScreen.y) > 0.998f)
            pointPixelY += 1.0f;
        const float2 pointCenter =
            float2(pointPixelX, pointPixelY) + 0.5f;
        clipPosition.x +=
            (pointCenter.x - outputValue.lineScreen.x) *
            clipPosition.w / 400.0f;
        clipPosition.y +=
            (pointCenter.y - outputValue.lineScreen.y) *
            clipPosition.w / 250.0f;
        clipPosition.x += inputValue.expansion.y * clipPosition.w / 800.0f;
        clipPosition.y += inputValue.expansion.z * clipPosition.w / 500.0f;
    }
    outputValue.position = clipPosition;
    return outputValue;
}

/** Draws the active camera into the left half using the Scene's sole Set. */
class WebglCameraActiveCameraPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and left-camera state. */
    constructor(
        RenderSet<WebglCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex through the active camera. */
    WebglCameraRasterOutput vertex(
        WebglCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits one discrete line, point, or background color. */
    WebglCameraFrameBuffer fragment(WebglCameraRasterOutput inputValue)
    {
        if (inputValue.position.x >= 400.0f)
            discard_fragment();
        if (inputValue.kind < 1.5f && inputValue.visible < 0.5f)
            discard_fragment();
        if (inputValue.kind < 0.5f)
        {
            const float2 start = float2(
                inputValue.lineScreen.x + inputValue.lineScreen.y,
                inputValue.lineScreen.x - inputValue.lineScreen.y);
            const float2 end = float2(
                inputValue.lineScreen.z + inputValue.lineScreen.w,
                inputValue.lineScreen.z - inputValue.lineScreen.w);
            const float2 center = float2(
                inputValue.position.x + inputValue.position.y,
                inputValue.position.x - inputValue.position.y);
            const float2 direction = end - start;
            float minimumT = 0.0f;
            float maximumT = 1.0f;
            if (abs(direction.x) < 0.000001f)
            {
                if (start.x < center.x - 0.5f || start.x > center.x + 0.5f)
                    discard_fragment();
            }
            else
            {
                const float firstT = (center.x - 0.5f - start.x) / direction.x;
                const float secondT = (center.x + 0.5f - start.x) / direction.x;
                minimumT = max(minimumT, min(firstT, secondT));
                maximumT = min(maximumT, max(firstT, secondT));
            }
            if (abs(direction.y) < 0.000001f)
            {
                if (start.y < center.y - 0.5f || start.y > center.y + 0.5f)
                    discard_fragment();
            }
            else
            {
                const float firstT = (center.y - 0.5f - start.y) / direction.y;
                const float secondT = (center.y + 0.5f - start.y) / direction.y;
                minimumT = max(minimumT, min(firstT, secondT));
                maximumT = min(maximumT, max(firstT, secondT));
            }
            if (maximumT < minimumT)
                discard_fragment();
        }
        WebglCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the observer camera into the right half using the same Scene Set. */
class WebglCameraObserverCameraPass final : public IRenderClass
{
public:
    /** Binds the same unique Scene Set and observer-camera state. */
    constructor(
        RenderSet<WebglCameraSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglCameraPassResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one Set vertex through the observer camera. */
    WebglCameraRasterOutput vertex(
        WebglCameraVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglCameraProjectVertex(
            sceneSet, resources, inputValue,
            renderEntityID, renderEntityInstanceID);
    }

    /** Emits one discrete line, point, or background color. */
    WebglCameraFrameBuffer fragment(WebglCameraRasterOutput inputValue)
    {
        if (inputValue.position.x < 400.0f)
            discard_fragment();
        if (inputValue.kind < 1.5f && inputValue.visible < 0.5f)
            discard_fragment();
        if (inputValue.kind < 0.5f)
        {
            const float2 start = float2(
                inputValue.lineScreen.x + inputValue.lineScreen.y,
                inputValue.lineScreen.x - inputValue.lineScreen.y);
            const float2 end = float2(
                inputValue.lineScreen.z + inputValue.lineScreen.w,
                inputValue.lineScreen.z - inputValue.lineScreen.w);
            const float2 center = float2(
                inputValue.position.x + inputValue.position.y,
                inputValue.position.x - inputValue.position.y);
            const float2 direction = end - start;
            float minimumT = 0.0f;
            float maximumT = 1.0f;
            if (abs(direction.x) < 0.000001f)
            {
                if (start.x < center.x - 0.5f || start.x > center.x + 0.5f)
                    discard_fragment();
            }
            else
            {
                const float firstT = (center.x - 0.5f - start.x) / direction.x;
                const float secondT = (center.x + 0.5f - start.x) / direction.x;
                minimumT = max(minimumT, min(firstT, secondT));
                maximumT = min(maximumT, max(firstT, secondT));
            }
            if (abs(direction.y) < 0.000001f)
            {
                if (start.y < center.y - 0.5f || start.y > center.y + 0.5f)
                    discard_fragment();
            }
            else
            {
                const float firstT = (center.y - 0.5f - start.y) / direction.y;
                const float secondT = (center.y + 0.5f - start.y) / direction.y;
                minimumT = max(minimumT, min(firstT, secondT));
                maximumT = min(maximumT, max(firstT, secondT));
            }
            if (maximumT < minimumT)
                discard_fragment();
        }
        WebglCameraFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated two-view Scene pipeline with one ordinary sample. */
class WebglCameraRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglCameraSceneRenderSet> sceneSet;
    Buffer<WebglCameraPassUniforms, BufferUsage<Uniform, CopyDst>> leftUniforms;
    Buffer<WebglCameraPassUniforms, BufferUsage<Uniform, CopyDst>> rightUniforms;
    BindGroup<WebglCameraPassResources> leftResources;
    BindGroup<WebglCameraPassResources> rightResources;
    RenderClass<WebglCameraActiveCameraPass> leftPass;
    RenderClass<WebglCameraObserverCameraPass> rightPass;
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
        sceneSet = device->createRenderSet<WebglCameraSceneRenderSet>();
        leftUniforms = device->createBuffer("WebglCameraLeftUniforms", 1u);
        rightUniforms = device->createBuffer("WebglCameraRightUniforms", 1u);
    }

    /** Allocates the ordinary single-sample final color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglCameraOutput", width, height, 1u);
        sceneDepth = device->createTexture("WebglCameraDepth", width, height, 1u);
    }

    /** Uploads both target-frame camera states and creates the two Scene passes. */
    void configureCameras(
        float4x4 leftViewProjection,
        float4x4 rightViewProjection)
    {
        WebglCameraPassUniforms leftState;
        leftState.viewProjection = leftViewProjection;
        leftState.viewportAndPass = float4(-0.5f, 0.5f, 0.0f, 0.0f);
        WebglCameraPassUniforms rightState;
        rightState.viewProjection = rightViewProjection;
        rightState.viewportAndPass = float4(0.5f, 0.5f, 1.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(BufferRange(leftUniforms), &leftState, sizeof(leftState))
            ->writeBuffer(BufferRange(rightUniforms), &rightState, sizeof(rightState))
            ->submit();
        leftResources = device->createBindGroup<WebglCameraPassResources>(leftUniforms);
        rightResources = device->createBindGroup<WebglCameraPassResources>(rightUniforms);
        leftPass = device->createRenderClass<WebglCameraActiveCameraPass>(
            sceneSet, leftResources);
        rightPass = device->createRenderClass<WebglCameraObserverCameraPass>(
            sceneSet, rightResources);
    }

    /** Submits both Scene passes against the same unique RenderSet. */
    void render() override
    {
        sceneSet->update();
        WebglCameraFrameBuffer leftFrame;
        leftFrame.color = outputColor->createView();
        leftFrame.color.loadOp = LoadOp::Clear;
        leftFrame.color.storeOp = StoreOp::Store;
        leftFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        leftFrame.depth = sceneDepth->createView();
        leftFrame.depth.depthLoadOp = LoadOp::Clear;
        leftFrame.depth.depthStoreOp = StoreOp::Store;
        leftFrame.depth.depthClearValue = 1.0f;
        WebglCameraFrameBuffer rightFrame;
        rightFrame.color = outputColor->createView();
        rightFrame.color.loadOp = LoadOp::Load;
        rightFrame.color.storeOp = StoreOp::Store;
        rightFrame.depth = sceneDepth->createView();
        rightFrame.depth.depthLoadOp = LoadOp::Load;
        rightFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglCameraActiveCamera", leftFrame, leftPass())
            ->renderPass("WebglCameraObserverCamera", rightFrame, rightPass())
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
