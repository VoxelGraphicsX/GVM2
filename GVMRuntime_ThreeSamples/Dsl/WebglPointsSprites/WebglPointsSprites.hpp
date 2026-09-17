#ifndef GVM_THREE_WEBGL_POINTS_SPRITES_HPP
#define GVM_THREE_WEBGL_POINTS_SPRITES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglPointsSpritesTextureCapacity = 8u;

/** Stores one statically expanded snowflake point vertex. */
struct WebglPointsSpritesVertex
{
    float4 position [[Attribute0]];
    float4 corner [[Attribute1]];
};

/** Stores one entity's current model-view and projection matrices. */
struct WebglPointsSpritesObjectData
{
    float4x4 modelView;
    float4x4 projection;
};

/** Stores the mandatory non-instanced component record. */
struct WebglPointsSpritesInstanceData
{
    float4 reserved;
};

/** Stores linear material color, point size, and texture-enabled flag. */
struct WebglPointsSpritesMaterialData
{
    float4 colorAndSize;
    float4 flags;
};

/** Defines the unique five-entity Scene RenderSet. */
struct WebglPointsSpritesSceneRenderSet : public IRenderSet
{
    /** Declares packed point triangles and per-entity texture ownership. */
    constructor(
        BufferComponent<WebglPointsSpritesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPointsSpritesObjectData> objects,
        BufferComponent<WebglPointsSpritesInstanceData> instances,
        BufferComponent<WebglPointsSpritesMaterialData> materials,
        (TextureComponent<float4, WebglPointsSpritesTextureCapacity> textures))
    {
    }
};

/** Binds the trilinear sampler shared by all snowflake entities. */
struct WebglPointsSpritesSamplerResources final : public IBindGroup
{
    /** Declares the immutable Repeat-wrapped snowflake sampler. */
    constructor(Sampler snowflakeSampler [[Binding0]])
    {
    }
};

/** Carries PointCoord, material data, and entity identity to fragments. */
struct WebglPointsSpritesVertexOutput
{
    float4 position [[Position]];
    float2 pointCoord [[Attribute0]];
    float pointSize [[Attribute1]];
    float fogDepth [[Attribute2]];
    float4 pointCoverage [[Attribute3]];
    uint entityID [[Attribute4]];
};

/** Defines the ordinary single-sample RGBA8 target. */
struct WebglPointsSpritesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Decodes one sRGB texture or material channel into linear light. */
float webglPointsSpritesSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value * 0.0773993808f
        : pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
}

/** Encodes one linear-light channel for the browser canvas. */
float webglPointsSpritesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped < 0.0031308f
        ? clamped * 12.92f
        : 1.055f * pow(clamped, 0.41666f) - 0.055f;
}

