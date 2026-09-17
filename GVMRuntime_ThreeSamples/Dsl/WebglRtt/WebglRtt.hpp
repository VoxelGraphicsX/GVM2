#ifndef GVM_THREE_WEBGL_RTT_HPP
#define GVM_THREE_WEBGL_RTT_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one plane, torus, or sphere vertex for the RTT example. */
struct WebglRttVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one entity camera and normal transform. */
struct WebglRttObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
};

/** Stores the mandatory ordinary-entity instance component. */
struct WebglRttInstanceData
{
    float4 reserved;
};

/** Stores the shader phase, time, and Phong values. */
struct WebglRttMaterialData
{
    float4 phaseTimeAndColor;
    float4 specularAndShininess;
};

/** Defines the common ABI used by each logical Scene's unique RenderSet. */
struct WebglRttSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry and per-entity data for one Scene. */
    constructor(
        BufferComponent<WebglRttVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglRttObjectData> objects,
        BufferComponent<WebglRttInstanceData> instances,
        BufferComponent<WebglRttMaterialData> materials,
        (TextureComponent<half4, 2u> textures))
    {
    }
};

/** Binds the generated RTT to the fullscreen and sphere passes. */
struct WebglRttTextureResources final : public IBindGroup
{
    /** Declares one ordinary single-sample RTT and linear sampler. */
    constructor(
        Texture2D<half4> rttTexture [[Binding0]],
        Sampler rttSampler [[Binding1]])
    {
    }
};

/** Carries Scene data to RTT and sphere fragment stages. */
struct WebglRttSceneOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    float3 viewNormal [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Carries fullscreen coordinates to the copy stage. */
struct WebglRttScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear RTT and shared depth target. */
struct WebglRttSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final display-encoded output. */
struct WebglRttOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear channel with Three r185's sRGB output transfer. */
float webglRttLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.4166667f) * 1.055f - 0.055f;
}

/** Evaluates the direct lighting used by the two MeshPhong torus entities. */
float3 webglRttPhongLight(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 specularColor,
    float shininess,
    float3 lightColor,
    float intensity)
{
    const float normalDotLight = max(dot(normal, lightDirection), 0.0f);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float normalDotHalf = max(dot(normal, halfDirection), 0.0f);
    const float viewDotHalf = max(dot(viewDirection, halfDirection), 0.0f);
    const float fresnelWeight = exp2(
        (-5.55473f * viewDotHalf - 6.98316f) * viewDotHalf);
    const float3 fresnel = specularColor * (1.0f - fresnelWeight) +
        float3(fresnelWeight);
    const float3 specular = fresnel * normalDotLight *
        0.25f * (shininess * 0.5f + 1.0f) *
        0.3183098862f * pow(normalDotHalf, shininess);
    return lightColor * intensity * specular;
}

