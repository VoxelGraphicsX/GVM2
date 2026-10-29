#ifndef GVM_THREE_WEBGPU_SKINNING_INSTANCING_HPP
#define GVM_THREE_WEBGPU_SKINNING_INSTANCING_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the attribute union shared by the ground and skinned Michelle entity. */
struct WebgpuSkinningInstancingVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    uint4 joints [[Attribute2]];
    float4 weights [[Attribute3]];
};

/** Stores the immutable Scene transforms and deterministic animation time. */
struct WebgpuSkinningInstancingObjectData
{
    float4x4 model;
    float4x4 viewProjection;
    float4 cameraPositionAndTime;
};

/** Stores one instance transform used by the single Set indirect draw. */
struct WebgpuSkinningInstancingInstanceData
{
    float4x4 transform;
    float4 randomColorAndMetalness;
};

/** Stores one Scene material phase and its fixed lighting parameters. */
struct WebgpuSkinningInstancingMaterialData
{
    float4 baseColorAndRoughness;
    float4 pointColorAndPower;
    float4 cameraLightColorAndPower;
    uint4 phaseAndReserved;
};

/** Stores one animated joint matrix in the shared 65-joint palette. */
struct WebgpuSkinningInstancingSkinMatrix
{
    float4x4 value;
};

/** Defines the unique RenderSet owned by the complete logical Scene. */
struct WebgpuSkinningInstancingSceneRenderSet : public IRenderSet
{
    /** Declares canonical geometry plus object, instance, material, and skin data. */
    constructor(
        BufferComponent<WebgpuSkinningInstancingVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuSkinningInstancingObjectData> objects,
        BufferComponent<WebgpuSkinningInstancingInstanceData> instances,
        BufferComponent<WebgpuSkinningInstancingMaterialData> materials,
        BufferComponent<WebgpuSkinningInstancingSkinMatrix> skinPalettes,
        BufferComponent<uint4> renderFlags)
    {
    }
};

/** Stores viewport dimensions for the depth-directed separable blur. */
struct WebgpuSkinningInstancingBlurUniforms
{
    float4 viewportAndDirection;
};

/** Binds the Scene color, linear depth, and exact linear sampler. */
struct WebgpuSkinningInstancingBlurResources final : public IBindGroup
{
    /** Declares the immutable inputs for one blur direction. */
    constructor(
        Texture2D<float4> colorTexture [[Binding0]],
        Texture2D<float> depthTexture [[Binding1]],
        Sampler linearSampler [[Binding2]],
        UniformBuffer<WebgpuSkinningInstancingBlurUniforms> uniforms [[Binding3]])
    {
    }
};

/** Carries world attributes and entity identity into standard lighting. */
struct WebgpuSkinningInstancingSceneOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    uint entityID [[Attribute2]];
    uint instanceID [[Attribute3]];
    float linearDepth [[Attribute4]];
};

/** Carries fullscreen UV coordinates into both blur passes. */
struct WebgpuSkinningInstancingScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the single-sample Scene color, linear depth, and depth attachments. */
struct WebgpuSkinningInstancingSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    ColorAttachment<TextureFormat::R32Float> linearDepth;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one linear intermediate blur attachment. */
struct WebgpuSkinningInstancingLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final display-encoded single-sample attachment. */
struct WebgpuSkinningInstancingOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear-light channel to the browser output transfer. */
float webgpuSkinningInstancingLinearToSrgb(float value)
{
    const float bounded = clamp(value, 0.0f, 1.0f);
    return bounded <= 0.0031308f
        ? bounded * 12.92f
        : pow(bounded, 0.41666f) * 1.055f - 0.055f;
}

/** Returns one deterministic range value for the specified instance. */
float webgpuSkinningInstancingRange(uint instanceID, float salt)
{
    return frac(sin(float(instanceID) * 12.9898f + salt) * 43758.5453f);
}

