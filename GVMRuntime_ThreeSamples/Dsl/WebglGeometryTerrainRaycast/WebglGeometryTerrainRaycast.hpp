#ifndef GVM_THREE_WEBGL_GEOMETRY_TERRAIN_RAYCAST_HPP
#define GVM_THREE_WEBGL_GEOMETRY_TERRAIN_RAYCAST_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglGeometryTerrainRaycastWidth = 256u;
static const uint WebglGeometryTerrainRaycastDepth = 256u;
static const uint WebglGeometryTerrainRaycastTextureExtent = 1024u;
static const uint WebglGeometryTerrainRaycastMaxTextures = 2u;

/** Stores the shared terrain and normal-cone attribute union. */
struct WebglGeometryTerrainRaycastVertex
{
    float4 position [[Attribute0]];
    float4 normal [[Attribute1]];
    float4 uv [[Attribute2]];
};

/** Stores one entity transform and its projection. */
struct WebglGeometryTerrainRaycastObjectData
{
    float4x4 modelView;
    float4x4 projection;
};

/** Stores the mandatory non-instanced component record. */
struct WebglGeometryTerrainRaycastInstanceData
{
    float4 reserved;
};

/** Selects the textured terrain or normal-cone material. */
struct WebglGeometryTerrainRaycastMaterialData
{
    float4 phaseAndReserved;
};

/** Defines the only RenderSet owned by the terrain raycast Scene. */
struct WebglGeometryTerrainRaycastSceneRenderSet : public IRenderSet
{
    /** Declares packed geometry, entity data, and the generated terrain texture. */
    constructor(
        BufferComponent<WebglGeometryTerrainRaycastVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglGeometryTerrainRaycastObjectData> objects,
        BufferComponent<WebglGeometryTerrainRaycastInstanceData> instances,
        BufferComponent<WebglGeometryTerrainRaycastMaterialData> materials,
        (TextureComponent<half4, WebglGeometryTerrainRaycastMaxTextures>
            textures))
    {
    }
};

/** Binds deterministic height and grain data to the private texture Compute pass. */
struct WebglGeometryTerrainRaycastComputeResources final : public IBindGroup
{
    /** Declares the source buffers and writable ordinary single-sample texture. */
    constructor(
        StructuredBuffer<uint> heights [[Binding0]],
        StructuredBuffer<uint> grain [[Binding1]],
        RWTexture2D<TextureFormat::RGBA8Unorm>
            generatedTexture [[Binding2]])
    {
    }
};

/** Binds the immutable sampler used by the Scene TextureComponent. */
struct WebglGeometryTerrainRaycastSamplerResources final : public IBindGroup
{
    /** Declares one clamped linear sampler. */
    constructor(Sampler terrainSampler [[Binding0]])
    {
    }
};

/** Carries Scene material inputs and entity identity to the fragment stage. */
struct WebglGeometryTerrainRaycastVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float2 uv [[Attribute1]];
    uint entityID [[Attribute2]];
    float phase [[Attribute3]];
};

/** Defines the ordinary single-sample Scene attachments. */
struct WebglGeometryTerrainRaycastFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces one Uint8-clamped source texel before Canvas scaling. */
float3 webglGeometryTerrainRaycastSourceColor(
    float leftHeight,
    float rightHeight,
    float upperHeight,
    float lowerHeight,
    float centerHeight,
    float valid)
{
    if (valid < 0.5f)
    {
        return float3(0.0f);
    }
    const float3 gradient = normalize(float3(
        leftHeight - rightHeight,
        2.0f,
        upperHeight - lowerHeight));
    const float shade = dot(gradient, normalize(float3(1.0f)));
    const float heightScale = 0.5f + centerHeight * 0.007f;
    return floor(
        clamp(
            (float3(96.0f, 32.0f, 0.0f) +
             shade * float3(128.0f, 96.0f, 96.0f)) *
                heightScale,
            float3(0.0f),
            float3(255.0f)) +
        float3(0.5f));
}

