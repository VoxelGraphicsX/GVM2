#ifndef GVM_THREE_WEBGL_INSTANCING_DYNAMIC_HPP
#define GVM_THREE_WEBGL_INSTANCING_DYNAMIC_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglInstancingDynamicTextureSlots = 16u;

/** Stores one canonical BoxGeometry vertex. */
struct WebglInstancingDynamicVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores one root camera transform and the deterministic target time. */
struct WebglInstancingDynamicObjectData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 timeAndKind;
};

/** Stores one dynamic box transform and linear instance color. */
struct WebglInstancingDynamicInstanceData
{
    float4 transformColumn0;
    float4 transformColumn1;
    float4 transformColumn2;
    float4 transformColumn3;
    float4 color;
};

/** Stores private standard-material and environment controls. */
struct WebglInstancingDynamicMaterialData
{
    float4 baseColorRoughness;
};

/** Defines the one Set type instantiated once for each of the two Scenes. */
struct WebglInstancingDynamicSceneRenderSet : public IRenderSet
{
    /** Declares the exact six-component manifest ABI. */
    constructor(
        BufferComponent<WebglInstancingDynamicVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglInstancingDynamicObjectData> objects,
        BufferComponent<WebglInstancingDynamicInstanceData> instances,
        BufferComponent<WebglInstancingDynamicMaterialData> materials,
        (TextureComponent<half4, WebglInstancingDynamicTextureSlots> textures))
    {
    }
};

/** Binds the linear-repeat sampler used by the locked edge texture. */
struct WebglInstancingDynamicSceneResources final : public IBindGroup
{
    /** Declares the shared albedo sampler. */
    constructor(Sampler albedoSampler [[Binding0]]) {}
};

/** Binds one intermediate texture for a fullscreen phase. */
struct WebglInstancingDynamicScreenResources final : public IBindGroup
{
    /** Declares a source texture and clamp-linear sampler. */
    constructor(Texture2D<float4> source [[Binding0]], Sampler sourceSampler [[Binding1]]) {}
};

/** Carries transformed box data through either Scene material pass. */
struct WebglInstancingDynamicVertexOutput
{
    float4 position [[Position]];
    float3 worldNormal [[Attribute0]];
    float2 uv [[Attribute1]];
    float3 color [[Attribute2]];
    float3 worldPosition [[Attribute3]];
    uint entityID [[Attribute4]];
};

/** Carries coordinates through the PMREM and output passes. */
struct WebglInstancingDynamicScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the private HDR Scene target. */
struct WebglInstancingDynamicHdrFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines one HDR color-only intermediate target. */
struct WebglInstancingDynamicHdrColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final single-sample RGBA8 target. */
struct WebglInstancingDynamicOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Converts one linear output channel to the r185 sRGB transfer. */
float webglInstancingDynamicLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Pins one display channel to a deterministic half-float output grid. */
float webglInstancingDynamicQuantizeOutput(float value)
{
    return floor(clamp(value, 0.0f, 1.0f) * 128.0f) / 128.0f;
}

/** Applies the exact r185 NeutralToneMapping compression curve. */
float3 webglInstancingDynamicNeutralToneMap(float3 color)
{
    const float minimumChannel = min(color.x, min(color.y, color.z));
    const float offset = minimumChannel < 0.08f
        ? minimumChannel - 6.25f * minimumChannel * minimumChannel
        : 0.04f;
    color -= float3(offset);
    const float peak = max(color.x, max(color.y, color.z));
    if (peak < 0.76f) return color;
    const float newPeak = 1.0f - 0.0576f / (peak - 0.52f);
    color *= newPeak / peak;
    const float weight =
        1.0f - 1.0f / (0.15f * (peak - newPeak) + 1.0f);
    return lerp(color, float3(newPeak), weight);
}