/** Evaluates the r185 correlated Smith GGX specular BRDF. */
float3 webgpuSkinningInstancingGgx(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 f0,
    float roughness)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNL = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNV = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotNH = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotVH = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float gv = dotNL * sqrt(
        alphaSquared + (1.0f - alphaSquared) * dotNV * dotNV);
    const float gl = dotNV * sqrt(
        alphaSquared + (1.0f - alphaSquared) * dotNL * dotNL);
    const float visibility = 0.5f / max(gv + gl, 0.000001f);
    const float distributionDenominator =
        1.0f - dotNH * dotNH * (1.0f - alphaSquared);
    const float distribution = alphaSquared /
        max(distributionDenominator * distributionDenominator *
            3.14159265359f, 0.000001f);
    const float fresnelWeight = exp2(
        (-5.55473f * dotVH - 6.98316f) * dotVH);
    const float3 fresnel = f0 * (1.0f - fresnelWeight) +
        float3(fresnelWeight);
    return fresnel * visibility * distribution;
}

/** Evaluates one finite-distance point light with the r185 standard BRDF. */
float3 webgpuSkinningInstancingPointLight(
    float3 worldPosition,
    float3 normal,
    float3 viewDirection,
    float3 lightPosition,
    float3 lightColor,
    float lightIntensity,
    float3 diffuseColor,
    float3 specularColor,
    float roughness)
{
    const float3 lightVector = lightPosition - worldPosition;
    const float lightDistanceSquared = max(dot(lightVector, lightVector), 0.01f);
    const float lightDistance = sqrt(lightDistanceSquared);
    const float cutoffWindow = clamp(
        1.0f - pow(lightDistance / 100.0f, 4.0f), 0.0f, 1.0f);
    const float attenuation =
        cutoffWindow * cutoffWindow / lightDistanceSquared;
    const float3 lightDirection = lightVector / lightDistance;
    const float dotNL = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float3 irradiance =
        lightColor * (lightIntensity * attenuation * dotNL);
    return irradiance * (diffuseColor * 0.3183098862f +
        webgpuSkinningInstancingGgx(
            normal, viewDirection, lightDirection, specularColor, roughness));
}

/** Applies one four-weight Scene Set palette to a position or direction. */
float4 webgpuSkinningInstancingSkinVector(
    IN RenderSet<WebgpuSkinningInstancingSceneRenderSet> sceneSet,
    uint entityID,
    WebgpuSkinningInstancingVertex inputValue,
    float4 vectorValue)
{
    const float4x4 matrix0 = sceneSet->skinPalettes
        ->get(entityID, inputValue.joints.x).value;
    const float4x4 matrix1 = sceneSet->skinPalettes
        ->get(entityID, inputValue.joints.y).value;
    const float4x4 matrix2 = sceneSet->skinPalettes
        ->get(entityID, inputValue.joints.z).value;
    const float4x4 matrix3 = sceneSet->skinPalettes
        ->get(entityID, inputValue.joints.w).value;
    return mul(matrix0, vectorValue) * inputValue.weights.x +
        mul(matrix1, vectorValue) * inputValue.weights.y +
        mul(matrix2, vectorValue) * inputValue.weights.z +
        mul(matrix3, vectorValue) * inputValue.weights.w;
}

