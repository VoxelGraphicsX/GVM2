#ifndef GVM_THREE_WEBGL_READ_FLOAT_BUFFER_HPP
#define GVM_THREE_WEBGL_READ_FLOAT_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one packed plane or torus vertex for the float readback Scene. */
struct WebglReadFloatBufferVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 textureCoordinate [[Attribute2]];
};

/** Stores one entity's camera transform and view-space normal transform. */
struct WebglReadFloatBufferObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalTransform;
};

/** Stores the mandatory ordinary instance entry for one Scene entity. */
struct WebglReadFloatBufferInstanceData
{
    float4 translation;
};

/** Stores the shader kind, time, and Phong material values for one entity. */
struct WebglReadFloatBufferMaterialData
{
    float4 kindTimeAndColor;
    float4 specularAndShininess;
};

/** Defines the one RenderSet containing the plane and both torus entities. */
struct WebglReadFloatBufferSceneRenderSet : public IRenderSet
{
    /** Declares the complete consolidated Scene component ABI. */
    constructor(
        BufferComponent<WebglReadFloatBufferVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglReadFloatBufferObjectData> objects,
        BufferComponent<WebglReadFloatBufferInstanceData> instances,
        BufferComponent<WebglReadFloatBufferMaterialData> materials)
    {
    }
};

/** Binds the float RTT texture to the final fullscreen pass. */
struct WebglReadFloatBufferScreenResources final : public IBindGroup
{
    /** Declares the RGBA32Float Scene texture and its nearest sampler. */
    constructor(
        Texture2D<float4> sceneTexture [[Binding0]],
        Sampler sceneSampler [[Binding1]])
    {
    }
};

