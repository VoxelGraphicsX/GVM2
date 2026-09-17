#ifndef GVM_THREE_WEBGL_GEOMETRY_SHAPES_HPP
#define GVM_THREE_WEBGL_GEOMETRY_SHAPES_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the attribute union used by flat shapes, extrusions, lines, and point markers. */
struct WebglGeometryShapesVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores one Scene entity camera transform and light state. */
struct WebglGeometryShapesObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 pointLightViewPositionAndIntensity;
};

/** Stores the mandatory one-entry instance component for every normalized object. */
struct WebglGeometryShapesInstanceData
{
    float4 translation;
};

/** Stores per-entity color, texture selection, and material mode. */
struct WebglGeometryShapesMaterialData
{
    float4 baseColorAndFlags;
};

/** Defines the single RenderSet for the complete 93-renderable Scene. */
struct WebglGeometryShapesSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, entity transforms, material groups, and one texture slot. */
    constructor(
        BufferComponent<WebglGeometryShapesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryShapesObjectData> objects,
        BufferComponent<WebglGeometryShapesInstanceData> instances,
        BufferComponent<WebglGeometryShapesMaterialData> materials,
        (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Binds the explicit sampler used by the textured flat-shape rows. */
struct WebglGeometryShapesSamplerResources final : public IBindGroup
{
    /** Declares the repeat linear sampler for the UV-grid asset. */
    constructor(Sampler textureSampler [[Binding0]])
    {
    }
};

/** Carries camera-space position, normal, UV, and entity identity to the fragment stage. */
struct WebglGeometryShapesVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    float2 primitiveCoord [[Attribute3]];
    float primitiveMode [[Attribute4]];
    uint entityID [[Attribute5]];
};

/** Defines the ordinary single-sample capture attachments. */
struct WebglGeometryShapesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Performs the same linear-to-sRGB transfer used by the Three canvas output. */
float webglGeometryShapesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Resolves one RenderSet entity and applies its exact camera-space transform. */
WebglGeometryShapesVertexOutput webglGeometryShapesTransformVertex(
    IN RenderSet<WebglGeometryShapesSceneRenderSet> sceneSet,
    WebglGeometryShapesVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglGeometryShapesObjectData objectData =
        sceneSet->objects->get(renderEntityID, 0u);
    const WebglGeometryShapesInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const float4 localPosition =
        inputValue.position + float4(instanceData.translation.xyz, 0.0f);
    const float4 viewPosition =
        mul(objectData.modelView, localPosition);
    float4 clipPosition =
        mul(objectData.modelViewProjection, localPosition);
    clipPosition.y = -clipPosition.y;
    clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
    WebglGeometryShapesVertexOutput outputValue;
    outputValue.position = clipPosition;
    outputValue.viewPosition = viewPosition.xyz;
    const float3 transformedNormal =
        mul(objectData.modelView, float4(inputValue.normal.xyz, 0.0f)).xyz;
    outputValue.viewNormal = normalize(transformedNormal);
    outputValue.uv = inputValue.uv.xy;
    outputValue.primitiveCoord = inputValue.uv.zw;
    outputValue.primitiveMode = inputValue.normal.w;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates textured and untextured Phong material groups in DSL. */
float4 webglGeometryShapesShade(
    IN RenderSet<WebglGeometryShapesSceneRenderSet> sceneSet,
    BindGroup<WebglGeometryShapesSamplerResources> samplerResources,
    WebglGeometryShapesVertexOutput inputValue)
{
    const WebglGeometryShapesObjectData objectData =
        sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglGeometryShapesMaterialData materialData =
        sceneSet->materials->get(inputValue.entityID, 0u);
    float3 albedo = materialData.baseColorAndFlags.xyz;
    if (materialData.baseColorAndFlags.w > 0.5f)
    {
        albedo *= float3(sceneSet->textures->get(inputValue.entityID, 0u)
            ->sample(samplerResources->textureSampler, inputValue.uv).xyz);
    }
    if (inputValue.primitiveMode > 2.5f)
    {
        return float4(0.9411764706f, 0.9411764706f, 0.9411764706f, 1.0f);
    }
    if (inputValue.primitiveMode > 0.5f)
    {
        return float4(
            webglGeometryShapesLinearToSrgb(albedo.x),
            webglGeometryShapesLinearToSrgb(albedo.y),
            webglGeometryShapesLinearToSrgb(albedo.z),
            1.0f);
    }
    const float3 normal = normalize(inputValue.viewNormal);
    const float3 lightDirection = normalize(
        objectData.pointLightViewPositionAndIntensity.xyz - inputValue.viewPosition);
    const float dotNL = max(dot(normal, lightDirection), 0.0f);
    // MeshPhong's Lambert BRDF contributes one over pi.  The previous
    // approximation omitted that normalization and saturated every front
    // cap when the source PointLight intensity was 2.5.
    const float irradiance =
        objectData.pointLightViewPositionAndIntensity.w * dotNL;
    const float diffuse = irradiance * 0.3183098861837907f;
    const float3 viewDirection = normalize(-inputValue.viewPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNH = saturate(dot(normal, halfDirection));
    const float dotVH = saturate(dot(viewDirection, halfDirection));
    const float fresnel = exp2((-5.55473f * dotVH - 6.98316f) * dotVH);
    const float3 specularColor = float3(0.005605391f);
    const float3 fresnelColor = specularColor * (1.0f - fresnel) + float3(fresnel);
    const float specularDistribution =
        (30.0f * 0.5f + 1.0f) * 0.3183098861837907f * pow(dotNH, 30.0f);
    const float3 specular = irradiance * fresnelColor *
        (0.25f * specularDistribution);
    const float3 linearColor = albedo * diffuse + specular;
    return float4(
        webglGeometryShapesLinearToSrgb(linearColor.x),
        webglGeometryShapesLinearToSrgb(linearColor.y),
        webglGeometryShapesLinearToSrgb(linearColor.z),
        1.0f);
}

/** Draws all 93 normalized mesh, line, and point entities through one Scene Set. */
class WebglGeometryShapesMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and configures double-sided depth-tested drawing. */
    constructor(
        RenderSet<WebglGeometryShapesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryShapesSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads entity and instance builtins and forwards the packed union to the fragment stage. */
    WebglGeometryShapesVertexOutput vertex(
        WebglGeometryShapesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglGeometryShapesTransformVertex(
            sceneSet, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Shades the shape, expanded line, or expanded point entity. */
    WebglGeometryShapesFrameBuffer fragment(
        WebglGeometryShapesVertexOutput inputValue)
    {
        if (inputValue.primitiveMode > 1.5f)
        {
            if (length(inputValue.primitiveCoord) > 1.0f)
                discard_fragment();
        }
        else if (inputValue.primitiveMode > 0.5f)
        {
            if (abs(inputValue.primitiveCoord.y) > 1.0f)
                discard_fragment();
        }
        WebglGeometryShapesFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(webglGeometryShapesShade(
            sceneSet, samplerResources, inputValue).xyz), half(1.0f));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet and the single-sample output targets. */
class WebglGeometryShapesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryShapesSceneRenderSet> sceneSet;
    Sampler textureSampler;
    BindGroup<WebglGeometryShapesSamplerResources> samplerResources;
    RenderClass<WebglGeometryShapesMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the dedicated Scene Set, sampler, and Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglGeometryShapesSceneRenderSet>();
        textureSampler = device->createSampler({
            .label = "WebglGeometryShapesUvGridSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 16,
            .maxAnisotropy = 1,
        });
        samplerResources = device->createBindGroup<WebglGeometryShapesSamplerResources>(
            textureSampler);
        scenePass = device->createRenderClass<WebglGeometryShapesMainPass>(
            sceneSet, samplerResources);
    }

    /** Allocates ordinary single-sample RGBA8 and depth targets. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglGeometryShapesColor", width, height, 1u);
        outputDepth = device->createTexture("WebglGeometryShapesDepth", width, height, 1u);
    }

    /** Submits the one indexed-indirect Scene pass and presents the result. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryShapesFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.9411764706, 0.9411764706, 0.9411764706, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglGeometryShapesScene", frameBuffer, scenePass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the RenderSet and explicit single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglGeometryShapesRenderer
#undef WebglGeometryShapesFrameBuffer
#undef WebglGeometryShapesMainPass
#undef WebglGeometryShapesSceneRenderSet

#endif