/** Draws the plane and both torus entities into the unique RTT Scene Set. */
class WebglRttOffscreenScenePass final : public IRenderClass
{
public:
    /** Binds exactly one RTT Scene Set with opaque depth state. */
    constructor(RenderSet<WebglRttSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies the entity transform and mandatory instance component. */
    WebglRttSceneOutput vertex(
        WebglRttVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglRttObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglRttInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        WebglRttSceneOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.uv = inputValue.textureCoordinate.xy;
        outputValue.viewPosition = mul(objectData.modelView, localPosition).xyz;
        outputValue.viewNormal = normalize(float3(mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Evaluates the inline ShaderMaterial or the exact two-light Phong path. */
    WebglRttSceneFrameBuffer fragment(WebglRttSceneOutput inputValue)
    {
        const WebglRttMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        float3 color;
        if (materialData.phaseTimeAndColor.x < 0.5f)
        {
            float red = inputValue.uv.x;
            if (inputValue.uv.y < 0.5f) red = 0.0f;
            float green = inputValue.uv.y;
            if (inputValue.uv.x < 0.5f) green = 0.0f;
            color = float3(red, green, materialData.phaseTimeAndColor.y);
        }
        else
        {
            const float3 normal = normalize(inputValue.viewNormal);
            const float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
            const float3 diffuseColor = materialData.phaseTimeAndColor.yzw;
            const float3 specularColor =
                materialData.specularAndShininess.xyz;
            const float shininess = materialData.specularAndShininess.w;
            const float diffuseA = max(dot(normal, float3(0.0f, 0.0f, 1.0f)), 0.0f);
            const float diffuseB = max(dot(normal, float3(0.0f, 0.0f, -1.0f)), 0.0f);
            const float3 secondLightColor =
                float3(1.0f, 0.6653873f, 0.6653873f);
            color = diffuseColor * (
                3.0f * diffuseA * 0.3183098862f * float3(1.0f) +
                4.5f * diffuseB * 0.3183098862f * secondLightColor);
            color += webglRttPhongLight(
                normal, viewDirection, float3(0.0f, 0.0f, 1.0f),
                specularColor, shininess,
                float3(1.0f), 3.0f);
            color += webglRttPhongLight(
                normal, viewDirection, float3(0.0f, 0.0f, -1.0f),
                specularColor, shininess, secondLightColor, 4.5f);
        }
        WebglRttSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(color), half(1.0f));
        return frameBuffer;
    }
};

/** Draws all twenty-five textured spheres through the main Scene RenderSet. */
class WebglRttMainSpheresPass final : public IRenderClass
{
public:
    /** Binds the unique main Scene Set and shared generated RTT. */
    constructor(
        RenderSet<WebglRttSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebglRttTextureResources> resources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies each sphere transform and reads both entity builtins. */
    WebglRttSceneOutput vertex(
        WebglRttVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglRttObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglRttInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        WebglRttSceneOutput outputValue;
        outputValue.position = mul(objectData.modelViewProjection, localPosition);
        outputValue.position.y = -outputValue.position.y;
        outputValue.position.z =
            (outputValue.position.z + outputValue.position.w) * 0.5f;
        outputValue.uv = inputValue.textureCoordinate.xy;
        outputValue.viewPosition = float3(0.0f);
        outputValue.viewNormal = float3(0.0f, 0.0f, 1.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Samples the linear RTT and performs the MeshBasic output conversion. */
    WebglRttSceneFrameBuffer fragment(WebglRttSceneOutput inputValue)
    {
        const float3 linearColor = float3(resources->rttTexture->sample(
            resources->rttSampler,
            float2(inputValue.uv.x, 1.0f - inputValue.uv.y)).xyz);
        // Keep the entity-local TextureComponent in the Scene ABI. The
        // generated RTT itself is a shared frame resource, as in Three.
        const float identity = float(sceneSet->textures->get(
            inputValue.entityID, 0u)->sampleLevel(
                resources->rttSampler, float2(0.5f), 0.0f).x) * 0.0f + 1.0f;
        const float3 srgb = float3(
            webglRttLinearToSrgb(linearColor.x),
            webglRttLinearToSrgb(linearColor.y),
            webglRttLinearToSrgb(linearColor.z)) * identity;
        WebglRttSceneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgb), half(1.0f));
        return frameBuffer;
    }
};

/** Copies the generated RTT to the display before sphere overlay. */
class WebglRttFullscreenCopyPass final : public IRenderClass
{
public:
    /** Binds the generated RTT without Scene geometry. */
    constructor(BindGroup<WebglRttTextureResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the standard fullscreen triangle. */
    WebglRttScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglRttScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Samples and display-encodes the linear RTT. */
    WebglRttOutputFrameBuffer fragment(WebglRttScreenOutput inputValue)
    {
        const float3 linearColor = float3(resources->rttTexture->sample(
            resources->rttSampler, inputValue.uv).xyz);
        WebglRttOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglRttLinearToSrgb(linearColor.x)),
            half(webglRttLinearToSrgb(linearColor.y)),
            half(webglRttLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns both logical Scene Sets and the ordered RTT/copy/overlay passes. */
class WebglRttRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglRttSceneRenderSet> rttSceneSet;
    [[Export]] RenderSet<WebglRttSceneRenderSet> mainSceneSet;
    Sampler rttSampler;
    BindGroup<WebglRttTextureResources> textureResources;
    RenderClass<WebglRttOffscreenScenePass> rttPass;
    RenderClass<WebglRttFullscreenCopyPass> copyPass;
    RenderClass<WebglRttMainSpheresPass> spherePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> rttTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> rttDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> outputDepth;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates both Scene Sets and immutable RTT sampler. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        rttSceneSet = device->createRenderSet<WebglRttSceneRenderSet>();
        mainSceneSet = device->createRenderSet<WebglRttSceneRenderSet>();
        rttSampler = device->createSampler({
            .label = "WebglRttLinearSampler",
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
        rttPass = device->createRenderClass<WebglRttOffscreenScenePass>(
            rttSceneSet);
    }

    /** Allocates ordinary single-sample RTT, display, and depth attachments. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        rttTexture = device->createTexture("WebglRttLinear", width, height, 1u);
        rttDepth = device->createTexture("WebglRttDepth", width, height, 1u);
        outputTexture = device->createTexture("WebglRttOutput", width, height, 1u);
        outputDepth = device->createTexture("WebglRttMainDepth", width, height, 1u);
        textureResources = device->createBindGroup<WebglRttTextureResources>(
            rttTexture->createView(), rttSampler);
        copyPass = device->createRenderClass<WebglRttFullscreenCopyPass>(
            textureResources);
        spherePass = device->createRenderClass<WebglRttMainSpheresPass>(
            mainSceneSet, textureResources);
    }

    /** Executes RTT, fullscreen copy, then load-preserving sphere overlay. */
    void render() override
    {
        rttSceneSet->update();
        mainSceneSet->update();
        WebglRttSceneFrameBuffer rttFrame;
        rttFrame.color = rttTexture->createView();
        rttFrame.color.loadOp = LoadOp::Clear;
        rttFrame.color.storeOp = StoreOp::Store;
        rttFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        rttFrame.depth = rttDepth->createView();
        rttFrame.depth.depthLoadOp = LoadOp::Clear;
        rttFrame.depth.depthStoreOp = StoreOp::Store;
        rttFrame.depth.depthClearValue = 1.0f;
        WebglRttOutputFrameBuffer copyFrame;
        copyFrame.color = outputTexture->createView();
        copyFrame.color.loadOp = LoadOp::Clear;
        copyFrame.color.storeOp = StoreOp::Store;
        WebglRttSceneFrameBuffer mainFrame;
        mainFrame.color = outputTexture->createView();
        mainFrame.color.loadOp = LoadOp::Load;
        mainFrame.color.storeOp = StoreOp::Store;
        mainFrame.depth = outputDepth->createView();
        mainFrame.depth.depthLoadOp = LoadOp::Clear;
        mainFrame.depth.depthStoreOp = StoreOp::Store;
        mainFrame.depth.depthClearValue = 1.0f;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglRttOffscreen", rttFrame, rttPass())
            ->renderPass("WebglRttCopy", copyFrame, copyPass(3u, 1u, 0u, 0u))
            ->renderPass("WebglRttSpheres", mainFrame, spherePass())
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const { return outputTexture; }

    /** Returns the configured output width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured output height. */
    uint getReadbackHeight() const { return height; }

    /** Releases both Scene Sets and all attachments. */
    void destroy() override
    {
        rttSceneSet->destroy();
        mainSceneSet->destroy();
        device->freeTexture(rttTexture);
        device->freeTexture(rttDepth);
        device->freeTexture(outputTexture);
        device->freeTexture(outputDepth);
    }
};

#endif
