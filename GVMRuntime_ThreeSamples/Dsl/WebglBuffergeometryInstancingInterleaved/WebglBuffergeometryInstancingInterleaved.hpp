#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_INTERLEAVED_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_INSTANCING_INTERLEAVED_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglBuffergeometryInstancingInterleavedMaxTextures = 8u;

/** Stores one position and UV record from the upstream interleaved box buffer. */
struct WebglBuffergeometryInstancingInterleavedVertex
{
    float4 position [[Attribute0]];
    float4 texCoord [[Attribute1]];
};

/** Stores the root transform and material selection for the sole instanced box entity. */
struct WebglBuffergeometryInstancingInterleavedObjectData
{
    float4x4 rootModelViewProjection;
    uint4 materialAndFlags;
};

/** Stores one r185 instance matrix addressed by RenderEntityInstanceID. */
struct WebglBuffergeometryInstancingInterleavedInstanceData
{
    float4 matrixColumn0;
    float4 matrixColumn1;
    float4 matrixColumn2;
    float4 matrixColumn3;
};

/** Stores the base color of the dedicated MeshBasicMaterial. */
struct WebglBuffergeometryInstancingInterleavedMaterialData
{
    float4 baseColor;
};

/** Defines the only Scene RenderSet used by the interleaved instancing example. */
struct WebglBuffergeometryInstancingInterleavedSceneRenderSet : public IRenderSet
{
    /** Declares the indexed cube, object, instance matrix, material, and crate texture components. */
    constructor(
        BufferComponent<WebglBuffergeometryInstancingInterleavedVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglBuffergeometryInstancingInterleavedObjectData> objects,
        BufferComponent<WebglBuffergeometryInstancingInterleavedInstanceData> instances,
        BufferComponent<WebglBuffergeometryInstancingInterleavedMaterialData> materials,
        (TextureComponent<half4, WebglBuffergeometryInstancingInterleavedMaxTextures> textures))
    {
    }
};

/** Binds the immutable trilinear crate sampler without a standalone texture resource. */
struct WebglBuffergeometryInstancingInterleavedSamplerResources final : public IBindGroup
{
    /** Declares the sampler shared by all 5,000 instances. */
    constructor(Sampler crateSampler [[Binding0]])
    {
    }
};

/** Carries texture coordinates and entity identity from the instance-expanded vertex stage. */
struct WebglBuffergeometryInstancingInterleavedVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
    uint2 entityAndMaterial [[Attribute1]];
};

/** Defines the deterministic RGBA8 color and native depth attachments. */
struct WebglBuffergeometryInstancingInterleavedFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts a linear-light channel using Three r185's output transfer constants. */
float webglBuffergeometryInstancingInterleavedLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the single 5,000-instance entity through RenderSet indexed-indirect metadata. */
class WebglBuffergeometryInstancingInterleavedMainPass final : public IRenderClass
{
public:
    /** Configures Three's opaque MeshBasicMaterial depth and back-face state. */
    constructor(
        RenderSet<WebglBuffergeometryInstancingInterleavedSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglBuffergeometryInstancingInterleavedSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity root transform and one instance matrix selected by the builtin instance ID. */
    WebglBuffergeometryInstancingInterleavedVertexOutput vertex(
        WebglBuffergeometryInstancingInterleavedVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglBuffergeometryInstancingInterleavedObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglBuffergeometryInstancingInterleavedInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 instancePosition =
            instanceData.matrixColumn0 * inputValue.position.x +
            instanceData.matrixColumn1 * inputValue.position.y +
            instanceData.matrixColumn2 * inputValue.position.z +
            instanceData.matrixColumn3;

        WebglBuffergeometryInstancingInterleavedVertexOutput outputValue;
        outputValue.position = mul(
            objectData.rootModelViewProjection,
            instancePosition);
        outputValue.texCoord = float2(
            inputValue.texCoord.x,
            1.0f - inputValue.texCoord.y);
        outputValue.entityAndMaterial =
            uint2(renderEntityID, objectData.materialAndFlags.x);
        return outputValue;
    }

    /** Samples the entity crate TextureComponent and writes display-sRGB color. */
    WebglBuffergeometryInstancingInterleavedFrameBuffer fragment(
        WebglBuffergeometryInstancingInterleavedVertexOutput inputValue)
    {
        const WebglBuffergeometryInstancingInterleavedMaterialData materialData =
            sceneSet->materials->get(
                inputValue.entityAndMaterial.x,
                inputValue.entityAndMaterial.y);
        auto crateTexture =
            sceneSet->textures->get(inputValue.entityAndMaterial.x, 0u);
        const float4 linearColor =
            float4(crateTexture->sample(
                samplerResources->crateSampler,
                inputValue.texCoord)) *
            materialData.baseColor;

        const float3 displayColor = float3(
            webglBuffergeometryInstancingInterleavedLinearToSrgb(linearColor.x),
            webglBuffergeometryInstancingInterleavedLinearToSrgb(linearColor.y),
            webglBuffergeometryInstancingInterleavedLinearToSrgb(linearColor.z));
        const float3 quantizedDisplayColor =
            floor(displayColor * 51.0f + 0.7f) / 51.0f;

        WebglBuffergeometryInstancingInterleavedFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(quantizedDisplayColor), half(linearColor.w));
        return frameBuffer;
    }
};

/** Owns the example-specific Scene RenderSet, texture sampler, and deterministic output. */
class WebglBuffergeometryInstancingInterleavedRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglBuffergeometryInstancingInterleavedSceneRenderSet> sceneSet;
    Sampler crateSampler;
    BindGroup<WebglBuffergeometryInstancingInterleavedSamplerResources> samplerResources;
    RenderClass<WebglBuffergeometryInstancingInterleavedMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates exactly one Scene RenderSet and one RenderSet-bound Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<
                WebglBuffergeometryInstancingInterleavedSceneRenderSet>();
        crateSampler = device->createSampler({
            .label = "WebglBuffergeometryInstancingInterleavedCrateSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 8,
            .maxAnisotropy = 1,
        });
        samplerResources =
            device->createBindGroup<
                WebglBuffergeometryInstancingInterleavedSamplerResources>(
                crateSampler);
        scenePass =
            device->createRenderClass<
                WebglBuffergeometryInstancingInterleavedMainPass>(
                sceneSet,
                samplerResources);
    }

    /** Allocates the host-sized RGBA8 color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryInstancingInterleavedRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryInstancingInterleavedDepth32",
            width,
            height,
            1u);
    }

    /** Updates entity metadata and issues the parameterless RenderSet-only draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        WebglBuffergeometryInstancingInterleavedFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            0.06274509803921569f,
            0.06274509803921569f,
            0.06274509803921569f,
            1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->renderPass(
                "WebglBuffergeometryInstancingInterleavedScene",
                frameBuffer,
                scenePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final texture used by strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
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
