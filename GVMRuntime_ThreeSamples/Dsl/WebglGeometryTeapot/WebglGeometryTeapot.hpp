#ifndef GVM_THREE_WEBGL_GEOMETRY_TEAPOT_HPP
#define GVM_THREE_WEBGL_GEOMETRY_TEAPOT_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one camera-specialized r185 teapot triangle vertex. */
struct WebglGeometryTeapotVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores the frozen camera, light, and viewport state. */
struct WebglGeometryTeapotObjectData
{
    float4x4 modelViewProjection;
    float4 cameraPosition;
    float4 directionalLight;
    float4 cameraRightAndTanHalfFov;
    float4 cameraUpAndAspect;
    float4 cameraForwardAndReserved;
};

/** Stores the mandatory non-instanced component element. */
struct WebglGeometryTeapotInstanceData
{
    float4 reserved;
};

/** Stores glossy or reflective material controls for the active entity. */
struct WebglGeometryTeapotMaterialData
{
    float4 diffuseAndShininess;
    float4 specularAndMode;
    float4 ambientAndDirectionalIntensity;
};

/** Defines the only RenderSet owned by the logical teapot Scene. */
struct WebglGeometryTeapotSceneRenderSet : public IRenderSet
{
    /** Declares the manifest-locked six-component Scene layout. */
    constructor(
        BufferComponent<WebglGeometryTeapotVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryTeapotObjectData> objects,
        BufferComponent<WebglGeometryTeapotInstanceData> instances,
        BufferComponent<WebglGeometryTeapotMaterialData> materials,
        (TextureComponent<half4, 8u> textures))
    {
    }
};

