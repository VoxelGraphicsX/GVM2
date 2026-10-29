#ifndef GVM_THREE_WEBGPU_TEXTUREGATHER_HPP
#define GVM_THREE_WEBGPU_TEXTUREGATHER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one source-box vertex for both independent source Scenes. */
struct WebgpuTexturegatherVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
};

/** Stores the frozen source-camera transform. */
struct WebgpuTexturegatherObjectData
{
    float4x4 modelViewProjection;
    float4x4 model;
};

/** Stores one mandatory instance record for RenderSet indirect drawing. */
struct WebgpuTexturegatherInstanceData
{
    float4 reserved;
};

/** Stores the red Standard-material source parameters. */
struct WebgpuTexturegatherMaterialData
{
    float4 baseColorAndAmbient;
};

/** Defines the source geometry ABI shared by two distinct runtime Sets. */
struct WebgpuTexturegatherSourceRenderSet : public IRenderSet
{
    /** Declares packed source geometry and required entity components. */
    constructor(BufferComponent<WebgpuTexturegatherVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<WebgpuTexturegatherObjectData> objects,
                BufferComponent<WebgpuTexturegatherInstanceData> instances,
                BufferComponent<WebgpuTexturegatherMaterialData> materials)
    {
    }
};

/** Carries one source-box position and normal to fragment shading. */
struct WebgpuTexturegatherSourceVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    uint entityID [[Attribute1]];
};

/** Defines the source color and depth render target attachments. */
struct WebgpuTexturegatherSourceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Binds one source color/depth pair and both gather samplers. */
struct WebgpuTexturegatherMainResources final : public IBindGroup
{
    /** Declares sampled source attachments and immutable sampler behavior. */
    constructor(Texture2D<float4> sourceColor [[Binding0]],
                Texture2D<TextureFormat::Depth32Float> sourceDepth [[Binding1]],
                Sampler colorSampler [[Binding2]],
                Sampler depthSampler [[Binding3]])
    {
    }
};

/** Carries fixed capture coordinates through one fullscreen panel pass. */
struct WebgpuTexturegatherMainVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the composed single-sample RGBA8 output attachment. */
struct WebgpuTexturegatherMainFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds one source mip for deterministic linear downsampling. */
struct WebgpuTexturegatherMipResources final : public IBindGroup
{
    /** Declares the previous color mip and its linear clamp sampler. */
    constructor(Texture2D<float4> sourceColor [[Binding0]],
                Sampler sourceSampler [[Binding1]])
    {
    }
};

/** Converts one linear-light channel through the r185 sRGB output transfer. */
float webgpuTexturegatherLinearToSrgb(float value)
{
    const float bounded = max(value, 0.0f);
    return bounded <= 0.0031308f
        ? bounded * 12.92f
        : pow(bounded, 0.41666f) * 1.055f - 0.055f;
}

/** Resolves the shared source-box vertex from one logical source Scene Set. */
WebgpuTexturegatherSourceVertexOutput webgpuTexturegatherSourceVertex(
    IN RenderSet<WebgpuTexturegatherSourceRenderSet> sourceSet,
    WebgpuTexturegatherVertex inputValue,
    uint renderEntityID)
{
    const WebgpuTexturegatherObjectData objectData =
        sourceSet->objects->get(renderEntityID, 0u);
    WebgpuTexturegatherSourceVertexOutput outputValue;
    outputValue.position = mul(objectData.modelViewProjection,
                               inputValue.position);
    outputValue.normal = mul(objectData.model,
                             float4(inputValue.normal.xyz, 0.0f)).xyz;
    outputValue.entityID = renderEntityID;
    return outputValue;
}

