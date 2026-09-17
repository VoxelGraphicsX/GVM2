#ifndef GVM_THREE_WEBGL_MATERIALS_CHANNELS_HPP
#define GVM_THREE_WEBGL_MATERIALS_CHANNELS_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one OBJ-expanded Ninja vertex and its authored material coordinates. */
struct WebglMaterialsChannelsVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores the current camera transform and the selected material channel. */
struct WebglMaterialsChannelsObjectData
{
    float4x4 modelViewProjection;
    float4x4 previousModelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
    float4 cameraAndViewport;
    uint4 materialAndCamera;
};

/** Provides the mandatory one-entry instance component for this non-instanced Scene. */
struct WebglMaterialsChannelsInstanceData
{
    float4 reserved;
};

/** Stores displacement constants and the selected material side. */
struct WebglMaterialsChannelsMaterialData
{
    float4 displacementAndSide;
};

/** Defines the one Scene RenderSet shared by both cull-mode material passes. */
struct WebglMaterialsChannelsSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, transforms, material modes, previous transforms, and maps. */
    constructor(
        BufferComponent<WebglMaterialsChannelsVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglMaterialsChannelsObjectData> objects,
        BufferComponent<WebglMaterialsChannelsInstanceData> instances,
        BufferComponent<WebglMaterialsChannelsMaterialData> materials,
        BufferComponent<float4x4> previousTransforms,
        (TextureComponent<half4, 4u> textures))
    {
    }
};

/** Binds the stable sampler used for the three Scene texture-component maps. */
struct WebglMaterialsChannelsSamplerResources final : public IBindGroup
{
    /** Declares one clamped linear sampler for normal, AO, and displacement maps. */
    constructor(Sampler materialSampler [[Binding0]])
    {
    }
};

/** Carries current/previous clip coordinates and view-space material inputs. */
struct WebglMaterialsChannelsVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
    float2 textureCoordinate [[Attribute2]];
    float4 currentClip [[Attribute3]];
    float4 previousClip [[Attribute4]];
    uint entityID [[Attribute5]];
};

/** Defines the RGBA8 color and depth attachments used by both Scene passes. */
struct WebglMaterialsChannelsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one non-negative linear channel through the Three.js sRGB transfer. */
float webglMaterialsChannelsLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Converts one authored sRGB channel into linear working space. */
float webglMaterialsChannelsSrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : pow((value + 0.055f) / 1.055f, 2.4f);
}

/** Packs a normalized scalar into the four-byte RGBA depth representation. */
float4 webglMaterialsChannelsPackRgbaDepth(float depth)
{
    const float4 bitShift = float4(1.0f, 255.0f, 65025.0f, 160581375.0f);
    const float4 bitMask = float4(0.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f);
    float4 result = frac(depth * bitShift);
    result -= float4(result.y, result.z, result.w, result.w) * bitMask;
    return result;
}

/** Packs one normalized scalar into Three.js RGB depth packing. */
float3 webglMaterialsChannelsPackRgbDepth(float depth)
{
    return webglMaterialsChannelsPackRgbaDepth(depth).xyz;
}

/** Packs one normalized scalar into Three.js RG depth packing. */
float2 webglMaterialsChannelsPackRgDepth(float depth)
{
    return webglMaterialsChannelsPackRgbaDepth(depth).xy;
}