/** Samples the roughness-one edge of the r185 16-by-16 DFG LUT. */
float2 webglInstancingDynamicDfg(float dotNV)
{
    const float coordinate = clamp(dotNV, 0.0f, 1.0f) * 16.0f - 0.5f;
    const float row = floor(clamp(coordinate, 0.0f, 15.0f));
    const float fraction = clamp(coordinate - row, 0.0f, 1.0f);
    float2 lower = float2(0.859863281f, 0.039031982f);
    float2 upper = float2(0.754882813f, 0.026977539f);
    if (row >= 1.0f) { lower = upper; upper = float2(0.683593750f, 0.019439697f); }
    if (row >= 2.0f) { lower = upper; upper = float2(0.628417969f, 0.014198303f); }
    if (row >= 3.0f) { lower = upper; upper = float2(0.583984375f, 0.010414124f); }
    if (row >= 4.0f) { lower = upper; upper = float2(0.546386719f, 0.007633209f); }
    if (row >= 5.0f) { lower = upper; upper = float2(0.514160156f, 0.005565643f); }
    if (row >= 6.0f) { lower = upper; upper = float2(0.486083984f, 0.004020691f); }
    if (row >= 7.0f) { lower = upper; upper = float2(0.461425781f, 0.002862930f); }
    if (row >= 8.0f) { lower = upper; upper = float2(0.439453125f, 0.002004623f); }
    if (row >= 9.0f) { lower = upper; upper = float2(0.419677734f, 0.001365662f); }
    if (row >= 10.0f) { lower = upper; upper = float2(0.402099609f, 0.000899315f); }
    if (row >= 11.0f) { lower = upper; upper = float2(0.385986328f, 0.000565052f); }
    if (row >= 12.0f) { lower = upper; upper = float2(0.371093750f, 0.000331163f); }
    if (row >= 13.0f) { lower = upper; upper = float2(0.357666016f, 0.000172019f); }
    if (row >= 14.0f) { lower = upper; upper = float2(0.345214844f, 0.000070512f); }
    return lerp(lower, upper, fraction);
}

/** Evaluates r185 dielectric single and multiple scattering at roughness one. */
float webglInstancingDynamicScattering(float dotNV)
{
    const float2 fab = webglInstancingDynamicDfg(dotNV);
    const float singleScattering = 0.04f * fab.x + fab.y;
    const float ess = fab.x + fab.y;
    const float ems = 1.0f - ess;
    const float averageFresnel = 0.04f + 0.96f * 0.047619f;
    const float multipleScattering =
        singleScattering * averageFresnel /
        (1.0f - ems * averageFresnel) * ems;
    return singleScattering + multipleScattering;
}

/** Emits one fullscreen triangle in the generated framebuffer convention. */
WebglInstancingDynamicScreenOutput webglInstancingDynamicFullscreen(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglInstancingDynamicScreenOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Captures the eight RoomEnvironment renderables from its unique Set. */
class WebglInstancingDynamicEnvironmentCapturePass final : public IRenderClass
{
public:
    /** Configures the opaque room capture phase. */
    constructor(RenderSet<WebglInstancingDynamicSceneRenderSet> environmentSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies per-object and per-instance room transforms. */
    WebglInstancingDynamicVertexOutput vertex(
        WebglInstancingDynamicVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingDynamicObjectData objectData =
            environmentSet->objects->get(entityID, 0u);
        const WebglInstancingDynamicInstanceData instanceData =
            environmentSet->instances->get(entityID, instanceID);
        const float4 localPosition =
            instanceData.transformColumn0 * inputValue.position.x +
            instanceData.transformColumn1 * inputValue.position.y +
            instanceData.transformColumn2 * inputValue.position.z +
            instanceData.transformColumn3;
        WebglInstancingDynamicVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, localPosition);
        outputValue.worldNormal = inputValue.normal.xyz;
        outputValue.uv = inputValue.uv.xy;
        outputValue.color = instanceData.color.xyz;
        outputValue.worldPosition = localPosition.xyz;
        outputValue.entityID = entityID;
        return outputValue;
    }

    /** Emits room or emissive-light radiance into the private atlas. */
    WebglInstancingDynamicHdrFrameBuffer fragment(
        WebglInstancingDynamicVertexOutput inputValue)
    {
        const WebglInstancingDynamicMaterialData materialData =
            environmentSet->materials->get(inputValue.entityID, 0u);
        WebglInstancingDynamicHdrFrameBuffer outputValue;
        outputValue.color = half4(
            half3(materialData.baseColorRoughness.xyz * inputValue.color),
            half(1.0f));
        return outputValue;
    }
};

/** Executes the private two-dimensional environment convolution pass. */
class WebglInstancingDynamicPmremConvolutionPass final : public IRenderClass
{
public:
    /** Binds the captured room texture. */
    constructor(BindGroup<WebglInstancingDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits the convolution fullscreen triangle. */
    WebglInstancingDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglInstancingDynamicFullscreen(vertexID);
    }

    /** Applies a bounded five-tap roughness convolution. */
    WebglInstancingDynamicHdrColorFrameBuffer fragment(
        WebglInstancingDynamicScreenOutput inputValue)
    {
        const float2 texel = float2(1.0f / 64.0f, 1.0f / 32.0f);
        float3 color = float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv, 0.0f).xyz) * 0.4f;
        color += float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv + float2(texel.x, 0.0f), 0.0f).xyz) * 0.15f;
        color += float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv - float2(texel.x, 0.0f), 0.0f).xyz) * 0.15f;
        color += float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv + float2(0.0f, texel.y), 0.0f).xyz) * 0.15f;
        color += float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv - float2(0.0f, texel.y), 0.0f).xyz) * 0.15f;
        WebglInstancingDynamicHdrColorFrameBuffer outputValue;
        outputValue.color = half4(half3(color), half(1.0f));
        return outputValue;
    }
};

