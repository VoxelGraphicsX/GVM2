#ifndef GVM_THREE_WEBGL_LOADER_COLLADA_KINEMATICS_HPP
#define GVM_THREE_WEBGL_LOADER_COLLADA_KINEMATICS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the common robot and expanded-grid vertex attribute union. */
struct WebglLoaderColladaKinematicsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 color [[Attribute2]];
};

/** Stores one entity transform and the shared camera state. */
struct WebglLoaderColladaKinematicsObjectData
{
    float4x4 model;
    float4x4 viewProjection;
    float4x4 normalTransform;
    float4 cameraPosition;
};

/** Stores the required one-instance entry for each robot or grid entity. */
struct WebglLoaderColladaKinematicsInstanceData
{
    float4 reserved;
};

/** Stores one Phong or line material plus its render phase. */
struct WebglLoaderColladaKinematicsMaterialData
{
    float4 baseColorAndPhase;
    float4 specularAndShininess;
};

/** Defines the unique RenderSet owned by the complete robot Scene. */
struct WebglLoaderColladaKinematicsSceneRenderSet : public IRenderSet
{
    /** Declares the packed robot and expanded-grid component schema. */
    constructor(
        BufferComponent<WebglLoaderColladaKinematicsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglLoaderColladaKinematicsObjectData> objects,
        BufferComponent<WebglLoaderColladaKinematicsInstanceData> instances,
        BufferComponent<WebglLoaderColladaKinematicsMaterialData> materials)
    {
    }
};