/** Draws five additive point layers through one RenderSet-only Scene pass. */
class WebglPointsSpritesAdditivePass final : public IRenderClass
{
public:
    /** Reproduces additive transparent PointsMaterial with depth testing disabled. */
    constructor(
        RenderSet<WebglPointsSpritesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPointsSpritesSamplerResources> samplerResources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::One;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::One;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Expands one authored point corner using Three's perspective size formula. */
    WebglPointsSpritesVertexOutput vertex(
        WebglPointsSpritesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebglPointsSpritesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPointsSpritesMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const float4 viewPosition = mul(objectData.modelView, inputValue.position);
        float4 clipPosition = mul(objectData.projection, viewPosition);
        const float rawPointSize =
            materialData.colorAndSize.w * (250.0f / -viewPosition.z);
        const float pointSize = clamp(rawPointSize, 1.0f, 511.0f);
        float2 pointCenter =
            (clipPosition.xy / clipPosition.w + float2(1.0f)) *
            float2(400.0f, 250.0f);
        pointCenter -= float2(materialData.flags.y, materialData.flags.z);
        const float2 topPointCenter = float2(
            pointCenter.x,
            500.0f - pointCenter.y);
        const float halfPointSize = pointSize * 0.5f;
        const float2 roundedCoverageLow = floor(
            (topPointCenter - float2(halfPointSize)) * 16.0f +
            float2(0.5f)) / 16.0f;
        const float2 roundedCoverageHigh = floor(
            (topPointCenter + float2(halfPointSize)) * 16.0f +
            float2(0.5f)) / 16.0f;
        const float2 coverageSize =
            roundedCoverageHigh - roundedCoverageLow;
        const float2 pointCoverageEdge = floor(
            float2(
                topPointCenter.x - halfPointSize,
                topPointCenter.y - (coverageSize.y - halfPointSize)) *
                256.0f + float2(0.5f, 0.75f)) / 256.0f;
        const float2 pointCoverageCenter =
            pointCoverageEdge + coverageSize * 0.5f;
        const float2 rasterCenter = pointCoverageCenter;
        const float2 rasterSize = coverageSize;
        clipPosition.x =
            (rasterCenter.x / 400.0f - 1.0f) * clipPosition.w;
        clipPosition.y =
            ((500.0f - rasterCenter.y) / 250.0f - 1.0f) *
            clipPosition.w;
        clipPosition.x +=
            inputValue.corner.x * rasterSize.x / 800.0f * clipPosition.w;
        clipPosition.y +=
            inputValue.corner.y * rasterSize.y / 500.0f * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglPointsSpritesVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.pointCoord = float2(
            inputValue.corner.x * 0.5f + 0.5f,
            0.5f - inputValue.corner.y * 0.5f);
        outputValue.pointSize = pointSize;
        outputValue.fogDepth = -viewPosition.z;
        outputValue.pointCoverage = float4(
            pointCoverageEdge.x,
            pointCoverageEdge.y,
            coverageSize.x,
            coverageSize.y);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples optional sRGB snowflakes, applies fog, and emits encoded additive color. */
    WebglPointsSpritesFrameBuffer fragment(
        WebglPointsSpritesVertexOutput inputValue)
    {
        const WebglPointsSpritesMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        // PointsMaterial.map=null does not execute a texture sample. Keep the
        // descriptor present for the fixed RenderSet slot, but select the
        // texture-free path from the material component so its filtering and
        // alpha semantics remain identical to the upstream shader.
        float4 textureColor = float4(1.0f);
        if (materialData.flags.x > 0.5f)
        {
            auto texture = sceneSet->textures->get(inputValue.entityID, 0u);
            const float2 pointCoord =
                (inputValue.position.xy - inputValue.pointCoverage.xy) /
                inputValue.pointCoverage.zw;
            textureColor = float4(texture->sample(
                samplerResources->snowflakeSampler,
                pointCoord));
        }
        const float3 textureLinear = textureColor.xyz;
        const float fogDensity = 0.0008f;
        const float fogFactor = 1.0f - exp(
            -fogDensity * fogDensity *
            inputValue.fogDepth * inputValue.fogDepth);
        const float3 linearColor =
            materialData.colorAndSize.xyz * textureLinear;
        const float3 encodedColor = float3(
            webglPointsSpritesLinearToSrgb(linearColor.x),
            webglPointsSpritesLinearToSrgb(linearColor.y),
            webglPointsSpritesLinearToSrgb(linearColor.z));
        const float3 outputColor = encodedColor * (1.0f - fogFactor);
        WebglPointsSpritesFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(outputColor), half(textureColor.w));
        return frameBuffer;
    }
};

/** Owns the dedicated Scene RenderSet and deterministic offscreen output. */
class WebglPointsSpritesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPointsSpritesSceneRenderSet> sceneSet;
    Sampler snowflakeSampler;
    BindGroup<WebglPointsSpritesSamplerResources> samplerResources;
    RenderClass<WebglPointsSpritesAdditivePass> additivePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates exactly one Scene RenderSet and one additive Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglPointsSpritesSceneRenderSet>();
        snowflakeSampler = device->createSampler({
            .label = "WebglPointsSpritesSnowflakeSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1u,
        });
        samplerResources = device->createBindGroup<
            WebglPointsSpritesSamplerResources>(snowflakeSampler);
        additivePass = device->createRenderClass<
            WebglPointsSpritesAdditivePass>(sceneSet, samplerResources);
    }

    /** Allocates the ordinary single-sample RGBA8 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglPointsSpritesRGBA8", width, height, 1u);
    }

    /** Submits the automatic five-entity indexed-indirect RenderSet draw. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();
        WebglPointsSpritesFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->renderPass("WebglPointsSpritesScene", frameBuffer, additivePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final texture for strict readback. */
    auto getReadbackTextureHandle() const
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

    /** Releases the sole Scene RenderSet and output texture. */
    void destroy() override
    {
        sceneSet->destroy();
        outputTexture->destroy();
    }
};

#endif
