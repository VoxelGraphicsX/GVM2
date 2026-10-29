#ifndef GVM_THREE_WEBGL_POSTPROCESSING_BACKGROUNDS_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_BACKGROUNDS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one SphereGeometry position, normal, and UV. */
struct WebglPostprocessingBackgroundsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores camera matrices, pass flags, and the camera basis for CubeTexturePass. */
struct WebglPostprocessingBackgroundsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 cameraPosition;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
    float4 viewportWidthHeight;
    float4 clearColorAndAlpha;
    uint4 passFlags;
};

/** Stores the StandardMaterial parameters and postprocessing opacities. */
struct WebglPostprocessingBackgroundsMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Stores the required one-entry instance component. */
struct WebglPostprocessingBackgroundsInstanceData
{
    float4 offsetAndScale;
    float4 tint;
};

/** Owns the sphere geometry, hardwood texture, and six Pisa faces. */
struct WebglPostprocessingBackgroundsSceneRenderSet : public IRenderSet
{
    /** Declares the single Scene Set shared by every background and main pass. */
    constructor(
        BufferComponent<WebglPostprocessingBackgroundsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingBackgroundsObjectData> objects,
        BufferComponent<WebglPostprocessingBackgroundsInstanceData> instances,
        BufferComponent<WebglPostprocessingBackgroundsMaterialData> materials,
        (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Binds the one sampler used by both 2D and six-face background passes. */
struct WebglPostprocessingBackgroundsSamplerResources final : public IBindGroup
{
    /** Declares the filtered clamp-to-edge sampler. */
    constructor(Sampler backgroundSampler [[Binding0]])
    {
    }
};

/** Binds one linear intermediate for the final OutputPass conversion. */
struct WebglPostprocessingBackgroundsOutputResources final : public IBindGroup
{
    /** Declares the linear composer texture and filtered sampler. */
    constructor(
        Texture2D<half4> inputTexture [[Binding0]],
        Sampler outputSampler [[Binding1]])
    {
    }
};

/** Carries screen UV, world values, and RenderEntity identity. */
struct WebglPostprocessingBackgroundsVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Carries the fullscreen position from vertex to raster stages. */
struct WebglPostprocessingBackgroundsScreenVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear single-sample scene attachment and depth buffer. */
struct WebglPostprocessingBackgroundsLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a linear color-only attachment for background screen passes. */
struct WebglPostprocessingBackgroundsLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final display-encoded single-sample attachment. */
struct WebglPostprocessingBackgroundsOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts linear values to the output canvas sRGB domain. */
float webglPostprocessingBackgroundsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the r185 StandardMaterial GGX direct-specular lobe. */
float3 webglPostprocessingBackgroundsSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal,
    float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalLight = clamp(dot(normal, lightDirection), 0.0f, 1.0f);
    const float dotNormalView = clamp(dot(normal, viewDirection), 0.0f, 1.0f);
    const float dotNormalHalf = clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotViewHalf = clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float fresnel = exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 fresnelColor = float3(0.04f) * (1.0f - fresnel) + float3(fresnel);
    const float gv = dotNormalLight * sqrt(alphaSquared +
        (1.0f - alphaSquared) * dotNormalView * dotNormalView);
    const float gl = dotNormalView * sqrt(alphaSquared +
        (1.0f - alphaSquared) * dotNormalLight * dotNormalLight);
    const float visibility = 0.5f / max(gv + gl, 0.000001f);
    const float denominator = dotNormalHalf * dotNormalHalf *
        (alphaSquared - 1.0f) + 1.0f;
    const float distribution = 0.3183098861837907f * alphaSquared /
        max(denominator * denominator, 0.000001f);
    return fresnelColor * (visibility * distribution);
}

/** Resolves the r185 six-face Pisa cube convention. */
float3 webglPostprocessingBackgroundsFace(float3 direction)
{
    const float3 a = abs(direction);
    if (a.x > a.z)
    {
        if (a.x > a.y)
            return direction.x > 0.0f
                ? float3(3.0f, direction.z / a.x, -direction.y / a.x)
                : float3(0.0f, -direction.z / a.x, -direction.y / a.x);
        return direction.y > 0.0f
            ? float3(1.0f, -direction.x / a.y, direction.z / a.y)
            : float3(4.0f, -direction.x / a.y, -direction.z / a.y);
    }
    if (a.z > a.y)
        return direction.z > 0.0f
            ? float3(2.0f, -direction.x / a.z, -direction.y / a.z)
            : float3(5.0f, direction.x / a.z, -direction.y / a.z);
    return direction.y > 0.0f
        ? float3(1.0f, -direction.x / a.y, direction.z / a.y)
        : float3(4.0f, -direction.x / a.y, -direction.z / a.y);
}

/** Samples one of the six RenderSet-owned Pisa faces. */
float3 webglPostprocessingBackgroundsSampleCube(
    IN RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet,
    IN BindGroup<WebglPostprocessingBackgroundsSamplerResources> resources,
    float3 direction)
{
    const float3 face = webglPostprocessingBackgroundsFace(normalize(direction));
    const uint faceIndex = uint(face.x);
    const float2 uv = clamp(face.yz * 0.5f + 0.5f, float2(0.000001f), float2(0.999999f));
    // CubeTexturePass samples the uploaded face images in image-space
    // coordinates.  Pixel-space Y is opposite to the cube-face UV convention,
    // so mirror the second coordinate exactly once at the sample boundary.
    const float2 textureUv = float2(uv.x, 1.0f - uv.y);
    const uint slot = faceIndex == 0u ? 1u : faceIndex == 1u ? 4u : faceIndex == 2u ? 5u :
                      faceIndex == 3u ? 2u : faceIndex == 4u ? 3u : 6u;
    return slot == 1u
        ? float3(sceneSet->textures->get(0u, 1u)->sample(resources->backgroundSampler, textureUv).xyz)
        : slot == 2u
            ? float3(sceneSet->textures->get(0u, 2u)->sample(resources->backgroundSampler, textureUv).xyz)
            : slot == 3u
                ? float3(sceneSet->textures->get(0u, 3u)->sample(resources->backgroundSampler, textureUv).xyz)
                : slot == 4u
                    ? float3(sceneSet->textures->get(0u, 4u)->sample(resources->backgroundSampler, textureUv).xyz)
                    : slot == 5u
                        ? float3(sceneSet->textures->get(0u, 5u)->sample(resources->backgroundSampler, textureUv).xyz)
                        : float3(sceneSet->textures->get(0u, 6u)->sample(resources->backgroundSampler, textureUv).xyz);
}

/** Emits the common fullscreen triangle for both screen passes. */
WebglPostprocessingBackgroundsScreenVertexOutput webglPostprocessingBackgroundsFullscreenVertex(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglPostprocessingBackgroundsScreenVertexOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Draws the TexturePass hardwood background with the scenario opacity. */
class WebglPostprocessingBackgroundsClearPass final : public IRenderClass
{
public:
    /** Writes the replayed ClearPass color before subsequent alpha passes. */
    constructor(
        RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle. */
    WebglPostprocessingBackgroundsScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingBackgroundsFullscreenVertex(vertexID);
    }

    /** Returns the canonical clear color and alpha from the Scene component. */
    WebglPostprocessingBackgroundsLinearColorFrameBuffer fragment(
        float4 pixelPosition [[PixelCoord]])
    {
        (void)pixelPosition;
        const WebglPostprocessingBackgroundsObjectData objectData =
            sceneSet->objects->get(0u, 0u);
        WebglPostprocessingBackgroundsLinearColorFrameBuffer frameBuffer;
        // EffectComposer's half-float target is premultiplied-alpha; the
        // renderer's clear operation therefore stores clear RGB multiplied
        // by the requested clear alpha before TexturePass blending.
        frameBuffer.color = half4(
            objectData.clearColorAndAlpha.x * objectData.clearColorAndAlpha.w,
            objectData.clearColorAndAlpha.y * objectData.clearColorAndAlpha.w,
            objectData.clearColorAndAlpha.z * objectData.clearColorAndAlpha.w,
            objectData.clearColorAndAlpha.w);
        return frameBuffer;
    }
};

/** Draws the TexturePass hardwood background with the scenario opacity. */
class WebglPostprocessingBackgroundsTexturePass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and enables alpha compositing over ClearPass. */
    constructor(
        RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPostprocessingBackgroundsSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Emits a fullscreen triangle. */
    WebglPostprocessingBackgroundsScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingBackgroundsFullscreenVertex(vertexID);
    }

    /** Samples the hardwood map and applies the GUI-replayed opacity. */
    WebglPostprocessingBackgroundsLinearColorFrameBuffer fragment(float4 pixelPosition [[PixelCoord]])
    {
        const WebglPostprocessingBackgroundsObjectData objectData = sceneSet->objects->get(0u, 0u);
        if (objectData.passFlags.x == 0u) discard_fragment();
        const WebglPostprocessingBackgroundsMaterialData materialData = sceneSet->materials->get(0u, 0u);
        const float2 inputUv = float2(
            pixelPosition.x / objectData.viewportWidthHeight.x,
            1.0f - pixelPosition.y / objectData.viewportWidthHeight.y);
        const float3 color = float3(sceneSet->textures->get(0u, 0u)->sample(
            resources->backgroundSampler,
            clamp(inputUv, float2(0.0f), float2(1.0f))).xyz);
        WebglPostprocessingBackgroundsLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(color.x, color.y, color.z, materialData.parameters.z);
        return frameBuffer;
    }
};

/** Draws the CubeTexturePass Pisa environment with its replayed opacity. */
class WebglPostprocessingBackgroundsCubePass final : public IRenderClass
{
public:
    /** Binds the same Scene Set texture component and blends over the texture pass. */
    constructor(
        RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglPostprocessingBackgroundsSamplerResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
    }

private:
    /** Emits a fullscreen triangle. */
    WebglPostprocessingBackgroundsScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingBackgroundsFullscreenVertex(vertexID);
    }

    /** Reconstructs the camera ray and samples Pisa. */
    WebglPostprocessingBackgroundsLinearColorFrameBuffer fragment(float4 pixelPosition [[PixelCoord]])
    {
        const WebglPostprocessingBackgroundsObjectData objectData = sceneSet->objects->get(0u, 0u);
        if (objectData.passFlags.y == 0u) discard_fragment();
        const WebglPostprocessingBackgroundsMaterialData materialData = sceneSet->materials->get(0u, 0u);
        const float2 inputUv = float2(
            pixelPosition.x / objectData.viewportWidthHeight.x,
            1.0f - pixelPosition.y / objectData.viewportWidthHeight.y);
        const float2 ndc = clamp(inputUv, float2(0.0f), float2(1.0f)) * 2.0f - 1.0f;
        const float3 direction = normalize(
            objectData.cameraForwardAndReserved.xyz +
            objectData.cameraRightAndTanHalfFov.xyz *
                (ndc.x * objectData.cameraUpAndAspect.w * objectData.cameraRightAndTanHalfFov.w) +
            objectData.cameraUpAndAspect.xyz * (ndc.y * objectData.cameraRightAndTanHalfFov.w));
        const float3 color = webglPostprocessingBackgroundsSampleCube(sceneSet, resources, direction);
        WebglPostprocessingBackgroundsLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(color.x, color.y, color.z, materialData.parameters.w);
        return frameBuffer;
    }
};

/** Converts the linear EffectComposer result to display sRGB. */
class WebglPostprocessingBackgroundsOutputPass final : public IRenderClass
{
public:
    /** Binds the linear composer texture without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingBackgroundsOutputResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle. */
    WebglPostprocessingBackgroundsScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingBackgroundsFullscreenVertex(vertexID);
    }