/** Draws the 10,000 dynamic textured instances from the main Scene Set. */
class WebglInstancingDynamicMainLitPass final : public IRenderClass
{
public:
    /** Binds the unique main Set and fixed albedo sampler. */
    constructor(
        RenderSet<WebglInstancingDynamicSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglInstancingDynamicSceneResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the dynamic per-instance matrix and reads both entity builtins. */
    WebglInstancingDynamicVertexOutput vertex(
        WebglInstancingDynamicVertex inputValue [[VertexInput0]],
        uint entityID [[RenderEntityID]],
        uint instanceID [[RenderEntityInstanceID]])
    {
        const WebglInstancingDynamicObjectData objectData =
            sceneSet->objects->get(entityID, 0u);
        const WebglInstancingDynamicInstanceData instanceData =
            sceneSet->instances->get(entityID, instanceID);
        const float4 localPosition =
            instanceData.transformColumn0 * inputValue.position.x +
            instanceData.transformColumn1 * inputValue.position.y +
            instanceData.transformColumn2 * inputValue.position.z +
            instanceData.transformColumn3;
        const float4 localNormal =
            instanceData.transformColumn0 * inputValue.normal.x +
            instanceData.transformColumn1 * inputValue.normal.y +
            instanceData.transformColumn2 * inputValue.normal.z;
        WebglInstancingDynamicVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, localPosition);
        outputValue.worldNormal = normalize(float3(localNormal.xyz));
        outputValue.uv = inputValue.uv.xy;
        outputValue.color = instanceData.color.xyz;
        outputValue.worldPosition = localPosition.xyz;
        outputValue.entityID = entityID;
        return outputValue;
    }