/** Shades one red Standard box source with the frozen r185 light state. */
half4 webgpuTexturegatherSourceColor(
    IN RenderSet<WebgpuTexturegatherSourceRenderSet> sourceSet,
    WebgpuTexturegatherSourceVertexOutput inputValue)
{
    const WebgpuTexturegatherMaterialData materialData =
        sourceSet->materials->get(inputValue.entityID, 0u);
    const float3 normal = normalize(inputValue.normal);
    const float3 lightDirection = normalize(float3(1.0f, 1.0f, 0.0f));
    const float irradiance =
        materialData.baseColorAndAmbient.w +
        max(dot(normal, lightDirection), 0.0f);
    const float3 linearColor = materialData.baseColorAndAmbient.xyz *
                               irradiance * 0.31830988618f;
    return half4(
        webgpuTexturegatherLinearToSrgb(linearColor.x),
        webgpuTexturegatherLinearToSrgb(linearColor.y),
        webgpuTexturegatherLinearToSrgb(linearColor.z), 1.0f);
}

/** Renders the WebGPU-labeled independent source Scene through its unique Set. */
class WebgpuTexturegatherWebgpuSourcePass final : public IRenderClass
{
public:
    /** Binds exactly the WebGPU-labeled source Scene Set. */
    constructor(RenderSet<WebgpuTexturegatherSourceRenderSet> sourceSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one source-box vertex from RenderSet storage. */
    WebgpuTexturegatherSourceVertexOutput vertex(
        WebgpuTexturegatherVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        return webgpuTexturegatherSourceVertex(
            sourceSet, inputValue, renderEntityID);
    }

    /** Shades one source-box fragment in linear working space. */
    WebgpuTexturegatherSourceFrameBuffer fragment(
        WebgpuTexturegatherSourceVertexOutput inputValue)
    {
        WebgpuTexturegatherSourceFrameBuffer frameBuffer;
        frameBuffer.color = webgpuTexturegatherSourceColor(
            sourceSet, inputValue);
        return frameBuffer;
    }
};

/** Renders the WebGL-labeled independent source Scene through its unique Set. */
class WebgpuTexturegatherWebglSourcePass final : public IRenderClass
{
public:
    /** Binds exactly the WebGL-labeled source Scene Set. */
    constructor(RenderSet<WebgpuTexturegatherSourceRenderSet> sourceSet [[Slot0]])
    {
        setCullMode(CullMode::Back);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one source-box vertex from RenderSet storage. */
    WebgpuTexturegatherSourceVertexOutput vertex(
        WebgpuTexturegatherVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]])
    {
        return webgpuTexturegatherSourceVertex(
            sourceSet, inputValue, renderEntityID);
    }

    /** Shades one source-box fragment in linear working space. */
    WebgpuTexturegatherSourceFrameBuffer fragment(
        WebgpuTexturegatherSourceVertexOutput inputValue)
    {
        WebgpuTexturegatherSourceFrameBuffer frameBuffer;
        frameBuffer.color = webgpuTexturegatherSourceColor(
            sourceSet, inputValue);
        return frameBuffer;
    }
};

/** Emits the common fullscreen triangle for one gather panel. */
WebgpuTexturegatherMainVertexOutput webgpuTexturegatherMainVertex(
    uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebgpuTexturegatherMainVertexOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Downsamples one authored color mip without automatic mip generation. */
class WebgpuTexturegatherMipPass final : public IRenderClass
{
public:
    /** Binds exactly one previous-mip view. */
    constructor(BindGroup<WebgpuTexturegatherMipResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle with normalized coordinates. */
    WebgpuTexturegatherMainVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuTexturegatherMainVertex(vertexID);
    }

    /** Writes one box-filtered sample into the next mip. */
    WebgpuTexturegatherMainFrameBuffer fragment(
        WebgpuTexturegatherMainVertexOutput inputValue)
    {
        WebgpuTexturegatherMainFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->sourceColor->sampleLevel(
            resources->sourceSampler, inputValue.uv, 0.0f));
        return frameBuffer;
    }
};

