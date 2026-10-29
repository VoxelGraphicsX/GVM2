#ifndef GVM_THREE_WEBGL_GEOMETRY_TERRAIN_HPP
#define GVM_THREE_WEBGL_GEOMETRY_TERRAIN_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometryTerrainWidth = 256u;
static const uint WebglGeometryTerrainDepth = 256u;
static const uint WebglGeometryTerrainTextureExtent = 1024u;

/** Stores one indexed heightfield vertex and its texture coordinate. */
struct WebglGeometryTerrainVertex
{
    float4 position [[Attribute0]];
    float4 uv [[Attribute1]];
};

/** Stores the fixed camera, fog, texture, and frame controls. */
struct WebglGeometryTerrainUniforms
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 fogColorDensity;
    float4 textureExtentAndTime;
};

/** Binds height samples and the writable generated terrain texture. */
struct WebglGeometryTerrainComputeResources final : public IBindGroup
{
    /** Declares the CPU heightfield, storage texture, and fixed controls. */
    constructor(
        StructuredBuffer<uint> heights [[Binding0]],
        RWTexture2D<TextureFormat::RGBA8Unorm>
            generatedTexture [[Binding1]],
        UniformBuffer<WebglGeometryTerrainUniforms>
            uniforms [[Binding2]],
        StructuredBuffer<uint> grain [[Binding3]])
    {
    }
};

/** Binds the generated texture and fixed Scene uniforms. */
struct WebglGeometryTerrainDrawResources final : public IBindGroup
{
    /** Declares the sampled compute output and its clamp sampler. */
    constructor(
        Texture2D<float4> generatedTexture [[Binding0]],
        Sampler terrainSampler [[Binding1]],
        UniformBuffer<WebglGeometryTerrainUniforms>
            uniforms [[Binding2]])
    {
    }
};

/** Carries texture coordinates and view distance to the terrain material. */
struct WebglGeometryTerrainVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float viewDistance [[Attribute1]];
};

/** Defines the final terrain color and depth targets. */
struct WebglGeometryTerrainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces one Uint8-clamped source texel before Canvas 4x scaling. */
float3 webglGeometryTerrainSourceColor(
    float leftHeight,
    float rightHeight,
    float upperHeight,
    float lowerHeight,
    float centerHeight,
    float valid)
{
    if (valid < 0.5f)
    {
        return float3(0.0f);
    }
    const float3 gradient = normalize(float3(
        leftHeight - rightHeight,
        2.0f,
        upperHeight - lowerHeight));
    const float shade =
        dot(gradient, normalize(float3(1.0f)));
    const float heightScale =
        0.5f + centerHeight * 0.007f;
    const float3 sourceBytes =
        clamp(
            (float3(96.0f, 32.0f, 0.0f) +
             shade * float3(128.0f, 96.0f, 96.0f)) *
                heightScale,
            float3(0.0f),
            float3(255.0f));
    return floor(sourceBytes + float3(0.5f));
}

/** Generates the deterministic 4x terrain texture entirely in DSL Compute. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglGeometryTerrainTexturePass final : public IComputeClass
{
public:
    /** Binds the existing structured height and storage-texture resources. */
    constructor(
        BindGroup<WebglGeometryTerrainComputeResources>
            resources [[Slot0]])
    {
    }

