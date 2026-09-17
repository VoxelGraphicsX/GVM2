#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_UINT_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_UINT_HPP

#include "UGL.h"
#include "WebglBuffergeometryUintVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryUintVertexCount = 3000000u;

/** Stores the frozen Three transforms and fixed pixel-center sample position. */
struct WebglBuffergeometryUintUniforms
{
    float4x4 projection;
    float4x4 modelView;
    float4 samplePositionAndReserved;
};

/** Binds the immutable frame transforms used by the private MeshPhong material. */
struct WebglBuffergeometryUintBindGroup final : public IBindGroup
{
    /** Declares one current-surface uniform buffer through the frozen DSL interface. */
    constructor(UniformBuffer<WebglBuffergeometryUintUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries decoded integer attributes and view-space Phong inputs to the fragment stage. */
struct WebglBuffergeometryUintVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float3 vertexColor [[Attribute1]];
    float3 viewDirectionVector [[Attribute2]];
    float fogDepth [[Attribute3]];
};

/** Defines the single-sample color and depth target. */
struct WebglBuffergeometryUintFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Decodes one signed normalized Int16 component from its authored raw bits. */
inline float webglBuffergeometryUintDecodeSnorm16(uint bits)
{
    const uint value = bits & 0xffffu;
    if (value <= 32767u)
    {
        return float(value) / 32767.0f;
    }
    return max((float(value) - 65536.0f) / 32767.0f, -1.0f);
}

/** Decodes the complete Int16 normalized normal inside the DSL vertex stage. */
inline float3 webglBuffergeometryUintDecodeNormal(uint2 packedNormal)
{
    return float3(
        webglBuffergeometryUintDecodeSnorm16(packedNormal.x),
        webglBuffergeometryUintDecodeSnorm16(packedNormal.x >> 16u),
        webglBuffergeometryUintDecodeSnorm16(packedNormal.y));
}

/** Decodes the complete Uint8 normalized vertex color inside the DSL vertex stage. */
inline float3 webglBuffergeometryUintDecodeColor(uint packedColor)
{
    return float3(
               float(packedColor & 0xffu),
               float((packedColor >> 8u) & 0xffu),
               float((packedColor >> 16u) & 0xffu)) /
           255.0f;
}

/** Converts one linear-light channel with Three r185's exact output constants. */
inline float webglBuffergeometryUintLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three r185's optimized Schlick Fresnel approximation. */
inline float3 webglBuffergeometryUintFresnelSchlick(
    float3 f0,
    float f90,
    float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(f90) * fresnel;
}

/** Evaluates Three r185's normalized Blinn-Phong direct specular BRDF. */
inline float3 webglBuffergeometryUintPhongSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf =
        clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotViewHalf =
        clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 fresnel = webglBuffergeometryUintFresnelSchlick(
        float3(1.0f),
        1.0f,
        dotViewHalf);
    const float distribution =
        0.3183098861837907f * 126.0f * pow(dotNormalHalf, 250.0f);
    return fresnel * (0.25f * distribution);
}

/** Applies the fixed pixel-center sample translation in clip space. */
inline float4 webglBuffergeometryUintApplyCoverageOffset(
    float4 clipPosition,
    float2 samplePosition)
{
    clipPosition.x +=
        (1.0f - 2.0f * samplePosition.x) / 800.0f * clipPosition.w;
    clipPosition.y +=
        (2.0f * samplePosition.y - 1.0f) / 500.0f * clipPosition.w;
    return clipPosition;
}

/** Reproduces the single DoubleSide MeshPhong scene draw through dual winding. */
class WebglBuffergeometryUintMainPass final : public IRenderClass
{
public:
    /** Uses back-face culling because C++ supplies exact front and reversed triangles. */
    constructor(BindGroup<WebglBuffergeometryUintBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Decodes raw integer attributes and prepares Three's view-space Phong varyings. */
    WebglBuffergeometryUintVertexOutput vertex(
        WebglBuffergeometryUintVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.position, 1.0f));
        const float3 decodedNormal = webglBuffergeometryUintDecodeNormal(
            inputValue.packedNormalSnorm16x3);
        const float3 viewNormal = mul(
            bindGroup->uniforms->modelView,
            float4(decodedNormal, 0.0f)).xyz;

        WebglBuffergeometryUintVertexOutput outputValue;
        float4 clipPosition = mul(
            bindGroup->uniforms->projection,
            viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = webglBuffergeometryUintApplyCoverageOffset(
            clipPosition,
            bindGroup->uniforms->samplePositionAndReserved.xy);
        outputValue.viewNormal = viewNormal;
        outputValue.vertexColor = webglBuffergeometryUintDecodeColor(
            inputValue.packedColorUnorm8x3);
        outputValue.viewDirectionVector = -viewPosition.xyz;
        outputValue.fogDepth = -viewPosition.z;
        return outputValue;
    }