/** Evaluates one 400x500 panel using color and depth-comparison gather. */
half4 webgpuTexturegatherPanel(
    IN BindGroup<WebgpuTexturegatherMainResources> resources,
    float2 pixel,
    float background)
{
    if (pixel.x < 75.0f || pixel.x >= 325.0f ||
        pixel.y < 125.0f || pixel.y >= 375.0f)
    {
        return half4(half(background), half(background),
                     half(background), half(1.0f));
    }
    const float2 uv = float2(
        (pixel.x - 75.0f) / 250.0f,
        1.0f - (pixel.y - 125.0f) / 250.0f);
    if (pixel.y < 250.0f)
    {
        return half4(resources->sourceColor->gatherRed(
            resources->colorSampler, uv * 10.0f, int2(0, 7)));
    }
    const float4 gatheredDepth = float4(resources->sourceDepth->gatherRed(
        resources->depthSampler, uv, int2(0, 7)));
    return half4(
        gatheredDepth.x >= 1.0f ? half(1.0f) : half(0.0f),
        gatheredDepth.y >= 1.0f ? half(1.0f) : half(0.0f),
        gatheredDepth.z >= 1.0f ? half(1.0f) : half(0.0f),
        gatheredDepth.w >= 1.0f ? half(1.0f) : half(0.0f));
}

/** Draws the left WebGPU-labeled gather panel. */
class WebgpuTexturegatherWebgpuMainPass final : public IRenderClass
{
public:
    /** Binds the WebGPU source attachments without Scene geometry. */
    constructor(BindGroup<WebgpuTexturegatherMainResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuTexturegatherMainVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuTexturegatherMainVertex(vertexID);
    }

    /** Writes only the left half of the deterministic composite. */
    WebgpuTexturegatherMainFrameBuffer fragment(
        WebgpuTexturegatherMainVertexOutput inputValue)
    {
        if (inputValue.position.x >= 400.0f) discard_fragment();
        WebgpuTexturegatherMainFrameBuffer frameBuffer;
        frameBuffer.color = webgpuTexturegatherPanel(
            resources, inputValue.position.xy, 49.0f / 255.0f);
        return frameBuffer;
    }
};

/** Draws the right WebGL-labeled gather panel. */
class WebgpuTexturegatherWebglMainPass final : public IRenderClass
{
public:
    /** Binds the WebGL source attachments without Scene geometry. */
    constructor(BindGroup<WebgpuTexturegatherMainResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuTexturegatherMainVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webgpuTexturegatherMainVertex(vertexID);
    }

    /** Writes only the right half of the deterministic composite. */
    WebgpuTexturegatherMainFrameBuffer fragment(
        WebgpuTexturegatherMainVertexOutput inputValue)
    {
        if (inputValue.position.x < 400.0f) discard_fragment();
        WebgpuTexturegatherMainFrameBuffer frameBuffer;
        frameBuffer.color = webgpuTexturegatherPanel(
            resources,
            float2(inputValue.position.x - 400.0f, inputValue.position.y),
            33.0f / 255.0f);
        return frameBuffer;
    }
};

/** Owns two source Scene Sets and the deterministic two-panel gather output. */
class WebgpuTexturegatherRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuTexturegatherSourceRenderSet> webgpuSourceSet;
    [[Export]] RenderSet<WebgpuTexturegatherSourceRenderSet> webglSourceSet;
    RenderClass<WebgpuTexturegatherWebgpuSourcePass> webgpuSourcePass;
    RenderClass<WebgpuTexturegatherWebglSourcePass> webglSourcePass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip1Pass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip2Pass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip3Pass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip4Pass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip5Pass;
    RenderClass<WebgpuTexturegatherMipPass> webgpuMip6Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip1Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip2Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip3Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip4Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip5Pass;
    RenderClass<WebgpuTexturegatherMipPass> webglMip6Pass;
    RenderClass<WebgpuTexturegatherWebgpuMainPass> webgpuMainPass;
    RenderClass<WebgpuTexturegatherWebglMainPass> webglMainPass;
    Sampler colorSampler;
    Sampler depthSampler;
    Sampler mipSampler;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip1Resources;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip2Resources;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip3Resources;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip4Resources;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip5Resources;
    BindGroup<WebgpuTexturegatherMipResources> webgpuMip6Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip1Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip2Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip3Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip4Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip5Resources;
    BindGroup<WebgpuTexturegatherMipResources> webglMip6Resources;
    BindGroup<WebgpuTexturegatherMainResources> webgpuMainResources;
    BindGroup<WebgpuTexturegatherMainResources> webglMainResources;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuSourceColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglSourceColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip1Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip2Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip3Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip4Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip5Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuMip6Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip1Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip2Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip3Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip4Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip5Color;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglMip6Color;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webgpuSourceDepth;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> webglSourceDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;

