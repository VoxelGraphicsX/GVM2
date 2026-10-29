#ifndef GVM_THREE_WEBGL_MORPHTARGETS_HORSE_HPP
#define GVM_THREE_WEBGL_MORPHTARGETS_HORSE_HPP

#include "UGL.h"
#include "WebglMorphtargetsHorseData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglMorphtargetsHorseVertexCount = 796u;
static const uint WebglMorphtargetsHorseTargetCount = 15u;

/** Binds the fixed camera, clip weights, and target-major morph position buffer. */
struct WebglMorphtargetsHorseResources final : public IBindGroup
{
    /** Declares every resource read by the dedicated Horse material. */
    constructor(
        UniformBuffer<WebglMorphtargetsHorseUniforms> uniforms [[Binding0]],
        StructuredBuffer<float4> morphPositions [[Binding1]])
    {
    }
};

/** Carries the morphed view position and authored color into flat PBR shading. */
struct WebglMorphtargetsHorseVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 color [[Attribute1]];
};

/** Defines the ordinary single-sample Horse color and depth targets. */
struct WebglMorphtargetsHorseFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one non-negative linear component to the r185 sRGB output transfer. */
float webglMorphtargetsHorseLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three's optimized Schlick Fresnel approximation. */
float3 webglMorphtargetsHorseFresnel(float dotViewHalf)
{
    const float factor = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return float3(0.04f) * (1.0f - factor) + float3(factor);
}

/** Evaluates direct roughness-one GGX for one directional light. */
float3 webglMorphtargetsHorseDirectLight(
    float3 baseColor,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 irradiance)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));
    if (normalDotLight <= 0.0f) return float3(0.0f);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float viewDotHalf = saturate(dot(viewDirection, halfDirection));
    const float inversePi = 0.3183098861837907f;
    const float3 diffuse = baseColor * inversePi;
    const float3 specular =
        webglMorphtargetsHorseFresnel(viewDotHalf) * 0.25f * inversePi;
    return (diffuse + specular) * irradiance * normalDotLight;
}

/** Renders the one non-instanced, hierarchy-free Horse primitive without RenderSet. */
class WebglMorphtargetsHorseMainPass final : public IRenderClass
{
public:
    /** Configures Three's opaque front-sided depth-tested standard material. */
    constructor(BindGroup<WebglMorphtargetsHorseResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies all fifteen relative POSITION morph targets in target order. */
    WebglMorphtargetsHorseVertexOutput vertex(
        WebglMorphtargetsHorseVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]])
    {
        const float weights[16] = {
            resources->uniforms->morphWeights0.x,
            resources->uniforms->morphWeights0.y,
            resources->uniforms->morphWeights0.z,
            resources->uniforms->morphWeights0.w,
            resources->uniforms->morphWeights1.x,
            resources->uniforms->morphWeights1.y,
            resources->uniforms->morphWeights1.z,
            resources->uniforms->morphWeights1.w,
            resources->uniforms->morphWeights2.x,
            resources->uniforms->morphWeights2.y,
            resources->uniforms->morphWeights2.z,
            resources->uniforms->morphWeights2.w,
            resources->uniforms->morphWeights3.x,
            resources->uniforms->morphWeights3.y,
            resources->uniforms->morphWeights3.z,
            resources->uniforms->morphWeights3.w,
        };
        float3 morphed = inputValue.position;
        for (uint target = 0u;
             target < WebglMorphtargetsHorseTargetCount;
             ++target)
        {
            morphed += resources->morphPositions[
                target * WebglMorphtargetsHorseVertexCount + vertexID].xyz *
                weights[target];
        }
        const float4 viewPosition = mul(
            resources->uniforms->modelView,
            float4(morphed, 1.0f));
        float4 clipPosition = mul(
            resources->uniforms->modelViewProjection,
            float4(morphed, 1.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglMorphtargetsHorseVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    /** Reproduces flat MeshStandardMaterial lighting from both authored lights. */
    WebglMorphtargetsHorseFrameBuffer fragment(
        WebglMorphtargetsHorseVertexOutput inputValue)
    {
        const float3 normal = normalize(cross(
            ddy(inputValue.viewPosition),
            ddx(inputValue.viewPosition)));
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 linearColor =
            webglMorphtargetsHorseDirectLight(
                inputValue.color,
                normal,
                viewDirection,
                resources->uniforms->lightDirectionIntensity0.xyz,
                resources->uniforms->lightColor0.xyz *
                    resources->uniforms->lightDirectionIntensity0.w) +
            webglMorphtargetsHorseDirectLight(
                inputValue.color,
                normal,
                viewDirection,
                resources->uniforms->lightDirectionIntensity1.xyz,
                resources->uniforms->lightColor1.xyz *
                    resources->uniforms->lightDirectionIntensity1.w);
        WebglMorphtargetsHorseFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMorphtargetsHorseLinearToSrgb(linearColor.x)),
            half(webglMorphtargetsHorseLinearToSrgb(linearColor.y)),
            half(webglMorphtargetsHorseLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated Horse buffers, one ordinary RenderClass, and single-sample output. */
class WebglMorphtargetsHorseRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMorphtargetsHorseVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<float4, BufferUsage<Storage, CopyDst>> morphBuffer;
    Buffer<WebglMorphtargetsHorseUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglMorphtargetsHorseResources> resources;
    RenderClass<WebglMorphtargetsHorseMainPass> mainPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint indexCount = 0u;

public:
    /** Stores the generated device and queue used by all explicit uploads. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
    }

    /** Allocates the canonical single-sample color and depth attachments. */
    void configureOutput(uint width, uint height)
    {
        outputTexture = device->createTexture(
            "WebglMorphtargetsHorseOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglMorphtargetsHorseDepth", width, height, 1u);
    }

    /** Uploads the locked GLB data and creates the one ordinary material pass. */
    void configureScene(
        const eastl::vector<WebglMorphtargetsHorseVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<float4> &morphPositions,
        WebglMorphtargetsHorseUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer(
            "WebglMorphtargetsHorseVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer(
            "WebglMorphtargetsHorseIndices", indexCount);
        morphBuffer = device->createBuffer(
            "WebglMorphtargetsHorseMorphPositions", uint(morphPositions.size()));
        uniformBuffer = device->createBuffer(
            "WebglMorphtargetsHorseUniforms", 1u);
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(),
                          uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(),
                          uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(morphBuffer), morphPositions.data(),
                          uint64_t(morphPositions.size()) * sizeof(morphPositions[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        resources = device->createBindGroup<WebglMorphtargetsHorseResources>(
            uniformBuffer, morphBuffer);
        mainPass = device->createRenderClass<WebglMorphtargetsHorseMainPass>(resources);
    }

    /** Draws one indexed Horse primitive and presents the single-sample texture. */
    void render() override
    {
        WebglMorphtargetsHorseFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            0.9411764706, 0.9411764706, 0.9411764706, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglMorphtargetsHorseMain",
                frameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final RGBA8 texture used by the generic host readback path. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the canonical capture width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the canonical capture height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases every dedicated Horse GPU resource. */
    void destroy() override
    {
        device->freeBuffer(uniformBuffer);
        device->freeBuffer(morphBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(vertexBuffer);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