    /** Applies OutputPass's linear-to-sRGB transfer to the final buffer. */
    WebglPostprocessingBackgroundsOutputFrameBuffer fragment(
        WebglPostprocessingBackgroundsScreenVertexOutput inputValue)
    {
        const float4 linearColor = float4(resources->inputTexture->sample(
            resources->outputSampler, inputValue.uv));
        WebglPostprocessingBackgroundsOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingBackgroundsLinearToSrgb(linearColor.x)),
            half(webglPostprocessingBackgroundsLinearToSrgb(linearColor.y)),
            half(webglPostprocessingBackgroundsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws the animated StandardMaterial sphere through the sole Scene RenderSet. */
class WebglPostprocessingBackgroundsMainPass final : public IRenderClass
{
public:
    /** Binds the single Set and enables opaque depth-tested drawing. */
    constructor(RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects the sphere and forwards world-space values. */
    WebglPostprocessingBackgroundsVertexOutput vertex(
        WebglPostprocessingBackgroundsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingBackgroundsObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingBackgroundsInstanceData instanceData = sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position + instanceData.offsetAndScale;
        const float4 clip = mul(objectData.modelViewProjection, localPosition);
        WebglPostprocessingBackgroundsVertexOutput outputValue;
        outputValue.position = clip;
        outputValue.position.y = -clip.y;
        outputValue.position.z = (clip.z + clip.w) * 0.5f;
        outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
        outputValue.worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.uv = inputValue.uv.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies a compact StandardMaterial-compatible direct-light response. */
    WebglPostprocessingBackgroundsLinearFrameBuffer fragment(WebglPostprocessingBackgroundsVertexOutput inputValue)
    {
        const WebglPostprocessingBackgroundsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglPostprocessingBackgroundsMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.passFlags.z == 0u) discard_fragment();
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 viewDirection = normalize(objectData.cameraPosition.xyz - inputValue.worldPosition);
        const float3 positions[3u] = {
            float3(-10.0f, -10.0f, 10.0f), float3(-10.0f, 10.0f, 10.0f), float3(10.0f, -10.0f, 10.0f)};
        const float3 colors[3u] = {
            float3(0.8631573f, 1.0f, 0.8631573f),
            float3(1.0f, 0.8631573f, 0.8631573f),
            float3(0.8631573f, 0.8631573f, 1.0f)};
        float3 linearColor = float3(0.0f);
        for (uint index = 0u; index < 3u; ++index)
        {
            const float3 toLight = positions[index] - inputValue.worldPosition;
            const float3 lightDirection = normalize(toLight);
            const float dotNormalLight = max(dot(normal, lightDirection), 0.0f);
            const float attenuation = 500.0f /
                max(dot(toLight, toLight), 0.000001f);
            const float3 irradiance = colors[index] * attenuation * dotNormalLight;
            linearColor += irradiance * materialData.baseColor.xyz *
                0.3183098861837907f;
            linearColor += irradiance *
                webglPostprocessingBackgroundsSpecular(
                    lightDirection,
                    viewDirection,
                    normal,
                    materialData.parameters.x);
        }
        linearColor = saturate(linearColor);
        WebglPostprocessingBackgroundsLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(linearColor.x, linearColor.y, linearColor.z, 1.0f);
        return frameBuffer;
    }
};

/** Owns the unique RenderSet and the three screen/scene passes. */
class WebglPostprocessingBackgroundsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingBackgroundsSceneRenderSet> sceneSet;
    Sampler backgroundSampler;
    BindGroup<WebglPostprocessingBackgroundsSamplerResources> resources;
    RenderClass<WebglPostprocessingBackgroundsTexturePass> texturePass;
    RenderClass<WebglPostprocessingBackgroundsClearPass> clearPass;
    RenderClass<WebglPostprocessingBackgroundsCubePass> cubePass;
    RenderClass<WebglPostprocessingBackgroundsMainPass> scenePass;
    RenderClass<WebglPostprocessingBackgroundsOutputPass> outputPass;
    Texture<TextureFormat::RGBA16Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> linearColor;
    BindGroup<WebglPostprocessingBackgroundsOutputResources> outputResources;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the RenderSet, sampler, and dedicated passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglPostprocessingBackgroundsSceneRenderSet>();
        backgroundSampler = device->createSampler({
            .label = "WebglPostprocessingBackgroundsSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        resources = device->createBindGroup<WebglPostprocessingBackgroundsSamplerResources>(backgroundSampler);
        clearPass = device->createRenderClass<WebglPostprocessingBackgroundsClearPass>(sceneSet);
        texturePass = device->createRenderClass<WebglPostprocessingBackgroundsTexturePass>(sceneSet, resources);
        cubePass = device->createRenderClass<WebglPostprocessingBackgroundsCubePass>(sceneSet, resources);
        scenePass = device->createRenderClass<WebglPostprocessingBackgroundsMainPass>(sceneSet);
    }

    /** Allocates the explicit single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        linearColor = device->createTexture("WebglPostprocessingBackgroundsLinear", width, height, 1u);
        outputColor = device->createTexture("WebglPostprocessingBackgroundsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglPostprocessingBackgroundsDepth", width, height, 1u);
        outputResources = device->createBindGroup<WebglPostprocessingBackgroundsOutputResources>(
            linearColor->createView(), backgroundSampler);
        outputPass = device->createRenderClass<WebglPostprocessingBackgroundsOutputPass>(outputResources);
    }

    /** Runs TexturePass, CubeTexturePass, and RenderPass in order. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglPostprocessingBackgroundsLinearColorFrameBuffer clearFrame;
        clearFrame.color = linearColor->createView();
        clearFrame.color.loadOp = LoadOp::Clear;
        clearFrame.color.storeOp = StoreOp::Store;
        clearFrame.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglPostprocessingBackgroundsLinearColorFrameBuffer textureFrame = clearFrame;
        textureFrame.color.loadOp = LoadOp::Load;
        WebglPostprocessingBackgroundsLinearColorFrameBuffer cubeFrame = textureFrame;
        WebglPostprocessingBackgroundsLinearFrameBuffer sceneFrame;
        sceneFrame.color = linearColor->createView();
        sceneFrame.color.loadOp = LoadOp::Load;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.depth = outputDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingBackgroundsOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        graphicsQueue
            // These are fullscreen passes.  They still bind the Scene RenderSet for
            // object/material/texture components, but must use the explicit
            // three-vertex draw rather than the RenderSet indexed-indirect draw.
            ->renderPass("WebglPostprocessingBackgroundsClear", clearFrame, clearPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPostprocessingBackgroundsTexture", textureFrame, texturePass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPostprocessingBackgroundsCube", cubeFrame, cubePass(3u, 1u, 0u, 0u))
            ->renderPass("WebglPostprocessingBackgroundsScene", sceneFrame, scenePass())
            ->renderPass("WebglPostprocessingBackgroundsOutput", outputFrame, outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 output texture. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(linearColor);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglPostprocessingBackgroundsRenderer
#undef WebglPostprocessingBackgroundsOutputPass
#undef WebglPostprocessingBackgroundsOutputResources
#undef WebglPostprocessingBackgroundsOutputFrameBuffer
#undef WebglPostprocessingBackgroundsLinearColorFrameBuffer
#undef WebglPostprocessingBackgroundsLinearFrameBuffer
#undef WebglPostprocessingBackgroundsMainPass
#undef WebglPostprocessingBackgroundsCubePass
#undef WebglPostprocessingBackgroundsTexturePass
#undef WebglPostprocessingBackgroundsSceneRenderSet

#endif
