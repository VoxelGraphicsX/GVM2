#ifndef GVM_THREE_MISC_UV_TESTS_HPP
#define GVM_THREE_MISC_UV_TESTS_HPP

#include "MiscUvTestsVertexData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

static const uint MiscUvTestsOutputWidth = 800u;
static const uint MiscUvTestsOutputHeight = 500u;
static const uint MiscUvTestsCanvasExtent = 1024u;

/** Binds the sample-private Arial glyph coverage atlas. */
struct MiscUvTestsResources final : public IBindGroup
{
    /** Declares the immutable atlas and its linear clamp sampler. */
    constructor(
        Texture2D<float4> glyphAtlas [[Binding0]],
        Sampler glyphSampler [[Binding1]])
    {
    }
};

/** Stores the selected page-scroll placement of the 1024-square Canvas. */
struct MiscUvTestsCompositeUniforms
{
    float4 canvasOriginAndExtent;
};

/** Binds the rendered Canvas and its CSS-scale placement. */
struct MiscUvTestsCompositeResources final : public IBindGroup
{
    /** Declares the Canvas texture, linear sampler, and selected page origin. */
    constructor(
        UniformBuffer<MiscUvTestsCompositeUniforms> uniforms [[Binding0]],
        Texture2D<float4> canvasTexture [[Binding1]],
        Sampler canvasSampler [[Binding2]])
    {
    }
};

/** Carries screen position, atlas coordinates, and painter color to the fragment stage. */
struct MiscUvTestsVertexOutput
{
    float4 position [[Position]];
    float2 atlasCoordinate [[Attribute0]];
    float4 colorAndAtlasFlag [[Attribute1]];
    float4 lineSegment [[Attribute2]];
};

/** Carries one fullscreen page coordinate into the CSS composite fragment. */
struct MiscUvTestsScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the internal Canvas single-sample RGBA8 attachment. */
struct MiscUvTestsCanvasFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines the captured utility page's single-sample RGBA8 attachment. */
struct MiscUvTestsOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws exact r185 UV face records and sample-private glyph-atlas labels. */
class MiscUvTestsMainPass final : public IRenderClass
{
public:
    /** Configures painter-order alpha composition without depth or culling. */
    constructor(BindGroup<MiscUvTestsResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        BlendState blendState;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.color.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        setBlendState(0u, blendState);
    }

private:
    /** Converts one internal Canvas pixel into the fixed 1024-square clip space. */
    MiscUvTestsVertexOutput vertex(MiscUvTestsVertex inputValue [[VertexInput0]])
    {
        MiscUvTestsVertexOutput outputValue;
        outputValue.position = float4(
            inputValue.pixelPosition.x / 512.0f - 1.0f,
            inputValue.pixelPosition.y / 512.0f - 1.0f,
            0.0f,
            1.0f);
        outputValue.atlasCoordinate = inputValue.atlasCoordinate;
        outputValue.colorAndAtlasFlag = inputValue.colorAndAtlasFlag;
        outputValue.lineSegment = inputValue.lineSegment;
        return outputValue;
    }

    /** Composites analytic contour coverage or one glyph-atlas alpha lookup. */
    MiscUvTestsCanvasFrameBuffer fragment(MiscUvTestsVertexOutput inputValue)
    {
        float coverage = 1.0f;
        if (inputValue.colorAndAtlasFlag.w > 0.5f)
        {
            if (inputValue.colorAndAtlasFlag.w > 1.5f)
            {
                const float2 segmentStart = inputValue.lineSegment.xy;
                const float2 segmentEnd = inputValue.lineSegment.zw;
                const float2 segment = segmentEnd - segmentStart;
                const float segmentLengthSquared =
                    max(dot(segment, segment), 0.0001f);
                const float segmentParameter = clamp(
                    dot(inputValue.position.xy - segmentStart, segment) /
                        segmentLengthSquared,
                    0.0f,
                    1.0f);
                const float distanceToSegment = length(
                    inputValue.position.xy -
                    (segmentStart + segment * segmentParameter));
                coverage = clamp(
                    1.0f - distanceToSegment,
                    0.0f,
                    1.0f);
            }
            else
            {
                coverage = resources->glyphAtlas->sample(
                    resources->glyphSampler,
                    inputValue.atlasCoordinate).x;
            }
            coverage *= 0.96f;
        }
        MiscUvTestsCanvasFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(inputValue.colorAndAtlasFlag.xyz),
            half(coverage));
        return frameBuffer;
    }
};

