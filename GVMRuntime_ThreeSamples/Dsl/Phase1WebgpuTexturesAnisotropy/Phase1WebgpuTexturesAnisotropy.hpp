#ifndef GVM_THREE_PHASE1_WEBGPU_TEXTURES_ANISOTROPY_HPP
#define GVM_THREE_PHASE1_WEBGPU_TEXTURES_ANISOTROPY_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the deterministic WebGPU camera and viewport state. */
struct WebgpuTexturesAnisotropyUniforms
{
    float4 cameraPositionAndAspect;
    float4 viewportAndBackground;
};

/** Binds one explicit crate mip chain and one sampler policy. */
struct WebgpuTexturesAnisotropyResources final : public IBindGroup
{
    /** Declares the inputs used by one independent WebGPU Scene. */
    constructor(
        UniformBuffer<WebgpuTexturesAnisotropyUniforms>
            uniforms [[Binding0]],
        Texture2D<float4> crateTexture [[Binding1]],
        Sampler crateSampler [[Binding2]])
    {
    }
};

/** Carries fullscreen coordinates into the analytic PlaneGeometry material. */
struct WebgpuTexturesAnisotropyVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the shared deterministic RGBA8 attachment. */
struct WebgpuTexturesAnisotropyFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel with Three r185's output transfer. */
float webgpuTexturesAnisotropyLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reconstructs and shades the exact large horizontal r185 plane. */
float4 webgpuTexturesAnisotropyShadePlane(
    BindGroup<WebgpuTexturesAnisotropyResources> resources,
    float2 screenUv)
{
    const float3 cameraPosition =
        resources->uniforms->cameraPositionAndAspect.xyz;
    const float aspect =
        resources->uniforms->cameraPositionAndAspect.w;
    const float3 forward = normalize(-cameraPosition);
    const float3 right = normalize(
        cross(forward, float3(0.0f, 1.0f, 0.0f)));
    const float3 cameraUp = normalize(cross(right, forward));
    const float2 ndc = float2(
        screenUv.x * 2.0f - 1.0f,
        1.0f - screenUv.y * 2.0f);
    const float3 direction = normalize(
        forward +
        right * ndc.x * aspect * 0.3152987889f +
        cameraUp * ndc.y * 0.3152987889f);
    const float3 background =
        resources->uniforms->viewportAndBackground.yzw;
    if (direction.y >= -0.000001f)
    {
        return float4(background, 1.0f);
    }
    const float distance = -cameraPosition.y / direction.y;
    const float3 worldPosition =
        cameraPosition + direction * distance;
    if (distance < 1.0f ||
        distance > 25000.0f ||
        abs(worldPosition.x) > 50000.0f ||
        abs(worldPosition.z) > 50000.0f)
    {
        return float4(background, 1.0f);
    }
    const float2 textureUv =
        float2(
            worldPosition.x / 100000.0f + 0.5f,
            -worldPosition.z / 100000.0f + 0.5f) *
        512.0f;
    const float3 linearTexture =
        resources->crateTexture->sample(
            resources->crateSampler,
            textureUv).xyz;
    const float3 litTexture = linearTexture * 1.95f;
    const float3 encodedTexture = float3(
        webgpuTexturesAnisotropyLinearToSrgb(litTexture.x),
        webgpuTexturesAnisotropyLinearToSrgb(litTexture.y),
        webgpuTexturesAnisotropyLinearToSrgb(litTexture.z));
    const float fogFactor =
        smoothstep(1.0f, 25000.0f, distance);
    return float4(
        lerp(encodedTexture, background, fogFactor),
        1.0f);
}

/** Draws sceneLeft into the exact 398-pixel maximum-anisotropy region. */
class WebgpuTexturesAnisotropyLeftPass final : public IRenderClass
{
public:
    /** Configures the first independent ordinary Plane Scene. */
    constructor(
        BindGroup<WebgpuTexturesAnisotropyResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle with exact screen coordinates. */
    WebgpuTexturesAnisotropyVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTexturesAnisotropyVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = uv;
        return outputValue;
    }

    /** Applies the left scissor and maximum-anisotropy sampling. */
    WebgpuTexturesAnisotropyFrameBuffer fragment(
        WebgpuTexturesAnisotropyVertexOutput inputValue)
    {
        const float pixelX =
            inputValue.screenUv.x *
            resources->uniforms->viewportAndBackground.x;
        if (pixelX >= 398.0f)
        {
            discard_fragment();
        }
        WebgpuTexturesAnisotropyFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            webgpuTexturesAnisotropyShadePlane(
                resources,
                inputValue.screenUv));
        return frameBuffer;
    }
};

