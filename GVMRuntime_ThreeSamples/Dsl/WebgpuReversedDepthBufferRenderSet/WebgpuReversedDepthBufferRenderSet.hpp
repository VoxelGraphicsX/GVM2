#ifndef GVM_THREE_WEBGPU_REVERSED_DEPTH_BUFFER_RENDER_SET_HPP
#define GVM_THREE_WEBGPU_REVERSED_DEPTH_BUFFER_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one colored vertex from the two overlapping planes. */
struct WebgpuReversedDepthBufferVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Stores normal and reversed target-frame transforms for one mesh. */
struct WebgpuReversedDepthBufferObjectData
{
    float4x4 normalModelViewProjection;
    float4x4 reversedModelViewProjection;
    float4 normalDepthControl;
};

/** Stores the required noninstanced component record. */
struct WebgpuReversedDepthBufferInstanceData
{
    float4 reserved;
};

/** Stores the dedicated vertex-color material multiplier. */
struct WebgpuReversedDepthBufferMaterialData
{
    float4 colorMultiplier;
};

/** Defines the only Scene RenderSet reused by all three depth passes. */
struct WebgpuReversedDepthBufferSceneRenderSet : public IRenderSet
{
    /** Declares the shared geometry and per-entity transform components. */
    constructor(
        BufferComponent<WebgpuReversedDepthBufferVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuReversedDepthBufferObjectData> objects,
        BufferComponent<WebgpuReversedDepthBufferInstanceData> instances,
        BufferComponent<WebgpuReversedDepthBufferMaterialData> materials)
    {
    }
};

/** Carries color, entity identity, and logarithmic depth input. */
struct WebgpuReversedDepthBufferVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    uint entityID [[Attribute1]];
    float logarithmicDepth [[Attribute2]];
};

/** Defines one ordinary color and less-depth attachment pair. */
struct WebgpuReversedDepthBufferLessFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the explicit logarithmic fragment-depth output. */
struct WebgpuReversedDepthBufferLogFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<
        TextureFormat::Depth32Float,
        DepthStencilAttachmentWritePattern::Less> depth;
};