private:
    /** Shades one output texel using the r185 height gradients and grain. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint x = dispatchThreadID.x;
        const uint y = dispatchThreadID.y;
        if (x >= WebglGeometryTerrainTextureExtent ||
            y >= WebglGeometryTerrainTextureExtent)
        {
            return;
        }
        const float2 sourceCoordinate =
            (float2(float(x), float(y)) + float2(0.5f)) *
                0.25f -
            float2(0.5f);
        const float2 lowerCoordinate =
            floor(sourceCoordinate);
        const float2 interpolation =
            sourceCoordinate - lowerCoordinate;
        const int lowerX = int(lowerCoordinate.x);
        const int lowerY = int(lowerCoordinate.y);
        float3 sourceColors[4u];
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            const int cornerX = clamp(
                lowerX + int(corner & 1u),
                0,
                int(WebglGeometryTerrainWidth - 1u));
            const int cornerY = clamp(
                lowerY + int(corner >> 1u),
                0,
                int(WebglGeometryTerrainDepth - 1u));
            const uint sourceX = uint(cornerX);
            const uint sourceY = uint(cornerY);
            const uint sourceIndex =
                sourceY * WebglGeometryTerrainWidth +
                sourceX;
            const float valid =
                sourceIndex >=
                    WebglGeometryTerrainWidth * 2u &&
                sourceIndex + WebglGeometryTerrainWidth * 2u <
                    WebglGeometryTerrainWidth *
                        WebglGeometryTerrainDepth
                    ? 1.0f
                    : 0.0f;
            sourceColors[corner] =
                webglGeometryTerrainSourceColor(
                    float(resources->heights[
                        sourceIndex >= 2u
                            ? sourceIndex - 2u
                            : 0u]),
                    float(resources->heights[
                        min(
                            sourceIndex + 2u,
                            WebglGeometryTerrainWidth *
                                    WebglGeometryTerrainDepth -
                                1u)]),
                    float(resources->heights[
                        sourceIndex >=
                                WebglGeometryTerrainWidth * 2u
                            ? sourceIndex -
                                WebglGeometryTerrainWidth * 2u
                            : 0u]),
                    float(resources->heights[
                        min(
                            sourceIndex +
                                WebglGeometryTerrainWidth * 2u,
                            WebglGeometryTerrainWidth *
                                    WebglGeometryTerrainDepth -
                                1u)]),
                    float(resources->heights[
                        sourceIndex]),
                    valid);
        }
        const float3 upperColor =
            lerp(
                sourceColors[0u],
                sourceColors[1u],
                interpolation.x);
        const float3 lowerColor =
            lerp(
                sourceColors[2u],
                sourceColors[3u],
                interpolation.x);
        const float3 scaledColor =
            floor(
                lerp(
                    upperColor,
                    lowerColor,
                    interpolation.y) +
                float3(0.5f));
        const uint linearIndex =
            y * WebglGeometryTerrainTextureExtent + x;
        const float grain =
            float(resources->grain[linearIndex]);
        const float3 encoded =
            clamp(
                scaledColor +
                float3(grain),
                float3(0.0f),
                float3(255.0f)) /
            255.0f;
        resources->generatedTexture->write(
            uint2(x, y),
            half4(half3(encoded), half(1.0f)));
    }
};

/** Draws the one non-instanced terrain mesh with texture and exp2 fog. */
class WebglGeometryTerrainMainPass final : public IRenderClass
{
public:
    /** Binds standalone terrain geometry and opaque depth state. */
    constructor(
        BindGroup<WebglGeometryTerrainDrawResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one heightfield vertex and forwards view distance. */
    WebglGeometryTerrainVertexOutput vertex(
        WebglGeometryTerrainVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition =
            mul(resources->uniforms->modelView, inputValue.position);
        WebglGeometryTerrainVertexOutput outputValue;
        outputValue.position =
            mul(
                resources->uniforms->modelViewProjection,
                inputValue.position);
        outputValue.uv = inputValue.uv.xy;
        outputValue.viewDistance = -viewPosition.z;
        return outputValue;
    }

    /** Samples the generated texture and applies the r185 encoded-space fog mix. */
    WebglGeometryTerrainFrameBuffer fragment(
        WebglGeometryTerrainVertexOutput inputValue)
    {
        const float3 sampled =
            resources->generatedTexture->sample(
                resources->terrainSampler,
                inputValue.uv).xyz;
        const float density =
            resources->uniforms->fogColorDensity.w;
        const float fogFactor =
            1.0f -
            exp(
                -density * density *
                inputValue.viewDistance *
                inputValue.viewDistance);
        const float3 fogged =
            lerp(
                sampled,
                float3(
                    resources->uniforms->fogColorDensity.x,
                    resources->uniforms->fogColorDensity.y,
                    resources->uniforms->fogColorDensity.z),
                fogFactor);
        WebglGeometryTerrainFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(fogged),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the standalone heightfield, texture Compute, and main Scene pass. */
class WebglGeometryTerrainRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglGeometryTerrainVertex,
           BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> heightBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> grainBuffer;
    Buffer<WebglGeometryTerrainUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, TextureBinding>,
            TextureDimension::e2D> generatedTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    BindGroup<WebglGeometryTerrainComputeResources> computeResources;
    BindGroup<WebglGeometryTerrainDrawResources> drawResources;
    ComputeClass<WebglGeometryTerrainTexturePass> texturePass;
    RenderClass<WebglGeometryTerrainMainPass> mainPass;
    Sampler terrainSampler;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;
    bool textureGenerated = false;

public:
    /** Creates all dedicated standalone and Compute resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglGeometryTerrainVertices",
            WebglGeometryTerrainWidth *
                WebglGeometryTerrainDepth);
        indexBuffer = device->createBuffer(
            "WebglGeometryTerrainIndices",
            (WebglGeometryTerrainWidth - 1u) *
                (WebglGeometryTerrainDepth - 1u) *
                6u);
        heightBuffer = device->createBuffer(
            "WebglGeometryTerrainHeights",
            WebglGeometryTerrainWidth *
                WebglGeometryTerrainDepth);
        grainBuffer = device->createBuffer(
            "WebglGeometryTerrainGrain",
            WebglGeometryTerrainTextureExtent *
                WebglGeometryTerrainTextureExtent);
        uniformBuffer = device->createBuffer(
            "WebglGeometryTerrainUniforms",
            1u);
        generatedTexture = device->createTexture(
            "WebglGeometryTerrainGeneratedTexture",
            WebglGeometryTerrainTextureExtent,
            WebglGeometryTerrainTextureExtent,
            1u);
        terrainSampler = device->createSampler({
            .label = "WebglGeometryTerrainSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        computeResources =
            device->createBindGroup<
                WebglGeometryTerrainComputeResources>(
                heightBuffer,
                generatedTexture->createView(),
                uniformBuffer,
                grainBuffer);
        drawResources =
            device->createBindGroup<
                WebglGeometryTerrainDrawResources>(
                generatedTexture->createView(),
                terrainSampler,
                uniformBuffer);
        texturePass =
            device->createComputeClass<
                WebglGeometryTerrainTexturePass>(
                computeResources);
        mainPass =
            device->createRenderClass<
                WebglGeometryTerrainMainPass>(
                drawResources);
    }

    /** Allocates the host-sized final color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture(
            "WebglGeometryTerrainOutput",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglGeometryTerrainDepth",
            width,
            height,
            1u);
    }

    /** Uploads exact CPU heights, indexed PlaneGeometry, and camera state. */
    void configureScene(
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &textureCoordinates,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint> &heights,
        const eastl::vector<uint> &grain,
        float4 mvp0,
        float4 mvp1,
        float4 mvp2,
        float4 mvp3,
        float4 modelView0,
        float4 modelView1,
        float4 modelView2,
        float4 modelView3,
        float timeValue)
    {
        indexCount = uint(indices.size());
        eastl::vector<WebglGeometryTerrainVertex> vertices;
        vertices.resize(positions.size());
        for (uint vertexIndex = 0u;
             vertexIndex < uint(vertices.size());
             ++vertexIndex)
        {
            vertices[vertexIndex].position =
                positions[vertexIndex];
            vertices[vertexIndex].uv =
                textureCoordinates[vertexIndex];
        }
        WebglGeometryTerrainUniforms uniforms;
        uniforms.modelViewProjection =
            float4x4(mvp0, mvp1, mvp2, mvp3);
        uniforms.modelView =
            float4x4(
                modelView0,
                modelView1,
                modelView2,
                modelView3);
        uniforms.fogColorDensity =
            float4(
                0.937255f,
                0.819608f,
                0.709804f,
                0.0025f);
        uniforms.textureExtentAndTime =
            float4(
                float(WebglGeometryTerrainTextureExtent),
                float(WebglGeometryTerrainTextureExtent),
                timeValue,
                0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglGeometryTerrainVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(heightBuffer),
                heights.data(),
                uint64_t(heights.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(grainBuffer),
                grain.data(),
                uint64_t(grain.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
    }

    /** Generates the texture once, then draws the one ordinary terrain Scene. */
    void render() override
    {
        if (!textureGenerated)
        {
            graphicsQueue
                ->computePass(
                    "WebglGeometryTerrainTexture",
                    texturePass(
                        WebglGeometryTerrainTextureExtent,
                        WebglGeometryTerrainTextureExtent,
                        1u))
                ->submit();
            textureGenerated = true;
        }
        WebglGeometryTerrainFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue =
            {0.937255f, 0.819608f, 0.709804f, 1.0f};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometryTerrainMain",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the fixed output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the fixed output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases all standalone buffers and DSL-owned textures. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(heightBuffer);
        device->freeBuffer(grainBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(generatedTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