/** Scales the exact 1024 Canvas into its 784-pixel CSS content box. */
class MiscUvTestsCompositePass final : public IRenderClass
{
public:
    /** Configures an opaque fullscreen page composite. */
    constructor(BindGroup<MiscUvTestsCompositeResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle and normalized page coordinates. */
    MiscUvTestsScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        MiscUvTestsScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the browser's linear CSS downscale and viewport crop. */
    MiscUvTestsOutputFrameBuffer fragment(MiscUvTestsScreenOutput inputValue)
    {
        const float2 pagePixel = inputValue.uv * float2(800.0f, 500.0f);
        const float4 placement = resources->uniforms->canvasOriginAndExtent;
        const float2 canvasUv =
            (pagePixel - placement.xy) / placement.zz;
        float3 pageColor = float3(1.0f);
        if (canvasUv.x >= 0.0f && canvasUv.x <= 1.0f &&
            canvasUv.y >= 0.0f && canvasUv.y <= 1.0f)
        {
            pageColor = resources->canvasTexture->sample(
                resources->canvasSampler,
                canvasUv).xyz;
        }
        MiscUvTestsOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(pageColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated UV utility compositor and its immutable generated geometry. */
class MiscUvTestsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<MiscUvTestsVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        glyphAtlas;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        canvasTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Sampler glyphSampler;
    Buffer<MiscUvTestsCompositeUniforms, BufferUsage<Uniform, CopyDst>>
        compositeUniformBuffer;
    BindGroup<MiscUvTestsResources> resources;
    BindGroup<MiscUvTestsCompositeResources> compositeResources;
    RenderClass<MiscUvTestsMainPass> mainPass;
    RenderClass<MiscUvTestsCompositePass> compositePass;
    uint indexCount = 0u;
    bool drawCurrentFrame = false;

public:
    /** Stores the device handles used by the scenario-sized immutable resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        glyphSampler = device->createSampler({
            .label = "MiscUvTestsGlyphSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        compositeUniformBuffer = device->createBuffer(
            "MiscUvTestsCompositeUniforms", 1u);
    }

    /** Allocates the fixed ordinary single-sample output. */
    void configureOutput(uint width, uint height)
    {
        if (width != MiscUvTestsOutputWidth || height != MiscUvTestsOutputHeight)
        {
            return;
        }
        outputTexture = device->createTexture(
            "MiscUvTestsOutput", width, height, 1u);
    }

    /** Uploads one exact geometry section plus the deterministic Arial coverage atlas. */
    void configureScene(
        const eastl::vector<MiscUvTestsVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint8_t> &atlasPixels,
        uint atlasWidth,
        uint atlasHeight,
        float canvasTop)
    {
        vertexBuffer = device->createBuffer(
            "MiscUvTestsVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "MiscUvTestsIndices", uint(indices.size()));
        glyphAtlas = device->createTexture(
            "MiscUvTestsArialAtlas", atlasWidth, atlasHeight, 1u);
        canvasTexture = device->createTexture(
            "MiscUvTestsInternalCanvas",
            MiscUvTestsCanvasExtent,
            MiscUvTestsCanvasExtent,
            1u);
        MiscUvTestsCompositeUniforms compositeUniforms;
        compositeUniforms.canvasOriginAndExtent =
            float4(8.0f, canvasTop, 784.0f, 0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(MiscUvTestsVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeTexture(
                glyphAtlas,
                atlasPixels.data(),
                uint64_t(atlasPixels.size()))
            ->writeBuffer(
                BufferRange(compositeUniformBuffer),
                &compositeUniforms,
                sizeof(compositeUniforms))
            ->submit();
        resources = device->createBindGroup<MiscUvTestsResources>(
            glyphAtlas->createView(), glyphSampler);
        mainPass = device->createRenderClass<MiscUvTestsMainPass>(resources);
        compositeResources =
            device->createBindGroup<MiscUvTestsCompositeResources>(
                compositeUniformBuffer,
                canvasTexture->createView(),
                glyphSampler);
        compositePass =
            device->createRenderClass<MiscUvTestsCompositePass>(
                compositeResources);
        indexCount = uint(indices.size());
    }

    /** Arms the one requested immutable target frame. */
    void updateFrame(bool shouldDraw)
    {
        drawCurrentFrame = shouldDraw;
    }

    /** Draws all UV contours and labels in source painter order. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (drawCurrentFrame)
        {
            MiscUvTestsCanvasFrameBuffer canvasFrameBuffer;
            canvasFrameBuffer.color = canvasTexture->createView();
            canvasFrameBuffer.color.loadOp = LoadOp::Clear;
            canvasFrameBuffer.color.storeOp = StoreOp::Store;
            canvasFrameBuffer.color.clearValue = {1.0, 1.0, 1.0, 1.0};
            MiscUvTestsOutputFrameBuffer outputFrameBuffer;
            outputFrameBuffer.color = outputTexture->createView();
            outputFrameBuffer.color.loadOp = LoadOp::Clear;
            outputFrameBuffer.color.storeOp = StoreOp::Store;
            outputFrameBuffer.color.clearValue = {1.0, 1.0, 1.0, 1.0};
            graphicsQueue
                ->renderPass(
                    "MiscUvTestsInternalCanvas",
                    canvasFrameBuffer,
                    mainPass->setVertexBuffer(vertexBuffer),
                    mainPass->setIndexBuffer(indexBuffer),
                    mainPass(indexCount, 1u, 0u, 0, 0u))
                ->renderPass(
                    "MiscUvTestsCssComposite",
                    outputFrameBuffer,
                    compositePass(3u, 1u, 0u, 0u))
                ->submit();
            drawCurrentFrame = false;
        }
        graphicsQueue
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 utility page. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the locked output width. */
    uint getReadbackWidth() const
    {
        return MiscUvTestsOutputWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return MiscUvTestsOutputHeight;
    }

    /** Releases all scenario-sized buffers and textures. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(compositeUniformBuffer);
        device->freeTexture(glyphAtlas);
        device->freeTexture(canvasTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
