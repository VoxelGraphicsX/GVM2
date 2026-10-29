#ifndef GVM_THREE_WEBGL_EFFECTS_ASCII_HPP
#define GVM_THREE_WEBGL_EFFECTS_ASCII_HPP

#include "UGL.h"

#include <EASTL/vector.h>

#include <cstdint>

using namespace UGL;

/*
 * This header is included by a dedicated sample shard after defining
 * THREE_BASIC_WebglEffectsAscii.  The preprocessor only supplies the type prefix; all
 * rendering remains ordinary UGL DSL code and every generated shard owns its
 * own RenderSet, pass, and renderer symbols.
 */

/** Stores one normalized triangle-list vertex packed by the host adapter. */
struct ThreeBasicVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 normal [[Attribute2]];
};

/** Carries transformed clip coordinates and interpolated color to the fragment stage. */
struct ThreeBasicVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    float3 viewPosition [[Attribute1]];
    float3 viewNormal [[Attribute2]];
    uint entityID [[Attribute3]];
};

/** Stores one entity transform and material index in the Scene RenderSet. */
struct ThreeBasicObjectData
{
    float4 offsetAndScale;
    uint4 materialAndFlags;
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 light0PositionIntensity;
    float4 light1PositionIntensity;
};

/** Stores one per-instance transform and tint selected by the entity builtin. */
struct ThreeBasicInstanceData
{
    float4 offsetAndScale;
    float4 tint;
};

/** Stores one material color and the private semantic mode selected by C++. */
struct ThreeBasicMaterialData
{
    float4 baseColor;
};

/** Stores the ASCII effect phase flags required by the manifest component schema. */
struct WebglEffectsAsciiRenderFlagsData
{
    uint4 flags;
};

/** Carries the fixed capture and ASCII logical-grid dimensions to screen passes. */
struct WebglEffectsAsciiScreenUniforms
{
    float4 viewportAndGrid;
};

/** Defines the single RenderSet used by this dedicated sample shard. */
struct WebglEffectsAsciiSceneRenderSet : public IRenderSet
{
    /** Declares consolidated geometry, object, instance, and material storage. */
    constructor(BufferComponent<ThreeBasicVertex> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]],
                BufferComponent<ThreeBasicObjectData> objects,
                BufferComponent<ThreeBasicInstanceData> instances,
                BufferComponent<ThreeBasicMaterialData> materials,
                BufferComponent<WebglEffectsAsciiRenderFlagsData> renderFlags)
    {
    }
};

