#ifndef GVM_THREE_WEBGL_MATH_OBB_HPP
#define GVM_THREE_WEBGL_MATH_OBB_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one box vertex and a barycentric edge coordinate for the optional hitbox. */
struct WebglMathObbVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 barycentric [[Attribute2]];
};

/** Stores the camera transform and the collision color selected on the CPU. */
struct WebglMathObbObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalMatrix;
    float4 colorAndPhase;
};

/** Provides the required one-entry instance component for every ordinary entity. */
struct WebglMathObbInstanceData
{
    float4 reserved;
};

/** Stores the material color and the deterministic wireframe phase. */
struct WebglMathObbMaterialData
{
    float4 baseColor;
};

/** Stores visibility, collision, and hitbox selection flags. */
struct WebglMathObbRenderFlags
{
    uint4 values;
};

/** Defines the one Scene RenderSet shared by Lambert and wireframe passes. */
struct WebglMathObbSceneRenderSet : public IRenderSet
{
    /** Declares consolidated box geometry and all per-entity components. */
    constructor(
        BufferComponent<WebglMathObbVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMathObbObjectData> objects,
        BufferComponent<WebglMathObbInstanceData> instances,
        BufferComponent<WebglMathObbMaterialData> materials,
        BufferComponent<WebglMathObbRenderFlags> renderFlags)
    {
    }
};

/** Carries lighting inputs and entity identity between the two Scene passes. */
struct WebglMathObbVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 barycentric [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the explicit single-sample color and depth attachments. */
struct WebglMathObbFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the locked Three output transfer function. */
float webglMathObbLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws every box with the Lambert hemisphere and collision color. */
class WebglMathObbMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet for the opaque box phase. */
    constructor(RenderSet<WebglMathObbSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the object and instance components through RenderEntity builtins. */
    WebglMathObbVertexOutput vertex(
        WebglMathObbVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMathObbObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglMathObbInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglMathObbVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection,
                                   inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        const float3 viewNormal = mul(objectData.normalMatrix,
                                      float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(viewNormal);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies hemisphere Lambert lighting and the CPU collision flag. */
    WebglMathObbFrameBuffer fragment(WebglMathObbVertexOutput inputValue)
    {
        const WebglMathObbObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglMathObbMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 lightDirection = normalize(float3(1.0f, 1.0f, 1.0f));
        const float hemisphereWeight = dot(normal, lightDirection) * 0.5f + 0.5f;
        const float3 skyIrradiance = float3(4.0f);
        const float3 groundIrradiance = float3(0.0159962934f) * 4.0f;
        const float3 irradiance = (groundIrradiance +
                                   (skyIrradiance - groundIrradiance) * hemisphereWeight) *
                                  0.3183098861837907f;
        const float3 lit = materialData.baseColor.xyz * irradiance * objectData.colorAndPhase.xyz;
        WebglMathObbFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMathObbLinearToSrgb(lit.x)),
            half(webglMathObbLinearToSrgb(lit.y)),
            half(webglMathObbLinearToSrgb(lit.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the selected hitbox from the same RenderSet with barycentric coverage. */
class WebglMathObbWireframePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set and enables transparent wireframe blending. */
    constructor(RenderSet<WebglMathObbSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
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
    /** Reuses the exact RenderSet transform and entity-index path. */
    WebglMathObbVertexOutput vertex(
        WebglMathObbVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglMathObbObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglMathObbInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        WebglMathObbVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection,
                                   inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewNormal = float3(0.0f);
        outputValue.barycentric = inputValue.barycentric.xyz;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits only the selected hitbox and evaluates analytic triangle-edge coverage. */
    WebglMathObbFrameBuffer fragment(WebglMathObbVertexOutput inputValue)
    {
        const WebglMathObbRenderFlags flags = sceneSet->renderFlags->get(inputValue.entityID, 0u);
        if (flags.values.x == 0u)
            discard_fragment();
        const float edge = min(inputValue.barycentric.x,
                               min(inputValue.barycentric.y, inputValue.barycentric.z));
        if (edge > 0.045f)
            discard_fragment();
        WebglMathObbFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.03f, 0.02f, 0.01f), half(0.85f));
        return frameBuffer;
    }
};

/** Owns the single Scene RenderSet and both geometry passes for webgl_math_obb. */
class WebglMathObbRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMathObbSceneRenderSet> sceneSet;
    RenderClass<WebglMathObbMainPass> mainPass;
    RenderClass<WebglMathObbWireframePass> wireframePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and the opaque/wireframe RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMathObbSceneRenderSet>();
        mainPass = device->createRenderClass<WebglMathObbMainPass>(sceneSet);
        wireframePass = device->createRenderClass<WebglMathObbWireframePass>(sceneSet);
    }

    /** Allocates the fixed single-sample readback targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglMathObbColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMathObbDepth", width, height, 1u);
    }

    /** Submits the two Scene passes using parameterless RenderSet indexed-indirect draws. */
    void render() override
    {
        sceneSet->update();
        WebglMathObbFrameBuffer mainFrame;
        mainFrame.color = outputColor->createView();
        mainFrame.color.loadOp = LoadOp::Clear;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.color.clearValue = {1.0f, 1.0f, 1.0f, 1.0f};
        mainFrame.depth = outputDepth->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Clear;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        mainFrame.depth.depthClearValue = 1.0f;
        WebglMathObbFrameBuffer wireFrame;
        wireFrame.color = outputColor->createView();
        wireFrame.color.loadOp = LoadOp::Load;
        wireFrame.color.storeOp = StoreOp::Store;
        wireFrame.depth = outputDepth->createView();
        wireFrame.depth.depthLoadOp = LoadOp::Load;
        wireFrame.depth.depthStoreOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglMathObbMain", mainFrame, mainPass())
            ->renderPass("WebglMathObbWireframe", wireFrame, wireframePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and explicit single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
