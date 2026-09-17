#ifndef GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_SOBEL_HPP
#define GVM_THREE_PHASE1_WEBGL_POSTPROCESSING_SOBEL_HPP

#include "UGL.h"
#include "WebglPostprocessingSobelData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Stores the exact camera, lighting, viewport, and effect state. */
struct WebglPostprocessingSobelUniforms
{
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
    float4 ambientAndEffect;
    float4 viewportAndReserved;
};

/** Binds the TorusKnot Scene transform and light state. */
struct WebglPostprocessingSobelSceneResources final : public IBindGroup
{
    /** Declares the one Scene uniform buffer. */
    constructor(
        UniformBuffer<WebglPostprocessingSobelUniforms> uniforms [[Binding0]])
    {
    }
};

/** Binds one intermediate texture and its exact linear sampler. */
struct WebglPostprocessingSobelTextureResources final : public IBindGroup
{
    /** Declares one sampled postprocessing input. */
    constructor(
        Texture2D<float4> inputTexture [[Binding0]],
        Sampler inputSampler [[Binding1]],
        UniformBuffer<WebglPostprocessingSobelUniforms> uniforms [[Binding2]])
    {
    }
};

/** Carries view-space Phong inputs from the TorusKnot Scene. */
struct WebglPostprocessingSobelSceneOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
};

/** Carries fullscreen coordinates through the three screen passes. */
struct WebglPostprocessingSobelScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear HDR Scene and luminance attachments. */
struct WebglPostprocessingSobelLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the TorusKnot Scene attachment with depth. */
struct WebglPostprocessingSobelSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final canonical RGBA8 output attachment. */
struct WebglPostprocessingSobelOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel with Three r185 output constants. */
float webglPostprocessingSobelLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three r185's optimized Schlick Fresnel approximation. */
float3 webglPostprocessingSobelFresnel(
    float3 f0,
    float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Evaluates the normalized Three r185 Blinn-Phong specular BRDF. */
float3 webglPostprocessingSobelSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection =
        normalize(lightDirection + viewDirection);
    const float dotNormalHalf =
        saturate(dot(normal, halfDirection));
    const float dotViewHalf =
        saturate(dot(viewDirection, halfDirection));
    const float3 fresnel =
        webglPostprocessingSobelFresnel(
            float3(0.0056053917f),
            dotViewHalf);
    const float distribution =
        0.3183098861837907f *
        16.0f *
        pow(dotNormalHalf, 30.0f);
    return fresnel * (0.25f * distribution);
}