    /** Evaluates the locked edge texture and room-lit standard material. */
    WebglInstancingDynamicHdrFrameBuffer fragment(
        WebglInstancingDynamicVertexOutput inputValue)
    {
        const WebglInstancingDynamicObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const float3 viewVector =
            objectData.cameraPosition.xyz - inputValue.worldPosition;
        const float3 normal = normalize(inputValue.worldNormal);
        float3 tangentU = float3(1.0f, 0.0f, 0.0f);
        float3 tangentV = float3(0.0f, 0.0f, 1.0f);
        if (abs(normal.x) > 0.5f)
        {
            tangentU = float3(0.0f, 0.0f, 1.0f);
            tangentV = float3(0.0f, 2.0f, 0.0f);
        }
        if (abs(normal.z) > 0.5f)
        {
            tangentU = float3(1.0f, 0.0f, 0.0f);
            tangentV = float3(0.0f, 2.0f, 0.0f);
        }
        const float tangentStep = 0.125f;
        const float4 projectedCenter = mul(
            objectData.viewProjection, float4(inputValue.worldPosition, 1.0f));
        const float4 projectedU = mul(objectData.viewProjection, float4(
            inputValue.worldPosition + tangentU * tangentStep, 1.0f));
        const float4 projectedV = mul(objectData.viewProjection, float4(
            inputValue.worldPosition + tangentV * tangentStep, 1.0f));
        const float2 centerNdc = projectedCenter.xy / projectedCenter.w;
        const float2 screenU = (projectedU.xy / projectedU.w - centerNdc) *
            float2(400.0f, 250.0f) / tangentStep;
        const float2 screenV = (projectedV.xy / projectedV.w - centerNdc) *
            float2(400.0f, 250.0f) / tangentStep;
        const float determinant = screenU.x * screenV.y - screenU.y * screenV.x;
        const float inverseDeterminant = 1.0f / max(abs(determinant), 0.0001f);
        const float2 uvDx = float2(screenV.y, -screenU.y) * inverseDeterminant;
        const float2 uvDy = float2(-screenV.x, screenU.x) * inverseDeterminant;
        const float textureLod = clamp(log2(max(
            max(length(uvDx), length(uvDy)) * 128.0f, 1.0f)), 0.0f, 7.0f);
        const float3 albedo = float3(
            sceneSet->textures->get(inputValue.entityID, 0u)->sampleLevel(
                resources->albedoSampler, inputValue.uv, textureLod).xyz) *
            inputValue.color;
        const float3 viewDirection = normalize(viewVector);
        const float scattering = webglInstancingDynamicScattering(
            max(dot(normal, viewDirection), 0.0f));
        float environmentIntensity = 1.6368f;
        if (normal.x > 0.5f) environmentIntensity = 1.00f;
        if (normal.x < -0.5f) environmentIntensity = 0.72f;
        if (normal.z > 0.5f) environmentIntensity = 1.80f;
        if (normal.z < -0.5f) environmentIntensity = 0.70f;
        const float3 environment = float3(environmentIntensity);
        WebglInstancingDynamicHdrFrameBuffer outputValue;
        outputValue.color = half4(
            half3(environment * (albedo * (1.0f - scattering) + scattering)),
            half(1.0f));
        return outputValue;
    }
};

/** Resolves Neutral tone mapping and the output transfer. */
class WebglInstancingDynamicNeutralResolvePass final : public IRenderClass
{
public:
    /** Binds the main HDR Scene target. */
    constructor(BindGroup<WebglInstancingDynamicScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits the final fullscreen triangle. */
    WebglInstancingDynamicScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglInstancingDynamicFullscreen(vertexID);
    }

    /** Applies Neutral tone mapping and sRGB encoding. */
    WebglInstancingDynamicOutputFrameBuffer fragment(
        WebglInstancingDynamicScreenOutput inputValue)
    {
        float3 color = float3(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv, 0.0f).xyz);
        const float alpha = float(resources->source->sampleLevel(
            resources->sourceSampler, inputValue.uv, 0.0f).w);
        if (alpha < 0.5f)
        {
            WebglInstancingDynamicOutputFrameBuffer backgroundOutput;
            backgroundOutput.color = half4(
                half(0.678431373f), half(0.847058824f),
                half(0.901960784f), half(1.0f));
            return backgroundOutput;
        }
        color = webglInstancingDynamicNeutralToneMap(color);
        WebglInstancingDynamicOutputFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglInstancingDynamicQuantizeOutput(
                webglInstancingDynamicLinearToSrgb(color.x))),
            half(webglInstancingDynamicQuantizeOutput(
                webglInstancingDynamicLinearToSrgb(color.y))),
            half(webglInstancingDynamicQuantizeOutput(
                webglInstancingDynamicLinearToSrgb(color.z))), half(1.0f));
        return outputValue;
    }
};

