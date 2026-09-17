#ifndef GVM_THREE_WEBGL_MATERIALS_NORMALMAP_HPP
#define GVM_THREE_WEBGL_MATERIALS_NORMALMAP_HPP

#include "UGL.h"
#include "WebglMaterialsNormalmapData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

/** Binds the three authored material textures and immutable scene state. */
struct WebglMaterialsNormalmapSceneResources final : public IBindGroup
{
    /** Declares all resources consumed by the dedicated Phong scene pass. */
    constructor(
        UniformBuffer<WebglMaterialsNormalmapUniforms> uniforms [[Binding0]],
        Texture2D<float4> diffuseMap [[Binding1]],
        Texture2D<float4> specularMap [[Binding2]],
        Texture2D<float4> normalMap [[Binding3]],
        Sampler materialSampler [[Binding4]])
    {
    }
};

/** Binds one postprocess source and the shared linear sampler. */
struct WebglMaterialsNormalmapScreenResources final : public IBindGroup
{
    /** Declares a single fullscreen source texture. */
    constructor(
        Texture2D<float4> source [[Binding0]],
        Sampler linearSampler [[Binding1]])
    {
    }
};

/** Carries view-space tangent-frame inputs to the Phong fragment stage. */
struct WebglMaterialsNormalmapVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float3 viewTangent [[Attribute2]];
    float3 viewBitangent [[Attribute3]];
    float2 textureCoordinate [[Attribute4]];
};

