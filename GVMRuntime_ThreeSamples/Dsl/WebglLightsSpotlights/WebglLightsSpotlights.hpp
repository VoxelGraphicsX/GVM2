#ifndef GVM_THREE_WEBGL_LIGHTS_SPOTLIGHTS_HPP
#define GVM_THREE_WEBGL_LIGHTS_SPOTLIGHTS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores a position, normal, and helper-line color in the unified scene vertex buffer. */
struct WebglLightsSpotlightsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
};

/** Stores one object transform and its light/helper classification. */
struct WebglLightsSpotlightsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 model;
    float4 positionAndKind;
    float4 light0PositionAngle;
    float4 light1PositionAngle;
    float4 light2PositionAngle;
    float4 lightPenumbra;
};

/** Provides the required one-entry instance component for every entity. */
struct WebglLightsSpotlightsInstanceData
{
    float4 reserved;
};

/** Stores the material tint, roughness, and render phase. */
struct WebglLightsSpotlightsMaterialData
{
    float4 baseColor;
    float4 parameters;
};

/** Defines the single RenderSet shared by shadow, opaque, and helper passes. */
struct WebglLightsSpotlightsSceneRenderSet : public IRenderSet
{
    /** Declares the consolidated geometry and per-entity component banks. */
    constructor(
        BufferComponent<WebglLightsSpotlightsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLightsSpotlightsObjectData> objects,
        BufferComponent<WebglLightsSpotlightsInstanceData> instances,
        BufferComponent<WebglLightsSpotlightsMaterialData> materials)
    {
    }
};

