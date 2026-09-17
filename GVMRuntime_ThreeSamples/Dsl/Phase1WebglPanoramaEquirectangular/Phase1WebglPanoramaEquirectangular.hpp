#ifndef GVM_THREE_PHASE1_WEBGL_PANORAMA_EQUIRECTANGULAR_HPP
#define GVM_THREE_PHASE1_WEBGL_PANORAMA_EQUIRECTANGULAR_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the exact panorama longitude, latitude, FOV, and viewport state. */
struct WebglPanoramaEquirectangularUniforms
{
    float4 longitudeLatitudeFovAndReserved;
    float4 viewportAndReserved;
};

/** Binds the pinned panorama and deterministic camera state. */
struct WebglPanoramaEquirectangularResources final : public IBindGroup
{
    /** Declares the complete ordinary Scene material resource set. */
    constructor(
        UniformBuffer<WebglPanoramaEquirectangularUniforms>
            uniforms [[Binding0]],
        Texture2D<float4> panorama [[Binding1]],
        Sampler panoramaSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the inward-sphere fragment. */
struct WebglPanoramaEquirectangularVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final deterministic RGBA8 attachment. */
struct WebglPanoramaEquirectangularFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws the one inward SphereGeometry through its analytic perspective ray. */
class WebglPanoramaEquirectangularMainPass final : public IRenderClass
{
public:
    /** Binds one texture material without a RenderSet or C++ draw path. */
    constructor(
        BindGroup<WebglPanoramaEquirectangularResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle for the enclosing sphere projection. */
    WebglPanoramaEquirectangularVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglPanoramaEquirectangularVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Reconstructs the camera ray and samples the original sphere UV map. */
    WebglPanoramaEquirectangularFrameBuffer fragment(
        WebglPanoramaEquirectangularVertexOutput inputValue)
    {
        const float longitude =
            resources->uniforms
                ->longitudeLatitudeFovAndReserved.x;
        const float latitude =
            resources->uniforms
                ->longitudeLatitudeFovAndReserved.y;
        const float fov =
            resources->uniforms
                ->longitudeLatitudeFovAndReserved.z;
        const float aspect =
            resources->uniforms->viewportAndReserved.x /
            resources->uniforms->viewportAndReserved.y;
        const float theta =
            longitude * 0.017453292519943295f;
        const float phi =
            (90.0f - latitude) *
            0.017453292519943295f;
        const float3 forward = normalize(float3(
            sin(phi) * cos(theta),
            cos(phi),
            sin(phi) * sin(theta)));
        const float3 right = normalize(
            cross(forward, float3(0.0f, 1.0f, 0.0f)));
        const float3 cameraUp =
            normalize(cross(right, forward));
        const float tangentHalfFov =
            tan(fov * 0.008726646259971648f);
        const float2 ndc =
            inputValue.screenUv * 2.0f - 1.0f;
        const float3 direction = normalize(
            forward +
            right * ndc.x * aspect * tangentHalfFov +
            cameraUp * ndc.y * tangentHalfFov);
        const float2 panoramaUv = float2(
            atan2(direction.z, direction.x) *
                0.15915494309189535f,
            0.5f -
                asin(clamp(direction.y, -1.0f, 1.0f)) *
                    0.3183098861837907f);
        const float3 encoded =
            resources->panorama->sample(
                resources->panoramaSampler,
                panoramaUv).xyz;
        WebglPanoramaEquirectangularFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(encoded), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the pinned panorama, ordinary Scene pass, and fixed-step camera. */
class Phase1WebglPanoramaEquirectangularRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> panorama;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebglPanoramaEquirectangularUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler panoramaSampler;
    BindGroup<WebglPanoramaEquirectangularResources> resources;
    RenderClass<WebglPanoramaEquirectangularMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;
    float baseLongitude = 0.0f;
    float latitude = 0.0f;
    float fieldOfView = 75.0f;

public:
    /** Creates the uniform buffer and exact mipmapped panorama sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer =
            device->createBuffer(
                "WebglPanoramaEquirectangularUniforms",
                1u);
        panoramaSampler = device->createSampler({
            .label = "WebglPanoramaEquirectangularSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 12.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the final deterministic RGBA8 readback texture. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor =
            device->createTexture(
                "WebglPanoramaEquirectangularOutput",
                width,
                height,
                1u);
    }

    /** Uploads the locked explicit mip chain and selected camera state. */
    void configureScene(
        const eastl::vector<eastl::vector<uint8_t>> &mipLevels,
        uint panoramaWidth,
        uint panoramaHeight,
        float inBaseLongitude,
        float inLatitude,
        float inFieldOfView)
    {
        panorama = device->createTexture(
            "WebglPanoramaEquirectangularPanorama",
            panoramaWidth,
            panoramaHeight,
            1u,
            uint(mipLevels.size()));
        for (uint mipLevel = 0u;
             mipLevel < uint(mipLevels.size());
             mipLevel += 1u)
        {
            graphicsQueue->writeTexture(
                panorama,
                mipLevels[mipLevel].data(),
                uint64_t(mipLevels[mipLevel].size()),
                mipLevel);
        }
        baseLongitude = inBaseLongitude;
        latitude = inLatitude;
        fieldOfView = inFieldOfView;
        resources =
            device->createBindGroup<
                WebglPanoramaEquirectangularResources>(
                    uniformBuffer,
                    panorama->createView(),
                    panoramaSampler);
        mainPass =
            device->createRenderClass<
                WebglPanoramaEquirectangularMainPass>(
                    resources);
        graphicsQueue->submit();
    }

    /** Advances the exact auto-pan and draws one ordinary Scene object. */
    void render() override
    {
        const WebglPanoramaEquirectangularUniforms uniforms = {
            float4(
                baseLongitude +
                    0.1f * float(frameIndex + 1u),
                latitude,
                fieldOfView,
                0.0f),
            float4(
                float(width),
                float(height),
                0.0f,
                0.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebglPanoramaEquirectangularFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue =
            {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture =
            swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglPanoramaEquirectangularMain",
                frameBuffer,
                mainPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        frameIndex += 1u;
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the panorama, output, and uniform resources. */
    void destroy() override
    {
        device->freeTexture(panorama);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