/** Carries one analytic fullscreen coordinate through every screen pass. */
struct WebglMaterialsNormalmapScreenVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the half-float scene and intermediate color target. */
struct WebglMaterialsNormalmapHdrFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the half-float scene target with its depth attachment. */
struct WebglMaterialsNormalmapSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one encoded RGBA8 postprocess or final output target. */
struct WebglMaterialsNormalmapOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts an authored sRGB channel into linear working space. */
float webglMaterialsNormalmapSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Converts a non-negative linear channel through Three's output transfer. */
float webglMaterialsNormalmapLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Emits one oversized fullscreen triangle with top-down texture coordinates. */
WebglMaterialsNormalmapScreenVertexOutput
webglMaterialsNormalmapFullscreenVertex(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglMaterialsNormalmapScreenVertexOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Evaluates the r185 Schlick approximation for one Phong half vector. */
float3 webglMaterialsNormalmapFresnel(float3 specularColor, float dotVH)
{
    const float factor = exp2((-5.55473f * dotVH - 6.98316f) * dotVH);
    return specularColor * (1.0f - factor) + float3(factor);
}

/** Evaluates one direct Blinn-Phong light contribution. */
float3 webglMaterialsNormalmapDirectLight(
    float3 lightDirection,
    float3 lightColor,
    float3 normal,
    float3 viewDirection,
    float3 diffuseColor,
    float3 specularColor,
    float specularStrength)
{
    const float dotNL = max(dot(normal, lightDirection), 0.0f);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNH = max(dot(normal, halfDirection), 0.0f);
    const float dotVH = max(dot(viewDirection, halfDirection), 0.0f);
    const float distribution =
        (35.0f * 0.5f + 1.0f) * 0.3183098861837907f *
        pow(dotNH, 35.0f);
    const float3 diffuse =
        diffuseColor * dotNL * 0.3183098861837907f;
    const float3 specular =
        webglMaterialsNormalmapFresnel(specularColor, dotVH) *
        (0.25f * distribution * dotNL * specularStrength);
    return lightColor * (diffuse + specular);
}

/** Renders the one Lee Perry Smith mesh with tangent-space normal Phong. */
class WebglMaterialsNormalmapMainPass final : public IRenderClass
{
public:
    /** Configures the standalone opaque front-sided scene draw. */
    constructor(
        BindGroup<WebglMaterialsNormalmapSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the selected Orbit camera and transforms the tangent frame. */
    WebglMaterialsNormalmapVertexOutput vertex(
        WebglMaterialsNormalmapVertex inputValue [[VertexInput0]])
    {
        const float4 localPosition = float4(inputValue.position, 1.0f);
        const float4 viewPosition = mul(
            resources->uniforms->modelView, localPosition);
        WebglMaterialsNormalmapVertexOutput outputValue;
        outputValue.position = mul(
            resources->uniforms->modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(float3(mul(
            resources->uniforms->normalTransform,
            float4(inputValue.normal, 0.0f)).xyz));
        outputValue.viewTangent = normalize(float3(mul(
            resources->uniforms->modelView,
            float4(inputValue.tangent, 0.0f)).xyz));
        outputValue.viewBitangent = normalize(float3(mul(
            resources->uniforms->modelView,
            float4(inputValue.bitangent, 0.0f)).xyz));
        outputValue.textureCoordinate = inputValue.textureCoordinate;
        return outputValue;
    }

    /** Samples all authored maps and evaluates ambient, point, and directional Phong. */
    WebglMaterialsNormalmapSceneFrameBuffer fragment(
        WebglMaterialsNormalmapVertexOutput inputValue)
    {
        const float2 uv = inputValue.textureCoordinate;
        const float4 sampledDiffuse = resources->diffuseMap->sample(
            resources->materialSampler, uv);
        const float3 diffuseColor = float3(
            webglMaterialsNormalmapSrgbToLinear(sampledDiffuse.r),
            webglMaterialsNormalmapSrgbToLinear(sampledDiffuse.g),
            webglMaterialsNormalmapSrgbToLinear(sampledDiffuse.b)) *
            resources->uniforms->materialAndNormalState.xyz;
        const float sampledSpecular = webglMaterialsNormalmapSrgbToLinear(
            resources->specularMap->sample(
                resources->materialSampler, uv).r);
        float3 normal = normalize(inputValue.viewNormal);
        if (resources->uniforms->materialAndNormalState.w > 0.5f)
        {
            float3 mappedNormal = resources->normalMap->sample(
                resources->materialSampler, uv).xyz * 2.0f - float3(1.0f);
            mappedNormal.xy *= resources->uniforms->viewport.z;
            normal = normalize(
                inputValue.viewTangent * mappedNormal.x +
                inputValue.viewBitangent * mappedNormal.y +
                inputValue.viewNormal * mappedNormal.z);
        }
        const float3 viewDirection = normalize(-inputValue.viewPosition);
        const float3 pointVector =
            resources->uniforms->pointLightPositionAndIntensity.xyz -
            inputValue.viewPosition;
        const float pointDistance = max(length(pointVector), 0.0001f);
        const float3 pointDirection = pointVector / pointDistance;
        const float pointAttenuation =
            resources->uniforms->pointLightPositionAndIntensity.w /
            (pointDistance * pointDistance);
        const float3 specularColor = float3(
            webglMaterialsNormalmapSrgbToLinear(34.0f / 255.0f));
        float3 linearColor = diffuseColor * 0.3183098861837907f;
        linearColor += webglMaterialsNormalmapDirectLight(
            pointDirection,
            float3(pointAttenuation),
            normal,
            viewDirection,
            diffuseColor,
            specularColor,
            sampledSpecular);
        linearColor += webglMaterialsNormalmapDirectLight(
            normalize(float3(resources->uniforms->directionalLightAndIntensity.xyz)),
            float3(resources->uniforms->directionalLightAndIntensity.w),
            normal,
            viewDirection,
            diffuseColor,
            specularColor,
            sampledSpecular);
        WebglMaterialsNormalmapSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Applies the authored BleachBypassShader at opacity 0.2. */
class WebglMaterialsNormalmapBleachPass final : public IRenderClass
{
public:
    /** Binds the linear scene target for fullscreen processing. */
    constructor(
        BindGroup<WebglMaterialsNormalmapScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }
private:
    /** Emits one fullscreen triangle. */
    WebglMaterialsNormalmapScreenVertexOutput vertex(uint id [[VertexID]])
    { return webglMaterialsNormalmapFullscreenVertex(id); }
    /** Reproduces the complete r185 bleach-bypass fragment shader. */
    WebglMaterialsNormalmapHdrFrameBuffer fragment(
        WebglMaterialsNormalmapScreenVertexOutput inputValue)
    {
        const float4 base = resources->source->sample(
            resources->linearSampler, inputValue.uv);
        const float luminance = dot(float3(base.rgb), float3(0.2126f, 0.7152f, 0.0722f));
        const float3 blend = float3(luminance);
        const float blendWeight = clamp(10.0f * (luminance - 0.45f), 0.0f, 1.0f);
        const float3 result1 = 2.0f * base.rgb * blend;
        const float3 result2 = 1.0f - 2.0f * (1.0f - blend) * (1.0f - base.rgb);
        const float3 bleachColor = lerp(result1, result2, blendWeight);
        const float opacity = 0.2f * base.a;
        WebglMaterialsNormalmapHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(opacity * bleachColor + (1.0f - opacity) * base.rgb),
            half(base.a));
        return frameBuffer;
    }
};

/** Applies the authored ColorCorrectionShader constants. */
class WebglMaterialsNormalmapColorPass final : public IRenderClass
{
public:
    /** Binds the bleach result for deterministic color correction. */
    constructor(
        BindGroup<WebglMaterialsNormalmapScreenResources> resources [[Slot0]])
    { setCullMode(CullMode::None); setDepthWriteEnabled(false); }
private:
    /** Emits one fullscreen triangle. */
    WebglMaterialsNormalmapScreenVertexOutput vertex(uint id [[VertexID]])
    { return webglMaterialsNormalmapFullscreenVertex(id); }
    /** Applies powRGB and mulRGB exactly as authored. */
    WebglMaterialsNormalmapHdrFrameBuffer fragment(
        WebglMaterialsNormalmapScreenVertexOutput inputValue)
    {
        const float4 color = resources->source->sample(
            resources->linearSampler, inputValue.uv);
        WebglMaterialsNormalmapHdrFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(float3(1.1f) * pow(
                float3(color.rgb), float3(1.4f, 1.45f, 1.45f))),
            half(color.a));
        return frameBuffer;
    }
};

/** Converts the corrected linear image before the final FXAA pass. */
class WebglMaterialsNormalmapOutputPass final : public IRenderClass
{
public:
    /** Binds the corrected HDR source without depth or culling. */
    constructor(
        BindGroup<WebglMaterialsNormalmapScreenResources> resources [[Slot0]])
    { setCullMode(CullMode::None); setDepthWriteEnabled(false); }
private:
    /** Emits one fullscreen triangle. */
    WebglMaterialsNormalmapScreenVertexOutput vertex(uint id [[VertexID]])
    { return webglMaterialsNormalmapFullscreenVertex(id); }
    /** Applies Three's default sRGB output transfer. */
    WebglMaterialsNormalmapOutputFrameBuffer fragment(
        WebglMaterialsNormalmapScreenVertexOutput inputValue)
    {
        const float4 color = resources->source->sample(
            resources->linearSampler, inputValue.uv);
        WebglMaterialsNormalmapOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglMaterialsNormalmapLinearToSrgb(color.r)),
            half(webglMaterialsNormalmapLinearToSrgb(color.g)),
            half(webglMaterialsNormalmapLinearToSrgb(color.b)),
            half(color.a));
        return frameBuffer;
    }
};

/** Applies the deterministic r185 FXAA neighborhood and subpixel blend. */
class WebglMaterialsNormalmapFxaaPass final : public IRenderClass
{
public:
    /** Binds the encoded image sampled at the fixed 800-by-500 extent. */
    constructor(
        BindGroup<WebglMaterialsNormalmapScreenResources> resources [[Slot0]])
    { setCullMode(CullMode::None); setDepthWriteEnabled(false); }
private:
    /** Emits one fullscreen triangle. */
    WebglMaterialsNormalmapScreenVertexOutput vertex(uint id [[VertexID]])
    { return webglMaterialsNormalmapFullscreenVertex(id); }
    /** Performs the standard center/cardinal/diagonal FXAA subpixel resolve. */
    WebglMaterialsNormalmapOutputFrameBuffer fragment(
        WebglMaterialsNormalmapScreenVertexOutput inputValue)
    {
        const float2 texel = float2(1.0f / 800.0f, 1.0f / 500.0f);
        const float4 center = resources->source->sample(
            resources->linearSampler, inputValue.uv);
        const float3 north = resources->source->sample(
            resources->linearSampler, inputValue.uv + float2(0.0f, texel.y)).rgb;
        const float3 east = resources->source->sample(
            resources->linearSampler, inputValue.uv + float2(texel.x, 0.0f)).rgb;
        const float3 south = resources->source->sample(
            resources->linearSampler, inputValue.uv - float2(0.0f, texel.y)).rgb;
        const float3 west = resources->source->sample(
            resources->linearSampler, inputValue.uv - float2(texel.x, 0.0f)).rgb;
        const float luminanceCenter = dot(float3(center.rgb), float3(0.3f, 0.59f, 0.11f));
        const float luminanceNorth = dot(north, float3(0.3f, 0.59f, 0.11f));
        const float luminanceEast = dot(east, float3(0.3f, 0.59f, 0.11f));
        const float luminanceSouth = dot(south, float3(0.3f, 0.59f, 0.11f));
        const float luminanceWest = dot(west, float3(0.3f, 0.59f, 0.11f));
        const float highest = max(luminanceCenter, max(max(luminanceNorth, luminanceEast), max(luminanceSouth, luminanceWest)));
        const float lowest = min(luminanceCenter, min(min(luminanceNorth, luminanceEast), min(luminanceSouth, luminanceWest)));
        const float contrast = highest - lowest;
        float3 resolved = float3(center.rgb);
        if (contrast >= max(0.0312f, highest * 0.063f))
        {
            const float average = (2.0f * (luminanceNorth + luminanceEast + luminanceSouth + luminanceWest) + 4.0f * luminanceCenter) / 12.0f;
            float blend = clamp(abs(average - luminanceCenter) / max(contrast, 0.000001f), 0.0f, 1.0f);
            blend = smoothstep(0.0f, 1.0f, blend);
            blend *= blend;
            resolved = lerp(float3(center.rgb), (north + east + south + west) * 0.25f, blend);
        }
        WebglMaterialsNormalmapOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(resolved), half(center.a));
        return frameBuffer;
    }
};