/** Carries the transformed normal, world position, and RenderEntity identity. */
struct WebglLightsSpotlightsVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float4 color [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the explicit single-sample color/depth target. */
struct WebglLightsSpotlightsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear channel to the locked Three.js sRGB output transfer. */
float webglLightsSpotlightsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates one Three.js-compatible soft-edged spotlight irradiance. */
float3 webglLightsSpotlightsContribution(
    float3 worldPosition,
    float3 normal,
    float3 lightPosition,
    float3 lightDirection,
    float3 lightColor,
    float3 baseColor,
    float outerCosine,
    float innerCosine)
{
    const float3 toLight = lightPosition - worldPosition;
    const float distanceToLight = max(length(toLight), 0.0001f);
    const float3 lightVector = toLight / distanceToLight;
    // Three.js SpotLight points from its position toward its target.  The
    // deterministic fixture keeps the target at the scene origin, so the
    // direction is supplied as -lightPosition rather than +lightPosition.
    // `lightVector` points from the shaded point to the light, whereas the
    // SpotLight direction points from the light toward its target.
    const float angleCosine = dot(-lightVector, normalize(lightDirection));
    const float cone = smoothstep(outerCosine, innerCosine, angleCosine);
    const float cutoff = saturate(1.0f - pow(distanceToLight / 50.0f, 4.0f));
    const float attenuation = (1.0f / max(distanceToLight * distanceToLight, 0.01f)) * cutoff * cutoff;
    const float dotNormalLight = max(dot(normal, lightVector), 0.0f);
    const float3 viewDirection = normalize(
        float3(4.6f, 2.2f, -2.1f) - worldPosition);
    const float3 halfDirection = normalize(lightVector + viewDirection);
    const float dotNormalHalf = saturate(dot(normal, halfDirection));
    const float dotViewHalf = saturate(dot(viewDirection, halfDirection));
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 fresnelTerm =
        float3(0.0056053917f) * (1.0f - fresnel) + float3(fresnel);
    const float distribution =
        0.31830988618f * 16.0f * pow(dotNormalHalf, 30.0f);
    const float3 diffuse = baseColor * dotNormalLight * 0.31830988618f;
    const float3 specular = fresnelTerm * (0.25f * distribution);
    return lightColor * (10.0f * cone * attenuation) * (diffuse + specular);
}

/** Returns a hard analytic occlusion factor for one projected floor sample. */
float webglLightsSpotlightsHardBoxShadow(float3 floorPosition, float3 lightPosition)
{
    const float3 boxMin = float3(-0.15f, 0.45f, -0.10f);
    const float3 boxMax = float3(0.15f, 0.55f, 0.10f);
    const float3 ray = floorPosition - lightPosition;
    // The shadow map tests the complete cuboid, not only its bottom face.
    // A slab intersection keeps side-face silhouettes correct when a tweened
    // spotlight becomes low or oblique to the box.
    const float3 safeRay = float3(
        abs(ray.x) < 0.000001f ? (ray.x < 0.0f ? -0.000001f : 0.000001f) : ray.x,
        abs(ray.y) < 0.000001f ? (ray.y < 0.0f ? -0.000001f : 0.000001f) : ray.y,
        abs(ray.z) < 0.000001f ? (ray.z < 0.0f ? -0.000001f : 0.000001f) : ray.z);
    const float tx0 = (boxMin.x - lightPosition.x) / safeRay.x;
    const float tx1 = (boxMax.x - lightPosition.x) / safeRay.x;
    const float ty0 = (boxMin.y - lightPosition.y) / safeRay.y;
    const float ty1 = (boxMax.y - lightPosition.y) / safeRay.y;
    const float tz0 = (boxMin.z - lightPosition.z) / safeRay.z;
    const float tz1 = (boxMax.z - lightPosition.z) / safeRay.z;
    const float entry = max(max(min(tx0, tx1), min(ty0, ty1)), min(tz0, tz1));
    const float exit = min(min(max(tx0, tx1), max(ty0, ty1)), max(tz0, tz1));
    return (entry <= exit && exit >= 0.0f && entry <= 1.0f) ? 1.0f : 0.0f;
}

/** Approximates Three.js PCFShadowMap with the fixed five-tap Vogel pattern. */
float webglLightsSpotlightsBoxShadow(
    float3 floorPosition,
    float3 lightPosition,
    float lightAngle)
{
    const float3 forward = normalize(-lightPosition);
    const float3 upReference = abs(dot(forward, float3(0.0f, 1.0f, 0.0f))) > 0.98f
        ? float3(1.0f, 0.0f, 0.0f)
        : float3(0.0f, 1.0f, 0.0f);
    const float3 right = normalize(cross(upReference, forward));
    const float3 up = normalize(cross(forward, right));
    const float3 receiver = floorPosition - lightPosition;
    const float receiverDepth = dot(receiver, forward);
    if (receiverDepth <= 0.0f) return 0.0f;
    const float tangent = tan(lightAngle);
    if (tangent <= 0.0001f) return 0.0f;
    const float2 receiverUv = float2(
        dot(receiver, right) / (receiverDepth * tangent) * 0.5f + 0.5f,
        dot(receiver, up) / (receiverDepth * tangent) * 0.5f + 0.5f);
    const float goldenAngle = 2.399963229728653f;
    float shadow = 0.0f;
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        const float sampleRadius = sqrt((float(sampleIndex) + 0.5f) / 5.0f) / 512.0f;
        const float sampleAngle = float(sampleIndex) * goldenAngle;
        const float2 sampleUv = receiverUv + float2(
            cos(sampleAngle), sin(sampleAngle)) * sampleRadius;
        if (sampleUv.x < 0.0f || sampleUv.x > 1.0f ||
            sampleUv.y < 0.0f || sampleUv.y > 1.0f)
            continue;
        const float2 sampleNdc = sampleUv * 2.0f - 1.0f;
        const float3 sampleRay = normalize(
            forward + right * (sampleNdc.x * tangent) +
            up * (sampleNdc.y * tangent));
        if (abs(sampleRay.y) < 0.0001f) continue;
        const float floorDistance = (-0.05f - lightPosition.y) / sampleRay.y;
        if (floorDistance <= 0.0f) continue;
        const float3 sampleFloor = lightPosition + sampleRay * floorDistance;
        shadow += webglLightsSpotlightsHardBoxShadow(sampleFloor, lightPosition);
    }
    return shadow * 0.2f;
}

