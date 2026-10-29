#ifndef GVM_THREE_WEBGL_GEOMETRY_MINECRAFT_HPP
#define GVM_THREE_WEBGL_GEOMETRY_MINECRAFT_HPP

#include "UGL.h"
#include "WebglGeometryMinecraftData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglGeometryMinecraftVertexCount = 92124u;
static const uint WebglGeometryMinecraftIndexCount = 138186u;
static const uint WebglGeometryMinecraftAtlasWidth = 16u;
static const uint WebglGeometryMinecraftAtlasHeight = 32u;
static const uint WebglGeometryMinecraftAtlasMipCount = 6u;

/** Binds the current camera, sRGB atlas, and immutable Three-compatible sampler. */
struct WebglGeometryMinecraftSceneResources final : public IBindGroup
{
    /** Declares all resources consumed by the sole ordinary Scene draw. */
    constructor(
        UniformBuffer<WebglGeometryMinecraftUniforms> uniforms [[Binding0]],
        Texture2D<float4> atlasTexture [[Binding1]],
        Sampler atlasSampler [[Binding2]])
    {
    }
};

/** Carries atlas coordinates and Three's negated view position to Lambert shading. */
struct WebglGeometryMinecraftSceneVertexOutput
{
    float4 position [[Position]];
    float2 texCoord [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    float2 samplePosition [[Attribute2]];
};

/** Defines one full-size deterministic coverage color and depth pair. */
struct WebglGeometryMinecraftSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel with Three r185's default sRGB output transfer. */
float webglGeometryMinecraftLinearToSrgb(float value)
{
    return value <= 0.0031308f
               ? value * 12.92f
               : pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Locks one derivative normal to the exact axis of the source PlaneGeometry face. */
float3 webglGeometryMinecraftAxisNormal(float3 derivativeNormal)
{
    const float3 magnitude = abs(derivativeNormal);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return float3(
            derivativeNormal.x >= 0.0f ? 1.0f : -1.0f,
            0.0f,
            0.0f);
    }
    if (magnitude.y >= magnitude.z)
    {
        return float3(
            0.0f,
            derivativeNormal.y >= 0.0f ? 1.0f : -1.0f,
            0.0f);
    }
    return float3(
        0.0f,
        0.0f,
        derivativeNormal.z >= 0.0f ? 1.0f : -1.0f);
}

/** Draws the one CPU-merged atlas Mesh with r185 Lambert and DoubleSide semantics. */
class WebglGeometryMinecraftMainPass final : public IRenderClass
{
public:
    /** Binds the standalone one-object Scene and preserves opaque depth behavior. */
    constructor(BindGroup<WebglGeometryMinecraftSceneResources> sceneResources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the current camera and converts OpenGL clip conventions for both backends. */
    WebglGeometryMinecraftSceneVertexOutput vertex(
        WebglGeometryMinecraftVertex inputValue [[VertexInput0]])
    {
        const float4 cameraPosition = mul(
            sceneResources->uniforms->viewMatrix,
            float4(inputValue.position, 1.0f));
        float4 clipPosition = mul(
            sceneResources->uniforms->projectionMatrix,
            cameraPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        const float2 samplePosition =
            sceneResources->uniforms->samplePositionAndReserved.xy;
        clipPosition.x +=
            (1.0f - 2.0f * samplePosition.x) / 800.0f * clipPosition.w;
        clipPosition.y +=
            (2.0f * samplePosition.y - 1.0f) / 500.0f * clipPosition.w;

        WebglGeometryMinecraftSceneVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.texCoord = inputValue.texCoord;
        outputValue.viewPosition = -cameraPosition.xyz;
        outputValue.samplePosition = samplePosition;
        return outputValue;
    }

    /** Samples the sRGB atlas and reproduces ambient plus directional Lambert irradiance. */
    WebglGeometryMinecraftSceneFrameBuffer fragment(
        WebglGeometryMinecraftSceneVertexOutput inputValue)
    {
        const float3 normal = webglGeometryMinecraftAxisNormal(cross(
            ddy(inputValue.viewPosition),
            ddx(inputValue.viewPosition)));
        const float3 directionalDirection =
            float3(0.6666666667f, 0.6666666667f, 0.3333333333f);
        const float directionalDot =
            saturate(dot(normal, directionalDirection));
        const float2 centerTexCoord =
            inputValue.texCoord +
            ddx(inputValue.texCoord) *
                (0.5f - inputValue.samplePosition.x) +
            ddy(inputValue.texCoord) *
                (inputValue.samplePosition.y - 0.5f);
        const float3 atlasColor = sceneResources->atlasTexture
                                      ->sample(
                                          sceneResources->atlasSampler,
                                          centerTexCoord)
                                      .xyz;
        const float ambientIrradiance = 2.5649778244f;
        const float totalIrradiance =
            ambientIrradiance + directionalDot * 12.0f;
        const float3 linearColor =
            atlasColor * totalIrradiance * 0.3183098861837907f;
        const float3 displayColor = float3(
            webglGeometryMinecraftLinearToSrgb(linearColor.x),
            webglGeometryMinecraftLinearToSrgb(linearColor.y),
            webglGeometryMinecraftLinearToSrgb(linearColor.z));

        WebglGeometryMinecraftSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Returns one camera payload fixed to the native pixel-center sample. */
WebglGeometryMinecraftUniforms webglGeometryMinecraftSingleSampleUniforms(
    WebglGeometryMinecraftUniforms source)
{
    source.samplePositionAndReserved =
        float4(0.5f, 0.5f, 0.0f, 0.0f);
    return source;
}

/** Owns the ordinary one-Mesh Scene and its single-sample output. */
class WebglGeometryMinecraftRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglGeometryMinecraftVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglGeometryMinecraftUniforms, BufferUsage<Uniform, CopyDst>>
        uniformBuffer0;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        atlasTexture;
    Sampler atlasSampler;
    BindGroup<WebglGeometryMinecraftSceneResources> sceneResources0;
    RenderClass<WebglGeometryMinecraftMainPass> mainPass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        outputDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates fixed standalone buffers and the immutable atlas sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglGeometryMinecraftVertices",
            WebglGeometryMinecraftVertexCount);
        indexBuffer = device->createBuffer(
            "WebglGeometryMinecraftIndices",
            WebglGeometryMinecraftIndexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglGeometryMinecraftUniforms0",
            1u);
        atlasTexture = device->createTexture(
            "WebglGeometryMinecraftAtlasSrgb",
            WebglGeometryMinecraftAtlasWidth,
            WebglGeometryMinecraftAtlasHeight,
            1u,
            WebglGeometryMinecraftAtlasMipCount);
        atlasSampler = device->createSampler({
            .label = "WebglGeometryMinecraftAtlasSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 5.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the requested-size single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture(
            "WebglGeometryMinecraftOutputRGBA8",
            width,
            height,
            1u);
        outputDepth = device->createTexture(
            "WebglGeometryMinecraftOutputDepth32",
            width,
            height,
            1u);
    }

    /** Uploads geometry, atlas mips, and the single-sample camera state. */
    void configureScene(
        const eastl::vector<WebglGeometryMinecraftVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<uint8_t> &atlasBytes,
        const eastl::vector<uint64_t> &atlasMipOffsets,
        WebglGeometryMinecraftUniforms uniforms)
    {
        const WebglGeometryMinecraftUniforms uniforms0 =
            webglGeometryMinecraftSingleSampleUniforms(uniforms);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglGeometryMinecraftVertexCount) *
                    sizeof(WebglGeometryMinecraftVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(WebglGeometryMinecraftIndexCount) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer0),
                &uniforms0,
                sizeof(uniforms0))
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[0u],
                atlasMipOffsets[1u] - atlasMipOffsets[0u],
                0u)
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[1u],
                atlasMipOffsets[2u] - atlasMipOffsets[1u],
                1u)
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[2u],
                atlasMipOffsets[3u] - atlasMipOffsets[2u],
                2u)
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[3u],
                atlasMipOffsets[4u] - atlasMipOffsets[3u],
                3u)
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[4u],
                atlasMipOffsets[5u] - atlasMipOffsets[4u],
                4u)
            ->writeTexture(
                atlasTexture,
                atlasBytes.data() + atlasMipOffsets[5u],
                atlasBytes.size() - atlasMipOffsets[5u],
                5u)
            ->submit();
        sceneResources0 =
            device->createBindGroup<WebglGeometryMinecraftSceneResources>(
                uniformBuffer0,
                atlasTexture->createView(),
                atlasSampler);
        mainPass0 =
            device->createRenderClass<WebglGeometryMinecraftMainPass>(
                sceneResources0);
    }

    /** Uploads one fixed-step FirstPersonControls camera before the next draw. */
    void updateFrame(WebglGeometryMinecraftUniforms uniforms)
    {
        const WebglGeometryMinecraftUniforms uniforms0 =
            webglGeometryMinecraftSingleSampleUniforms(uniforms);
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer0),
                &uniforms0,
                sizeof(uniforms0))
            ->submit();
    }

    /** Draws the sole Mesh directly into the single-sample output. */
    void render() override
    {
        WebglGeometryMinecraftSceneFrameBuffer sceneFrameBuffer0;
        sceneFrameBuffer0.color = outputColor->createView();
        sceneFrameBuffer0.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer0.color.storeOp = StoreOp::Store;
        sceneFrameBuffer0.color.clearValue = {
            0.7490196078,
            0.8196078431,
            0.8980392157,
            1.0};
        sceneFrameBuffer0.depth = outputDepth->createView();
        sceneFrameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer0.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer0.depth.depthClearValue = 1.0f;

        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "main-atlas-lambert",
                sceneFrameBuffer0,
                mainPass0->setVertexBuffer(vertexBuffer),
                mainPass0->setIndexBuffer(indexBuffer),
                mainPass0(
                    WebglGeometryMinecraftIndexCount,
                    1u,
                    0u,
                    0,
                    0u))
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 output used by deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
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

    /** Releases every standalone buffer and private texture owned by this sample. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(atlasTexture);
        device->freeTexture(outputDepth);
        device->freeTexture(outputColor);
    }
};

#endif