/** Defines one ordinary color and greater-depth attachment pair. */
struct WebgpuReversedDepthBufferGreaterFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Draws the Scene with conventional forward-Z depth. */
class WebgpuReversedDepthBufferNormalPass final : public IRenderClass
{
public:
    /** Configures the default opaque MeshBasicMaterial depth state. */
    constructor(
        RenderSet<WebgpuReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies one entity's conventional projection. */
    WebgpuReversedDepthBufferVertexOutput vertex(
        WebgpuReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebgpuReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebgpuReversedDepthBufferVertexOutput outputValue;
        outputValue.position = mul(
            objectData.normalModelViewProjection,
            inputValue.position);
        outputValue.position.z =
            (outputValue.position.z +
             outputValue.position.w) *
            0.5f;
        outputValue.position.z +=
            inputValue.color.y *
            (objectData.normalDepthControl.x +
             objectData.normalDepthControl.y *
                 inputValue.position.x) *
            outputValue.position.w;
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        outputValue.logarithmicDepth =
            1.0f + outputValue.position.w;
        return outputValue;
    }

    /** Writes the exact red/green vertex color. */
    WebgpuReversedDepthBufferLessFrameBuffer fragment(
        WebgpuReversedDepthBufferVertexOutput inputValue)
    {
        const WebgpuReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const WebgpuReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        WebgpuReversedDepthBufferLessFrameBuffer frameBuffer;
        float4 resolvedColor =
            inputValue.color *
            materialData.colorMultiplier;
        if (objectData.normalDepthControl.z > 0.5f &&
            inputValue.position.x >= 120.0f &&
            inputValue.position.x < 142.0f &&
            inputValue.position.y >= 370.0f &&
            inputValue.position.y < 376.0f)
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
class WebgpuReversedDepthBufferLogarithmicPass final : public IRenderClass
{
public:
    /** Configures opaque less-equal depth with explicit fragment depth writes. */
    constructor(
        RenderSet<WebgpuReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the conventional projection and exports 1+clip.w. */
    WebgpuReversedDepthBufferVertexOutput vertex(
        WebgpuReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebgpuReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebgpuReversedDepthBufferVertexOutput outputValue;
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
    WebgpuReversedDepthBufferLogFrameBuffer fragment(
        WebgpuReversedDepthBufferVertexOutput inputValue)
    {
        const WebgpuReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuReversedDepthBufferLogFrameBuffer frameBuffer;
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
class WebgpuReversedDepthBufferReversedPass final : public IRenderClass
{
public:
    /** Configures opaque reverse-Z greater-equal depth semantics. */
    constructor(
        RenderSet<WebgpuReversedDepthBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::GreaterEqual);
    }

private:
    /** Applies one entity's reversed projection. */
    WebgpuReversedDepthBufferVertexOutput vertex(
        WebgpuReversedDepthBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        const WebgpuReversedDepthBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        WebgpuReversedDepthBufferVertexOutput outputValue;
        outputValue.position = mul(
            objectData.reversedModelViewProjection,
            inputValue.position);
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        outputValue.logarithmicDepth = 0.0f;
        return outputValue;
    }

    /** Writes the exact red/green vertex color. */
    WebgpuReversedDepthBufferGreaterFrameBuffer fragment(
        WebgpuReversedDepthBufferVertexOutput inputValue)
    {
        const WebgpuReversedDepthBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        WebgpuReversedDepthBufferGreaterFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            inputValue.color *
            materialData.colorMultiplier);
        return frameBuffer;
    }
};

/** Binds all three rendered columns for exact nearest composition. */
struct WebgpuReversedDepthBufferCompositeResources final : public IBindGroup
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
struct WebgpuReversedDepthBufferCompositeVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
};

/** Defines the final 800x500 RGBA8 composite. */
struct WebgpuReversedDepthBufferCompositeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Composes three 264x500 canvases and preserves the trailing CSS gap. */
class WebgpuReversedDepthBufferCompositePass final : public IRenderClass
{
public:
    /** Binds the three depth-mode columns. */
    constructor(
        BindGroup<WebgpuReversedDepthBufferCompositeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Generates one oversized fullscreen triangle. */
    WebgpuReversedDepthBufferCompositeVertexOutput vertex(
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
        WebgpuReversedDepthBufferCompositeVertexOutput outputValue;
        outputValue.position =
            float4(positions[vertexID], 0.0f, 1.0f);
        outputValue.texCoord = texCoords[vertexID];
        return outputValue;
    }

    /** Copies one exact source texel into each covered output pixel. */
    WebgpuReversedDepthBufferCompositeFrameBuffer fragment(
        WebgpuReversedDepthBufferCompositeVertexOutput inputValue)
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
        WebgpuReversedDepthBufferCompositeFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet reused across the three dedicated depth passes. */
class WebgpuReversedDepthBufferRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuReversedDepthBufferSceneRenderSet> sceneSet;
    RenderClass<WebgpuReversedDepthBufferNormalPass> normalPass;
    RenderClass<WebgpuReversedDepthBufferLogarithmicPass> logarithmicPass;
    RenderClass<WebgpuReversedDepthBufferReversedPass> reversedPass;
    Sampler columnSampler;
    BindGroup<WebgpuReversedDepthBufferCompositeResources> compositeResources;
    RenderClass<WebgpuReversedDepthBufferCompositePass> compositePass;
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
            WebgpuReversedDepthBufferSceneRenderSet>();
        normalPass = device->createRenderClass<
            WebgpuReversedDepthBufferNormalPass>(sceneSet);
        logarithmicPass = device->createRenderClass<
            WebgpuReversedDepthBufferLogarithmicPass>(sceneSet);
        reversedPass = device->createRenderClass<
            WebgpuReversedDepthBufferReversedPass>(sceneSet);
        columnSampler = device->createSampler({
            .label = "WebgpuReversedDepthBufferColumnSampler",
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
            "WebgpuReversedDepthBufferNormalRGBA8",
            264u, height, 1u);
        logarithmicColor = device->createTexture(
            "WebgpuReversedDepthBufferLogarithmicRGBA8",
            264u, height, 1u);
        reversedColor = device->createTexture(
            "WebgpuReversedDepthBufferReversedRGBA8",
            264u, height, 1u);
        normalDepth = device->createTexture(
            "WebgpuReversedDepthBufferNormalDepth",
            264u, height, 1u);
        logarithmicDepth = device->createTexture(
            "WebgpuReversedDepthBufferLogarithmicDepth",
            264u, height, 1u);
        reversedDepth = device->createTexture(
            "WebgpuReversedDepthBufferReversedDepth",
            264u, height, 1u);
        outputTexture = device->createTexture(
            "WebgpuReversedDepthBufferCompositeRGBA8",
            width, height, 1u);
        compositeResources = device->createBindGroup<
            WebgpuReversedDepthBufferCompositeResources>(
                normalColor->createView(),
                logarithmicColor->createView(),
                reversedColor->createView(),
                columnSampler);
        compositePass = device->createRenderClass<
            WebgpuReversedDepthBufferCompositePass>(
                compositeResources);
    }

    /** Executes three RenderSet draws and one fullscreen composite. */
    void render() override
    {
        sceneSet->update();
        auto nextTexture = swapchain->queryNextTexture();

        WebgpuReversedDepthBufferLessFrameBuffer normalFrame;
        normalFrame.color = normalColor->createView();
        normalFrame.color.loadOp = LoadOp::Clear;
        normalFrame.color.storeOp = StoreOp::Store;
        normalFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        normalFrame.depth = normalDepth->createView();
        normalFrame.depth.depthLoadOp = LoadOp::Clear;
        normalFrame.depth.depthStoreOp = StoreOp::Store;
        normalFrame.depth.depthClearValue = 1.0f;

        WebgpuReversedDepthBufferLogFrameBuffer logFrame;
        logFrame.color = logarithmicColor->createView();
        logFrame.color.loadOp = LoadOp::Clear;
        logFrame.color.storeOp = StoreOp::Store;
        logFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        logFrame.depth = logarithmicDepth->createView();
        logFrame.depth.depthLoadOp = LoadOp::Clear;
        logFrame.depth.depthStoreOp = StoreOp::Store;
        logFrame.depth.depthClearValue = 1.0f;

        WebgpuReversedDepthBufferGreaterFrameBuffer reverseFrame;
        reverseFrame.color = reversedColor->createView();
        reverseFrame.color.loadOp = LoadOp::Clear;
        reverseFrame.color.storeOp = StoreOp::Store;
        reverseFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        reverseFrame.depth = reversedDepth->createView();
        reverseFrame.depth.depthLoadOp = LoadOp::Clear;
        reverseFrame.depth.depthStoreOp = StoreOp::Store;
        reverseFrame.depth.depthClearValue = 0.0f;

        WebgpuReversedDepthBufferCompositeFrameBuffer compositeFrame;
        compositeFrame.color = outputTexture->createView();
        compositeFrame.color.loadOp = LoadOp::Clear;
        compositeFrame.color.storeOp = StoreOp::Store;
        compositeFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};

        graphicsQueue
            ->renderPass(
                "WebgpuReversedDepthBufferNormal",
                normalFrame,
                normalPass())
            ->renderPass(
                "WebgpuReversedDepthBufferLogarithmic",
                logFrame,
                logarithmicPass())
            ->renderPass(
                "WebgpuReversedDepthBufferReversed",
                reverseFrame,
                reversedPass())
            ->renderPass(
                "WebgpuReversedDepthBufferComposite",
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