/** Resolves object and instance data through the mandatory RenderEntity builtins. */
WebglLightsSpotlightsVertexOutput webglLightsSpotlightsVertex(
    IN RenderSet<WebglLightsSpotlightsSceneRenderSet> sceneSet,
    WebglLightsSpotlightsVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglLightsSpotlightsObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglLightsSpotlightsInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition = inputValue.position + instanceData.reserved;
    WebglLightsSpotlightsVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection, localPosition);
    outputValue.position.y = -outputValue.position.y;
    outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
    outputValue.worldPosition = float3(mul(objectData.model, localPosition).xyz);
    outputValue.worldNormal = normalize(float3(mul(objectData.model, float4(inputValue.normal.xyz, 0.0f)).xyz));
    outputValue.color = inputValue.color;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Emits the shadow/depth prepass for the same RenderSet geometry. */
class WebglLightsSpotlightsShadowDepthPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet for the depth-only phase. */
    constructor(RenderSet<WebglLightsSpotlightsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects every entity using RenderEntity and discards line helpers. */
    WebglLightsSpotlightsVertexOutput vertex(
        WebglLightsSpotlightsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsSpotlightsVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Keeps the depth prepass color neutral while preserving depth writes. */
    WebglLightsSpotlightsFrameBuffer fragment(WebglLightsSpotlightsVertexOutput inputValue)
    {
        const WebglLightsSpotlightsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w > 1.5f) discard_fragment();
        WebglLightsSpotlightsFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(0.0f), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the floor and box with three animated spotlights. */
class WebglLightsSpotlightsOpaqueLitPass final : public IRenderClass
{
public:
    /** Binds the same Scene RenderSet and enables opaque depth-tested lighting. */
    constructor(RenderSet<WebglLightsSpotlightsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Projects floor and box vertices and resolves object identity. */
    WebglLightsSpotlightsVertexOutput vertex(
        WebglLightsSpotlightsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsSpotlightsVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Applies the three original spotlights, ambient light, and material tint. */
    WebglLightsSpotlightsFrameBuffer fragment(WebglLightsSpotlightsVertexOutput inputValue)
    {
        const WebglLightsSpotlightsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglLightsSpotlightsMaterialData materialData = sceneSet->materials->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w > 1.5f) discard_fragment();
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 light0Position = objectData.light0PositionAngle.xyz;
        const float3 light1Position = objectData.light1PositionAngle.xyz;
        const float3 light2Position = objectData.light2PositionAngle.xyz;
        const float coneCosine0 = cos(objectData.light0PositionAngle.w);
        const float coneCosine1 = cos(objectData.light1PositionAngle.w);
        const float coneCosine2 = cos(objectData.light2PositionAngle.w);
        const float penumbraCosine0 = cos(
            objectData.light0PositionAngle.w * (1.0f - objectData.lightPenumbra.x));
        const float penumbraCosine1 = cos(
            objectData.light1PositionAngle.w * (1.0f - objectData.lightPenumbra.y));
        const float penumbraCosine2 = cos(
            objectData.light2PositionAngle.w * (1.0f - objectData.lightPenumbra.z));
        const float3 light0 = webglLightsSpotlightsContribution(
            inputValue.worldPosition, normal, light0Position,
            normalize(-light0Position), float3(1.0f, 0.21223076f, 0.0f),
            materialData.baseColor.xyz,
            coneCosine0, penumbraCosine0);
        const float3 light1 = webglLightsSpotlightsContribution(
            inputValue.worldPosition, normal, light1Position,
            normalize(-light1Position), float3(0.0f, 1.0f, 0.21223076f),
            materialData.baseColor.xyz,
            coneCosine1, penumbraCosine1);
        const float3 light2 = webglLightsSpotlightsContribution(
            inputValue.worldPosition, normal, light2Position,
            normalize(-light2Position), float3(0.21223076f, 0.0f, 1.0f),
            materialData.baseColor.xyz,
            coneCosine2, penumbraCosine2);
        const float3 ambient = float3(0.05780543f) * materialData.baseColor.xyz;
        const bool isFloor = inputValue.worldPosition.y < 0.1f;
        const float shadow0 = isFloor ? webglLightsSpotlightsBoxShadow(
            inputValue.worldPosition, light0Position,
            objectData.light0PositionAngle.w) : 0.0f;
        const float shadow1 = isFloor ? webglLightsSpotlightsBoxShadow(
            inputValue.worldPosition, light1Position,
            objectData.light1PositionAngle.w) : 0.0f;
        const float shadow2 = isFloor ? webglLightsSpotlightsBoxShadow(
            inputValue.worldPosition, light2Position,
            objectData.light2PositionAngle.w) : 0.0f;
        const float3 lit = ambient * 0.31830988618f +
            light0 * (1.0f - shadow0) + light1 * (1.0f - shadow1) + light2 * (1.0f - shadow2);
        WebglLightsSpotlightsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglLightsSpotlightsLinearToSrgb(lit.x)),
            half(webglLightsSpotlightsLinearToSrgb(lit.y)),
            half(webglLightsSpotlightsLinearToSrgb(lit.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the three SpotLightHelper cones expanded to triangle-list line geometry. */
class WebglLightsSpotlightsHelperLinesPass final : public IRenderClass
{
public:
    /** Binds the same Set and enables transparent additive helper lines. */
    constructor(RenderSet<WebglLightsSpotlightsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::LessEqual);
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
    /** Projects helper triangle-list vertices through the same entity path. */
    WebglLightsSpotlightsVertexOutput vertex(
        WebglLightsSpotlightsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglLightsSpotlightsVertex(sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Emits only helper entities and preserves their authored vertex colors. */
    WebglLightsSpotlightsFrameBuffer fragment(WebglLightsSpotlightsVertexOutput inputValue)
    {
        const WebglLightsSpotlightsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.positionAndKind.w < 1.5f) discard_fragment();
        WebglLightsSpotlightsFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglLightsSpotlightsLinearToSrgb(inputValue.color.x)),
            half(webglLightsSpotlightsLinearToSrgb(inputValue.color.y)),
            half(webglLightsSpotlightsLinearToSrgb(inputValue.color.z)), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene RenderSet and all three geometry passes for the spotlight example. */
class WebglLightsSpotlightsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLightsSpotlightsSceneRenderSet> sceneSet;
    RenderClass<WebglLightsSpotlightsShadowDepthPass> shadowDepthPass;
    RenderClass<WebglLightsSpotlightsOpaqueLitPass> opaqueLitPass;
    RenderClass<WebglLightsSpotlightsHelperLinesPass> helperLinesPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique RenderSet and its generated Scene RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLightsSpotlightsSceneRenderSet>();
        shadowDepthPass = device->createRenderClass<WebglLightsSpotlightsShadowDepthPass>(sceneSet);
        opaqueLitPass = device->createRenderClass<WebglLightsSpotlightsOpaqueLitPass>(sceneSet);
        helperLinesPass = device->createRenderClass<WebglLightsSpotlightsHelperLinesPass>(sceneSet);
    }

    /** Allocates explicit single-sample capture targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLightsSpotlightsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglLightsSpotlightsDepth", width, height, 1u);
    }

    /** Submits all Scene passes through RenderSet indexed-indirect draws. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglLightsSpotlightsFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglLightsSpotlightsFrameBuffer sceneFrameBuffer = frameBuffer;
        sceneFrameBuffer.color.loadOp = LoadOp::Load;
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        graphicsQueue
            ->renderPass("WebglLightsSpotlightsShadowDepth", frameBuffer, shadowDepthPass())
            ->renderPass("WebglLightsSpotlightsOpaqueLit", sceneFrameBuffer, opaqueLitPass())
            ->renderPass("WebglLightsSpotlightsHelperLines", sceneFrameBuffer, helperLinesPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned capture texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const { return outputColor; }

    /** Returns the fixed capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the fixed capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases all single-sample attachments and the Scene RenderSet. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