/** Carries Scene UV, view-space data, and entity identity to the fragment stage. */
struct WebglReadFloatBufferSceneOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    float3 viewNormal [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Carries fullscreen UV to the screen composite. */
struct WebglReadFloatBufferScreenOutput
{
    float4 position [[Position]];
    float2 textureCoordinate [[Attribute0]];
};

/** Defines the full-precision offscreen Scene target and depth attachment. */
struct WebglReadFloatBufferSceneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the final ordinary single-sample canvas target. */
struct WebglReadFloatBufferOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Encodes one linear channel with the Three r185 output transfer. */
float webglReadFloatBufferLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates the normalized Blinn-Phong specular term used by MeshPhongMaterial. */
float3 webglReadFloatBufferBlinnPhong(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 specularColor,
    float shininess,
    float lightIntensity,
    float3 lightColor)
{
    const float dotNormalLight = max(dot(normal, lightDirection), 0.0f);
    const float dotNormalHalf = max(
        dot(normal, normalize(lightDirection + viewDirection)), 0.0f);
    const float dotViewHalf = max(
        dot(viewDirection, normalize(lightDirection + viewDirection)), 0.0f);
    const float fresnel = exp2(
        (-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    const float3 fresnelColor = specularColor * (1.0f - fresnel) +
        float3(1.0f) * fresnel;
    return lightColor * fresnelColor * lightIntensity * dotNormalLight *
        0.25f * (shininess * 0.5f + 1.0f) *
        0.3183098861837907f * pow(dotNormalHalf, shininess);
}

/** Draws the three RTT entities through one RenderSet indexed-indirect pass. */
class WebglReadFloatBufferScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene Set and enables opaque depth-tested rendering. */
    constructor(
        RenderSet<WebglReadFloatBufferSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity transforms and maps the host top-down canvas convention. */
    WebglReadFloatBufferSceneOutput vertex(
        WebglReadFloatBufferVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglReadFloatBufferObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglReadFloatBufferInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 viewPosition = mul(
            objectData.modelView,
            inputValue.position + float4(instanceData.translation.xyz, 0.0f));
        float4 clipPosition = mul(objectData.modelViewProjection,
                                   inputValue.position +
                                       float4(instanceData.translation.xyz, 0.0f));
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglReadFloatBufferSceneOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.textureCoordinate = inputValue.textureCoordinate.xy;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = normalize(float3(mul(
            objectData.normalTransform,
            float4(inputValue.normal.xyz, 0.0f)).xyz));
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Writes the quadrant shader, or the two directional MeshPhong torus colors. */
    WebglReadFloatBufferSceneFrameBuffer fragment(
        WebglReadFloatBufferSceneOutput inputValue)
    {
        const WebglReadFloatBufferMaterialData materialData =
            sceneSet->materials->get(inputValue.entityID, 0u);
        const float kind = materialData.kindTimeAndColor.x;
        float3 linearColor;
        if (kind < 0.5f)
        {
            const float timeValue = materialData.kindTimeAndColor.y;
            float red = inputValue.textureCoordinate.x;
            if (inputValue.textureCoordinate.y < 0.5f) red = 0.0f;
            float green = inputValue.textureCoordinate.y;
            if (inputValue.textureCoordinate.x < 0.5f) green = 0.0f;
            linearColor = float3(red, green, timeValue);
        }
        else
        {
            const float3 normal = normalize(inputValue.viewNormal);
            // Three's lights_fragment_begin uses a constant +Z view direction
            // for an orthographic camera instead of a per-fragment position
            // vector.
            const float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
            const float3 lightDirectionA = normalize(float3(0.0f, 0.0f, 1.0f));
            const float3 lightDirectionB = normalize(float3(0.0f, 0.0f, -1.0f));
            const float3 diffuseColor = materialData.kindTimeAndColor.yzw;
            const float3 specularColor = materialData.specularAndShininess.xyz;
            const float shininess = materialData.specularAndShininess.w;
            const float diffuseA = max(dot(normal, lightDirectionA), 0.0f);
            const float diffuseB = max(dot(normal, lightDirectionB), 0.0f);
            const float3 secondLightColor =
                float3(1.0f, 0.6653873f, 0.6653873f);
            linearColor = diffuseColor * (
                3.0f * diffuseA * float3(1.0f) *
                    0.3183098861837907f +
                4.5f * diffuseB * secondLightColor *
                    0.3183098861837907f);
            linearColor += webglReadFloatBufferBlinnPhong(
                normal, viewDirection, lightDirectionA,
                specularColor, shininess, 3.0f, float3(1.0f));
            linearColor += webglReadFloatBufferBlinnPhong(
                normal, viewDirection, lightDirectionB,
                specularColor, shininess, 4.5f,
                secondLightColor);
        }
        WebglReadFloatBufferSceneFrameBuffer frameBuffer;
        frameBuffer.color = float4(linearColor, 1.0f);
        return frameBuffer;
    }
};

/** Composites the full-precision RTT into the final sRGB canvas. */
class WebglReadFloatBufferScreenPass final : public IRenderClass
{
public:
    /** Binds the float RTT and disables depth and culling. */
    constructor(
        BindGroup<WebglReadFloatBufferScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits one fullscreen triangle with the canonical UV orientation. */
    WebglReadFloatBufferScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglReadFloatBufferScreenOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.textureCoordinate = uv;
        return outputValue;
    }

    /** Samples the linear RTT and applies the browser output transfer. */
    WebglReadFloatBufferOutputFrameBuffer fragment(
        WebglReadFloatBufferScreenOutput inputValue)
    {
        const float4 source = resources->sceneTexture->sample(
            resources->sceneSampler,
            inputValue.textureCoordinate);
        WebglReadFloatBufferOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglReadFloatBufferLinearToSrgb(source.x)),
            half(webglReadFloatBufferLinearToSrgb(source.y)),
            half(webglReadFloatBufferLinearToSrgb(source.z)),
            half(source.w));
        return frameBuffer;
    }
};

/** Owns the one Scene RenderSet, RGBA32Float RTT, and fullscreen output pass. */
class WebglReadFloatBufferRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglReadFloatBufferSceneRenderSet> sceneSet;
    Sampler sceneSampler;
    BindGroup<WebglReadFloatBufferScreenResources> screenResources;
    RenderClass<WebglReadFloatBufferScenePass> scenePass;
    RenderClass<WebglReadFloatBufferScreenPass> screenPass;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint width = 800u;
    uint height = 500u;

public:
    /** Creates the RenderSet, sampler, and two generated DSL RenderClasses. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglReadFloatBufferSceneRenderSet>();
        sceneSampler = device->createSampler({
            .label = "WebglReadFloatBufferSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        scenePass = device->createRenderClass<WebglReadFloatBufferScenePass>(
            sceneSet);
    }

    /** Allocates the full-precision RTT, depth target, and final RGBA8 target. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        width = inWidth;
        height = inHeight;
        sceneTexture = device->createTexture(
            "WebglReadFloatBufferRGBA32Float", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglReadFloatBufferDepth32", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglReadFloatBufferOutput", width, height, 1u);
        screenResources = device->createBindGroup<WebglReadFloatBufferScreenResources>(
            sceneTexture->createView(), sceneSampler);
        screenPass = device->createRenderClass<WebglReadFloatBufferScreenPass>(
            screenResources);
    }

    /** Submits the Scene RenderSet followed by the fullscreen float composite. */
    void render() override
    {
        sceneSet->update();
        WebglReadFloatBufferSceneFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        sceneFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglReadFloatBufferOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        outputFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglReadFloatBufferScene",
                sceneFrame,
                scenePass())
            ->renderPass(
                "WebglReadFloatBufferScreen",
                outputFrame,
                screenPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the RGBA32Float Scene target for semantic CPU readback. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getFloatReadbackTextureHandle() const
    {
        return sceneTexture;
    }

    /** Returns the final RGBA8 target used by the host capture contract. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured capture width. */
    uint getReadbackWidth() const { return width; }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const { return height; }

    /** Releases all private RenderSet, texture, and generated pass resources. */
    void destroy() override
    {
        device->freeTexture(sceneTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
    }
};

#endif