public:
    /** Creates both unique source Sets and all single-sample gather resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        webgpuSourceSet = device->createRenderSet<WebgpuTexturegatherSourceRenderSet>();
        webglSourceSet = device->createRenderSet<WebgpuTexturegatherSourceRenderSet>();
        webgpuSourceColor = device->createTexture("WebgpuTexturegatherWebgpuColor", 100u, 100u, 1u);
        webglSourceColor = device->createTexture("WebgpuTexturegatherWebglColor", 100u, 100u, 1u);
        webgpuMip1Color = device->createTexture("WebgpuTexturegatherWebgpuMip1", 50u, 50u, 1u);
        webgpuMip2Color = device->createTexture("WebgpuTexturegatherWebgpuMip2", 25u, 25u, 1u);
        webgpuMip3Color = device->createTexture("WebgpuTexturegatherWebgpuMip3", 12u, 12u, 1u);
        webgpuMip4Color = device->createTexture("WebgpuTexturegatherWebgpuMip4", 6u, 6u, 1u);
        webgpuMip5Color = device->createTexture("WebgpuTexturegatherWebgpuMip5", 3u, 3u, 1u);
        webgpuMip6Color = device->createTexture("WebgpuTexturegatherWebgpuMip6", 1u, 1u, 1u);
        webglMip1Color = device->createTexture("WebgpuTexturegatherWebglMip1", 50u, 50u, 1u);
        webglMip2Color = device->createTexture("WebgpuTexturegatherWebglMip2", 25u, 25u, 1u);
        webglMip3Color = device->createTexture("WebgpuTexturegatherWebglMip3", 12u, 12u, 1u);
        webglMip4Color = device->createTexture("WebgpuTexturegatherWebglMip4", 6u, 6u, 1u);
        webglMip5Color = device->createTexture("WebgpuTexturegatherWebglMip5", 3u, 3u, 1u);
        webglMip6Color = device->createTexture("WebgpuTexturegatherWebglMip6", 1u, 1u, 1u);
        webgpuSourceDepth = device->createTexture("WebgpuTexturegatherWebgpuDepth", 100u, 100u, 1u);
        webglSourceDepth = device->createTexture("WebgpuTexturegatherWebglDepth", 100u, 100u, 1u);
        colorSampler = device->createSampler({
            .label = "WebgpuTexturegatherRepeatSampler",
            .addressModeU = AddressMode::Repeat,
            .addressModeV = AddressMode::Repeat,
            .addressModeW = AddressMode::Repeat,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        depthSampler = device->createSampler({
            .label = "WebgpuTexturegatherDepthSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        mipSampler = device->createSampler({
            .label = "WebgpuTexturegatherMipSampler",
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
        webgpuSourcePass = device->createRenderClass<WebgpuTexturegatherWebgpuSourcePass>(webgpuSourceSet);
        webglSourcePass = device->createRenderClass<WebgpuTexturegatherWebglSourcePass>(webglSourceSet);
        webgpuMip1Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuSourceColor->createView(), mipSampler);
        webgpuMip2Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuMip1Color->createView(), mipSampler);
        webgpuMip3Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuMip2Color->createView(), mipSampler);
        webgpuMip4Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuMip3Color->createView(), mipSampler);
        webgpuMip5Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuMip4Color->createView(), mipSampler);
        webgpuMip6Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webgpuMip5Color->createView(), mipSampler);
        webglMip1Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglSourceColor->createView(), mipSampler);
        webglMip2Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglMip1Color->createView(), mipSampler);
        webglMip3Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglMip2Color->createView(), mipSampler);
        webglMip4Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglMip3Color->createView(), mipSampler);
        webglMip5Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglMip4Color->createView(), mipSampler);
        webglMip6Resources = device->createBindGroup<WebgpuTexturegatherMipResources>(webglMip5Color->createView(), mipSampler);
        webgpuMip1Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip1Resources);
        webgpuMip2Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip2Resources);
        webgpuMip3Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip3Resources);
        webgpuMip4Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip4Resources);
        webgpuMip5Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip5Resources);
        webgpuMip6Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webgpuMip6Resources);
        webglMip1Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip1Resources);
        webglMip2Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip2Resources);
        webglMip3Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip3Resources);
        webglMip4Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip4Resources);
        webglMip5Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip5Resources);
        webglMip6Pass = device->createRenderClass<WebgpuTexturegatherMipPass>(webglMip6Resources);
        webgpuMainResources = device->createBindGroup<WebgpuTexturegatherMainResources>(
            webgpuSourceColor->createView(), webgpuSourceDepth->createView(),
            colorSampler, depthSampler);
        webglMainResources = device->createBindGroup<WebgpuTexturegatherMainResources>(
            webglSourceColor->createView(), webglSourceDepth->createView(),
            colorSampler, depthSampler);
        webgpuMainPass = device->createRenderClass<WebgpuTexturegatherWebgpuMainPass>(webgpuMainResources);
        webglMainPass = device->createRenderClass<WebgpuTexturegatherWebglMainPass>(webglMainResources);
    }

    /** Allocates the fixed 800x500 single-sample composite output. */
    void configureOutput(uint width, uint height)
    {
        outputColor = device->createTexture(
            "WebgpuTexturegatherOutput", width, height, 1u);
    }

    /** Updates both source Sets, gathers both panels, and presents RGBA8. */
    void render() override
    {
        webgpuSourceSet->update();
        webglSourceSet->update();
        WebgpuTexturegatherSourceFrameBuffer webgpuSourceFrame;
        webgpuSourceFrame.color = webgpuSourceColor->createView();
        webgpuSourceFrame.color.loadOp = LoadOp::Clear;
        webgpuSourceFrame.color.storeOp = StoreOp::Store;
        webgpuSourceFrame.color.clearValue = {0.5019608f, 0.5019608f, 0.5019608f, 1.0f};
        webgpuSourceFrame.depth = webgpuSourceDepth->createView();
        webgpuSourceFrame.depth.depthLoadOp = LoadOp::Clear;
        webgpuSourceFrame.depth.depthStoreOp = StoreOp::Store;
        webgpuSourceFrame.depth.depthClearValue = 1.0f;
        WebgpuTexturegatherSourceFrameBuffer webglSourceFrame;
        webglSourceFrame.color = webglSourceColor->createView();
        webglSourceFrame.color.loadOp = LoadOp::Clear;
        webglSourceFrame.color.storeOp = StoreOp::Store;
        webglSourceFrame.color.clearValue = {0.5019608f, 0.5019608f, 0.5019608f, 1.0f};
        webglSourceFrame.depth = webglSourceDepth->createView();
        webglSourceFrame.depth.depthLoadOp = LoadOp::Clear;
        webglSourceFrame.depth.depthStoreOp = StoreOp::Store;
        webglSourceFrame.depth.depthClearValue = 1.0f;
        WebgpuTexturegatherMainFrameBuffer webgpuMip1Frame;
        WebgpuTexturegatherMainFrameBuffer webgpuMip2Frame;
        WebgpuTexturegatherMainFrameBuffer webgpuMip3Frame;
        WebgpuTexturegatherMainFrameBuffer webgpuMip4Frame;
        WebgpuTexturegatherMainFrameBuffer webgpuMip5Frame;
        WebgpuTexturegatherMainFrameBuffer webgpuMip6Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip1Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip2Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip3Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip4Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip5Frame;
        WebgpuTexturegatherMainFrameBuffer webglMip6Frame;
        webgpuMip1Frame.color = webgpuMip1Color->createView();
        webgpuMip2Frame.color = webgpuMip2Color->createView();
        webgpuMip3Frame.color = webgpuMip3Color->createView();
        webgpuMip4Frame.color = webgpuMip4Color->createView();
        webgpuMip5Frame.color = webgpuMip5Color->createView();
        webgpuMip6Frame.color = webgpuMip6Color->createView();
        webglMip1Frame.color = webglMip1Color->createView();
        webglMip2Frame.color = webglMip2Color->createView();
        webglMip3Frame.color = webglMip3Color->createView();
        webglMip4Frame.color = webglMip4Color->createView();
        webglMip5Frame.color = webglMip5Color->createView();
        webglMip6Frame.color = webglMip6Color->createView();
        WebgpuTexturegatherMainFrameBuffer clearOutputFrame;
        clearOutputFrame.color = outputColor->createView();
        clearOutputFrame.color.loadOp = LoadOp::Clear;
        clearOutputFrame.color.storeOp = StoreOp::Store;
        clearOutputFrame.color.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuTexturegatherMainFrameBuffer loadOutputFrame;
        loadOutputFrame.color = outputColor->createView();
        loadOutputFrame.color.loadOp = LoadOp::Load;
        loadOutputFrame.color.storeOp = StoreOp::Store;
        auto swapchainTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuTexturegatherWebgpuSource", webgpuSourceFrame,
                         webgpuSourcePass())
            ->renderPass("WebgpuTexturegatherWebglSource", webglSourceFrame,
                         webglSourcePass())
            ->renderPass("WebgpuTexturegatherWebgpuMip1", webgpuMip1Frame,
                         webgpuMip1Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMip2", webgpuMip2Frame,
                         webgpuMip2Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMip3", webgpuMip3Frame,
                         webgpuMip3Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMip4", webgpuMip4Frame,
                         webgpuMip4Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMip5", webgpuMip5Frame,
                         webgpuMip5Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMip6", webgpuMip6Frame,
                         webgpuMip6Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip1", webglMip1Frame,
                         webglMip1Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip2", webglMip2Frame,
                         webglMip2Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip3", webglMip3Frame,
                         webglMip3Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip4", webglMip4Frame,
                         webglMip4Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip5", webglMip5Frame,
                         webglMip5Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMip6", webglMip6Frame,
                         webglMip6Pass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebgpuMain", clearOutputFrame,
                         webgpuMainPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuTexturegatherWebglMain", loadOutputFrame,
                         webglMainPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(swapchainTexture, outputColor,
                                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture for host readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the fixed capture width. */
    uint getReadbackWidth() const { return 800u; }

    /** Returns the fixed capture height. */
    uint getReadbackHeight() const { return 500u; }

    /** Releases both source Sets and every private attachment. */
    void destroy() override
    {
        webgpuSourceSet->destroy();
        webglSourceSet->destroy();
        device->freeTexture(webgpuSourceColor);
        device->freeTexture(webglSourceColor);
        device->freeTexture(webgpuMip1Color);
        device->freeTexture(webgpuMip2Color);
        device->freeTexture(webgpuMip3Color);
        device->freeTexture(webgpuMip4Color);
        device->freeTexture(webgpuMip5Color);
        device->freeTexture(webgpuMip6Color);
        device->freeTexture(webglMip1Color);
        device->freeTexture(webglMip2Color);
        device->freeTexture(webglMip3Color);
        device->freeTexture(webglMip4Color);
        device->freeTexture(webglMip5Color);
        device->freeTexture(webglMip6Color);
        device->freeTexture(webgpuSourceDepth);
        device->freeTexture(webglSourceDepth);
        device->freeTexture(outputColor);
    }
};

#endif