/** Carries smooth Phong inputs and entity identity to the fragment stage. */
struct WebglGeometryTeapotVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float2 uv [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the single-sample teapot color and depth attachments. */
struct WebglGeometryTeapotFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds the sampler shared by the Scene-owned Pisa texture faces. */
struct WebglGeometryTeapotEnvironmentResources final : public IBindGroup
{
    /** Declares a sampler without introducing standalone texture ownership. */
    constructor(Sampler environmentSampler [[Binding0]])
    {
    }
};

/** Carries one fullscreen coordinate for the reflective cube background. */
struct WebglGeometryTeapotScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear channel through Three r185's sRGB output transfer. */
float webglGeometryTeapotLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Projects a direction onto one fixed cube face for stable mip gradients. */
float2 webglGeometryTeapotCubeUvForFace(float3 direction, uint face)
{
    float2 coordinate;
    float divisor;
    if (face == 0u)
    {
        divisor = max(abs(direction.x), 0.000001f);
        coordinate = float2(-direction.z, -direction.y) / divisor;
    }
    else if (face == 1u)
    {
        divisor = max(abs(direction.y), 0.000001f);
        coordinate = float2(-direction.x, direction.z) / divisor;
    }
    else if (face == 2u)
    {
        divisor = max(abs(direction.z), 0.000001f);
        coordinate = float2(-direction.x, -direction.y) / divisor;
    }
    else if (face == 3u)
    {
        divisor = max(abs(direction.x), 0.000001f);
        coordinate = float2(direction.z, -direction.y) / divisor;
    }
    else if (face == 4u)
    {
        divisor = max(abs(direction.y), 0.000001f);
        coordinate = float2(-direction.x, -direction.z) / divisor;
    }
    else
    {
        divisor = max(abs(direction.z), 0.000001f);
        coordinate = float2(direction.x, -direction.y) / divisor;
    }
    return coordinate * 0.5f + 0.5f;
}

/** Samples one Scene-owned Pisa face with Three r185's cube convention. */
float3 webglGeometryTeapotSampleCube(
    IN RenderSet<WebglGeometryTeapotSceneRenderSet> sceneSet,
    IN BindGroup<WebglGeometryTeapotEnvironmentResources> resources,
    uint entityID,
    float3 direction)
{
    const float3 absoluteDirection = abs(direction);
    float2 coordinate = float2(0.0f);
    uint face = 0u;
    if (absoluteDirection.x > absoluteDirection.z &&
        absoluteDirection.x > absoluteDirection.y)
    {
        if (direction.x > 0.0f)
        {
            face = 3u;
            coordinate = float2(direction.z, -direction.y) /
                absoluteDirection.x;
        }
        else
        {
            face = 0u;
            coordinate = float2(-direction.z, -direction.y) /
                absoluteDirection.x;
        }
    }
    else if (absoluteDirection.z > absoluteDirection.y)
    {
        if (direction.z > 0.0f)
        {
            face = 2u;
            coordinate = float2(-direction.x, -direction.y) /
                absoluteDirection.z;
        }
        else
        {
            face = 5u;
            coordinate = float2(direction.x, -direction.y) /
                absoluteDirection.z;
        }
    }
    else if (direction.y > 0.0f)
    {
        face = 1u;
        coordinate = float2(-direction.x, direction.z) /
            absoluteDirection.y;
    }
    else
    {
        face = 4u;
        coordinate = float2(-direction.x, -direction.z) /
            absoluteDirection.y;
    }
    uint textureSlot = face;
    if (face == 1u)
    {
        textureSlot = 2u;
    }
    else if (face == 2u)
    {
        textureSlot = 4u;
    }
    else if (face == 3u)
    {
        textureSlot = 1u;
    }
    else if (face == 4u)
    {
        textureSlot = 3u;
    }
    const float2 uv = clamp(
        coordinate * 0.5f + 0.5f,
        float2(0.0000001f),
        float2(0.9999999f));
    const float2 gradientX =
        webglGeometryTeapotCubeUvForFace(direction + ddx(direction), face) -
        uv;
    const float2 gradientY =
        webglGeometryTeapotCubeUvForFace(direction + ddy(direction), face) -
        uv;
    const float footprint = max(
        max(length(gradientX * 256.0f), length(gradientY * 256.0f)),
        1.0f);
    const float mipLevel = clamp(log2(footprint), 0.0f, 8.0f);
    return float4(sceneSet->textures->get(entityID, textureSlot)->sampleLevel(
        resources->environmentSampler,
        float2(uv.x, 1.0f - uv.y),
        mipLevel)).xyz;
}

/** Draws the reflective cube background through the Scene's texture pool. */
class WebglGeometryTeapotCubeBackgroundPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and disables geometry depth writes. */
    constructor(
        RenderSet<WebglGeometryTeapotSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryTeapotEnvironmentResources> resources [[Slot1]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglGeometryTeapotScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGeometryTeapotScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reconstructs the fixed camera ray and emits the display cube color. */
    WebglGeometryTeapotFrameBuffer fragment(
        WebglGeometryTeapotScreenOutput inputValue)
    {
        const WebglGeometryTeapotObjectData objectData =
            sceneSet->objects->get(0u, 0u);
        const WebglGeometryTeapotMaterialData materialData =
            sceneSet->materials->get(0u, 0u);
        if (materialData.specularAndMode.w <= 0.5f)
        {
            WebglGeometryTeapotFrameBuffer blackOutput;
            blackOutput.color = half4(0.0f, 0.0f, 0.0f, 1.0f);
            return blackOutput;
        }
        const float2 ndc = inputValue.uv * 2.0f - 1.0f;
        const float3 direction = normalize(
            objectData.cameraForwardAndReserved.xyz +
            objectData.cameraRightAndTanHalfFov.xyz *
                (ndc.x * objectData.cameraUpAndAspect.w *
                 objectData.cameraRightAndTanHalfFov.w) +
            objectData.cameraUpAndAspect.xyz *
                (-ndc.y * objectData.cameraRightAndTanHalfFov.w));
        const float3 linearColor = webglGeometryTeapotSampleCube(
            sceneSet,
            resources,
            0u,
            direction);
        WebglGeometryTeapotFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglGeometryTeapotLinearToSrgb(linearColor.x)),
            half(webglGeometryTeapotLinearToSrgb(linearColor.y)),
            half(webglGeometryTeapotLinearToSrgb(linearColor.z)),
            half(1.0f));
        return outputValue;
    }
};

/** Evaluates Three's normalized Blinn-Phong direct specular BRDF. */
float3 webglGeometryTeapotBlinnPhong(
    float3 specularColor,
    float shininess,
    float dotNormalHalf,
    float dotViewHalf)
{
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 fresnelColor =
        specularColor + (float3(1.0f) - specularColor) * fresnel;
    const float distribution =
        (shininess * 0.5f + 1.0f) * 0.0795774715f *
        pow(max(dotNormalHalf, 0.0f), shininess);
    return fresnelColor * distribution;
}