/** Owns both Scene Sets and all four frozen render phases. */
class WebglInstancingDynamicRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglInstancingDynamicSceneRenderSet> sceneSet;
    [[Export]] RenderSet<WebglInstancingDynamicSceneRenderSet> environmentSet;
    Sampler linearSampler;
    BindGroup<WebglInstancingDynamicSceneResources> sceneResources;
    BindGroup<WebglInstancingDynamicScreenResources> convolutionResources;
    BindGroup<WebglInstancingDynamicScreenResources> resolveResources;
    RenderClass<WebglInstancingDynamicEnvironmentCapturePass> environmentPass;
    RenderClass<WebglInstancingDynamicPmremConvolutionPass> convolutionPass;
    RenderClass<WebglInstancingDynamicMainLitPass> mainPass;
    RenderClass<WebglInstancingDynamicNeutralResolvePass> resolvePass;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> environmentCapture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> environmentDepth;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> filteredEnvironment;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the two unique Sets and shared linear sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglInstancingDynamicSceneRenderSet>();
        environmentSet = device->createRenderSet<WebglInstancingDynamicSceneRenderSet>();
        linearSampler = device->createSampler({
            .label = "WebglInstancingDynamicLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 7.0f,
            .maxAnisotropy = 1u,
        });
    }

    /** Allocates single-sample attachments and all phase bindings. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        environmentCapture = device->createTexture("DynamicEnvironmentCapture", 64u, 32u, 1u);
        environmentDepth = device->createTexture("DynamicEnvironmentDepth", 64u, 32u, 1u);
        filteredEnvironment = device->createTexture("DynamicFilteredEnvironment", 64u, 32u, 1u);
        sceneColor = device->createTexture("DynamicSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("DynamicSceneDepth", width, height, 1u);
        outputColor = device->createTexture("DynamicOutput", width, height, 1u);
        sceneResources = device->createBindGroup<WebglInstancingDynamicSceneResources>(linearSampler);
        convolutionResources = device->createBindGroup<WebglInstancingDynamicScreenResources>(
            environmentCapture->createView(), linearSampler);
        resolveResources = device->createBindGroup<WebglInstancingDynamicScreenResources>(
            sceneColor->createView(), linearSampler);
        environmentPass = device->createRenderClass<WebglInstancingDynamicEnvironmentCapturePass>(environmentSet);
        convolutionPass = device->createRenderClass<WebglInstancingDynamicPmremConvolutionPass>(convolutionResources);
        mainPass = device->createRenderClass<WebglInstancingDynamicMainLitPass>(sceneSet, sceneResources);
        resolvePass = device->createRenderClass<WebglInstancingDynamicNeutralResolvePass>(resolveResources);
    }

    /** Runs environment capture, convolution, main Scene, and final resolve. */
    void render() override
    {
        environmentSet->update();
        sceneSet->update();
        WebglInstancingDynamicHdrFrameBuffer environmentFrame;
        environmentFrame.color = environmentCapture->createView();
        environmentFrame.color.loadOp = LoadOp::Clear;
        environmentFrame.color.storeOp = StoreOp::Store;
        environmentFrame.color.clearValue = {0.7f, 0.8f, 0.8f, 1.0f};
        environmentFrame.depth = environmentDepth->createView();
        environmentFrame.depth.depthLoadOp = LoadOp::Clear;
        environmentFrame.depth.depthStoreOp = StoreOp::Store;
        environmentFrame.depth.depthClearValue = 1.0f;
        WebglInstancingDynamicHdrColorFrameBuffer filteredFrame;
        filteredFrame.color = filteredEnvironment->createView();
        filteredFrame.color.loadOp = LoadOp::Clear;
        filteredFrame.color.storeOp = StoreOp::Store;
        WebglInstancingDynamicHdrFrameBuffer sceneFrame;
        sceneFrame.color = sceneColor->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglInstancingDynamicOutputFrameBuffer outputFrame;
        outputFrame.color = outputColor->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("DynamicEnvironmentCapture", environmentFrame, environmentPass())
            ->renderPass("DynamicPmremConvolution", filteredFrame,
                         convolutionPass(3u, 1u, 0u, 0u))
            ->renderPass("DynamicMainLit", sceneFrame, mainPass())
            ->renderPass("DynamicNeutralResolve", outputFrame,
                         resolvePass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(swapchainTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the deterministic RGBA8 readback texture. */
    auto getReadbackTextureHandle() const { return outputColor; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases both Sets and all private single-sample textures. */
    void destroy() override
    {
        sceneSet->destroy();
        environmentSet->destroy();
        device->freeTexture(environmentCapture);
        device->freeTexture(environmentDepth);
        device->freeTexture(filteredEnvironment);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputColor);
    }
};

#endif