/** Generates the exact r185 scaled terrain image in private DSL Compute. */
class [[LocalWorkGroupSize(8, 8, 1)]]
    WebglGeometryTerrainRaycastTexturePass final : public IComputeClass
{
public:
    /** Binds the existing frozen Compute resource contract. */
    constructor(
        BindGroup<WebglGeometryTerrainRaycastComputeResources>
            resources [[Slot0]])
    {
    }

private:
    /** Shades one 4x bilinear Canvas output texel and adds locked grain. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint x = dispatchThreadID.x;
        const uint y = dispatchThreadID.y;
        if (x >= WebglGeometryTerrainRaycastTextureExtent ||
            y >= WebglGeometryTerrainRaycastTextureExtent)
        {
            return;
        }
        const float2 sourceCoordinate =
            (float2(float(x), float(y)) + float2(0.5f)) * 0.25f -
            float2(0.5f);
        const float2 lowerCoordinate = floor(sourceCoordinate);
        const float2 interpolation = sourceCoordinate - lowerCoordinate;
        const int lowerX = int(lowerCoordinate.x);
        const int lowerY = int(lowerCoordinate.y);
        float3 sourceColors[4u];
        for (uint corner = 0u; corner < 4u; ++corner)
        {
            const uint sourceX = uint(clamp(
                lowerX + int(corner & 1u),
                0,
                int(WebglGeometryTerrainRaycastWidth - 1u)));
            const uint sourceY = uint(clamp(
                lowerY + int(corner >> 1u),
                0,
                int(WebglGeometryTerrainRaycastDepth - 1u)));
            const uint sourceIndex =
                sourceY * WebglGeometryTerrainRaycastWidth + sourceX;
            const float valid =
                sourceIndex >= WebglGeometryTerrainRaycastWidth * 2u &&
                sourceIndex + WebglGeometryTerrainRaycastWidth * 2u <
                    WebglGeometryTerrainRaycastWidth *
                        WebglGeometryTerrainRaycastDepth
                    ? 1.0f
                    : 0.0f;
            sourceColors[corner] = webglGeometryTerrainRaycastSourceColor(
                float(resources->heights[sourceIndex >= 2u
                    ? sourceIndex - 2u : 0u]),
                float(resources->heights[min(
                    sourceIndex + 2u,
                    WebglGeometryTerrainRaycastWidth *
                            WebglGeometryTerrainRaycastDepth -
                        1u)]),
                float(resources->heights[sourceIndex >=
                        WebglGeometryTerrainRaycastWidth * 2u
                    ? sourceIndex -
                        WebglGeometryTerrainRaycastWidth * 2u
                    : 0u]),
                float(resources->heights[min(
                    sourceIndex +
                        WebglGeometryTerrainRaycastWidth * 2u,
                    WebglGeometryTerrainRaycastWidth *
                            WebglGeometryTerrainRaycastDepth -
                        1u)]),
                float(resources->heights[sourceIndex]),
                valid);
        }
        const float3 upperColor = lerp(
            sourceColors[0u], sourceColors[1u], interpolation.x);
        const float3 lowerColor = lerp(
            sourceColors[2u], sourceColors[3u], interpolation.x);
        const float3 scaledColor = floor(
            lerp(upperColor, lowerColor, interpolation.y) + float3(0.5f));
        const uint grainValue = resources->grain[
            y * WebglGeometryTerrainRaycastTextureExtent + x];
        const float3 encoded = clamp(
            scaledColor + float3(float(grainValue)),
            float3(0.0f),
            float3(255.0f)) /
            255.0f;
        resources->generatedTexture->write(
            uint2(x, y),
            half4(half3(encoded), half(1.0f)));
    }
};

/** Converts one linear-light channel for the browser canvas. */
float webglGeometryTerrainRaycastLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws both Scene entities through the unique terrain RenderSet. */
class WebglGeometryTerrainRaycastMainPass final : public IRenderClass
{
public:
    /** Configures the opaque ordinary single-sample Scene state. */
    constructor(
        RenderSet<WebglGeometryTerrainRaycastSceneRenderSet>
            sceneSet [[Slot0]],
        BindGroup<WebglGeometryTerrainRaycastSamplerResources>
            samplerResources [[Slot1]])
    {
        setCullMode(CullMode::Front);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies per-entity transforms and reads both RenderSet identity builtins. */
    WebglGeometryTerrainRaycastVertexOutput vertex(
        WebglGeometryTerrainRaycastVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglGeometryTerrainRaycastObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglGeometryTerrainRaycastInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID,
                renderEntityInstanceID);
        const WebglGeometryTerrainRaycastMaterialData materialData =
            sceneSet->materials->get(renderEntityID, 0u);
        const float4 localPosition = inputValue.position +
            float4(instanceData.reserved.xyz, 0.0f);
        const float4 viewPosition = mul(objectData.modelView, localPosition);
        float4 clipPosition = mul(objectData.projection, viewPosition);
        const float4 viewNormal = mul(
            objectData.modelView,
            float4(inputValue.normal.xyz, 0.0f));
        WebglGeometryTerrainRaycastVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.viewNormal = normalize(float3(viewNormal.xyz));
        outputValue.uv = inputValue.uv.xy;
        outputValue.entityID = renderEntityID;
        outputValue.phase = materialData.phaseAndReserved.x;
        return outputValue;
    }

    /** Samples the terrain TextureComponent or evaluates MeshNormal color. */
    WebglGeometryTerrainRaycastFrameBuffer fragment(
        WebglGeometryTerrainRaycastVertexOutput inputValue)
    {
        float3 encodedColor;
        if (inputValue.phase < 0.5f)
        {
            const half4 texel = sceneSet->textures
                ->get(inputValue.entityID, 0u)
                ->sample(
                    samplerResources->terrainSampler,
                    inputValue.uv);
            encodedColor = float3(
                webglGeometryTerrainRaycastLinearToSrgb(float(texel.x)),
                webglGeometryTerrainRaycastLinearToSrgb(float(texel.y)),
                webglGeometryTerrainRaycastLinearToSrgb(float(texel.z)));
        }
        else
        {
            const float3 packedNormal =
                normalize(inputValue.viewNormal) * 0.5f + float3(0.5f);
            encodedColor = float3(
                webglGeometryTerrainRaycastLinearToSrgb(packedNormal.x),
                webglGeometryTerrainRaycastLinearToSrgb(packedNormal.y),
                webglGeometryTerrainRaycastLinearToSrgb(packedNormal.z));
        }
        WebglGeometryTerrainRaycastFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(encodedColor), half(1.0f));
        return frameBuffer;
    }
};

