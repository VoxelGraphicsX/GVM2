#ifndef GVM_THREE_WEBGL_MATERIALS_BUMPMAP_HPP
#define GVM_THREE_WEBGL_MATERIALS_BUMPMAP_HPP

#include "WebglMaterialsBumpmapData.hpp"
#include "UGL.h"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the height map, shadow depth, sampler, and immutable Scene state. */
struct WebglMaterialsBumpmapResources final : public IBindGroup
{
    /** Declares every private resource used by the main bump-material pass. */
    constructor(
        UniformBuffer<WebglMaterialsBumpmapUniforms> uniforms [[Binding0]],
        Texture2D<float4> heightMap [[Binding1]],
        Texture2D<TextureFormat::Depth32Float> shadowDepth [[Binding2]],
        Sampler materialSampler [[Binding3]])
    {
    }
};

/** Binds only the immutable transform state used for shadow depth rendering. */
struct WebglMaterialsBumpmapShadowResources final : public IBindGroup
{
    /** Declares the shared transforms for the dedicated shadow pass. */
    constructor(
        UniformBuffer<WebglMaterialsBumpmapUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries view-space geometry, UV, and shadow coordinates to the fragment stage. */
struct WebglMaterialsBumpmapVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float4 shadowPosition [[Attribute3]];
};

/** Carries only the projected position needed by the shadow pass. */
struct WebglMaterialsBumpmapShadowOutput
{
    float4 position [[Position]];
};

/** Defines the final single-sample color and depth attachments. */
struct WebglMaterialsBumpmapFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the 2048-square depth-only spotlight shadow attachment. */
struct WebglMaterialsBumpmapShadowFrameBuffer final : public IFrameBuffer
{
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one non-negative linear channel through Three's output transfer. */
float webglMaterialsBumpmapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Converts one authored sRGB material channel to linear working space. */
float webglMaterialsBumpmapSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Applies Three's derivative bump perturbation to one view-space normal. */
float3 webglMaterialsBumpmapPerturbNormal(
    float3 viewPosition,
    float3 viewNormal,
    float2 heightGradient)
{
    const float3 sigmaX = normalize(ddx(viewPosition));
    const float3 sigmaY = normalize(-ddy(viewPosition));
    const float3 r1 = cross(sigmaY, viewNormal);
    const float3 r2 = cross(viewNormal, sigmaX);
    const float determinant = dot(sigmaX, r1);
    const float3 gradient = sign(determinant) *
        (heightGradient.x * r1 + heightGradient.y * r2);
    return normalize(abs(determinant) * viewNormal - gradient);
}

/** Reproduces one hardware bilinear LessEqual depth comparison. */
float webglMaterialsBumpmapBilinearShadowCompare(
    Texture2D<TextureFormat::Depth32Float> shadowDepth,
    float2 uv,
    float compareDepth)
{
    const float2 texelPosition = uv * 2048.0f - 0.5f;
    const float2 lowerPosition = floor(texelPosition);
    const float2 fraction = texelPosition - lowerPosition;
    const uint x0 = uint(clamp(lowerPosition.x, 0.0f, 2047.0f));
    const uint y0 = uint(clamp(lowerPosition.y, 0.0f, 2047.0f));
    const uint x1 = min(x0 + 1u, 2047u);
    const uint y1 = min(y0 + 1u, 2047u);
    const float compare00 = compareDepth <= shadowDepth->read(uint2(x0, y0)).x ? 1.0f : 0.0f;
    const float compare10 = compareDepth <= shadowDepth->read(uint2(x1, y0)).x ? 1.0f : 0.0f;
    const float compare01 = compareDepth <= shadowDepth->read(uint2(x0, y1)).x ? 1.0f : 0.0f;
    const float compare11 = compareDepth <= shadowDepth->read(uint2(x1, y1)).x ? 1.0f : 0.0f;
    return lerp(
        lerp(compare00, compare10, fraction.x),
        lerp(compare01, compare11, fraction.x),
        fraction.y);
}

/** Returns one of Three r185's five rotated Vogel-disk shadow offsets. */
float2 webglMaterialsBumpmapVogelDiskSample(uint sampleIndex, float phase)
{
    const float radius = sqrt((float(sampleIndex) + 0.5f) * 0.2f);
    const float theta = float(sampleIndex) * 2.399963229728653f + phase;
    return float2(cos(theta), sin(theta)) * radius;
}

/** Evaluates Three r185's five hardware-PCF-equivalent spotlight samples. */
float webglMaterialsBumpmapShadow(
    Texture2D<TextureFormat::Depth32Float> shadowDepth,
    float4 shadowPosition,
    float2 finalPixelPosition)
{
    const float3 projected = shadowPosition.xyz / shadowPosition.w;
    const float2 uv = float2(
        projected.x * 0.5f + 0.5f,
        0.5f - projected.y * 0.5f);
    const float receiverDepth = projected.z * 0.5f + 0.5f - 0.005f;
    if (uv.x <= 0.0f || uv.x >= 1.0f ||
        uv.y <= 0.0f || uv.y >= 1.0f ||
        receiverDepth <= 0.0f || receiverDepth >= 1.0f)
    {
        return 1.0f;
    }
    const float phase = frac(52.9829189f * frac(dot(
        finalPixelPosition,
        float2(0.06711056f, 0.00583715f)))) * 6.283185307179586f;
    const float radius = 1.0f / 2048.0f;
    return (
        webglMaterialsBumpmapBilinearShadowCompare(shadowDepth, uv + webglMaterialsBumpmapVogelDiskSample(0u, phase) * radius, receiverDepth) +
        webglMaterialsBumpmapBilinearShadowCompare(shadowDepth, uv + webglMaterialsBumpmapVogelDiskSample(1u, phase) * radius, receiverDepth) +
        webglMaterialsBumpmapBilinearShadowCompare(shadowDepth, uv + webglMaterialsBumpmapVogelDiskSample(2u, phase) * radius, receiverDepth) +
        webglMaterialsBumpmapBilinearShadowCompare(shadowDepth, uv + webglMaterialsBumpmapVogelDiskSample(3u, phase) * radius, receiverDepth) +
        webglMaterialsBumpmapBilinearShadowCompare(shadowDepth, uv + webglMaterialsBumpmapVogelDiskSample(4u, phase) * radius, receiverDepth)) * 0.2f;
}

/** Writes the Lee Perry Smith geometry into the spotlight depth target. */
class WebglMaterialsBumpmapShadowDepthPass final : public IRenderClass
{
public:
    /** Configures the front-sided opaque shadow geometry pass. */
    constructor(
        BindGroup<WebglMaterialsBumpmapShadowResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects one standalone mesh vertex into spotlight clip space. */
    WebglMaterialsBumpmapShadowOutput vertex(
        WebglMaterialsBumpmapVertex inputValue [[VertexInput0]])
    {
        WebglMaterialsBumpmapShadowOutput outputValue;
        outputValue.position = mul(
            resources->uniforms->shadowModelViewProjection,
            float4(inputValue.position, 1.0f));
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        return outputValue;
    }

    /** Retains automatic hardware depth writes without producing a color attachment. */
    WebglMaterialsBumpmapShadowFrameBuffer fragment(
        WebglMaterialsBumpmapShadowOutput inputValue)
    {
        WebglMaterialsBumpmapShadowFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

/** Renders the one ordinary Scene mesh with derivative bump and Phong lighting. */
class WebglMaterialsBumpmapMainPass final : public IRenderClass
{
public:
    /** Configures the opaque front-sided material pass. */
    constructor(BindGroup<WebglMaterialsBumpmapResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one mesh vertex and preserves all derivative inputs. */
    WebglMaterialsBumpmapVertexOutput vertex(
        WebglMaterialsBumpmapVertex inputValue [[VertexInput0]])
    {
        const float4 localPosition = float4(inputValue.position, 1.0f);
        const float4 viewPosition = mul(
            resources->uniforms->modelView,
            localPosition);
        WebglMaterialsBumpmapVertexOutput outputValue;
        outputValue.position = mul(
            resources->uniforms->modelViewProjection,
            localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(float3(
            mul(resources->uniforms->modelView,
                float4(inputValue.normal, 0.0f)).xyz));
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        outputValue.shadowPosition = mul(
            resources->uniforms->shadowModelViewProjection,
            localPosition);
        return outputValue;
    }

    /** Evaluates bump derivatives, hemisphere/spot Phong, shadow, and sRGB output. */
    WebglMaterialsBumpmapFrameBuffer fragment(
        WebglMaterialsBumpmapVertexOutput inputValue)
    {
        const float2 uv = float2(
            inputValue.textureCoordinate.x,
            inputValue.textureCoordinate.y);
        const float2 uvDx = ddx(uv);
        const float2 uvDy = -ddy(uv);
        const float2 lodDx = uvDx;
        const float2 lodDy = uvDy;
        const float height = resources->heightMap->sample(
            resources->materialSampler, uv).x;
        const float bumpScale =
            resources->uniforms->spotDirectionAndBumpScale.w;
        const float heightX = resources->heightMap->sampleGrad(
            resources->materialSampler, uv + uvDx, lodDx, lodDy).x;
        const float heightY = resources->heightMap->sampleGrad(
            resources->materialSampler, uv + uvDy, lodDx, lodDy).x;
        const float3 normal = webglMaterialsBumpmapPerturbNormal(
            inputValue.viewPosition,
            normalize(inputValue.viewNormal),
            float2(heightX - height, heightY - height) * bumpScale);
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 lightVector =
            resources->uniforms->lightPositionAndIntensity.xyz -
            inputValue.viewPosition;
        const float lightDistance = max(length(lightVector), 0.0001f);
        const float3 lightDirection = lightVector / lightDistance;
        const float cone = dot(
            lightDirection,
            -resources->uniforms->spotDirectionAndBumpScale.xyz);
        const float spotFactor = cone >= 0.5f ? 1.0f : 0.0f;
        const float attenuation =
            resources->uniforms->lightPositionAndIntensity.w /
            (lightDistance * lightDistance);
        const float normalDotLight = max(dot(normal, lightDirection), 0.0f);
        const float shadow = webglMaterialsBumpmapShadow(
            resources->shadowDepth,
            inputValue.shadowPosition,
            inputValue.position.xy);
        const float3 materialColor = float3(
            webglMaterialsBumpmapSrgbToLinear(156.0f / 255.0f),
            webglMaterialsBumpmapSrgbToLinear(110.0f / 255.0f),
            webglMaterialsBumpmapSrgbToLinear(73.0f / 255.0f));
        const float3 specularColor = float3(
            webglMaterialsBumpmapSrgbToLinear(0.4f));
        const float hemisphereFactor = normal.y * 0.5f + 0.5f;
        const float3 hemisphere = lerp(
            resources->uniforms->hemisphereGround.xyz,
            resources->uniforms->hemisphereSky.xyz,
            hemisphereFactor) * 3.0f;
        const float3 spotColor = float3(1.0f, 1.0f, 0.73046074f) *
            attenuation * spotFactor * shadow;
        const float3 diffuse = materialColor *
            (hemisphere + spotColor * normalDotLight) *
            0.3183098861837907f;
        const float3 halfDirection = normalize(lightDirection + viewDirection);
        const float specularPower = pow(
            max(dot(normal, halfDirection), 0.0f), 25.0f);
        const float viewDotHalf = max(dot(viewDirection, halfDirection), 0.0f);
        const float fresnelWeight = pow(1.0f - viewDotHalf, 5.0f);
        const float3 fresnel = specularColor +
            (float3(1.0f) - specularColor) * fresnelWeight;
        const float3 specular = fresnel * spotColor * normalDotLight *
            ((27.0f / 25.132741228718345f) * specularPower);
        const float3 linearColor = diffuse + specular;
        WebglMaterialsBumpmapFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMaterialsBumpmapLinearToSrgb(linearColor.x)),
            half(webglMaterialsBumpmapLinearToSrgb(linearColor.y)),
            half(webglMaterialsBumpmapLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated Lee Perry Smith geometry, shadow, and bump material. */
class WebglMaterialsBumpmapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsBumpmapVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsBumpmapUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D> heightTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> shadowTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler materialSampler;
    BindGroup<WebglMaterialsBumpmapShadowResources> shadowResources;
    BindGroup<WebglMaterialsBumpmapResources> resources;
    RenderClass<WebglMaterialsBumpmapShadowDepthPass> shadowPass;
    RenderClass<WebglMaterialsBumpmapMainPass> mainPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates immutable buffers and the trilinear repeat sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer = device->createBuffer("WebglMaterialsBumpmapUniforms", 1u);
        materialSampler = device->createSampler({
            .label = "WebglMaterialsBumpmapSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
    }

    /** Allocates ordinary single-sample output, depth, and shadow targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputTexture = device->createTexture("WebglMaterialsBumpmapOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglMaterialsBumpmapDepth", width, height, 1u);
        shadowTexture = device->createTexture("WebglMaterialsBumpmapShadow", 2048u, 2048u, 1u);
    }

    /** Uploads the decoded GLB, explicit height mips, and complete frame state. */
    void configureScene(
        const eastl::vector<WebglMaterialsBumpmapVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &heightMips,
        uint textureWidth,
        uint textureHeight,
        WebglMaterialsBumpmapUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer("WebglMaterialsBumpmapVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer("WebglMaterialsBumpmapIndices", indexCount);
        heightTexture = device->createTexture("WebglMaterialsBumpmapHeight", textureWidth, textureHeight, 1u, uint(heightMips.size()));
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(), uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        for (uint mip = 0u; mip < uint(heightMips.size()); ++mip)
            graphicsQueue->writeTexture(heightTexture, heightMips[mip].data(), uint64_t(heightMips[mip].size()), mip)->submit();
        shadowResources = device->createBindGroup<WebglMaterialsBumpmapShadowResources>(uniformBuffer);
        resources = device->createBindGroup<WebglMaterialsBumpmapResources>(
            uniformBuffer, heightTexture->createView(), shadowTexture->createView(), materialSampler);
        shadowPass = device->createRenderClass<WebglMaterialsBumpmapShadowDepthPass>(shadowResources);
        mainPass = device->createRenderClass<WebglMaterialsBumpmapMainPass>(resources);
    }

    /** Executes the shared-geometry shadow and main Scene passes, then presents. */
    void render() override
    {
        WebglMaterialsBumpmapShadowFrameBuffer shadowFrame;
        shadowFrame.depth = shadowTexture->createView();
        shadowFrame.depth.depthLoadOp = LoadOp::Clear;
        shadowFrame.depth.depthStoreOp = StoreOp::Store;
        shadowFrame.depth.depthClearValue = 1.0f;
        WebglMaterialsBumpmapFrameBuffer mainFrame;
        mainFrame.color = outputTexture->createView();
        mainFrame.color.loadOp = LoadOp::Clear;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.color.clearValue = {6.0 / 255.0, 7.0 / 255.0, 8.0 / 255.0, 1.0};
        mainFrame.depth = depthTexture->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Clear;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        mainFrame.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglMaterialsBumpmapShadowDepth", shadowFrame,
                shadowPass->setVertexBuffer(vertexBuffer), shadowPass->setIndexBuffer(indexBuffer),
                shadowPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebglMaterialsBumpmapMain", mainFrame,
                mainPass->setVertexBuffer(vertexBuffer), mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for strict readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases every dedicated standalone Scene resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(heightTexture);
        device->freeTexture(shadowTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
