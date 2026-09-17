#ifndef GVM_THREE_PHASE1_GEOMETRY_RENDER_SET_HPP
#define GVM_THREE_PHASE1_GEOMETRY_RENDER_SET_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one exact non-indexed extrusion position, normal, and group slot. */
struct ExtrudeSceneVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 materialSlotAndReserved [[Attribute2]];
};

/** Stores one entity model-view transform and viewport dimensions. */
struct ExtrudeObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 viewportAndReserved;
    float4 ambientColorAndReserved;
    float4 pointLightViewPositionAndIntensity;
};

/** Stores the mandatory single non-instanced component record. */
struct ExtrudeInstanceData
{
    float4 translation;
};

/** Stores one private MeshLambert base color. */
struct ExtrudeMaterialData
{
    float4 baseColor;
};

/** Defines the unique Scene RenderSet used only by the extrusion example. */
struct WebglGeometryExtrudeShapesSceneRenderSet : public IRenderSet
{
    /** Declares the attribute union and all per-entity material data. */
    constructor(BufferComponent<ExtrudeSceneVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ExtrudeObjectData> objects,
                BufferComponent<ExtrudeInstanceData> instances,
                BufferComponent<ExtrudeMaterialData> materials)
    {
    }
};

/** Carries view-space position, flat normal, material slot, and entity identity. */
struct ExtrudeVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float materialSlot [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Defines one independently depth-tested Scene sample. */
struct ExtrudeSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's output transfer. */
float extrudeLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f)
    {
        return clamped * 12.92f;
    }
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws all three extrusion entities through the Scene's only RenderSet. */
class WebglGeometryExtrudeShapesMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet for ordinary single-sample drawing. */
    constructor(
        RenderSet<WebglGeometryExtrudeShapesSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity transform while preserving source flat normals. */
    ExtrudeVertexOutput vertex(
        ExtrudeSceneVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ExtrudeObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const ExtrudeInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        const float3 position =
            inputValue.position.xyz + instanceData.translation.xyz;
        const float4 viewPosition =
            mul(objectData.modelView, float4(position, 1.0f));
        float4 clipPosition =
            mul(objectData.modelViewProjection, float4(position, 1.0f));
        clipPosition.y = -clipPosition.y;
        const float3 transformedNormal = float3(
            mul(objectData.modelView,
                float4(inputValue.normal.xyz, 0.0f)).xyz);
        ExtrudeVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(transformedNormal);
        outputValue.materialSlot = inputValue.materialSlotAndReserved.x;
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the r185 ambient plus camera-following point Lambert light. */
    ExtrudeSceneFrameBuffer fragment(ExtrudeVertexOutput inputValue)
    {
        const ExtrudeObjectData objectData =
            sceneSet->objects->get(inputValue.entityID, 0u);
        const uint materialSlot = uint(inputValue.materialSlot + 0.5f);
        const ExtrudeMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, materialSlot);
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 lightDirection = normalize(
            objectData.pointLightViewPositionAndIntensity.xyz -
            inputValue.viewPosition);
        const float irradiance =
            objectData.ambientColorAndReserved.x +
            objectData.pointLightViewPositionAndIntensity.w *
                max(dot(normal, lightDirection), 0.0f);
        const float3 linearColor =
            materialData.baseColor.xyz * irradiance * 0.3183098861837907f;
        ExtrudeSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(extrudeLinearToSrgb(linearColor.x),
                  extrudeLinearToSrgb(linearColor.y),
                  extrudeLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the dedicated extrusion Scene RenderSet and single-sample output. */
class Phase1GeometryRenderSetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglGeometryExtrudeShapesSceneRenderSet> sceneSet;
    RenderClass<WebglGeometryExtrudeShapesMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the unique Scene Set and single-sample Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglGeometryExtrudeShapesSceneRenderSet>();
        scenePass =
            device->createRenderClass<WebglGeometryExtrudeShapesMainPass>(
                sceneSet);
    }

    /** Allocates one ordinary single-sample Scene color and depth target. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneColor =
            device->createTexture("ExtrudeColor", width, height, 1u);
        sceneDepth =
            device->createTexture("ExtrudeDepth", width, height, 1u);
    }

    /** Draws the unique Set exactly once through the ordinary Scene target. */
    void render() override
    {
        sceneSet->update();
        ExtrudeSceneFrameBuffer frame;
        configureSceneFrame(frame, sceneColor, sceneDepth);
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("ExtrudeScene", frame, scenePass())
            ->renderToSwapchain(
                swapchainTexture, sceneColor,
                RenderToSwapchainDescriptor{})
            ->submit();
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

    /** Releases the Scene Set and every private attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneColor);
        device->freeTexture(sceneDepth);
    }

private:
    /** Configures the one cleared single-sample Scene target. */
    void configureSceneFrame(
        ExtrudeSceneFrameBuffer &frame,
        Texture<TextureFormat::RGBA8Unorm,
                TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
                TextureDimension::e2D> color,
        Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depth)
    {
        frame.color = color->createView();
        frame.color.loadOp = LoadOp::Clear;
        frame.color.storeOp = StoreOp::Store;
        frame.color.clearValue =
            {0.1333333333, 0.1333333333, 0.1333333333, 1.0};
        frame.depth = depth->createView();
        frame.depth.depthLoadOp = LoadOp::Clear;
        frame.depth.depthStoreOp = StoreOp::Store;
        frame.depth.depthClearValue = 1.0f;
    }
};

#endif
