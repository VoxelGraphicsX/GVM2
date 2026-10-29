#ifndef GVM_THREE_PHASE1_LOADER_TEXTURE_HDR_SIMPLE_HPP
#define GVM_THREE_PHASE1_LOADER_TEXTURE_HDR_SIMPLE_HPP

#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the selected Reinhard exposure and locked image dimensions. */
struct WebglLoaderTextureHdrUniforms
{
    float4 exposureAndImageSize;
};

/** Binds the decoded RGBA16Float memorial image and its display state. */
struct WebglLoaderTextureHdrResources final : public IBindGroup
{
    /** Declares the complete private HDR quad resource set. */
    constructor(UniformBuffer<WebglLoaderTextureHdrUniforms> uniforms [[Binding0]],
                Texture2D<float4> hdrTexture [[Binding1]],
                Sampler hdrSampler [[Binding2]])
    {
    }
};

/** Carries canonical fullscreen coordinates into the HDR fragment shader. */
struct WebglLoaderTextureHdrVertexOutput
{
    float4 position [[Position]];
    float2 screenUv [[Attribute0]];
};

/** Defines the final RGBA8 HDR loader attachment. */
struct WebglLoaderTextureHdrFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one non-negative linear channel through Three's sRGB output transfer. */
float webglLoaderTextureHdrLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Reproduces the orthographic memorial quad and Reinhard output transform. */
class WebglLoaderTextureHdrQuadPass final : public IRenderClass
{
public:
    /** Disables culling and depth for the only simple Scene renderable. */
    constructor(BindGroup<WebglLoaderTextureHdrResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits one fullscreen triangle while preserving normalized screen coordinates. */
    WebglLoaderTextureHdrVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 screenUv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglLoaderTextureHdrVertexOutput outputValue;
        outputValue.position = float4(screenUv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.screenUv = screenUv;
        return outputValue;
    }

    /** Samples the locked quad, applies exposure, Reinhard, and sRGB encoding. */
    WebglLoaderTextureHdrFrameBuffer fragment(WebglLoaderTextureHdrVertexOutput inputValue)
    {
        const float imageWidth = resources->uniforms->exposureAndImageSize.y;
        const float imageHeight = resources->uniforms->exposureAndImageSize.z;
        const float quadWidthPixels = 375.0f * imageWidth / imageHeight;
        const float2 quadMinimum = float2((800.0f - quadWidthPixels) * 0.5f, 62.5f);
        const float2 quadMaximum = quadMinimum + float2(quadWidthPixels, 375.0f);
        float3 displayColor = float3(0.0f);
        if (inputValue.position.x >= quadMinimum.x &&
            inputValue.position.x < quadMaximum.x &&
            inputValue.position.y > quadMinimum.y &&
            inputValue.position.y <= quadMaximum.y)
        {
            const float2 uv = float2(
                (inputValue.position.x - quadMinimum.x) / quadWidthPixels,
                (inputValue.position.y - quadMinimum.y) / 375.0f);
            const float3 linearColor =
                resources->hdrTexture->sampleLevel(resources->hdrSampler, uv, 0.0f).xyz *
                resources->uniforms->exposureAndImageSize.x;
            const float3 reinhard = linearColor / (float3(1.0f) + linearColor);
            displayColor = float3(
                webglLoaderTextureHdrLinearToSrgb(reinhard.x),
                webglLoaderTextureHdrLinearToSrgb(reinhard.y),
                webglLoaderTextureHdrLinearToSrgb(reinhard.z));
        }
        WebglLoaderTextureHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the private decoded HDR texture and one ordinary RenderClass. */
class Phase1LoaderTextureHdrSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> hdrTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Buffer<WebglLoaderTextureHdrUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Sampler hdrSampler;
    BindGroup<WebglLoaderTextureHdrResources> resources;
    RenderClass<WebglLoaderTextureHdrQuadPass> quadPass;

public:
    /** Creates immutable sampler and uniform resources through the DSL. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer("WebglLoaderTextureHdrUniforms", 1u);
        hdrSampler = device->createSampler({
            .label = "WebglLoaderTextureHdrSampler",
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
    }

    /** Allocates the manifest-locked final RGBA8 output target. */
    void configureOutput(uint width, uint height)
    {
        outputColor = device->createTexture("WebglLoaderTextureHdrOutput", width, height, 1u);
    }

    /** Uploads the locked half-float image and selected exposure. */
    void configureScene(const eastl::vector<uint16_t> &halfRgba,
                        uint imageWidth,
                        uint imageHeight,
                        float exposure)
    {
        hdrTexture = device->createTexture(
            "WebglLoaderTextureHdrMemorial", imageWidth, imageHeight, 1u);
        WebglLoaderTextureHdrUniforms uniforms;
        uniforms.exposureAndImageSize =
            float4(exposure, float(imageWidth), float(imageHeight), 0.0f);
        graphicsQueue
            ->writeTexture(hdrTexture, halfRgba.data(),
                           uint64_t(halfRgba.size()) * sizeof(uint16_t), 0u)
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        resources = device->createBindGroup<WebglLoaderTextureHdrResources>(
            uniformBuffer, hdrTexture->createView(), hdrSampler);
        quadPass = device->createRenderClass<WebglLoaderTextureHdrQuadPass>(resources);
    }

    /** Draws and presents the only HDR quad through the generated pass. */
    void render() override
    {
        WebglLoaderTextureHdrFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderTextureHdrQuad", frameBuffer,
                         quadPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the locked capture width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the locked capture height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases every private HDR sample resource. */
    void destroy() override
    {
        device->freeBuffer(uniformBuffer);
        device->freeTexture(hdrTexture);
        device->freeTexture(outputColor);
    }
};

#endif