/** Defines the linear half-float scene color and depth attachments. */
struct WebglEffectsAsciiFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only target used by the two fullscreen ASCII passes. */
struct WebglEffectsAsciiScreenFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Binds the Scene color and fixed logical grid used by luminance quantization. */
struct WebglEffectsAsciiLuminanceResources final : public IBindGroup
{
    /** Declares the source Scene texture, nearest sampler, and grid constants. */
    constructor(
        Texture2D<float4> sourceTexture [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebglEffectsAsciiScreenUniforms> uniforms [[Binding2]])
    {
    }
};

/** Binds the sampled source texture used to compose the glyph grid. */
struct WebglEffectsAsciiGlyphResources final : public IBindGroup
{
    /** Declares luminance texture, atlas, samplers, and grid constants. */
    constructor(
        Texture2D<float4> sourceTexture [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        Texture2D<float4> glyphAtlas [[Binding2]],
        Sampler glyphSampler [[Binding3]],
        UniformBuffer<WebglEffectsAsciiScreenUniforms> uniforms [[Binding4]])
    {
    }
};

/** Carries one fullscreen triangle coordinate into an ASCII screen fragment. */
struct WebglEffectsAsciiScreenVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/**
 * Draws all entities through the Scene RenderSet indexed-indirect path.
 * The shader contains the common Three-compatible vertex-color, lighting,
 * clipping, and deterministic animation operations used by this wave.
 */
class WebglEffectsAsciiScenePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and enables depth-tested opaque drawing. */
    constructor(RenderSet<WebglEffectsAsciiSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves per-entity and per-instance data through the RenderEntity builtins. */
    ThreeBasicVertexOutput vertex(
        ThreeBasicVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(renderEntityID, 0u);
        const ThreeBasicInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 localPosition = inputValue.position +
            float4(instanceData.offsetAndScale.xyz, 0.0f);
        const float4 viewPosition = mul(objectData.modelView, localPosition);
        float4 clipPosition = mul(objectData.modelViewProjection, localPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        ThreeBasicVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.color = inputValue.color * instanceData.tint;
        outputValue.viewPosition = viewPosition.xyz;
        const float3 viewNormal = mul(objectData.modelView, float4(inputValue.normal.xyz, 0.0f)).xyz;
        outputValue.viewNormal = normalize(viewNormal);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Applies the material component and a stable linear-to-sRGB transfer. */
    WebglEffectsAsciiFrameBuffer fragment(
        ThreeBasicVertexOutput inputValue)
    {
        const ThreeBasicObjectData objectData = sceneSet->objects->get(inputValue.entityID, 0u);
        // Material components are stored per entity in this shard.  The
        // flag selects the material mode; it is not an element index into a
        // second material array.  Using it as an index made the plane's sole
        // material an out-of-range access and silently removed the plane.
        const ThreeBasicMaterialData materialData = sceneSet->materials->get(
            inputValue.entityID, 0u);
        // The CPU adapter expands the low-segment sphere into per-triangle
        // vertices, so the interpolated normal is constant for each face and
        // matches MeshPhongMaterial's flatShading path without relying on
        // backend-specific derivative precision.
        const float3 normal = normalize(inputValue.viewNormal);
        const float3 light0Vector = objectData.light0PositionIntensity.xyz - inputValue.viewPosition;
        const float3 light1Vector = objectData.light1PositionIntensity.xyz - inputValue.viewPosition;
        const float3 light0Direction = normalize(light0Vector);
        const float3 light1Direction = normalize(light1Vector);
        const float dotLight0 = max(dot(normal, light0Direction), 0.0f);
        const float dotLight1 = max(dot(normal, light1Direction), 0.0f);
        const float diffuse = dotLight0 * objectData.light0PositionIntensity.w
            + dotLight1 * objectData.light1PositionIntensity.w;
        const float3 baseColor = inputValue.color.xyz * materialData.baseColor.xyz;
        const float inversePi = 0.3183098861837907f;
        float3 linearColor = objectData.materialAndFlags.y > 0u
            ? saturate(baseColor)
            : saturate(baseColor * diffuse * inversePi);
        if (objectData.materialAndFlags.y == 0u)
        {
            // MeshPhongMaterial keeps the default 0x111111 specular and
            // shininess 30.  Match the r185 Blinn-Phong BRDF so the
            // luminance source seen by AsciiEffect has the same highlights.
            const float3 viewDirection = normalize(-inputValue.viewPosition);
            const float3 half0 = normalize(light0Direction + viewDirection);
            const float3 half1 = normalize(light1Direction + viewDirection);
            const float specularScale = 0.0056053917f;
            const float distribution0 =
                0.3183098861837907f * 16.0f *
                pow(max(dot(normal, half0), 0.0f), 30.0f);
            const float distribution1 =
                0.3183098861837907f * 16.0f *
                pow(max(dot(normal, half1), 0.0f), 30.0f);
            const float fresnel0 = specularScale +
                (1.0f - specularScale) *
                pow(1.0f - max(dot(viewDirection, half0), 0.0f), 5.0f);
            const float fresnel1 = specularScale +
                (1.0f - specularScale) *
                pow(1.0f - max(dot(viewDirection, half1), 0.0f), 5.0f);
            linearColor += float3(
                dotLight0 * objectData.light0PositionIntensity.w *
                    (fresnel0 * 0.25f * distribution0),
                dotLight0 * objectData.light0PositionIntensity.w *
                    (fresnel0 * 0.25f * distribution0),
                dotLight0 * objectData.light0PositionIntensity.w *
                    (fresnel0 * 0.25f * distribution0));
            linearColor += float3(
                dotLight1 * objectData.light1PositionIntensity.w *
                    (fresnel1 * 0.25f * distribution1),
                dotLight1 * objectData.light1PositionIntensity.w *
                    (fresnel1 * 0.25f * distribution1),
                dotLight1 * objectData.light1PositionIntensity.w *
                    (fresnel1 * 0.25f * distribution1));
            linearColor = saturate(linearColor);
        }
        const float3 srgbColor = float3(
            linearColor.x <= 0.0031308f
                ? linearColor.x * 12.92f
                : pow(linearColor.x, 0.41666f) * 1.055f - 0.055f,
            linearColor.y <= 0.0031308f
                ? linearColor.y * 12.92f
                : pow(linearColor.y, 0.41666f) * 1.055f - 0.055f,
            linearColor.z <= 0.0031308f
                ? linearColor.z * 12.92f
                : pow(linearColor.z, 0.41666f) * 1.055f - 0.055f);
        WebglEffectsAsciiFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(srgbColor), half(1.0f));
        return frameBuffer;
    }
};

/** Emits the shared fullscreen triangle used by both ASCII screen passes. */
WebglEffectsAsciiScreenVertexOutput webglEffectsAsciiFullscreenVertex(uint vertexID)
{
    const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglEffectsAsciiScreenVertexOutput outputValue;
    outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = uv;
    return outputValue;
}

/** Stores one quantized brightness cell for the glyph-composition pass. */
struct WebglEffectsAsciiLuminanceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Reduces the full Scene into the ten-character ASCII logical grid. */
class WebglEffectsAsciiLuminancePass final : public IRenderClass
{
public:
    /** Binds the Scene color and fixed 120 by 75 source grid. */
    constructor(
        BindGroup<WebglEffectsAsciiLuminanceResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle. */
    WebglEffectsAsciiScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglEffectsAsciiFullscreenVertex(vertexID);
    }

    /** Applies AsciiEffect's brightness weights and inverted ten-character index. */
    WebglEffectsAsciiLuminanceFrameBuffer fragment(
        WebglEffectsAsciiScreenVertexOutput inputValue)
    {
        const float2 viewport = resources->uniforms->viewportAndGrid.xy;
        const float2 grid = resources->uniforms->viewportAndGrid.zw;
        const float2 pixel = inputValue.uv * viewport;
        const float2 cell = floor(pixel / float2(viewport.x / grid.x, viewport.y / (grid.y * 0.5f)));
        const float2 sourceCell = float2(
            clamp(cell.x, 0.0f, grid.x - 1.0f),
            clamp(cell.y * 2.0f, 0.0f, grid.y - 1.0f));
        const float2 sourceUv = (sourceCell + float2(0.5f)) / grid;
        const float3 source = resources->sourceTexture->sample(
            resources->sourceSampler,
            sourceUv).xyz;
        const float brightness = saturate(
            dot(source, float3(0.3f, 0.59f, 0.11f)));
        // The upstream example passes invert=true: dark cells become spaces,
        // bright cells select the denser characters at the end of the charset.
        WebglEffectsAsciiLuminanceFrameBuffer frameBuffer;
        frameBuffer.color = half4(half(brightness), half(brightness), half(brightness), half(1.0f));
        return frameBuffer;
    }
};

/** Composes the quantized cells with a deterministic sample-private glyph atlas. */
class WebglEffectsAsciiGlyphPass final : public IRenderClass
{
public:
    /** Binds quantized luminance and the ten-character coverage atlas. */
    constructor(
        BindGroup<WebglEffectsAsciiGlyphResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits the fullscreen triangle. */
    WebglEffectsAsciiScreenVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglEffectsAsciiFullscreenVertex(vertexID);
    }

    /** Samples one 7 by 13 atlas glyph and emits white Courier-style coverage. */
    WebglEffectsAsciiScreenFrameBuffer fragment(
        WebglEffectsAsciiScreenVertexOutput inputValue)
    {
        const float2 viewport = resources->uniforms->viewportAndGrid.xy;
        const float2 grid = resources->uniforms->viewportAndGrid.zw;
        const float2 cellSize = float2(viewport.x / grid.x, viewport.y / (grid.y * 0.5f));
        const float2 pixel = inputValue.uv * viewport;
        const float2 cell = floor(pixel / cellSize);
        const float2 local = saturate((pixel - cell * cellSize) / cellSize);
        const float brightness = resources->sourceTexture->sample(
            resources->sourceSampler, inputValue.uv).x;
        // Select the nearest bucket with operations supported by both UGLC
        // lowering paths; round() is not available in every backend emitter.
        const float glyphIndex = clamp(floor(brightness * 9.0f + 0.5f), 0.0f, 9.0f);
        const float2 atlasUv = float2(
            (glyphIndex * 7.0f + clamp(floor(local.x * 7.0f), 0.0f, 6.0f) + 0.5f) / 128.0f,
            (clamp(floor(local.y * 13.0f), 0.0f, 12.0f) + 0.5f) / 16.0f);
        const float coverage = resources->glyphAtlas->sample(
            resources->glyphSampler, atlasUv).x;
        WebglEffectsAsciiScreenFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(coverage), half(1.0f));
        return frameBuffer;
    }
};

/** Owns this shard's unique RenderSet, Scene pass, and single-sample targets. */
class WebglEffectsAsciiRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglEffectsAsciiSceneRenderSet> sceneSet;
    RenderClass<WebglEffectsAsciiScenePass> scenePass;
    RenderClass<WebglEffectsAsciiLuminancePass> luminancePass;
    RenderClass<WebglEffectsAsciiGlyphPass> glyphPass;
    BindGroup<WebglEffectsAsciiLuminanceResources> luminanceResources;
    BindGroup<WebglEffectsAsciiGlyphResources> glyphResources;
    Buffer<WebglEffectsAsciiScreenUniforms,
           BufferUsage<Uniform, CopyDst>> screenUniforms;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        sceneColor;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D>
        luminanceColor;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<TextureBinding, CopyDst>,
            TextureDimension::e2D>
        glyphAtlas;
    Sampler screenSampler;
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
    /** Creates this shard's RenderSet and its generated Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet = device->createRenderSet<WebglEffectsAsciiSceneRenderSet>();
        scenePass = device->createRenderClass<WebglEffectsAsciiScenePass>(sceneSet);
        screenSampler = device->createSampler({
            .label = "WebglEffectsAsciiNearestSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0,
            .lodMaxClamp = 0,
            .maxAnisotropy = 1,
        });
    }

    /** Recreates the explicit single-sample capture attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputColor = device->createTexture("ThreeBasicRenderSetColor", width, height, 1u);
        outputDepth = device->createTexture("ThreeBasicRenderSetDepth", width, height, 1u);
        sceneColor = device->createTexture(
            "WebglEffectsAsciiSceneColor", width, height, 1u);
        luminanceColor = device->createTexture(
            "WebglEffectsAsciiLuminance", width, height, 1u);
        // Keep each uploaded row at 512 bytes.  This is the explicit
        // single-sample texture-write alignment required by both Metal and
        // Vulkan paths; the ten 7-pixel glyph cells occupy the first 70
        // columns and the remaining columns/rows stay deterministic zero.
        glyphAtlas = device->createTexture(
            "WebglEffectsAsciiGlyphAtlas", 128u, 16u, 1u);

        // A deterministic 7x13 coverage atlas for the pinned charset
        // " .:-+*=%@#".  These are glyph shapes (not a reference image):
        // each cell is sampled by the DSL glyph pass at the same logical
        // 120 by 75 grid that AsciiEffect uses.
        static const uint8_t glyphRows[130u] = {
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // ' '
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x00u, // '.'
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x00u, // ':'
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x3Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // '-'
            0x00u, 0x00u, 0x00u, 0x04u, 0x04u, 0x04u, 0x3Fu, 0x04u, 0x04u, 0x04u, 0x00u, 0x00u, 0x00u, // '+'
            0x00u, 0x00u, 0x04u, 0x04u, 0x1Fu, 0x0Cu, 0x0Eu, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // '*'
            0x00u, 0x00u, 0x00u, 0x00u, 0x3Fu, 0x00u, 0x00u, 0x3Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, // '='
            0x00u, 0x00u, 0x1Cu, 0x24u, 0x24u, 0x1Cu, 0x3Fu, 0x06u, 0x09u, 0x09u, 0x06u, 0x00u, 0x00u, // '%'
            0x00u, 0x00u, 0x0Eu, 0x12u, 0x31u, 0x27u, 0x2Du, 0x29u, 0x27u, 0x30u, 0x12u, 0x0Eu, 0x00u, // '@'
            0x00u, 0x00u, 0x0Au, 0x0Au, 0x0Au, 0x3Fu, 0x0Au, 0x0Au, 0x3Fu, 0x0Cu, 0x0Cu, 0x14u, 0x00u, // '#'
        };
        eastl::vector<uint8_t> glyphPixels(128u * 16u * 4u, 0u);
        for (uint32_t glyph = 0u; glyph < 10u; glyph += 1u)
        {
            for (uint32_t row = 0u; row < 13u; row += 1u)
            {
                for (uint32_t column = 0u; column < 7u; column += 1u)
                {
                    const uint32_t pixel = (row * 128u + glyph * 7u + column) * 4u;
                    const uint8_t bit = uint8_t(1u << (6u - column));
                    const uint8_t coverage = (glyphRows[glyph * 13u + row] & bit) != 0u ? 255u : 0u;
                    glyphPixels[pixel + 0u] = coverage;
                    glyphPixels[pixel + 1u] = coverage;
                    glyphPixels[pixel + 2u] = coverage;
                    glyphPixels[pixel + 3u] = 255u;
                }
            }
        }
        graphicsQueue->writeTexture(
            glyphAtlas,
            glyphPixels.data(),
            glyphPixels.size(),
            0u)->submit();
        screenUniforms = device->createBuffer(
            "WebglEffectsAsciiScreenUniforms", 1u);
        WebglEffectsAsciiScreenUniforms uniforms;
        uniforms.viewportAndGrid = float4(
            float(width), float(height), 120.0f, 75.0f);
        graphicsQueue->writeBuffer(
            BufferRange(screenUniforms), &uniforms, sizeof(uniforms))->submit();
        luminanceResources = device->createBindGroup<WebglEffectsAsciiLuminanceResources>(
            sceneColor->createView(), screenSampler, screenUniforms);
        glyphResources = device->createBindGroup<WebglEffectsAsciiGlyphResources>(
            luminanceColor->createView(), screenSampler,
            glyphAtlas->createView(), screenSampler, screenUniforms);
        luminancePass = device->createRenderClass<WebglEffectsAsciiLuminancePass>(
            luminanceResources);
        glyphPass = device->createRenderClass<WebglEffectsAsciiGlyphPass>(
            glyphResources);
    }

    /** Applies pending RenderSet commands and submits one indexed-indirect Scene pass. */
    void render() override
    {
        sceneSet->update();
        const auto nextTexture = swapchain->queryNextTexture();
        WebglEffectsAsciiFrameBuffer frameBuffer;
        frameBuffer.color = sceneColor->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        frameBuffer.depth = outputDepth->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        WebglEffectsAsciiLuminanceFrameBuffer luminanceFrameBuffer;
        luminanceFrameBuffer.color = luminanceColor->createView();
        luminanceFrameBuffer.color.loadOp = LoadOp::Clear;
        luminanceFrameBuffer.color.storeOp = StoreOp::Store;
        luminanceFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        WebglEffectsAsciiScreenFrameBuffer outputFrameBuffer;
        outputFrameBuffer.color = outputColor->createView();
        outputFrameBuffer.color.loadOp = LoadOp::Clear;
        outputFrameBuffer.color.storeOp = StoreOp::Store;
        outputFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        graphicsQueue
            ->renderPass("ThreeBasicRenderSetScene", frameBuffer, scenePass())
            ->renderPass("WebglEffectsAsciiLuminance", luminanceFrameBuffer,
                         luminancePass(3u, 1u, 0u, 0u))
            ->renderPass("WebglEffectsAsciiGlyphGrid", outputFrameBuffer,
                         glyphPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the DSL-owned RGBA8 target used for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the explicit capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicit capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the single-sample attachments and RenderSet after capture. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeBuffer(screenUniforms);
        device->freeTexture(sceneColor);
        device->freeTexture(luminanceColor);
        device->freeTexture(glyphAtlas);
        device->freeTexture(outputColor);
        device->freeTexture(outputDepth);
    }
};

#undef WebglEffectsAsciiRenderer
#undef WebglEffectsAsciiFrameBuffer
#undef WebglEffectsAsciiScenePass
#undef WebglEffectsAsciiSceneRenderSet
#undef WebglEffectsAsciiLuminancePass
#undef WebglEffectsAsciiGlyphPass
#undef THREE_BASIC_JOIN

#endif