/** Owns one Scene RenderSet, private texture Compute, and final output. */
class WebglGeometryTerrainRaycastRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]]
    RenderSet<WebglGeometryTerrainRaycastSceneRenderSet> sceneSet;
    Buffer<uint, BufferUsage<Storage, CopyDst>> heightBuffer;
    Buffer<uint, BufferUsage<Storage, CopyDst>> grainBuffer;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<StorageBinding, CopySrc>,
            TextureDimension::e2D> generatedTexture;
    BindGroup<WebglGeometryTerrainRaycastComputeResources> computeResources;
    ComputeClass<WebglGeometryTerrainRaycastTexturePass> texturePass;
    Sampler terrainSampler;
    BindGroup<WebglGeometryTerrainRaycastSamplerResources> samplerResources;
    RenderClass<WebglGeometryTerrainRaycastMainPass> scenePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputColor;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    uint outputWidth = 800u;
    uint outputHeight = 500u;

public:
    /** Creates the unique Set, Compute resources, and immutable Scene pass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<
            WebglGeometryTerrainRaycastSceneRenderSet>();
        heightBuffer = device->createBuffer(
            "WebglGeometryTerrainRaycastHeights",
            WebglGeometryTerrainRaycastWidth *
                WebglGeometryTerrainRaycastDepth);
        grainBuffer = device->createBuffer(
            "WebglGeometryTerrainRaycastGrain",
            WebglGeometryTerrainRaycastTextureExtent *
                WebglGeometryTerrainRaycastTextureExtent);
        generatedTexture = device->createTexture(
            "WebglGeometryTerrainRaycastGeneratedTexture",
            WebglGeometryTerrainRaycastTextureExtent,
            WebglGeometryTerrainRaycastTextureExtent,
            1u);
        computeResources = device->createBindGroup<
            WebglGeometryTerrainRaycastComputeResources>(
                heightBuffer,
                grainBuffer,
                generatedTexture->createView());
        texturePass = device->createComputeClass<
            WebglGeometryTerrainRaycastTexturePass>(computeResources);
        terrainSampler = device->createSampler({
            .label = "WebglGeometryTerrainRaycastSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
        samplerResources = device->createBindGroup<
            WebglGeometryTerrainRaycastSamplerResources>(terrainSampler);
        scenePass = device->createRenderClass<
            WebglGeometryTerrainRaycastMainPass>(
                sceneSet,
                samplerResources);
    }

    /** Allocates the fixed ordinary single-sample output attachments. */
    void configureOutput(uint width, uint height)
    {
        outputWidth = width;
        outputHeight = height;
        outputColor = device->createTexture(
            "WebglGeometryTerrainRaycastOutput",
            width,
            height,
            1u);
        sceneDepth = device->createTexture(
            "WebglGeometryTerrainRaycastDepth",
            width,
            height,
            1u);
    }

    /** Generates the TextureComponent payload through DSL Compute and readback. */
    void generateTerrainTexture(
        const eastl::vector<uint> &heights,
        const eastl::vector<uint> &grain,
        eastl::vector<uint8_t> &rgbaBytes)
    {
        graphicsQueue
            ->writeBuffer(
                BufferRange(heightBuffer),
                heights.data(),
                uint64_t(heights.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(grainBuffer),
                grain.data(),
                uint64_t(grain.size()) * sizeof(uint))
            ->submit();
        graphicsQueue
            ->computePass(
                "WebglGeometryTerrainRaycastTexture",
                texturePass(
                    WebglGeometryTerrainRaycastTextureExtent,
                    WebglGeometryTerrainRaycastTextureExtent,
                    1u))
            ->submit();
        rgbaBytes.resize(
            WebglGeometryTerrainRaycastTextureExtent *
            WebglGeometryTerrainRaycastTextureExtent * 4u);
        graphicsQueue
            ->readTexture(
                generatedTexture,
                rgbaBytes.data(),
                uint64_t(rgbaBytes.size()))
            ->submit();
    }

    /** Draws both entities by the current RenderSet-only indirect entry. */
    void render() override
    {
        sceneSet->update();
        WebglGeometryTerrainRaycastFrameBuffer frameBuffer;
        frameBuffer.color = outputColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {
            191.0f / 255.0f,
            209.0f / 255.0f,
            229.0f / 255.0f,
            1.0f};
        frameBuffer.depth = sceneDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass(
                "WebglGeometryTerrainRaycastMain",
                frameBuffer,
                scenePass())
            ->renderToSwapchain(
                nextTexture,
                outputColor,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned final readback texture. */
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
        return outputWidth;
    }

    /** Returns the configured capture height. */
    uint getReadbackHeight() const
    {
        return outputHeight;
    }

    /** Releases the unique Set and every private GPU resource. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(heightBuffer);
        device->freeBuffer(grainBuffer);
        device->freeTexture(generatedTexture);
        device->freeTexture(outputColor);
        device->freeTexture(sceneDepth);
    }
};

#endif