/** Carries resolved hierarchy and entity material state into one Scene phase. */
struct WebglLoaderColladaKinematicsVertexOutput
{
    float4 position [[Position]];
    float3 worldPosition [[Attribute0]];
    float3 worldNormal [[Attribute1]];
    float4 color [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines the single-sample robot Scene framebuffer. */
struct WebglLoaderColladaKinematicsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Encodes one linear channel for the browser-compatible output texture. */
float webglLoaderColladaKinematicsLinearToSrgb(float value)
{
    const float bounded = max(value, 0.0f);
    return bounded <= 0.0031308f
        ? bounded * 12.92f
        : pow(bounded, 0.41666f) * 1.055f - 0.055f;
}

/** Tests the WebGL one-pixel diamond rule in transformed screen coordinates. */
bool webglLoaderColladaKinematicsLineIntersectsDiamond(
    float2 lineStart,
    float2 lineEnd,
    float2 fragmentCenter)
{
    const float2 transformedStart = float2(
        lineStart.x + lineStart.y,
        lineStart.x - lineStart.y);
    const float2 transformedEnd = float2(
        lineEnd.x + lineEnd.y,
        lineEnd.x - lineEnd.y);
    const float2 transformedCenter = float2(
        fragmentCenter.x + fragmentCenter.y,
        fragmentCenter.x - fragmentCenter.y);
    const float2 offset = transformedStart - transformedCenter;
    const float2 direction = transformedEnd - transformedStart;
    float nearParameter = 0.0f;
    float farParameter = 1.0f;
    if (abs(direction.x) < 0.000001f)
    {
        if (abs(offset.x) > 0.5f) return false;
    }
    else
    {
        const float first = (-0.5f - offset.x) / direction.x;
        const float second = (0.5f - offset.x) / direction.x;
        nearParameter = max(nearParameter, min(first, second));
        farParameter = min(farParameter, max(first, second));
    }
    if (abs(direction.y) < 0.000001f)
    {
        if (abs(offset.y) > 0.5f) return false;
    }
    else
    {
        const float first = (-0.5f - offset.y) / direction.y;
        const float second = (0.5f - offset.y) / direction.y;
        nearParameter = max(nearParameter, min(first, second));
        farParameter = min(farParameter, max(first, second));
    }
    return nearParameter <= farParameter && farParameter >= 0.0f &&
        nearParameter < 1.0f;
}

/** Draws the seven Phong robot entities from the unique Scene Set. */
class WebglLoaderColladaKinematicsRobotMainPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set with the robot depth and cull state. */
    constructor(RenderSet<WebglLoaderColladaKinematicsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the current CPU-evaluated joint hierarchy transform. */
    WebglLoaderColladaKinematicsVertexOutput vertex(
        WebglLoaderColladaKinematicsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLoaderColladaKinematicsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderColladaKinematicsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 worldPosition = mul(
            objectData.model,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        WebglLoaderColladaKinematicsVertexOutput outputValue;
        outputValue.position = mul(objectData.viewProjection, worldPosition);
        outputValue.worldPosition = worldPosition.xyz;
        outputValue.worldNormal = float3(mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f)).xyz);
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the r185 hemisphere-lit robot Phong material. */
    WebglLoaderColladaKinematicsFrameBuffer fragment(
        WebglLoaderColladaKinematicsVertexOutput inputValue)
    {
        const WebglLoaderColladaKinematicsObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const WebglLoaderColladaKinematicsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        clip(0.25f - abs(materialData.baseColorAndPhase.w));
        // Three selects its flat-shaded derivative path when a lit mesh has no
        // NORMAL attribute, which is the case for the ABB robot geometry.
        const float3 normal = normalize(cross(
            ddy(inputValue.worldPosition),
            ddx(inputValue.worldPosition)));
        const float skyWeight = normal.y * 0.5f + 0.5f;
        const float3 skyColor = float3(1.0f, 0.930111f, 0.930111f);
        const float3 groundColor = float3(0.066626f, 0.066626f, 0.132868f);
        const float3 irradiance =
            (groundColor * (1.0f - skyWeight) + skyColor * skyWeight) * 3.0f;
        float3 linearColor = materialData.baseColorAndPhase.xyz *
            irradiance * 0.31830988618f;
        const float3 viewDirection = normalize(
            objectData.cameraPosition.xyz - inputValue.worldPosition);
        const float3 halfDirection = normalize(viewDirection + normal);
        linearColor += materialData.specularAndShininess.xyz *
            pow(max(dot(normal, halfDirection), 0.0f),
                materialData.specularAndShininess.w) * 0.02f;
        const float3 displayColor = float3(
            webglLoaderColladaKinematicsLinearToSrgb(linearColor.x),
            webglLoaderColladaKinematicsLinearToSrgb(linearColor.y),
            webglLoaderColladaKinematicsLinearToSrgb(linearColor.z));
        WebglLoaderColladaKinematicsFrameBuffer result;
        result.color = half4(half3(displayColor), half(1.0f));
        return result;
    }
};

/** Draws the CPU-expanded Cartesian grid from the same Scene Set. */
class WebglLoaderColladaKinematicsGridLinesPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set with grid depth testing and no culling. */
    constructor(RenderSet<WebglLoaderColladaKinematicsSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one endpoint-coded GridHelper segment to one screen pixel. */
    WebglLoaderColladaKinematicsVertexOutput vertex(
        WebglLoaderColladaKinematicsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglLoaderColladaKinematicsObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglLoaderColladaKinematicsInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 worldPosition = mul(
            objectData.model,
            inputValue.position + float4(instanceData.reserved.xyz, 0.0f));
        const float4 otherWorldPosition = mul(
            objectData.model,
            float4(inputValue.normal.xyz, 1.0f) +
                float4(instanceData.reserved.xyz, 0.0f));
        float4 clipPosition = mul(objectData.viewProjection, worldPosition);
        const float4 otherClipPosition = mul(
            objectData.viewProjection, otherWorldPosition);
        const bool useEnd = abs(inputValue.normal.w) > 1.5f;
        const float4 lineStartClip = useEnd ? otherClipPosition : clipPosition;
        const float4 lineEndClip = useEnd ? clipPosition : otherClipPosition;
        const float2 lineStartNdc = lineStartClip.xy / lineStartClip.w;
        const float2 lineEndNdc = lineEndClip.xy / lineEndClip.w;
        const float2 endpointNdc = clipPosition.xy / clipPosition.w;
        const float2 otherNdc = otherClipPosition.xy / otherClipPosition.w;
        const float2 directionInPixels = normalize(
            (otherNdc - endpointNdc) * float2(800.0f, 500.0f));
        // Keep the proxy triangles conservative; the fragment test below owns
        // the exact WebGL diamond-exit coverage decision.
        const float2 sideInNdc = float2(
            -directionInPixels.y * 32.0f / 800.0f,
            directionInPixels.x * 32.0f / 500.0f);
        clipPosition.xy += sideInNdc * clipPosition.w *
            (inputValue.normal.w < 0.0f ? -1.0f : 1.0f);
        WebglLoaderColladaKinematicsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.worldPosition = float3(
            (lineStartNdc.x * 0.5f + 0.5f) * 800.0f,
            (lineStartNdc.y * 0.5f + 0.5f) * 500.0f,
            0.0f);
        outputValue.worldNormal = float3(
            (lineEndNdc.x * 0.5f + 0.5f) * 800.0f,
            (lineEndNdc.y * 0.5f + 0.5f) * 500.0f,
            0.0f);
        outputValue.color = inputValue.color;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Outputs the exact linear GridHelper vertex color in the grid phase. */
    WebglLoaderColladaKinematicsFrameBuffer fragment(
        WebglLoaderColladaKinematicsVertexOutput inputValue)
    {
        const WebglLoaderColladaKinematicsMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        clip(0.25f - abs(materialData.baseColorAndPhase.w - 1.0f));
        clip(webglLoaderColladaKinematicsLineIntersectsDiamond(
            inputValue.worldPosition.xy,
            inputValue.worldNormal.xy,
            inputValue.position.xy) ? 1.0f : -1.0f);
        WebglLoaderColladaKinematicsFrameBuffer result;
        result.color = half4(half3(inputValue.color.xyz), half(1.0f));
        return result;
    }
};

/** Owns the dedicated kinematics Scene Set and two required Scene passes. */
class WebglLoaderColladaKinematicsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglLoaderColladaKinematicsSceneRenderSet> sceneSet;
    RenderClass<WebglLoaderColladaKinematicsRobotMainPass> robotPass;
    RenderClass<WebglLoaderColladaKinematicsGridLinesPass> gridPass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and both dedicated render phases. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglLoaderColladaKinematicsSceneRenderSet>();
        robotPass = device->createRenderClass<WebglLoaderColladaKinematicsRobotMainPass>(sceneSet);
        gridPass = device->createRenderClass<WebglLoaderColladaKinematicsGridLinesPass>(sceneSet);
    }

    /** Allocates the fixed single-sample output attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        outputColor = device->createTexture("WebglLoaderColladaKinematicsOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglLoaderColladaKinematicsDepth", width, height, 1u);
    }

    /** Preserves the common host adapter hook without enabling UI rendering. */
    void configureInspector(float enabled) { (void)enabled; }

    /** Updates the Set and executes the robot and grid passes in manifest order. */
    void render() override
    {
        sceneSet->update();
        WebglLoaderColladaKinematicsFrameBuffer first;
        first.color = outputColor->createView();
        first.color.loadOp = LoadOp::Clear;
        first.color.storeOp = StoreOp::Store;
        first.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        first.depth = outputDepth->createView();
        first.depth.depthLoadOp = LoadOp::Clear;
        first.depth.depthStoreOp = StoreOp::Store;
        first.depth.depthClearValue = 1.0f;
        WebglLoaderColladaKinematicsFrameBuffer second;
        second.color = outputColor->createView();
        second.color.loadOp = LoadOp::Load;
        second.color.storeOp = StoreOp::Store;
        second.depth = outputDepth->createView();
        second.depth.depthLoadOp = LoadOp::Load;
        second.depth.depthStoreOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglLoaderColladaKinematicsRobotMain", first, robotPass())
            ->renderPass("WebglLoaderColladaKinematicsGridLines", second, gridPass())
            ->renderToSwapchain(swapchainTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 readback texture. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const { return outputColor; }
    /** Returns the configured readback width. */
    uint getReadbackWidth() const { return width; }
    /** Returns the configured readback height. */
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