/** Owns the dedicated mesh, material maps, and five-pass output chain. */
class WebglMaterialsNormalmapRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMaterialsNormalmapVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<WebglMaterialsNormalmapUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> diffuseTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> specularTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> normalTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> bleachTexture;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> colorTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> encodedTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    Sampler materialSampler;
    Sampler linearSampler;
    BindGroup<WebglMaterialsNormalmapSceneResources> sceneResources;
    BindGroup<WebglMaterialsNormalmapScreenResources> bleachResources;
    BindGroup<WebglMaterialsNormalmapScreenResources> colorResources;
    BindGroup<WebglMaterialsNormalmapScreenResources> outputResources;
    BindGroup<WebglMaterialsNormalmapScreenResources> fxaaResources;
    RenderClass<WebglMaterialsNormalmapMainPass> mainPass;
    RenderClass<WebglMaterialsNormalmapBleachPass> bleachPass;
    RenderClass<WebglMaterialsNormalmapColorPass> colorPass;
    RenderClass<WebglMaterialsNormalmapOutputPass> outputPass;
    RenderClass<WebglMaterialsNormalmapFxaaPass> fxaaPass;
    uint indexCount = 0u;
    uint width = 800u;
    uint height = 500u;

public:
    /** Stores the generated device handles and creates immutable samplers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        materialSampler = device->createSampler({
            .label = "WebglMaterialsNormalmapMaterialSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
        });
        linearSampler = device->createSampler({
            .label = "WebglMaterialsNormalmapLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
        });
    }

    /** Allocates all single-sample HDR, encoded, depth, and readback targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture("WebglMaterialsNormalmapScene", width, height, 1u);
        bleachTexture = device->createTexture("WebglMaterialsNormalmapBleach", width, height, 1u);
        colorTexture = device->createTexture("WebglMaterialsNormalmapColor", width, height, 1u);
        encodedTexture = device->createTexture("WebglMaterialsNormalmapEncoded", width, height, 1u);
        outputTexture = device->createTexture("WebglMaterialsNormalmapOutput", width, height, 1u);
        depthTexture = device->createTexture("WebglMaterialsNormalmapDepth", width, height, 1u);
    }

    /** Uploads exact geometry, explicit texture mips, uniforms, and all passes. */
    void configureScene(
        const eastl::vector<WebglMaterialsNormalmapVertex> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<eastl::vector<uint8_t>> &diffuseMips,
        uint diffuseWidth,
        uint diffuseHeight,
        const eastl::vector<eastl::vector<uint8_t>> &specularMips,
        uint specularWidth,
        uint specularHeight,
        const eastl::vector<eastl::vector<uint8_t>> &normalMips,
        uint normalWidth,
        uint normalHeight,
        WebglMaterialsNormalmapUniforms uniforms)
    {
        indexCount = uint(indices.size());
        vertexBuffer = device->createBuffer("WebglMaterialsNormalmapVertices", uint(vertices.size()));
        indexBuffer = device->createBuffer("WebglMaterialsNormalmapIndices", indexCount);
        uniformBuffer = device->createBuffer("WebglMaterialsNormalmapUniforms", 1u);
        diffuseTexture = device->createTexture("WebglMaterialsNormalmapDiffuse", diffuseWidth, diffuseHeight, 1u, uint(diffuseMips.size()));
        specularTexture = device->createTexture("WebglMaterialsNormalmapSpecular", specularWidth, specularHeight, 1u, uint(specularMips.size()));
        normalTexture = device->createTexture("WebglMaterialsNormalmapNormal", normalWidth, normalHeight, 1u, uint(normalMips.size()));
        graphicsQueue
            ->writeBuffer(BufferRange(vertexBuffer), vertices.data(), uint64_t(vertices.size()) * sizeof(vertices[0u]))
            ->writeBuffer(BufferRange(indexBuffer), indices.data(), uint64_t(indices.size()) * sizeof(indices[0u]))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))
            ->submit();
        for (uint mip = 0u; mip < uint(diffuseMips.size()); ++mip)
            graphicsQueue->writeTexture(diffuseTexture, diffuseMips[mip].data(), uint64_t(diffuseMips[mip].size()), mip)->submit();
        for (uint mip = 0u; mip < uint(specularMips.size()); ++mip)
            graphicsQueue->writeTexture(specularTexture, specularMips[mip].data(), uint64_t(specularMips[mip].size()), mip)->submit();
        for (uint mip = 0u; mip < uint(normalMips.size()); ++mip)
            graphicsQueue->writeTexture(normalTexture, normalMips[mip].data(), uint64_t(normalMips[mip].size()), mip)->submit();
        sceneResources = device->createBindGroup<WebglMaterialsNormalmapSceneResources>(
            uniformBuffer, diffuseTexture->createView(), specularTexture->createView(),
            normalTexture->createView(), materialSampler);
        bleachResources = device->createBindGroup<WebglMaterialsNormalmapScreenResources>(sceneTexture->createView(), linearSampler);
        colorResources = device->createBindGroup<WebglMaterialsNormalmapScreenResources>(bleachTexture->createView(), linearSampler);
        outputResources = device->createBindGroup<WebglMaterialsNormalmapScreenResources>(colorTexture->createView(), linearSampler);
        fxaaResources = device->createBindGroup<WebglMaterialsNormalmapScreenResources>(encodedTexture->createView(), linearSampler);
        mainPass = device->createRenderClass<WebglMaterialsNormalmapMainPass>(sceneResources);
        bleachPass = device->createRenderClass<WebglMaterialsNormalmapBleachPass>(bleachResources);
        colorPass = device->createRenderClass<WebglMaterialsNormalmapColorPass>(colorResources);
        outputPass = device->createRenderClass<WebglMaterialsNormalmapOutputPass>(outputResources);
        fxaaPass = device->createRenderClass<WebglMaterialsNormalmapFxaaPass>(fxaaResources);
    }

    /** Executes the scene and complete authored postprocess sequence. */
    void render() override
    {
        WebglMaterialsNormalmapSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.06662594, 0.06662594, 0.06662594, 1.0};
        sceneFrame.depth = depthTexture->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglMaterialsNormalmapHdrFrameBuffer bleachFrame;
        bleachFrame.color = bleachTexture->createView();
        bleachFrame.color.loadOp = LoadOp::Clear;
        bleachFrame.color.storeOp = StoreOp::Store;
        WebglMaterialsNormalmapHdrFrameBuffer colorFrame;
        colorFrame.color = colorTexture->createView();
        colorFrame.color.loadOp = LoadOp::Clear;
        colorFrame.color.storeOp = StoreOp::Store;
        WebglMaterialsNormalmapOutputFrameBuffer encodedFrame;
        encodedFrame.color = encodedTexture->createView();
        encodedFrame.color.loadOp = LoadOp::Clear;
        encodedFrame.color.storeOp = StoreOp::Store;
        WebglMaterialsNormalmapOutputFrameBuffer finalFrame;
        finalFrame.color = outputTexture->createView();
        finalFrame.color.loadOp = LoadOp::Clear;
        finalFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglMaterialsNormalmapMain", sceneFrame,
                mainPass->setVertexBuffer(vertexBuffer), mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebglMaterialsNormalmapBleach", bleachFrame, bleachPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglMaterialsNormalmapColor", colorFrame, colorPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglMaterialsNormalmapOutput", encodedFrame, outputPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglMaterialsNormalmapFxaa", finalFrame, fxaaPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 target for strict readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases every dedicated geometry, texture, and attachment resource. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(diffuseTexture);
        device->freeTexture(specularTexture);
        device->freeTexture(normalTexture);
        device->freeTexture(sceneTexture);
        device->freeTexture(bleachTexture);
        device->freeTexture(colorTexture);
        device->freeTexture(encodedTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