/** Draws the ground and all thirty Michelle instances from one RenderSet. */
class WebgpuSkinningInstancingMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene Set and the shared opaque pipeline state. */
    constructor(
        RenderSet<WebgpuSkinningInstancingSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the current instance transform to CPU-evaluated skin attributes. */
    WebgpuSkinningInstancingSceneOutput vertex(
        WebgpuSkinningInstancingVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuSkinningInstancingObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuSkinningInstancingInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position;
        const float4 localNormal = float4(inputValue.normal.xyz, 0.0f);
        const float4 worldPosition = mul(
            objectData.model,
            mul(instanceData.transform, localPosition));
        const float4 worldNormal = float4(localNormal.xyz, 0.0f);
        WebgpuSkinningInstancingSceneOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = normalize(float3(worldNormal.xyz));
        outputValue.entityID = renderEntityID;
        outputValue.instanceID = renderEntityInstanceID;
        const float3 cameraForward = normalize(float3(-1.0f, -1.0f, -3.0f));
        const float viewDistance = dot(
            worldPosition.xyz - objectData.cameraPositionAndTime.xyz,
            cameraForward);
        outputValue.linearDepth = clamp(
            (viewDistance - 0.01f) / (40.0f - 0.01f), 0.0f, 1.0f);
        return outputValue;
    }

    /** Shades the plane or the oscillating per-instance standard material. */
    WebgpuSkinningInstancingSceneFrameBuffer fragment(
        WebgpuSkinningInstancingSceneOutput inputValue)
    {
        const WebgpuSkinningInstancingObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebgpuSkinningInstancingMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const uint4 renderFlags =
            sceneSet->renderFlags->get(inputValue.entityID, 0u);
        float3 linearColor = materialData.baseColorAndRoughness.xyz;
        if (renderFlags.x != 0u)
        {
            const float oscillator = sin(
                (objectData.cameraPositionAndTime.w * 0.1f + 0.75f) *
                6.28318530718f) * 0.5f + 0.5f;
            const WebgpuSkinningInstancingInstanceData instanceData =
                sceneSet->instances->get(
                    inputValue.entityID, inputValue.instanceID);
            const float3 randomColor =
                instanceData.randomColorAndMetalness.xyz;
            const float metallic =
                instanceData.randomColorAndMetalness.w * oscillator;
            const float3 albedo = lerp(float3(1.0f), randomColor, oscillator);
            const float3 normal = normalize(inputValue.worldNormal);
            const float3 viewDirection = normalize(
                objectData.cameraPositionAndTime.xyz - inputValue.worldPosition);
            const float roughness = materialData.baseColorAndRoughness.w;
            const float3 diffuseColor = albedo * (1.0f - metallic);
            const float3 specularColor = lerp(
                float3(0.04f), albedo, metallic);
            linearColor =
                webgpuSkinningInstancingPointLight(
                    inputValue.worldPosition, normal, viewDirection,
                    float3(0.0f, 4.5f, -2.0f),
                    materialData.pointColorAndPower.xyz,
                    materialData.pointColorAndPower.w,
                    diffuseColor, specularColor, roughness) +
                webgpuSkinningInstancingPointLight(
                    inputValue.worldPosition, normal, viewDirection,
                    objectData.cameraPositionAndTime.xyz,
                    materialData.cameraLightColorAndPower.xyz,
                    materialData.cameraLightColorAndPower.w,
                    diffuseColor, specularColor, roughness);
        }
        WebgpuSkinningInstancingSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(1.0f));
        const float3 cameraForward = normalize(float3(-1.0f, -1.0f, -3.0f));
        const float fragmentViewDistance = dot(
            inputValue.worldPosition - objectData.cameraPositionAndTime.xyz,
            cameraForward);
        frameBuffer.linearDepth = clamp(
            (fragmentViewDistance - 0.01f) / (40.0f - 0.01f),
            0.0f, 1.0f);
        return frameBuffer;
    }
};