/** Emits the shared fullscreen triangle coordinates. */
WebglPostprocessingSobelScreenOutput
webglPostprocessingSobelFullscreenVertex(uint vertexID)
{
    const float2 uv =
        float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglPostprocessingSobelScreenOutput outputValue;
    outputValue.position =
        float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Draws the exact one-object TorusKnot Scene with MeshPhong equations. */
class WebglPostprocessingSobelMainPass final : public IRenderClass
{
public:
    /** Binds standalone geometry state for the simple one-object Scene. */
    constructor(
        BindGroup<WebglPostprocessingSobelSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the perspective transform and prepares view-space lighting. */
    WebglPostprocessingSobelSceneOutput vertex(
        WebglPostprocessingSobelVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition =
            mul(resources->uniforms->modelViewMatrix, inputValue.position);
        float4 clipPosition =
            mul(resources->uniforms->projectionMatrix, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z =
            (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingSobelSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            resources->uniforms->modelViewMatrix,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates ambient plus camera-attached point-light MeshPhong shading. */
    WebglPostprocessingSobelSceneFrameBuffer fragment(
        WebglPostprocessingSobelSceneOutput inputValue)
    {
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 viewDirection =
            normalize(-inputValue.viewPosition);
        const float3 lightVector =
            -inputValue.viewPosition;
        const float lightDistanceSquared =
            max(dot(lightVector, lightVector), 0.01f);
        const float3 lightDirection = normalize(lightVector);
        const float irradiance =
            saturate(dot(normal, lightDirection)) *
            20.0f / lightDistanceSquared;
        const float inversePi = 0.3183098861837907f;
        const float ambient =
            resources->uniforms->ambientAndEffect.x;
        float3 linearColor =
            float3(1.0f, 1.0f, 0.0f) *
            ((ambient + irradiance) * inversePi);
        linearColor +=
            float3(irradiance) *
            webglPostprocessingSobelSpecular(
                lightDirection,
                viewDirection,
                normal);
        WebglPostprocessingSobelSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(linearColor),
            half(1.0f));
        return frameBuffer;
    }
};

/** Converts the linear Scene color into Three's luminance working texture. */
class WebglPostprocessingSobelLuminancePass final : public IRenderClass
{
public:
    /** Binds the linear Scene texture without depth. */
    constructor(
        BindGroup<WebglPostprocessingSobelTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingSobelFullscreenVertex(vertexID);
    }

    /** Applies Three's Rec.709 luminance coefficients. */
    WebglPostprocessingSobelLinearFrameBuffer fragment(
        WebglPostprocessingSobelScreenOutput inputValue)
    {
        const float3 color = resources->inputTexture
            ->sample(resources->inputSampler, inputValue.uv)
            .xyz;
        const float luminance =
            dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        WebglPostprocessingSobelLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(luminance),
            half(luminance),
            half(luminance),
            half(1.0f));
        return frameBuffer;
    }
};

/** Applies the complete Three SobelOperatorShader 3x3 kernel. */
class WebglPostprocessingSobelOperatorPass final : public IRenderClass
{
public:
    /** Binds the luminance texture and viewport resolution. */
    constructor(
        BindGroup<WebglPostprocessingSobelTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingSobelFullscreenVertex(vertexID);
    }

    /** Samples nine neighbours and emits the unclamped Sobel magnitude. */
    WebglPostprocessingSobelOutputFrameBuffer fragment(
        WebglPostprocessingSobelScreenOutput inputValue)
    {
        const float2 texel = 1.0f /
            resources->uniforms->viewportAndReserved.xy;
        const float tx0y0 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(-1.0f, -1.0f)).x;
        const float tx0y1 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(-1.0f, 0.0f)).x;
        const float tx0y2 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(-1.0f, 1.0f)).x;
        const float tx1y0 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(0.0f, -1.0f)).x;
        const float tx1y1 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv).x;
        const float tx1y2 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(0.0f, 1.0f)).x;
        const float tx2y0 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(1.0f, -1.0f)).x;
        const float tx2y1 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(1.0f, 0.0f)).x;
        const float tx2y2 = resources->inputTexture->sample(
            resources->inputSampler,
            inputValue.uv + texel * float2(1.0f, 1.0f)).x;
        const float valueGx =
            -tx0y0 + tx2y0 -
            2.0f * tx0y1 + 2.0f * tx2y1 -
            tx0y2 + tx2y2;
        const float valueGy =
            -tx0y0 - 2.0f * tx1y0 - tx2y0 +
            tx0y2 + 2.0f * tx1y2 + tx2y2;
        const float gradient =
            sqrt(valueGx * valueGx + valueGy * valueGy);
        WebglPostprocessingSobelOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(gradient),
            half(gradient),
            half(gradient),
            half(1.0f));
        return frameBuffer;
    }
};

/** Converts the direct Scene path into the renderer sRGB output space. */
class WebglPostprocessingSobelOutputPass final : public IRenderClass
{
public:
    /** Binds the linear Scene texture for the disabled-effect path. */
    constructor(
        BindGroup<WebglPostprocessingSobelTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the shared fullscreen triangle. */
    WebglPostprocessingSobelScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingSobelFullscreenVertex(vertexID);
    }

