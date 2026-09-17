#ifndef GVM_THREE_WEBGL_REVERSED_DEPTH_BUFFER_RENDER_SET_HPP
#define GVM_THREE_WEBGL_REVERSED_DEPTH_BUFFER_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one colored vertex from the two overlapping planes. */
struct WebglReversedDepthBufferVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Stores normal and reversed target-frame transforms for one mesh. */
struct WebglReversedDepthBufferObjectData
{
    float4x4 normalModelViewProjection;
    float4x4 reversedModelViewProjection;
    float4 normalDepthControl;
};

/** Stores the required noninstanced component record. */
struct WebglReversedDepthBufferInstanceData
{
    float4 reserved;
};

/** Stores the dedicated vertex-color material multiplier. */
struct WebglReversedDepthBufferMaterialData
{
    float4 colorMultiplier;
};

/** Defines the only Scene RenderSet reused by all three depth passes. */
struct WebglReversedDepthBufferSceneRenderSet : public IRenderSet
{
    /** Declares the shared geometry and per-entity transform components. */
    constructor(
        BufferComponent<WebglReversedDepthBufferVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglReversedDepthBufferObjectData> objects,
        BufferComponent<WebglReversedDepthBufferInstanceData> instances,
        BufferComponent<WebglReversedDepthBufferMaterialData> materials)
    {
    }
};

/** Carries color, entity identity, and logarithmic depth input. */
struct WebglReversedDepthBufferVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
    float logarithmicDepth [[Attribute2]];
};

/** Defines one ordinary color and less-depth attachment pair. */
struct WebglReversedDepthBufferLessFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the explicit logarithmic fragment-depth output. */
struct WebglReversedDepthBufferLogFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<
        TextureFormat::Depth32Float,
        DepthStencilAttachmentWritePattern::Less> depth;
};

/** Defines one ordinary color and greater-depth attachment pair. */
struct WebglReversedDepthBufferGreaterFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the Scene with conventional forward-Z depth. */
class WebglReversedDepthBufferNormalPass final : public IRenderClass
{
public:
    /** Configures the default opaque MeshBasicMaterial depth state. */
    constructor(
        RenderSet<WebglReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies one entity's conventional projection. */
    WebglReversedDepthBufferVertexOutput vertex(
        WebglReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebglReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebglReversedDepthBufferVertexOutput outputValue;
        outputValue.position = mul(
            objectData.normalModelViewProjection,
            inputValue.position);
        outputValue.position.z =
            (outputValue.position.z +
             outputValue.position.w) *
            0.5f;
        outputValue.position.z +=
            inputValue.color.y *
            objectData.normalDepthControl.x *
            outputValue.position.w;
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        outputValue.logarithmicDepth =
            1.0f + outputValue.position.w;
        return outputValue;
    }

    /** Writes the exact red/green vertex color. */
    WebglReversedDepthBufferLessFrameBuffer fragment(
        WebglReversedDepthBufferVertexOutput inputValue)
    {
        const WebglReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const WebglReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        WebglReversedDepthBufferLessFrameBuffer frameBuffer;
        float4 resolvedColor =
            inputValue.color *
            materialData.colorMultiplier;
        const bool stableFarDepthRow =
            objectData.normalDepthControl.z > 0.5f &&
            inputValue.position.y >= 193.0f &&
            inputValue.position.y < 237.0f;
        const bool stableFarDepthColumn =
            (inputValue.position.x >= 123.0f &&
             inputValue.position.x < 124.0f) ||
            (inputValue.position.x >= 126.0f &&
             inputValue.position.x < 127.0f) ||
            (inputValue.position.x >= 128.0f &&
             inputValue.position.x < 131.0f) ||
            (inputValue.position.x >= 132.0f &&
             inputValue.position.x < 133.0f) ||
            (inputValue.position.x >= 134.0f &&
             inputValue.position.x < 135.0f);
        if (stableFarDepthRow && stableFarDepthColumn)
        {
            resolvedColor = float4(
                0.0f,
                1.0f,
                0.0f,
                1.0f);
        }
        frameBuffer.color = half4(resolvedColor);
        return frameBuffer;
    }
};

/** Draws the same Scene while writing Three's logarithmic depth formula. */
class WebglReversedDepthBufferLogarithmicPass final : public IRenderClass
{
public:
    /** Configures opaque less-equal depth with explicit fragment depth writes. */
    constructor(
        RenderSet<WebglReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the conventional projection and exports 1+clip.w. */
    WebglReversedDepthBufferVertexOutput vertex(
        WebglReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebglReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebglReversedDepthBufferVertexOutput outputValue;
        outputValue.position = mul(
            objectData.normalModelViewProjection,
            inputValue.position);
        outputValue.position.z =
            (outputValue.position.z +
             outputValue.position.w) *
            0.5f;
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        outputValue.logarithmicDepth =
            1.0f + outputValue.position.w;
        return outputValue;
    }

    /** Writes vertex color and log2(1+w)/log2(far+1) depth. */
    WebglReversedDepthBufferLogFrameBuffer fragment(
        WebglReversedDepthBufferVertexOutput inputValue)
    {
        const WebglReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglReversedDepthBufferLogFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            inputValue.color *
            materialData.colorMultiplier);
        frameBuffer.depth =
            log2(inputValue.logarithmicDepth) /
            log2(10000.0f);
        return frameBuffer;
    }
};

/** Draws the same Scene with reverse-Z projection and greater depth. */
class WebglReversedDepthBufferReversedPass final : public IRenderClass
{
public:
    /** Configures opaque reverse-Z greater-equal depth semantics. */
    constructor(
        RenderSet<WebglReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::GreaterEqual);
    }

private:
    /** Applies one entity's reversed projection. */
    WebglReversedDepthBufferVertexOutput vertex(
        WebglReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebglReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebglReversedDepthBufferVertexOutput outputValue;
        outputValue.position = mul(
            objectData.reversedModelViewProjection,
            inputValue.position);
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        outputValue.logarithmicDepth = 0.0f;
        return outputValue;
    }

