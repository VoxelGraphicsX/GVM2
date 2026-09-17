#ifndef GVM_THREE_PHASE1_MORPH_TARGETS_RENDER_SET_HPP
#define GVM_THREE_PHASE1_MORPH_TARGETS_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one expanded BoxGeometry position in the Scene vertex component. */
struct MorphTargetsVertex
{
    float4 position [[Attribute0]];
};

/** Stores two absolute morph positions and the remaining corners of one source triangle. */
struct MorphTargetsTriangleData
{
    float4 spherePosition;
    float4 twistPosition;
    float4 corner1Position;
    float4 corner1SpherePosition;
    float4 corner1TwistPosition;
    float4 corner2Position;
    float4 corner2SpherePosition;
    float4 corner2TwistPosition;
};

/** Stores camera transforms, morph weights, viewport dimensions, and r185 lighting. */
struct MorphTargetsObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 morphWeightsAndViewport;
    float4 ambientColorAndPointIntensity;
};

/** Stores the mandatory single non-instanced component record. */
struct MorphTargetsInstanceData
{
    float4 translation;
};

/** Stores the private MeshPhong base color, specular color, and shininess. */
struct MorphTargetsMaterialData
{
    float4 baseColor;
    float4 specularAndShininess;
};

#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
/** Defines the unique Scene RenderSet used only by webgl_morphtargets. */
struct WebglMorphtargetsSceneRenderSet : public IRenderSet
{
    /** Declares exact expanded geometry and all per-entity morph and material data. */
    constructor(BufferComponent<MorphTargetsVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<MorphTargetsObjectData> objects,
                BufferComponent<MorphTargetsInstanceData> instances,
                BufferComponent<MorphTargetsMaterialData> materials,
                BufferComponent<MorphTargetsTriangleData> morphTargets)
    {
    }
};

#elif defined(GVM_THREE_WEBGPU_MORPH_TARGETS)
/** Defines the unique Scene RenderSet used only by webgpu_morphtargets. */
struct WebgpuMorphtargetsSceneRenderSet : public IRenderSet
{
    /** Declares exact expanded geometry and all per-entity morph and material data. */
    constructor(BufferComponent<MorphTargetsVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<MorphTargetsObjectData> objects,
                BufferComponent<MorphTargetsInstanceData> instances,
                BufferComponent<MorphTargetsMaterialData> materials,
                BufferComponent<MorphTargetsTriangleData> morphTargets)
    {
    }
};
#else
#error "Select exactly one dedicated morph target example."
#endif

/** Stores the fixed pixel-center raster offset. */
struct MorphTargetsSampleData
{
    float4 offsetAndReserved;
};

/** Binds one immutable sample offset to a Scene pass invocation. */
struct MorphTargetsSampleResources final : public IBindGroup
{
    /** Declares the current single-sample offset. */
    constructor(UniformBuffer<MorphTargetsSampleData> sample [[Binding0]])
    {
    }
};

/** Carries view-space position, dynamic flat normal, and entity identity. */
struct MorphTargetsVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the single-sample Scene color and depth attachments. */
struct MorphTargetsSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's output transfer constants. */
float morphTargetsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Blends one absolute pair of Three morph targets with the base position. */
float3 morphTargetsBlendPosition(float3 basePosition,
                                 float3 spherePosition,
                                 float3 twistPosition,
                                 float2 weights)
{
    return basePosition * (1.0f - weights.x - weights.y) +
           spherePosition * weights.x + twistPosition * weights.y;
}

/** Evaluates Three's normalized Blinn-Phong direct specular BRDF. */
float3 morphTargetsBlinnPhongSpecular(float3 specularColor,
                                     float shininess,
                                     float dotNormalHalf,
                                     float dotViewHalf)
{
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 schlick = specularColor * (1.0f - fresnel) + float3(fresnel);
    const float normalization = (shininess + 2.0f) * 0.15915494309189535f;
    return schlick * (normalization * pow(max(dotNormalHalf, 0.0f), shininess) * 0.25f);
}

