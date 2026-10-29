#ifndef GVM_THREE_WEBGPU_EQUIRECTANGULAR_SCREEN_HPP
#define GVM_THREE_WEBGPU_EQUIRECTANGULAR_SCREEN_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the deterministic camera and background state for one frame. */
struct WebgpuEquirectangularUniforms
{
    float4 orbitAndIntensity;
    float4 projectionAndViewport;
};

/** Binds the pinned panorama, sampler, and current camera state. */
struct WebgpuEquirectangularResources final : public IBindGroup
{
    /** Declares the complete screen-only background resource set. */
    constructor(
        UniformBuffer<WebgpuEquirectangularUniforms> uniforms [[Binding0]],
        Texture2D<float4> panorama [[Binding1]],
        Sampler panoramaSampler [[Binding2]])
    {
    }
};

/** Carries normalized screen coordinates into the background fragment. */
struct WebgpuEquirectangularVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final screen-only RGBA8 attachment. */
struct WebgpuEquirectangularFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one sRGB texture channel into linear-light Rec.709. */
float webgpuEquirectangularSrgbToLinear(float value)
{
    if (value <= 0.04045f) return value / 12.92f;
    return pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Converts one linear-light channel into the renderer sRGB output encoding. */
float webgpuEquirectangularLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Returns pixel coverage for one antialiased rounded screen rectangle. */
float webgpuEquirectangularRoundedCoverage(
    float2 pixelPosition,
    float2 center,
    float2 halfExtent,
    float radius)
{
    const float2 distanceToEdge =
        abs(pixelPosition - center) - halfExtent + radius;
    const float signedDistance =
        length(max(distanceToEdge, float2(0.0f, 0.0f))) +
        min(max(distanceToEdge.x, distanceToEdge.y), 0.0f) -
        radius;
    return saturate(0.5f - signedDistance);
}

/** Renders the equirectangular Scene background without Scene geometry. */
class WebgpuEquirectangularMainPass final : public IRenderClass
{
public:
    /** Binds only the screen background resources and disables culling. */
    constructor(BindGroup<WebgpuEquirectangularResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle and its unclamped normalized coordinates. */
    WebgpuEquirectangularVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 screenUv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuEquirectangularVertexOutput outputValue;
        outputValue.position =
            float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Reconstructs the perspective world ray and samples explicit mip zero. */
    WebgpuEquirectangularFrameBuffer fragment(
        WebgpuEquirectangularVertexOutput inputValue)
    {
        const float theta =
            resources->uniforms->orbitAndIntensity.x;
        const float phi =
            resources->uniforms->orbitAndIntensity.y;
        const float intensity =
            resources->uniforms->orbitAndIntensity.z;
        const float aspect =
            resources->uniforms->projectionAndViewport.x;
        const float tangentHalfFov =
            resources->uniforms->projectionAndViewport.y;
        const float3 cameraPosition = float3(
            sin(phi) * sin(theta),
            cos(phi),
            sin(phi) * cos(theta));
        const float3 forward = normalize(-cameraPosition);
        const float3 right =
            normalize(cross(forward, float3(0.0f, 1.0f, 0.0f)));
        const float3 cameraUp = normalize(cross(right, forward));
        const float2 ndc = inputValue.screenUv * 2.0f - 1.0f;
        const float3 direction = normalize(
            forward +
            right * (ndc.x * aspect * tangentHalfFov) +
            cameraUp * (ndc.y * tangentHalfFov));
        const float2 panoramaUv = float2(
            atan2(direction.z, direction.x) *
                    0.15915494309189535f +
                0.5f,
            0.5f -
                asin(clamp(direction.y, -1.0f, 1.0f)) *
                    0.3183098861837907f);
        const float3 encoded =
            resources->panorama
                ->sampleLevel(
                    resources->panoramaSampler,
                    panoramaUv,
                    0.0f)
                .xyz;
        const float3 linearColor = float3(
            webgpuEquirectangularSrgbToLinear(encoded.x),
            webgpuEquirectangularSrgbToLinear(encoded.y),
            webgpuEquirectangularSrgbToLinear(encoded.z)) *
            intensity;
        float3 displayColor = float3(
            webgpuEquirectangularLinearToSrgb(linearColor.x),
            webgpuEquirectangularLinearToSrgb(linearColor.y),
            webgpuEquirectangularLinearToSrgb(linearColor.z));
        const float2 pixelPosition = inputValue.position.xy;
        const float inspectorVisibility =
            intensity >= 0.999f ? 1.0f : 0.0f;
        const float outerCoverage =
            webgpuEquirectangularRoundedCoverage(
                pixelPosition,
                float2(699.5f, 34.5f),
                float2(85.5f, 19.5f),
                13.0f) *
            inspectorVisibility;
        const float innerCoverage =
            webgpuEquirectangularRoundedCoverage(
                pixelPosition,
                float2(699.5f, 34.5f),
                float2(84.5f, 18.5f),
                12.0f) *
            inspectorVisibility;
        displayColor = lerp(
            displayColor,
            float3(45.0f, 45.0f, 52.0f) / 255.0f,
            outerCoverage);
        const float3 sliderColor =
            pixelPosition.x < 663.0f
            ? float3(24.0f, 58.0f, 78.0f) / 255.0f
            : float3(30.0f, 30.0f, 34.0f) / 255.0f;
        displayColor = lerp(
            displayColor,
            sliderColor,
            innerCoverage);
        WebgpuEquirectangularFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(displayColor),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the pinned panorama, fullscreen pass, and deterministic orbit state. */
class WebgpuEquirectangularScreenRenderer final : public AbstractRenderer
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
    Buffer<WebgpuEquirectangularUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler panoramaSampler;
    BindGroup<WebgpuEquirectangularResources> resources;
    RenderClass<WebgpuEquirectangularMainPass> mainPass;
    uint width = 800u;
    uint height = 500u;
    uint frameIndex = 0u;
    float initialTheta = 1.5707963267948966f;
    float initialPhi = 1.5707963267948966f;
    float manualThetaOffset = 0.0f;
    float manualPhiOffset = 0.0f;
    float backgroundIntensity = 1.0f;

public:
    /** Creates the uniform buffer and exact linear panorama sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer =
            device->createBuffer("WebgpuEquirectangularUniforms", 1u);
        panoramaSampler = device->createSampler({
            .label = "WebgpuEquirectangularSampler",
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

    /** Allocates the canonical final RGBA8 readback target. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuEquirectangularOutput", width, height, 1u);
    }

    /** Uploads the locked explicit mip chain and selected scenario state. */
    void configureScene(
        const eastl::vector<eastl::vector<uint8_t>> &mipLevels,
        uint panoramaWidth,
        uint panoramaHeight,
        float inBackgroundIntensity,
        float inManualThetaOffset,
        float inManualPhiOffset)
    {
        panorama = device->createTexture(
            "WebgpuEquirectangularPanorama",
            panoramaWidth,
            panoramaHeight,
            1u,
            uint(mipLevels.size()));
        for (uint mipLevel = 0u;
             mipLevel < uint(mipLevels.size());
             ++mipLevel)
        {
            graphicsQueue->writeTexture(
                panorama,
                mipLevels[mipLevel].data(),
                uint64_t(mipLevels[mipLevel].size()),
                mipLevel);
        }
        backgroundIntensity = inBackgroundIntensity;
        manualThetaOffset = inManualThetaOffset;
        manualPhiOffset = inManualPhiOffset;
        resources =
            device->createBindGroup<WebgpuEquirectangularResources>(
                uniformBuffer,
                panorama->createView(),
                panoramaSampler);
        mainPass =
            device->createRenderClass<WebgpuEquirectangularMainPass>(
                resources);
        graphicsQueue->submit();
    }

    /** Advances fixed-step auto rotation and draws the screen-only Scene. */
    void render() override
    {
        const float autoRotationAngle =
            6.2831853071795865f / 3600.0f;
        const float theta =
            initialTheta + manualThetaOffset -
            autoRotationAngle * float(frameIndex + 1u);
        const float phi =
            clamp(
                initialPhi + manualPhiOffset,
                0.000001f,
                3.141591653589793f);
        const WebgpuEquirectangularUniforms uniforms = {
            float4(theta, phi, backgroundIntensity, 0.0f),
            float4(
                float(width) / float(height),
                0.4142135623730950f,
                float(width),
                float(height))};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebgpuEquirectangularFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuEquirectangularMain",
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

    /** Returns the final DSL-owned background texture. */
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

    /** Releases every screen-only panorama resource. */
    void destroy() override
    {
        device->freeTexture(panorama);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