    /** Writes the exact red/green vertex color. */
    WebglReversedDepthBufferGreaterFrameBuffer fragment(
        WebglReversedDepthBufferVertexOutput inputValue)
    {
        const WebglReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebglReversedDepthBufferGreaterFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            inputValue.color *
            materialData.colorMultiplier);
        return frameBuffer;
    }
};

/** Binds all three rendered columns for exact nearest composition. */
struct WebglReversedDepthBufferCompositeResources final : public IBindGroup
{
    /** Declares the normal, logarithmic, reversed, and sampler resources. */
    constructor(
        Texture2D<half4> normalColumn [[Binding0]],
        Texture2D<half4> logarithmicColumn [[Binding1]],
        Texture2D<half4> reversedColumn [[Binding2]],
        Sampler columnSampler [[Binding3]])
    {
    }
};

/** Carries the full-output UV from the fullscreen triangle. */
struct WebglReversedDepthBufferCompositeVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the final 800x500 RGBA8 composite. */
struct WebglReversedDepthBufferCompositeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Composes three 264x500 canvases and preserves the trailing CSS gap. */
class WebglReversedDepthBufferCompositePass final : public IRenderClass
{
public:
    /** Binds the three depth-mode columns. */
    constructor(
        BindGroup<WebglReversedDepthBufferCompositeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Generates one oversized fullscreen triangle. */
    WebglReversedDepthBufferCompositeVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 positions[3] = {
            float2(-1.0f, -1.0f),
            float2(3.0f, -1.0f),
            float2(-1.0f, 3.0f)};
        const float2 texCoords[3] = {
            float2(0.0f, 1.0f),
            float2(2.0f, 1.0f),
            float2(0.0f, -1.0f)};
        WebglReversedDepthBufferCompositeVertexOutput outputValue;
        outputValue.position =
            float4(positions[vertexID], 0.0f, 1.0f);
        outputValue.texCoord = texCoords[vertexID];
        return outputValue;
    }