/** Draws sceneRight into the exact 398-pixel one-anisotropy region. */
class WebgpuTexturesAnisotropyRightPass final : public IRenderClass
{
public:
    /** Configures the second independent ordinary Plane Scene. */
    constructor(
        BindGroup<WebgpuTexturesAnisotropyResources>
            resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle with exact screen coordinates. */
    WebgpuTexturesAnisotropyVertexOutput vertex(
        uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuTexturesAnisotropyVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = uv;
        return outputValue;
    }

    /** Applies the right scissor and one-anisotropy sampling. */
    WebgpuTexturesAnisotropyFrameBuffer fragment(
        WebgpuTexturesAnisotropyVertexOutput inputValue)
    {
        const float pixelX =
            inputValue.screenUv.x *
            resources->uniforms->viewportAndBackground.x;
        if (pixelX < 400.0f || pixelX >= 798.0f)
        {
            discard_fragment();
        }
        WebgpuTexturesAnisotropyFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            webgpuTexturesAnisotropyShadePlane(
                resources,
                inputValue.screenUv));
        return frameBuffer;
    }
};

/** Owns both independent WebGPU Scenes and their sampler comparison. */
class Phase1WebgpuTexturesAnisotropyRenderer final
    : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA8UnormSrgb,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> crateTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebgpuTexturesAnisotropyUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler maximumAnisotropySampler;
    Sampler oneAnisotropySampler;
    BindGroup<WebgpuTexturesAnisotropyResources> leftResources;
    BindGroup<WebgpuTexturesAnisotropyResources> rightResources;
    RenderClass<WebgpuTexturesAnisotropyLeftPass> leftPass;
    RenderClass<WebgpuTexturesAnisotropyRightPass> rightPass;
    uint width = 800u;
    uint height = 500u;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float targetMouseX = 0.0f;
    float targetMouseY = 0.0f;

public:
    /** Creates the maximum and one-anisotropy sampler descriptors. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer(
            "WebgpuTexturesAnisotropyUniforms",
            1u);
        maximumAnisotropySampler = device->createSampler({
            .label = "WebgpuTexturesAnisotropyMaximumSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 16u,
        });
        oneAnisotropySampler = device->createSampler({
            .label = "WebgpuTexturesAnisotropyOneSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 8.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates the exact deterministic output extent. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebgpuTexturesAnisotropyOutput",
            width,
            height,
            1u);
    }

    /** Uploads explicit crate mips and configures the pointer target. */
    void configureScene(
        const eastl::vector<eastl::vector<uint8_t>> &mipLevels,
        uint textureWidth,
        uint textureHeight,
        float inTargetMouseX,
        float inTargetMouseY,
        bool clearGapToBackground)
    {
        crateTexture = device->createTexture(
            "WebgpuTexturesAnisotropyCrate",
            textureWidth,
            textureHeight,
            1u,
            uint(mipLevels.size()));
        for (uint mip = 0u; mip < uint(mipLevels.size()); mip += 1u)
        {
            graphicsQueue->writeTexture(
                crateTexture,
                mipLevels[mip].data(),
                uint64_t(mipLevels[mip].size()),
                mip);
        }
        targetMouseX = inTargetMouseX;
        targetMouseY = inTargetMouseY;
        (void)clearGapToBackground;
        leftResources =
            device->createBindGroup<WebgpuTexturesAnisotropyResources>(
                uniformBuffer,
                crateTexture->createView(),
                maximumAnisotropySampler);
        rightResources =
            device->createBindGroup<WebgpuTexturesAnisotropyResources>(
                uniformBuffer,
                crateTexture->createView(),
                oneAnisotropySampler);
        leftPass =
            device->createRenderClass<WebgpuTexturesAnisotropyLeftPass>(
                leftResources);
        rightPass =
            device->createRenderClass<WebgpuTexturesAnisotropyRightPass>(
                rightResources);
        graphicsQueue->submit();
    }

    /** Advances Three's camera easing and draws both Scene passes. */
    void render() override
    {
        cameraX += (targetMouseX - cameraX) * 0.05f;
        cameraY = clamp(
            cameraY +
                (-(targetMouseY - 200.0f) - cameraY) * 0.05f,
            50.0f,
            1000.0f);
        const WebgpuTexturesAnisotropyUniforms uniforms = {
            float4(
                cameraX,
                cameraY,
                1500.0f,
                float(width) / float(height)),
            float4(
                float(width),
                241.0f / 255.0f,
                241.0f / 255.0f,
                241.0f / 255.0f)};
        graphicsQueue
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        WebgpuTexturesAnisotropyFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            241.0 / 255.0,
            241.0 / 255.0,
            241.0 / 255.0,
            1.0};
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebgpuTexturesAnisotropyScenes",
                frameBuffer,
                leftPass(3u, 1u, 0u, 0u),
                rightPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
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

    /** Releases all texture and buffer storage owned by this renderer. */
    void destroy() override
    {
        device->freeTexture(crateTexture);
        device->freeTexture(outputColor);
        device->freeBuffer(uniformBuffer);
    }
};

#endif