/** Draws the active teapot entity through indexed-indirect RenderSet metadata. */
class WebglGeometryTeapotTeapotPass final : public IRenderClass
{
public:
    /** Configures double-sided single-sample depth rendering. */
    constructor(
        RenderSet<WebglGeometryTeapotSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglGeometryTeapotEnvironmentResources> resources [[Slot1]])
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one CPU-tessellated vertex and reads both entity builtins. */
    WebglGeometryTeapotVertexOutput vertex(
        WebglGeometryTeapotVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryTeapotObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryTeapotInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const float4 worldPosition =
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f);
        WebglGeometryTeapotVertexOutput outputValue;
        outputValue.position =
            mul(objectData.modelViewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = inputValue.normal.xyz;
        outputValue.uv = inputValue.uv.xy;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Shades the glossy r185 material in linear space and emits display sRGB. */
    WebglGeometryTeapotFrameBuffer fragment(
        WebglGeometryTeapotVertexOutput inputValue)
    {
        const WebglGeometryTeapotObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglGeometryTeapotMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float3 normal = normalize(inputValue.worldNormal);
        const float3 lightDirection =
            normalize(float3(objectData.directionalLight.xyz));
        const float3 viewDirection = normalize(
            objectData.cameraPosition.xyz - inputValue.worldPosition);
        const float3 halfDirection =
            normalize(lightDirection + viewDirection);
        const float dotNormalLight =
            max(dot(normal, lightDirection), 0.0f);
        const float dotNormalHalf =
            max(dot(normal, halfDirection), 0.0f);
        const float dotViewHalf =
            max(dot(viewDirection, halfDirection), 0.0f);
        const float3 diffuseColor = materialData.diffuseAndShininess.xyz;
        const float shininess = materialData.diffuseAndShininess.w;
        const float3 directLight =
            float3(materialData.ambientAndDirectionalIntensity.y);
        const float3 ambientLight =
            float3(materialData.ambientAndDirectionalIntensity.x);
        float3 color = diffuseColor * ambientLight * 0.318309886f;
        color += diffuseColor * directLight *
            dotNormalLight * 0.318309886f;
        if (dotNormalLight > 0.0f)
        {
            color += directLight * dotNormalLight *
                webglGeometryTeapotBlinnPhong(
                    materialData.specularAndMode.xyz,
                    shininess,
                    dotNormalHalf,
                    dotViewHalf);
        }
        if (materialData.specularAndMode.w > 0.5f)
        {
            const float3 environmentDirection = reflect(
                -viewDirection,
                normal);
            color *= webglGeometryTeapotSampleCube(
                sceneSet,
                resources,
                inputValue.entityID,
                environmentDirection);
        }
        WebglGeometryTeapotFrameBuffer outputValue;
        outputValue.color = half4(
            half(webglGeometryTeapotLinearToSrgb(color.x)),
            half(webglGeometryTeapotLinearToSrgb(color.y)),
            half(webglGeometryTeapotLinearToSrgb(color.z)),
            half(1.0f));
        return outputValue;
    }
};

/** Owns the unique Scene Set and its dedicated glossy material pass. */
class WebglGeometryTeapotRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryTeapotSceneRenderSet> sceneSet;
    BindGroup<WebglGeometryTeapotEnvironmentResources> environmentResources;
    Sampler environmentSampler;
    RenderClass<WebglGeometryTeapotCubeBackgroundPass> backgroundPass;
    RenderClass<WebglGeometryTeapotTeapotPass> teapotPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the Scene's single RenderSet and dedicated pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglGeometryTeapotSceneRenderSet>();
        environmentSampler = device->createSampler({
            .label = "WebglGeometryTeapotEnvironmentSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 8,
            .maxAnisotropy = 1,
        });
        environmentResources =
            device->createBindGroup<WebglGeometryTeapotEnvironmentResources>(
                environmentSampler);
        backgroundPass = device->createRenderClass<
            WebglGeometryTeapotCubeBackgroundPass>(
                sceneSet,
                environmentResources);
        teapotPass =
            device->createRenderClass<WebglGeometryTeapotTeapotPass>(
                sceneSet,
                environmentResources);
    }

    /** Allocates ordinary single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture(
            "WebglGeometryTeapotColor", width, height, 1u);
        outputDepth = device->createTexture(
            "WebglGeometryTeapotDepth", width, height, 1u);
    }

    /** Renders the cube screen pass, Scene entity, and RGBA8 presentation. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryTeapotFrameBuffer backgroundFrame;
        backgroundFrame.color = outputColor->createView();
        backgroundFrame.color.loadOp = LoadOp::Clear;
        backgroundFrame.color.storeOp = StoreOp::Store;
        backgroundFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        backgroundFrame.depth = outputDepth->createView();
        backgroundFrame.depth.depthLoadOp = LoadOp::Clear;
        backgroundFrame.depth.depthStoreOp = StoreOp::Store;
        backgroundFrame.depth.depthClearValue = 1.0f;
        WebglGeometryTeapotFrameBuffer teapotFrame;
        teapotFrame.color = outputColor->createView();
        teapotFrame.color.loadOp = LoadOp::Load;
        teapotFrame.color.storeOp = StoreOp::Store;
        teapotFrame.depth = outputDepth->createView();
        teapotFrame.depth.depthLoadOp = LoadOp::Load;
        teapotFrame.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometryTeapotCubeBackground",
                backgroundFrame,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglGeometryTeapotTeapot", teapotFrame, teapotPass())
            ->renderToSwapchain(
                swapchainTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Exposes the deterministic RGBA8 attachment to the host. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases the unique Set and private single-sample attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