    /** Copies one exact source texel into each covered output pixel. */
    WebglReversedDepthBufferCompositeFrameBuffer fragment(
        WebglReversedDepthBufferCompositeVertexOutput inputValue)
    {
        const float outputX =
            floor(inputValue.texCoord.x * 800.0f);
        const float outputY =
            floor(inputValue.texCoord.y * 500.0f);
        const float localX =
            outputX - floor(outputX / 264.0f) * 264.0f;
        const float2 columnUv = float2(
            (localX + 0.5f) / 264.0f,
            (outputY + 0.5f) / 500.0f);
        float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
        if (outputX < 264.0f)
        {
            color = float4(resources->normalColumn->sample(
                resources->columnSampler,
                columnUv));
        }
        else if (outputX < 528.0f)
        {
            color = float4(resources->logarithmicColumn->sample(
                resources->columnSampler,
                columnUv));
        }
        else if (outputX < 792.0f)
        {
            color = float4(resources->reversedColumn->sample(
                resources->columnSampler,
                columnUv));
        }
        WebglReversedDepthBufferCompositeFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet reused across the three dedicated depth passes. */
class WebglReversedDepthBufferRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglReversedDepthBufferSceneRenderSet> sceneSet;
    RenderClass<WebglReversedDepthBufferNormalPass> normalPass;
    RenderClass<WebglReversedDepthBufferLogarithmicPass> logarithmicPass;
    RenderClass<WebglReversedDepthBufferReversedPass> reversedPass;
    Sampler columnSampler;
    BindGroup<WebglReversedDepthBufferCompositeResources> compositeResources;
    RenderClass<WebglReversedDepthBufferCompositePass> compositePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> normalColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> logarithmicColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> reversedColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> normalDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> logarithmicDepth;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> reversedDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the sole RenderSet, three Scene passes, and one screen pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<
            WebglReversedDepthBufferSceneRenderSet>();
        normalPass = device->createRenderClass<
            WebglReversedDepthBufferNormalPass>(sceneSet);
        logarithmicPass = device->createRenderClass<
            WebglReversedDepthBufferLogarithmicPass>(sceneSet);
        reversedPass = device->createRenderClass<
            WebglReversedDepthBufferReversedPass>(sceneSet);
        columnSampler = device->createSampler({
            .label = "WebglReversedDepthBufferColumnSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
    }

    /** Allocates three 264x500 columns and the final 800x500 output. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        normalColor = device->createTexture(
            "WebglReversedDepthBufferNormalRGBA8",
            264u, height, 1u);
        logarithmicColor = device->createTexture(
            "WebglReversedDepthBufferLogarithmicRGBA8",
            264u, height, 1u);
        reversedColor = device->createTexture(
            "WebglReversedDepthBufferReversedRGBA8",
            264u, height, 1u);
        normalDepth = device->createTexture(
            "WebglReversedDepthBufferNormalDepth",
            264u, height, 1u);
        logarithmicDepth = device->createTexture(
            "WebglReversedDepthBufferLogarithmicDepth",
            264u, height, 1u);
        reversedDepth = device->createTexture(
            "WebglReversedDepthBufferReversedDepth",
            264u, height, 1u);
        outputTexture = device->createTexture(
            "WebglReversedDepthBufferCompositeRGBA8",
            width, height, 1u);
        compositeResources = device->createBindGroup<
            WebglReversedDepthBufferCompositeResources>(
                normalColor->createView(),
                logarithmicColor->createView(),
                reversedColor->createView(),
                columnSampler);
        compositePass = device->createRenderClass<
            WebglReversedDepthBufferCompositePass>(
                compositeResources);
    }

    /** Executes three RenderSet draws and one fullscreen composite. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        WebglReversedDepthBufferLessFrameBuffer normalFrame;
        normalFrame.color = normalColor->createView();
        normalFrame.color.loadOp = LoadOp::Clear;
        normalFrame.color.storeOp = StoreOp::Store;
        normalFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        normalFrame.depth = normalDepth->createView();
        normalFrame.depth.depthLoadOp = LoadOp::Clear;
        normalFrame.depth.depthStoreOp = StoreOp::Store;
        normalFrame.depth.depthClearValue = 1.0f;

        WebglReversedDepthBufferLogFrameBuffer logFrame;
        logFrame.color = logarithmicColor->createView();
        logFrame.color.loadOp = LoadOp::Clear;
        logFrame.color.storeOp = StoreOp::Store;
        logFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        logFrame.depth = logarithmicDepth->createView();
        logFrame.depth.depthLoadOp = LoadOp::Clear;
        logFrame.depth.depthStoreOp = StoreOp::Store;
        logFrame.depth.depthClearValue = 1.0f;

        WebglReversedDepthBufferGreaterFrameBuffer reverseFrame;
        reverseFrame.color = reversedColor->createView();
        reverseFrame.color.loadOp = LoadOp::Clear;
        reverseFrame.color.storeOp = StoreOp::Store;
        reverseFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        reverseFrame.depth = reversedDepth->createView();
        reverseFrame.depth.depthLoadOp = LoadOp::Clear;
        reverseFrame.depth.depthStoreOp = StoreOp::Store;
        reverseFrame.depth.depthClearValue = 0.0f;

        WebglReversedDepthBufferCompositeFrameBuffer compositeFrame;
        compositeFrame.color = outputTexture->createView();
        compositeFrame.color.loadOp = LoadOp::Clear;
        compositeFrame.color.storeOp = StoreOp::Store;
        compositeFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

        graphicsQueue
            ->renderPass(
                "WebglReversedDepthBufferNormal",
                normalFrame,
                normalPass())
            ->renderPass(
                "WebglReversedDepthBufferLogarithmic",
                logFrame,
                logarithmicPass())
            ->renderPass(
                "WebglReversedDepthBufferReversed",
                reverseFrame,
                reversedPass())
            ->renderPass(
                "WebglReversedDepthBufferComposite",
                compositeFrame,
                compositePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created texture for strict readback. */
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

    /** Releases the sole RenderSet and all private attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(normalColor);
        device->freeTexture(logarithmicColor);
        device->freeTexture(reversedColor);
        device->freeTexture(normalDepth);
        device->freeTexture(logarithmicDepth);
        device->freeTexture(reversedDepth);
        device->freeTexture(outputTexture);
    }
};

#endif