/** Emits the common fullscreen triangle used by both screen passes. */
WebgpuSkinningInstancingScreenOutput webgpuSkinningInstancingFullscreenVertex(
    uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebgpuSkinningInstancingScreenOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Applies one direction of the depth-directed Gaussian blur. */
class WebgpuSkinningInstancingBlurPass final : public IRenderClass
{
public:
    /** Binds one immutable color/depth pair and disables geometry state. */
    constructor(
        BindGroup<WebgpuSkinningInstancingBlurResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuSkinningInstancingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuSkinningInstancingFullscreenVertex(vertexID);
    }

    /** Evaluates a deterministic nine-tap Gaussian in the configured direction. */
    WebgpuSkinningInstancingLinearFrameBuffer fragment(
        WebgpuSkinningInstancingScreenOutput inputValue)
    {
        const float depth = resources->depthTexture->sampleLevel(
            resources->linearSampler, inputValue.uv, 0.0f).x;
        const float directionScale = clamp(
            (depth - 0.15f) / 0.15f, 0.0f, 1.0f);
        const float2 texel = resources->uniforms->viewportAndDirection.zw *
            directionScale /
            resources->uniforms->viewportAndDirection.xy;
        const float sigma = 11.0f / 3.0f;
        float4 color = resources->colorTexture->sampleLevel(
            resources->linearSampler, inputValue.uv, 0.0f) *
            (0.39894f / sigma);
        for (uint sampleIndex = 1u; sampleIndex < 11u; ++sampleIndex)
        {
            const float sampleOffset = float(sampleIndex);
            const float weight = 0.39894f * exp(
                -0.5f * sampleOffset * sampleOffset / (sigma * sigma)) /
                sigma;
            color += resources->colorTexture->sampleLevel(
                resources->linearSampler,
                inputValue.uv + texel * sampleOffset, 0.0f) * weight;
            color += resources->colorTexture->sampleLevel(
                resources->linearSampler,
                inputValue.uv - texel * sampleOffset, 0.0f) * weight;
        }
        WebgpuSkinningInstancingLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Encodes the final blurred linear result into RGBA8. */
class WebgpuSkinningInstancingResolvePass final : public IRenderClass
{
public:
    /** Reuses the vertical blur resources as the final sampled input. */
    constructor(
        BindGroup<WebgpuSkinningInstancingBlurResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuSkinningInstancingScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuSkinningInstancingFullscreenVertex(vertexID);
    }

    /** Applies the exact browser output transfer after both blur directions. */
    WebgpuSkinningInstancingOutputFrameBuffer fragment(
        WebgpuSkinningInstancingScreenOutput inputValue)
    {
        const float3 linearColor = resources->colorTexture->sampleLevel(
            resources->linearSampler, inputValue.uv, 0.0f).xyz;
        WebgpuSkinningInstancingOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webgpuSkinningInstancingLinearToSrgb(linearColor.x)),
            half(webgpuSkinningInstancingLinearToSrgb(linearColor.y)),
            half(webgpuSkinningInstancingLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the complete single-sample render graph. */
class WebgpuSkinningInstancingRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuSkinningInstancingSceneRenderSet> sceneSet;
    RenderClass<WebgpuSkinningInstancingMainPass> mainPass;
    RenderClass<WebgpuSkinningInstancingBlurPass> horizontalBlurPass;
    RenderClass<WebgpuSkinningInstancingBlurPass> verticalBlurPass;
    RenderClass<WebgpuSkinningInstancingResolvePass> resolvePass;
    Sampler linearSampler;
    Buffer<WebgpuSkinningInstancingBlurUniforms,
           BufferUsage<Uniform, CopyDst>> horizontalUniforms;
    Buffer<WebgpuSkinningInstancingBlurUniforms,
           BufferUsage<Uniform, CopyDst>> verticalUniforms;
    BindGroup<WebgpuSkinningInstancingBlurResources> horizontalResources;
    BindGroup<WebgpuSkinningInstancingBlurResources> verticalResources;
    BindGroup<WebgpuSkinningInstancingBlurResources> resolveResources;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::R32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> sceneLinearDepth;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> horizontalColor;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> verticalColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;

public:
    /** Creates the unique Scene Set and every dedicated pipeline object. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebgpuSkinningInstancingSceneRenderSet>();
        mainPass = device->createRenderClass<WebgpuSkinningInstancingMainPass>(sceneSet);
        linearSampler = device->createSampler({
            .label = "WebgpuSkinningInstancingLinearSampler",
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

    /** Allocates the fixed 800x500 single-sample attachments and screen resources. */
    void configureOutput(uint width, uint height)
    {
        sceneColor = device->createTexture(
            "WebgpuSkinningInstancingSceneColor", width, height, 1u);
        sceneLinearDepth = device->createTexture(
            "WebgpuSkinningInstancingLinearDepth", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebgpuSkinningInstancingDepth", width, height, 1u);
        horizontalColor = device->createTexture(
            "WebgpuSkinningInstancingHorizontal", width, height, 1u);
        verticalColor = device->createTexture(
            "WebgpuSkinningInstancingVertical", width, height, 1u);
        outputColor = device->createTexture(
            "WebgpuSkinningInstancingOutput", width, height, 1u);
        horizontalUniforms = device->createBuffer(
            "WebgpuSkinningInstancingHorizontalUniforms", 1u);
        verticalUniforms = device->createBuffer(
            "WebgpuSkinningInstancingVerticalUniforms", 1u);
        WebgpuSkinningInstancingBlurUniforms horizontalValue;
        horizontalValue.viewportAndDirection = float4(float(width), float(height), 1.0f, 0.0f);
        WebgpuSkinningInstancingBlurUniforms verticalValue;
        verticalValue.viewportAndDirection = float4(float(width), float(height), 0.0f, 1.0f);
        graphicsQueue->writeBuffer(
            BufferRange(horizontalUniforms), &horizontalValue,
            sizeof(horizontalValue))->submit();
        graphicsQueue->writeBuffer(
            BufferRange(verticalUniforms), &verticalValue,
            sizeof(verticalValue))->submit();
        horizontalResources = device->createBindGroup<WebgpuSkinningInstancingBlurResources>(
            sceneColor->createView(), sceneLinearDepth->createView(),
            linearSampler, horizontalUniforms);
        verticalResources = device->createBindGroup<WebgpuSkinningInstancingBlurResources>(
            horizontalColor->createView(), sceneLinearDepth->createView(),
            linearSampler, verticalUniforms);
        resolveResources = device->createBindGroup<WebgpuSkinningInstancingBlurResources>(
            verticalColor->createView(), sceneLinearDepth->createView(),
            linearSampler, verticalUniforms);
        horizontalBlurPass = device->createRenderClass<WebgpuSkinningInstancingBlurPass>(
            horizontalResources);
        verticalBlurPass = device->createRenderClass<WebgpuSkinningInstancingBlurPass>(
            verticalResources);
        resolvePass = device->createRenderClass<WebgpuSkinningInstancingResolvePass>(
            resolveResources);
    }

    /** Executes the Set draw, both depth-directed blur passes, and final resolve. */
    void render() override
    {
        sceneSet->update();
        WebgpuSkinningInstancingSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneColor->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.linearDepth = sceneLinearDepth->createView();
        sceneFrame.linearDepth.loadOp = LoadOp::Clear;
        sceneFrame.linearDepth.storeOp = StoreOp::Store;
        sceneFrame.linearDepth.clearValue = {1.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebgpuSkinningInstancingLinearFrameBuffer horizontalFrame;
        horizontalFrame.color = horizontalColor->createView();
        horizontalFrame.color.loadOp = LoadOp::Clear;
        horizontalFrame.color.storeOp = StoreOp::Store;
        WebgpuSkinningInstancingLinearFrameBuffer verticalFrame;
        verticalFrame.color = verticalColor->createView();
        verticalFrame.color.loadOp = LoadOp::Clear;
        verticalFrame.color.storeOp = StoreOp::Store;
        WebgpuSkinningInstancingOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuSkinningInstancingMain", sceneFrame, mainPass())
            ->renderPass("WebgpuSkinningInstancingBlurHorizontal", horizontalFrame,
                         horizontalBlurPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuSkinningInstancingBlurVertical", verticalFrame,
                         verticalBlurPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuSkinningInstancingResolve", outputFrame,
                         resolvePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final texture for host readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the fixed capture width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the fixed capture height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases the unique Scene Set and all single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneColor);
        device->freeTexture(sceneLinearDepth);
        device->freeTexture(sceneDepth);
        device->freeTexture(horizontalColor);
        device->freeTexture(verticalColor);
        device->freeTexture(outputColor);
        device->freeBuffer(horizontalUniforms);
        device->freeBuffer(verticalUniforms);
    }
};

#endif
