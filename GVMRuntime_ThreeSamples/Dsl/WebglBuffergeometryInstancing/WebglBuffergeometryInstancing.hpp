#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one vertex of the upstream three-point triangle template. */
struct WebglBuffergeometryInstancingVertex
{
    float4 position [[Attribute0]];
};

/** Stores the target-frame transform and private raw-shader uniforms for the sole mesh entity. */
struct WebglBuffergeometryInstancingObjectData
{
    float4x4 modelViewProjection;
    float4 timeAndSineTime;
    uint4 materialAndFlags;
};

/** Stores every InstancedBufferAttribute record selected by RenderEntityInstanceID. */
struct WebglBuffergeometryInstancingInstanceData
{
    float4 offset;
    float4 color;
    float4 orientationStart;
    float4 orientationEnd;
};

/** Stores the private RawShaderMaterial color multiplier. */
struct WebglBuffergeometryInstancingMaterialData
{
    float4 baseColor;
};

/** Defines the unique Scene RenderSet for the instanced triangle mesh. */
struct WebglBuffergeometryInstancingSceneRenderSet : public IRenderSet
{
    /** Declares the frozen vertex, index, object, instance, and material component ABI. */
    constructor(BufferComponent<WebglBuffergeometryInstancingVertex> vertices [[RenderSetVertexBuffer]], BufferComponent<uint> indices [[RenderSetIndexBuffer]], BufferComponent<WebglBuffergeometryInstancingObjectData> objects, BufferComponent<WebglBuffergeometryInstancingInstanceData> instances, BufferComponent<WebglBuffergeometryInstancingMaterialData> materials)
    {
    }
};

/** Carries the quaternion-rotated position, instance color, and entity identity to the fragment stage. */
struct WebglBuffergeometryInstancingVertexOutput
{
    float4 position [[Position]];
    float3 instancePosition [[Attribute0]];
    float4 color [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the deterministic RGBA8 color and native depth attachments. */
struct WebglBuffergeometryInstancingFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Re-expresses the instanced RawShaderMaterial through the Scene RenderSet-only draw path. */
class WebglBuffergeometryInstancingMainPass final : public IRenderClass
{
public:
    /** Configures Three's DoubleSide, normal blending, triangle, and default depth semantics. */
    constructor(RenderSet<WebglBuffergeometryInstancingSceneRenderSet> sceneSet [[Slot0]])
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
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves the sole entity and one of up to 50,000 per-instance attribute records. */
    WebglBuffergeometryInstancingVertexOutput vertex(WebglBuffergeometryInstancingVertex inputValue [[VertexInput0]], uint renderEntityID [[RenderEntityID]], uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryInstancingObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryInstancingInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float sineTime = objectData.timeAndSineTime.y;
        float3 instancePosition = instanceData.offset.xyz * max(abs(sineTime * 2.0f + 1.0f), 0.5f) + inputValue.position.xyz;
        const float4 orientation = normalize(instanceData.orientationStart * (1.0f - sineTime) + instanceData.orientationEnd * sineTime);
        const float3 orientationCrossPosition = cross(orientation.xyz, instancePosition);
        instancePosition = orientationCrossPosition * (2.0f * orientation.w) + (cross(orientation.xyz, orientationCrossPosition) * 2.0f + instancePosition);

        WebglBuffergeometryInstancingVertexOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, float4(instancePosition, 1.0f));
        outputValue.instancePosition = instancePosition;
        outputValue.color = instanceData.color;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies the upstream position/time red modulation and RawShaderMaterial alpha. */
    WebglBuffergeometryInstancingFrameBuffer fragment(WebglBuffergeometryInstancingVertexOutput inputValue)
    {
        const WebglBuffergeometryInstancingObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglBuffergeometryInstancingMaterialData materialData = sceneSet->materials->get(inputValue.entityID, objectData.materialAndFlags.x);
        float4 color = inputValue.color * materialData.baseColor;
        color.x += sin(inputValue.instancePosition.x * 10.0f + objectData.timeAndSineTime.x) * 0.5f;

        WebglBuffergeometryInstancingFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the sample's sole Scene RenderSet and deterministic offscreen output. */
class WebglBuffergeometryInstancingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryInstancingSceneRenderSet> sceneSet;
    RenderClass<WebglBuffergeometryInstancingMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates exactly one Scene RenderSet and one RenderSet-bound RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglBuffergeometryInstancingSceneRenderSet>();
        scenePass = device->createRenderClass<WebglBuffergeometryInstancingMainPass>(sceneSet);
    }

    /** Allocates the fixed host-requested RGBA8 color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglBuffergeometryInstancingRGBA8", width, height, 1u);
        depthTexture = device->createTexture("WebglBuffergeometryInstancingDepth32", width, height, 1u);
    }

    /** Updates entity metadata and issues the sole parameterless indexed-indirect Scene draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        WebglBuffergeometryInstancingFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue->renderPass("main-instanced", frameBuffer, scenePass())->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-created RGBA8 target used by the host readback gate. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the unique Scene RenderSet and deterministic attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