/** Evaluates the private r185 MeshPhong material for one flat-shaded fragment. */
float3 morphTargetsShade(float3 viewPosition,
                         float3 viewNormal,
                         MorphTargetsObjectData objectData,
                         MorphTargetsMaterialData materialData)
{
    const float3 normal = normalize(viewNormal);
    const float3 pointVector = -viewPosition;
    const float distanceSquared = max(dot(pointVector, pointVector), 0.01f);
    const float3 lightDirection = pointVector * rsqrt(distanceSquared);
    const float dotNormalLight = max(dot(normal, lightDirection), 0.0f);
    const float3 viewDirection = normalize(-viewPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf = max(dot(normal, halfDirection), 0.0f);
    const float dotViewHalf = max(dot(viewDirection, halfDirection), 0.0f);
    const float pointIrradiance =
        objectData.ambientColorAndPointIntensity.w / distanceSquared;
    const float3 ambient = materialData.baseColor.xyz *
                           objectData.ambientColorAndPointIntensity.xyz *
                           0.3183098861837907f;
    const float3 diffuse = materialData.baseColor.xyz *
                           (pointIrradiance * dotNormalLight * 0.3183098861837907f);
    const float3 specular = morphTargetsBlinnPhongSpecular(
        materialData.specularAndShininess.xyz,
        materialData.specularAndShininess.w,
        dotNormalHalf,
        dotViewHalf) * (pointIrradiance * dotNormalLight);
    return ambient + diffuse + specular;
}

/** Builds the common morphed vertex result used by both dedicated Scene passes. */
MorphTargetsVertexOutput morphTargetsBuildVertex(
    MorphTargetsVertex inputValue,
    MorphTargetsTriangleData triangleData,
    MorphTargetsObjectData objectData,
    MorphTargetsInstanceData instanceData,
    float2 sampleOffset,
    uint renderEntityID)
{
    const float2 weights = objectData.morphWeightsAndViewport.xy;
    const float3 p0 = morphTargetsBlendPosition(
        inputValue.position.xyz,
        triangleData.spherePosition.xyz,
        triangleData.twistPosition.xyz,
        weights) + instanceData.translation.xyz;
    const float3 p1 = morphTargetsBlendPosition(
        triangleData.corner1Position.xyz,
        triangleData.corner1SpherePosition.xyz,
        triangleData.corner1TwistPosition.xyz,
        weights) + instanceData.translation.xyz;
    const float3 p2 = morphTargetsBlendPosition(
        triangleData.corner2Position.xyz,
        triangleData.corner2SpherePosition.xyz,
        triangleData.corner2TwistPosition.xyz,
        weights) + instanceData.translation.xyz;
    const float4 viewP0 = mul(objectData.modelView, float4(p0, 1.0f));
    const float4 viewP1 = mul(objectData.modelView, float4(p1, 1.0f));
    const float4 viewP2 = mul(objectData.modelView, float4(p2, 1.0f));
    float4 clipPosition = mul(objectData.modelViewProjection, float4(p0, 1.0f));
    clipPosition.xy += sampleOffset * 2.0f /
                       objectData.morphWeightsAndViewport.zw * clipPosition.w;

    MorphTargetsVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewP0.xyz;
    outputValue.viewNormal = -normalize(cross(viewP1.xyz - viewP0.xyz,
                                              viewP2.xyz - viewP0.xyz));
    outputValue.entityID = renderEntityID;
    return outputValue;
}

#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
/** Draws the webgl_morphtargets Scene through its only RenderSet. */
class WebglMorphtargetsMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and the fixed sample offset. */
    constructor(RenderSet<WebglMorphtargetsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<MorphTargetsSampleResources> sampleResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Blends both targets and derives the deformed triangle normal. */
    MorphTargetsVertexOutput vertex(MorphTargetsVertex inputValue [[VertexInput0]],
                                    uint vertexID [[VertexID]],
                                    uint renderEntityID [[RenderEntityID]],
                                    uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const uint localVertexID = vertexID;
        return morphTargetsBuildVertex(
            inputValue,
            sceneSet->morphTargets->get(renderEntityID, localVertexID),
            sceneSet->objects->get(renderEntityID, 0u),
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID),
            sampleResources->sample->offsetAndReserved.xy,
            renderEntityID);
    }

    /** Evaluates the exact private Phong lighting and output transfer. */
    MorphTargetsSceneFrameBuffer fragment(MorphTargetsVertexOutput inputValue)
    {
        const float3 linearColor = morphTargetsShade(
            inputValue.viewPosition,
            inputValue.viewNormal,
            sceneSet->objects->get(inputValue.entityID, 0u),
            sceneSet->materials->get(inputValue.entityID, 0u));
        MorphTargetsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(morphTargetsLinearToSrgb(linearColor.x),
                  morphTargetsLinearToSrgb(linearColor.y),
                  morphTargetsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};
#endif

#if defined(GVM_THREE_WEBGPU_MORPH_TARGETS)
/** Draws the webgpu_morphtargets Scene through its only RenderSet. */
class WebgpuMorphtargetsMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and the fixed sample offset. */
    constructor(RenderSet<WebgpuMorphtargetsSceneRenderSet> sceneSet [[Slot0]],
                BindGroup<MorphTargetsSampleResources> sampleResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Blends both targets and derives the deformed triangle normal. */
    MorphTargetsVertexOutput vertex(MorphTargetsVertex inputValue [[VertexInput0]],
                                    uint vertexID [[VertexID]],
                                    uint renderEntityID [[RenderEntityID]],
                                    uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const uint localVertexID = vertexID;
        return morphTargetsBuildVertex(
            inputValue,
            sceneSet->morphTargets->get(renderEntityID, localVertexID),
            sceneSet->objects->get(renderEntityID, 0u),
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID),
            sampleResources->sample->offsetAndReserved.xy,
            renderEntityID);
    }

    /** Evaluates the exact private Phong lighting and output transfer. */
    MorphTargetsSceneFrameBuffer fragment(MorphTargetsVertexOutput inputValue)
    {
        const float3 linearColor = morphTargetsShade(
            inputValue.viewPosition,
            inputValue.viewNormal,
            sceneSet->objects->get(inputValue.entityID, 0u),
            sceneSet->materials->get(inputValue.entityID, 0u));
        MorphTargetsSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(morphTargetsLinearToSrgb(linearColor.x),
                  morphTargetsLinearToSrgb(linearColor.y),
                  morphTargetsLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};
#endif

#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
/** Owns the dedicated webgl_morphtargets RenderSet and single-sample output. */
class WebglMorphtargetsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMorphtargetsSceneRenderSet> webglSceneSet;
    Buffer<MorphTargetsSampleData, BufferUsage<Uniform, CopyDst>> sampleBuffer0;
    BindGroup<MorphTargetsSampleResources> sampleResources0;
    RenderClass<WebglMorphtargetsMainPass> scenePass0;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates only the webgl Scene Set and fixed sample-offset resource. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        webglSceneSet = device->createRenderSet<WebglMorphtargetsSceneRenderSet>();
        sampleBuffer0 = device->createBuffer("WebglMorphSample0", 1u);
        const MorphTargetsSampleData sample0 = {float4(0.0f, 0.0f, 0.0f, 0.0f)};
        graphicsQueue->writeBuffer(BufferRange(sampleBuffer0), &sample0, sizeof(sample0))->submit();
        sampleResources0 = device->createBindGroup<MorphTargetsSampleResources>(sampleBuffer0);
        scenePass0 = device->createRenderClass<WebglMorphtargetsMainPass>(webglSceneSet, sampleResources0);
    }

    /** Allocates the single-sample Scene color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture("WebglMorphSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("WebglMorphSceneDepth", width, height, 1u);
    }

    /** Updates and draws the unique Scene Set into the single-sample target. */
    void render() override
    {
        webglSceneSet->update();
        MorphTargetsSceneFrameBuffer frame0;
        configureSceneFrame(frame0, sceneColor, sceneDepth);
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebglMorphtargetsSample0", frame0, scenePass0())
            ->renderToSwapchain(swapchainTexture, sceneColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 target. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return sceneColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the webgl Scene Set and every private attachment. */
    void destroy() override
    {
        webglSceneSet->destroy();
        device->freeBuffer(sampleBuffer0);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }

private:
    /** Configures the independently cleared single-sample Scene target. */
    void configureSceneFrame(MorphTargetsSceneFrameBuffer &frame,
                             Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> color,
                             Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depth)
    {
        frame.color = color->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue = {0.5607843137, 0.7372549020, 0.8313725490, 1.0};
        frame.depth = depth->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
    }
};
#endif

#if defined(GVM_THREE_WEBGPU_MORPH_TARGETS)
/** Owns the dedicated webgpu_morphtargets RenderSet and single-sample output. */
class WebgpuMorphtargetsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuMorphtargetsSceneRenderSet> webgpuSceneSet;
    Buffer<MorphTargetsSampleData, BufferUsage<Uniform, CopyDst>> sampleBuffer0;
    BindGroup<MorphTargetsSampleResources> sampleResources0;
    RenderClass<WebgpuMorphtargetsMainPass> scenePass0;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates only the webgpu Scene Set and fixed sample-offset resource. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        webgpuSceneSet = device->createRenderSet<WebgpuMorphtargetsSceneRenderSet>();
        sampleBuffer0 = device->createBuffer("WebgpuMorphSample0", 1u);
        const MorphTargetsSampleData sample0 = {float4(0.0f, 0.0f, 0.0f, 0.0f)};
        graphicsQueue->writeBuffer(BufferRange(sampleBuffer0), &sample0, sizeof(sample0))->submit();
        sampleResources0 = device->createBindGroup<MorphTargetsSampleResources>(sampleBuffer0);
        scenePass0 = device->createRenderClass<WebgpuMorphtargetsMainPass>(webgpuSceneSet, sampleResources0);
    }

    /** Allocates the single-sample Scene color and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor = device->createTexture("WebgpuMorphSceneColor", width, height, 1u);
        sceneDepth = device->createTexture("WebgpuMorphSceneDepth", width, height, 1u);
    }

    /** Updates and draws the unique Scene Set into the single-sample target. */
    void render() override
    {
        webgpuSceneSet->update();
        MorphTargetsSceneFrameBuffer frame0;
        configureSceneFrame(frame0, sceneColor, sceneDepth);
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue->renderPass("WebgpuMorphtargetsSample0", frame0, scenePass0())
            ->renderToSwapchain(swapchainTexture, sceneColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final RGBA8 target. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return sceneColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the webgpu Scene Set and every private attachment. */
    void destroy() override
    {
        webgpuSceneSet->destroy();
        device->freeBuffer(sampleBuffer0);
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }

private:
    /** Configures the independently cleared single-sample Scene target. */
    void configureSceneFrame(MorphTargetsSceneFrameBuffer &frame,
                             Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> color,
                             Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depth)
    {
        frame.color = color->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue = {0.5607843137, 0.7372549020, 0.8313725490, 1.0};
        frame.depth = depth->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
    }
};
#endif

#endif