    /** Applies the Three r185 linear-to-sRGB output transfer. */
    WebglPostprocessingSobelOutputFrameBuffer fragment(
        WebglPostprocessingSobelScreenOutput inputValue)
    {
        const float3 linearColor = resources->inputTexture
            ->sample(resources->inputSampler, inputValue.uv)
            .xyz;
        WebglPostprocessingSobelOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingSobelLinearToSrgb(linearColor.x)),
            half(webglPostprocessingSobelLinearToSrgb(linearColor.y)),
            half(webglPostprocessingSobelLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one ordinary Scene and all exact Sobel postprocessing passes. */
class Phase1WebglPostprocessingSobelRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglPostprocessingSobelVertex,
           BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglPostprocessingSobelUniforms,
           BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> luminanceColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Sampler linearSampler;
    BindGroup<WebglPostprocessingSobelSceneResources> sceneResources;
    BindGroup<WebglPostprocessingSobelTextureResources> luminanceResources;
    BindGroup<WebglPostprocessingSobelTextureResources> sobelResources;
    RenderClass<WebglPostprocessingSobelMainPass> mainPass;
    RenderClass<WebglPostprocessingSobelLuminancePass> luminancePass;
    RenderClass<WebglPostprocessingSobelOperatorPass> sobelPass;
    RenderClass<WebglPostprocessingSobelOutputPass> outputPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;
    bool effectEnabled = true;

public:
    /** Creates all DSL-owned buffers, sampler, and RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglPostprocessingSobelVertices",
            8481u);
        indexBuffer = device->createBuffer(
            "WebglPostprocessingSobelIndices",
            49152u);
        uniformBuffer = device->createBuffer(
            "WebglPostprocessingSobelUniforms",
            1u);
        linearSampler = device->createSampler({
            .label = "WebglPostprocessingSobelLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the fixed intermediate and final attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture(
            "WebglPostprocessingSobelSceneColor",
            width, height, 1u);
        luminanceColor = device->createTexture(
            "WebglPostprocessingSobelLuminance",
            width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglPostprocessingSobelDepth",
            width, height, 1u);
        outputColor = device->createTexture(
            "WebglPostprocessingSobelOutput",
            width, height, 1u);
    }

    /** Uploads the exact TorusKnot geometry and immutable scenario state. */
    void configureScene(
        const eastl::vector<WebglPostprocessingSobelVertex> &vertices,
        const eastl::vector<uint> &indices,
        float4x4 projectionMatrix,
        float4x4 modelViewMatrix,
        float ambientLight,
        bool inEffectEnabled)
    {
        WebglPostprocessingSobelUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.modelViewMatrix = modelViewMatrix;
        uniforms.ambientAndEffect = float4(
            ambientLight,
            inEffectEnabled ? 1.0f : 0.0f,
            0.0f,
            0.0f);
        uniforms.viewportAndReserved = float4(
            float(width),
            float(height),
            0.0f,
            0.0f);
        indexCount = uint(indices.size());
        effectEnabled = inEffectEnabled;
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(WebglPostprocessingSobelVertex))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(uniformBuffer),
                &uniforms,
                sizeof(uniforms))
            ->submit();
        sceneResources =
            device->createBindGroup<WebglPostprocessingSobelSceneResources>(
                uniformBuffer);
        luminanceResources =
            device->createBindGroup<WebglPostprocessingSobelTextureResources>(
                sceneColor->createView(),
                linearSampler,
                uniformBuffer);
        sobelResources =
            device->createBindGroup<WebglPostprocessingSobelTextureResources>(
                luminanceColor->createView(),
                linearSampler,
                uniformBuffer);
        mainPass =
            device->createRenderClass<WebglPostprocessingSobelMainPass>(
                sceneResources);
        luminancePass =
            device->createRenderClass<WebglPostprocessingSobelLuminancePass>(
                luminanceResources);
        sobelPass =
            device->createRenderClass<WebglPostprocessingSobelOperatorPass>(
                sobelResources);
        outputPass =
            device->createRenderClass<WebglPostprocessingSobelOutputPass>(
                luminanceResources);
    }

    /** Draws the Scene and executes the enabled or disabled screen path. */
    void render() override
    {
        WebglPostprocessingSobelSceneFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = sceneColor->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrameBuffer.depth = sceneDepth->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        WebglPostprocessingSobelLinearFrameBuffer luminanceFrameBuffer;
        luminanceFrameBuffer.color = luminanceColor->createView();
        luminanceFrameBuffer.color.loadOp = LoadOp::Clear;
        luminanceFrameBuffer.color.storeOp = StoreOp::Store;
        luminanceFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        WebglPostprocessingSobelOutputFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        auto nextTexture = swapchain->queryNextTexture();
        if (effectEnabled)
        {
            graphicsQueue
                ->renderPass(
                    "WebglPostprocessingSobelMain",
                    sceneFrameBuffer,
                    mainPass->setVertexBuffer(vertexBuffer),
                    mainPass->setIndexBuffer(indexBuffer),
                    mainPass(indexCount, 1u, 0u, 0, 0u))
                ->renderPass(
                    "WebglPostprocessingSobelLuminance",
                    luminanceFrameBuffer,
                    luminancePass(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglPostprocessingSobelOperator",
                    outputFrameBuffer,
                    sobelPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputColor,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            graphicsQueue
                ->renderPass(
                    "WebglPostprocessingSobelMain",
                    sceneFrameBuffer,
                    mainPass->setVertexBuffer(vertexBuffer),
                    mainPass->setIndexBuffer(indexBuffer),
                    mainPass(indexCount, 1u, 0u, 0, 0u))
                ->renderPass(
                    "WebglPostprocessingSobelOutput",
                    outputFrameBuffer,
                    outputPass(3u, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputColor,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        swapchain->present();
    }

    /** Returns the final DSL-owned output texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const { return height; }

    /** Releases every private Scene and postprocessing resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(sceneColor);
        device->freeTexture(luminanceColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