    /** Evaluates r185 Lambert, Blinn-Phong, output transfer, then display-space fog. */
    WebglBuffergeometryUintFrameBuffer fragment(
        WebglBuffergeometryUintVertexOutput inputValue)
    {
        const float inversePi = 0.3183098861837907f;
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection = normalize(inputValue.viewDirectionVector);
        const float3 lightDirection0 = normalize(float3(1.0f, 1.0f, 1.0f));
        const float3 lightDirection1 = float3(0.0f, -1.0f, 0.0f);
        const float dotLight0 =
            clamp(dot(normal, lightDirection0), 0.0f, 1.0f);
        const float dotLight1 =
            clamp(dot(normal, lightDirection1), 0.0f, 1.0f);
        const float irradiance0 = dotLight0 * 1.5f;
        const float irradiance1 = dotLight1 * 4.5f;
        const float3 diffuseColor =
            inputValue.vertexColor * 0.6653872728347778f;
        float3 linearColor =
            diffuseColor *
            ((float3(0.6038273572921753f) +
              float3(irradiance0) +
              float3(irradiance1)) * inversePi);
        linearColor += float3(irradiance0) *
                       webglBuffergeometryUintPhongSpecular(
                           lightDirection0,
                           viewDirection,
                           normal);
        linearColor += float3(irradiance1) *
                       webglBuffergeometryUintPhongSpecular(
                           lightDirection1,
                           viewDirection,
                           normal);

        float3 displayColor = float3(
            webglBuffergeometryUintLinearToSrgb(linearColor.x),
            webglBuffergeometryUintLinearToSrgb(linearColor.y),
            webglBuffergeometryUintLinearToSrgb(linearColor.z));
        const float fogFactor = smoothstep(2000.0f, 3500.0f, inputValue.fogDepth);
        displayColor = lerp(
            displayColor,
            float3(0.0196078431372549f),
            fogFactor);

        WebglBuffergeometryUintFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(displayColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the standalone packed geometry and all private DSL rendering resources. */
class WebglBuffergeometryUintRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryUintVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglBuffergeometryUintUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglBuffergeometryUintBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometryUintMainPass> mainPass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    bool sceneRendered = false;

public:
    /** Creates standalone packed vertex and uniform buffers through generated DSL methods. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometryUintPackedVertices",
            WebglBuffergeometryUintVertexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglBuffergeometryUintUniforms0", 1u);
    }

    /** Allocates the single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryUintOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryUintDepth", width, height, 1u);
    }

    /** Uploads dual-winding geometry and immutable transforms. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryUintVertex> &vertices,
        float4x4 projection,
        float4x4 modelView)
    {
        WebglBuffergeometryUintUniforms uniforms0;
        uniforms0.projection = projection;
        uniforms0.modelView = modelView;
        uniforms0.samplePositionAndReserved =
            float4(0.5f, 0.5f, 0.0f, 0.0f);

        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglBuffergeometryUintVertex))
            ->writeBuffer(
                BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->submit();

        bindGroup0 = device->createBindGroup<WebglBuffergeometryUintBindGroup>(
            uniformBuffer0);
        mainPass0 = device->createRenderClass<WebglBuffergeometryUintMainPass>(
            bindGroup0);
    }

    /** Rasterizes the Scene directly into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (!sceneRendered)
        {
            WebglBuffergeometryUintFrameBuffer frameBuffer0;
            frameBuffer0.color = outputTexture->createView();
            frameBuffer0.color.loadOp = LoadOp::Clear;
            frameBuffer0.color.storeOp = StoreOp::Store;
            frameBuffer0.color.clearValue = {
                5.0 / 255.0, 5.0 / 255.0, 5.0 / 255.0, 1.0};
            frameBuffer0.depth = depthTexture->createView();
            frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
            frameBuffer0.depth.depthStoreOp = StoreOp::Store;
            frameBuffer0.depth.depthClearValue = 1.0f;

            graphicsQueue
                ->renderPass(
                    "WebglBuffergeometryUintSample0",
                    frameBuffer0,
                    mainPass0->setVertexBuffer(vertexBuffer),
                    mainPass0(WebglBuffergeometryUintVertexCount, 1u, 0u, 0u))
                ->submit();
            sceneRendered = true;
        }

        graphicsQueue
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 output for deterministic readback. */
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
        return readbackWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases all standalone buffers and single-sample targets. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