/** Computes the common current/previous vertex state for both cull-mode passes. */
WebglMaterialsChannelsVertexOutput webglMaterialsChannelsVertexState(
    IN RenderSet<WebglMaterialsChannelsSceneRenderSet> sceneSet,
    IN BindGroup<WebglMaterialsChannelsSamplerResources> samplerResources,
    WebglMaterialsChannelsVertex inputValue,
    uint renderEntityID,
    uint renderEntityInstanceID)
{
    const WebglMaterialsChannelsObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
    const WebglMaterialsChannelsInstanceData instanceData =
        sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
    const WebglMaterialsChannelsMaterialData materialData =
        sceneSet->materials->get(renderEntityID, objectData.materialAndCamera.x);
    const half4 displacementSample = half4(sceneSet->textures->get(renderEntityID, 2u)->sampleLevel(
        samplerResources->materialSampler,
        inputValue.textureCoordinate.xy,
        0.0f));
    const float displacement = float(displacementSample.x) * materialData.displacementAndSide.z +
        materialData.displacementAndSide.w;
    const float4 displacedPosition = inputValue.position +
        float4(inputValue.normal.xyz * displacement, 0.0f) + instanceData.reserved;
    const float4 currentViewPosition = mul(objectData.modelView, displacedPosition);
    const float4 currentClip = mul(objectData.modelViewProjection, displacedPosition);
    const float4x4 previousModel = sceneSet->previousTransforms->get(renderEntityID, 0u);
    const float4 previousClip = mul(objectData.previousModelViewProjection, mul(previousModel, displacedPosition));
    WebglMaterialsChannelsVertexOutput outputValue;
    outputValue.position = currentClip;
    outputValue.position.y = -outputValue.position.y;
    outputValue.position.z = (outputValue.position.z + outputValue.position.w) * 0.5f;
    outputValue.viewPosition = currentViewPosition.xyz;
    outputValue.viewNormal = normalize(float3(mul(objectData.normalTransform, float4(inputValue.normal.xyz, 0.0f)).xyz));
    outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
    outputValue.currentClip = currentClip;
    outputValue.previousClip = previousClip;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Evaluates the selected Standard, Normal, Velocity, or Depth channel. */
half4 webglMaterialsChannelsShade(
    IN RenderSet<WebglMaterialsChannelsSceneRenderSet> sceneSet,
    IN BindGroup<WebglMaterialsChannelsSamplerResources> samplerResources,
    WebglMaterialsChannelsVertexOutput inputValue,
    bool flipNormal)
{
    const WebglMaterialsChannelsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
    const WebglMaterialsChannelsMaterialData materialData = sceneSet->materials->get(
        inputValue.entityID, objectData.materialAndCamera.x);
    const uint mode = objectData.materialAndCamera.y;
    float3 normal = normalize(inputValue.viewNormal);
    if (flipNormal) normal = -normal;
    const float2 uv = inputValue.textureCoordinate;
    const half4 normalSample = sceneSet->textures->get(inputValue.entityID, 0u)->sample(
        samplerResources->materialSampler, uv);
    const float3 mappedNormal = float3(
        float(normalSample.x) * 2.0f - 1.0f,
        float(normalSample.y) * 2.0f - 1.0f,
        float(normalSample.z) * 2.0f - 1.0f);
    const float3 shadedNormal = normalize(lerp(normal, normalize(mappedNormal), mode == 1u ? 1.0f : 0.35f));
    const float depth = clamp(inputValue.currentClip.z / max(inputValue.currentClip.w, 0.00001f) * 0.5f + 0.5f, 0.0f, 1.0f);
    if (mode == 1u)
    {
        return half4(half3(shadedNormal * 0.5f + 0.5f), half(1.0f));
    }
    if (mode == 2u)
    {
        const float2 currentNdc = inputValue.currentClip.xy / max(inputValue.currentClip.w, 0.00001f);
        const float2 previousNdc = inputValue.previousClip.xy / max(inputValue.previousClip.w, 0.00001f);
        const float2 velocity = (currentNdc - previousNdc) * 0.25f + 0.5f;
        const float2 packedX = webglMaterialsChannelsPackRgDepth(velocity.x);
        const float2 packedY = webglMaterialsChannelsPackRgDepth(velocity.y);
        return half4(half(packedX.x), half(packedX.y), half(packedY.x), half(packedY.y));
    }
    if (mode == 3u)
    {
        return half4(half(depth), half(depth), half(depth), half(1.0f));
    }
    if (mode == 4u)
    {
        const float4 packed = webglMaterialsChannelsPackRgbaDepth(depth);
        return half4(half3(packed.xyz), half(packed.w));
    }
    if (mode == 5u)
    {
        const float4 packed = webglMaterialsChannelsPackRgbaDepth(depth);
        return half4(half(packed.x), half(packed.y), half(packed.z), half(1.0f));
    }
    if (mode == 6u)
    {
        const float4 packed = webglMaterialsChannelsPackRgbaDepth(depth);
        return half4(half(packed.x), half(packed.y), half(0.0f), half(1.0f));
    }
    const float3 albedo = float3(
        webglMaterialsChannelsSrgbToLinear(0.8f),
        webglMaterialsChannelsSrgbToLinear(0.8f),
        webglMaterialsChannelsSrgbToLinear(0.8f));
    const float ao = float(sceneSet->textures->get(inputValue.entityID, 1u)->sample(
        samplerResources->materialSampler, uv).x);
    const float3 viewDirection = normalize(-inputValue.viewPosition);
    const float3 redLight = normalize(float3(0.0f, 0.0f, 1.0f));
    const float3 blueLight = normalize(float3(-0.55f, 0.0f, 0.45f));
    const float diffuse = max(dot(shadedNormal, redLight), 0.0f) * 1.5f +
        max(dot(shadedNormal, blueLight), 0.0f) * 1.5f + 0.3f;
    const float specular = pow(max(dot(reflect(-redLight, shadedNormal), viewDirection), 0.0f), 24.0f) * 0.35f;
    const float3 linearColor = saturate(albedo * diffuse * float(ao) + float3(specular));
    return half4(
        half(webglMaterialsChannelsLinearToSrgb(linearColor.x)),
        half(webglMaterialsChannelsLinearToSrgb(linearColor.y)),
        half(webglMaterialsChannelsLinearToSrgb(linearColor.z)),
        half(1.0f));
}

/** Renders the Scene front-side channel pass through the one RenderSet. */
class WebglMaterialsChannelsFrontFacingPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and culls back-facing triangles. */
    constructor(
        RenderSet<WebglMaterialsChannelsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglMaterialsChannelsSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one vertex with current and previous displacement transforms. */
    WebglMaterialsChannelsVertexOutput vertex(
        WebglMaterialsChannelsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglMaterialsChannelsVertexState(sceneSet, samplerResources, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the selected channel with the front-face normal convention. */
    WebglMaterialsChannelsFrameBuffer fragment(WebglMaterialsChannelsVertexOutput inputValue)
    {
        const WebglMaterialsChannelsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.materialAndCamera.z == 1u) discard_fragment();
        WebglMaterialsChannelsFrameBuffer frameBuffer;
        frameBuffer.color = webglMaterialsChannelsShade(sceneSet, samplerResources, inputValue, false);
        return frameBuffer;
    }
};

/** Renders the Scene back-side channel pass through the one RenderSet. */
class WebglMaterialsChannelsBackFacingPass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and culls front-facing triangles. */
    constructor(
        RenderSet<WebglMaterialsChannelsSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglMaterialsChannelsSamplerResources> samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one vertex with current and previous displacement transforms. */
    WebglMaterialsChannelsVertexOutput vertex(
        WebglMaterialsChannelsVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        return webglMaterialsChannelsVertexState(sceneSet, samplerResources, inputValue, renderEntityID, renderEntityInstanceID);
    }

    /** Writes the selected channel with a flipped back-face normal. */
    WebglMaterialsChannelsFrameBuffer fragment(WebglMaterialsChannelsVertexOutput inputValue)
    {
        const WebglMaterialsChannelsObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        if (objectData.materialAndCamera.z == 0u) discard_fragment();
        WebglMaterialsChannelsFrameBuffer frameBuffer;
        frameBuffer.color = webglMaterialsChannelsShade(sceneSet, samplerResources, inputValue, true);
        return frameBuffer;
    }
};

/** Owns the single-sample output and both RenderSet scene passes. */
class WebglMaterialsChannelsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglMaterialsChannelsSceneRenderSet> sceneSet;
    Sampler materialSampler;
    BindGroup<WebglMaterialsChannelsSamplerResources> samplerResources;
    RenderClass<WebglMaterialsChannelsFrontFacingPass> frontFacingPass;
    RenderClass<WebglMaterialsChannelsBackFacingPass> backFacingPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        outputDepth;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates the RenderSet, sampler, and dedicated front/back material passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglMaterialsChannelsSceneRenderSet>();
        materialSampler = device->createSampler({
            .label = "WebglMaterialsChannelsSampler",
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
        samplerResources = device->createBindGroup<WebglMaterialsChannelsSamplerResources>(materialSampler);
        frontFacingPass = device->createRenderClass<WebglMaterialsChannelsFrontFacingPass>(sceneSet, samplerResources);
        backFacingPass = device->createRenderClass<WebglMaterialsChannelsBackFacingPass>(sceneSet, samplerResources);
    }

    /** Recreates explicit single-sample RGBA8 and depth capture attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("WebglMaterialsChannelsColor", width, height, 1u);
        outputDepth = device->createTexture("WebglMaterialsChannelsDepth", width, height, 1u);
    }

    /** Applies pending RenderSet updates and draws both cull-mode passes. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglMaterialsChannelsFrameBuffer firstFrame;
        firstFrame.color = outputColor->createView();
        firstFrame.color.loadOp = LoadOp::Clear;
        firstFrame.color.storeOp = StoreOp::Store;
        firstFrame.color.clearValue = {0.02, 0.02, 0.025, 1.0};
        firstFrame.depth = outputDepth->createView();
        firstFrame.depth.depthLoadOp = LoadOp::Clear;
        firstFrame.depth.depthStoreOp = StoreOp::Store;
        firstFrame.depth.depthClearValue = 1.0f;
        WebglMaterialsChannelsFrameBuffer secondFrame;
        secondFrame.color = outputColor->createView();
        secondFrame.color.loadOp = LoadOp::Load;
        secondFrame.color.storeOp = StoreOp::Store;
        secondFrame.depth = outputDepth->createView();
        secondFrame.depth.depthLoadOp = LoadOp::Load;
        secondFrame.depth.depthStoreOp = StoreOp::Store;
        graphicsQueue
            ->renderPass("WebglMaterialsChannelsFront", firstFrame, frontFacingPass())
            ->renderPass("WebglMaterialsChannelsBack", secondFrame, backFacingPass())
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned output texture used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the RenderSet and explicit output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#endif
