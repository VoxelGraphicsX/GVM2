#ifndef GVM_THREE_WEBGL_MULTIPLE_SCENES_COMPARISON_HPP
#define GVM_THREE_WEBGL_MULTIPLE_SCENES_COMPARISON_HPP

#include "UGL.h"
#include "WebglMultipleScenesComparisonData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglMultipleScenesSolidVertexCount = 960u;
static const uint WebglMultipleScenesSolidIndexCount = 960u;
static const uint WebglMultipleScenesWireVertexCount = 3840u;
static const uint WebglMultipleScenesWireIndexCount = 5760u;
static const uint WebglMultipleScenesSourceWidth = 1600u;
static const uint WebglMultipleScenesSourceHeight = 1000u;
static const uint WebglMultipleScenesOutputWidth = 800u;
static const uint WebglMultipleScenesOutputHeight = 500u;

/** Binds the shared camera and hemisphere-light state for one simple Scene. */
struct WebglMultipleScenesSceneResources final : public IBindGroup
{
    /** Declares the immutable per-capture comparison state. */
    constructor(UniformBuffer<WebglMultipleScenesUniforms> uniforms [[Binding0]])
    {
    }
};

/** Binds both simple Scene colors and the final writable comparison output. */
struct WebglMultipleScenesResolveResources final : public IBindGroup
{
    /** Declares the two source textures and final RGBA8 storage texture. */
    constructor(Texture2D<float4> leftColor [[Binding0]],
                Texture2D<float4> rightColor [[Binding1]],
                UniformBuffer<WebglMultipleScenesUniforms> uniforms [[Binding2]],
                RWTexture2D<TextureFormat::RGBA8Unorm> outputColor [[Binding3]])
    {
    }
};

/** Carries one lit smooth icosahedron vertex into the fragment stage. */
struct WebglMultipleScenesVertexOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
};

/** Defines each simple Scene's display-encoded color and depth attachments. */
struct WebglMultipleScenesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel using the Three r185 output transfer. */
float webglMultipleScenesLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Shades the left Scene's only smooth icosahedron through an ordinary RenderClass. */
class WebglMultipleScenesComparisonLeftPass final : public IRenderClass
{
public:
    /** Binds no RenderSet and preserves the opaque standard-material depth state. */
    constructor(BindGroup<WebglMultipleScenesSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms one CPU-generated detail-3 icosahedron vertex. */
    WebglMultipleScenesVertexOutput vertex(WebglMultipleScenesSolidVertex inputValue [[VertexInput0]])
    {
        WebglMultipleScenesVertexOutput outputValue;
        outputValue.position = resources->uniforms->modelViewProjectionColumn0 * inputValue.position.x +
                               resources->uniforms->modelViewProjectionColumn1 * inputValue.position.y +
                               resources->uniforms->modelViewProjectionColumn2 * inputValue.position.z +
                               resources->uniforms->modelViewProjectionColumn3 * inputValue.position.w;
        outputValue.normal = inputValue.normal.xyz;
        return outputValue;
    }

    /** Evaluates the white/gray intensity-three hemisphere diffuse response. */
    WebglMultipleScenesFrameBuffer fragment(WebglMultipleScenesVertexOutput inputValue)
    {
        const float weight = dot(normalize(inputValue.normal), normalize(float3(resources->uniforms->hemisphereDirection.xyz))) * 0.5f + 0.5f;
        const float ground = 0.0578054302f;
        const float3 linearColor = float3(ground + (1.0f - ground) * weight) * 0.9549296586f;
        WebglMultipleScenesFrameBuffer frameBuffer;
        frameBuffer.color = half4(webglMultipleScenesLinearToSrgb(linearColor.x),
                                  webglMultipleScenesLinearToSrgb(linearColor.y),
                                  webglMultipleScenesLinearToSrgb(linearColor.z), 1.0f);
        return frameBuffer;
    }
};

/** Shades CPU triangle-expanded wire edges for the right simple Scene. */
class WebglMultipleScenesComparisonRightPass final : public IRenderClass
{
public:
    /** Uses the right Scene's standalone geometry with opaque line depth semantics. */
    constructor(BindGroup<WebglMultipleScenesSceneResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one original wire endpoint to a two-source-pixel-wide segment. */
    WebglMultipleScenesVertexOutput vertex(WebglMultipleScenesWireVertex inputValue [[VertexInput0]])
    {
        const float4 startClip = resources->uniforms->modelViewProjectionColumn0 * inputValue.segmentStart.x +
                                 resources->uniforms->modelViewProjectionColumn1 * inputValue.segmentStart.y +
                                 resources->uniforms->modelViewProjectionColumn2 * inputValue.segmentStart.z +
                                 resources->uniforms->modelViewProjectionColumn3 * inputValue.segmentStart.w;
        const float4 endClip = resources->uniforms->modelViewProjectionColumn0 * inputValue.segmentEnd.x +
                               resources->uniforms->modelViewProjectionColumn1 * inputValue.segmentEnd.y +
                               resources->uniforms->modelViewProjectionColumn2 * inputValue.segmentEnd.z +
                               resources->uniforms->modelViewProjectionColumn3 * inputValue.segmentEnd.w;
        const float2 startNdc = startClip.xy / startClip.w;
        const float2 endNdc = endClip.xy / endClip.w;
        const float2 direction = (endNdc - startNdc) * float2(float(WebglMultipleScenesSourceWidth), float(WebglMultipleScenesSourceHeight));
        const float2 pixelNormal = float2(-direction.y, direction.x) * rsqrt(max(dot(direction, direction), 0.000001f));
        const bool useEnd = inputValue.endpointAndSide.x > 0.5f;
        float4 clipPosition = useEnd ? endClip : startClip;
        clipPosition.xy += pixelNormal * inputValue.endpointAndSide.y * 2.0f /
                           float2(float(WebglMultipleScenesSourceWidth), float(WebglMultipleScenesSourceHeight)) * clipPosition.w;
        WebglMultipleScenesVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.normal = (useEnd ? inputValue.endNormal : inputValue.startNormal).xyz;
        return outputValue;
    }

    /** Reuses the same hemisphere material response as the solid Scene. */
    WebglMultipleScenesFrameBuffer fragment(WebglMultipleScenesVertexOutput inputValue)
    {
        const float weight = dot(normalize(inputValue.normal), normalize(float3(resources->uniforms->hemisphereDirection.xyz))) * 0.5f + 0.5f;
        const float ground = 0.0578054302f;
        const float3 linearColor = float3(ground + (1.0f - ground) * weight) * 0.9549296586f;
        WebglMultipleScenesFrameBuffer frameBuffer;
        frameBuffer.color = half4(webglMultipleScenesLinearToSrgb(linearColor.x),
                                  webglMultipleScenesLinearToSrgb(linearColor.y),
                                  webglMultipleScenesLinearToSrgb(linearColor.z), 1.0f);
        return frameBuffer;
    }
};

/** Resolves two 2x Scene targets and composes the deterministic slider split. */
class [[LocalWorkGroupSize(8, 8, 1)]] WebglMultipleScenesComparisonResolvePass final : public IComputeClass
{
public:
    /** Binds only screen-space textures, uniforms, and output. */
    constructor(BindGroup<WebglMultipleScenesResolveResources> resources [[Slot0]])
    {
    }

private:
    /** Averages one 2x2 footprint from the Scene selected by the slider. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= WebglMultipleScenesOutputWidth || threadID.y >= WebglMultipleScenesOutputHeight)
        {
            return;
        }
        const uint2 source = threadID.xy * 2u;
        float4 color;
        if (float(threadID.x) < resources->uniforms->sliderAndReserved.x)
        {
            color = resources->leftColor->read(source, 0u) + resources->leftColor->read(source + uint2(1u, 0u), 0u) +
                    resources->leftColor->read(source + uint2(0u, 1u), 0u) + resources->leftColor->read(source + uint2(1u, 1u), 0u);
        }
        else
        {
            color = resources->rightColor->read(source, 0u) + resources->rightColor->read(source + uint2(1u, 0u), 0u) +
                    resources->rightColor->read(source + uint2(0u, 1u), 0u) + resources->rightColor->read(source + uint2(1u, 1u), 0u);
        }
        resources->outputColor->write(threadID.xy, half4(color * 0.25f));
    }
};

/** Owns both independent ordinary Scene RenderClasses and their screen composition. */
class WebglMultipleScenesComparisonRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglMultipleScenesSolidVertex, BufferUsage<Vertex, CopyDst>> solidVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> solidIndexBuffer;
    Buffer<WebglMultipleScenesWireVertex, BufferUsage<Vertex, CopyDst>> wireVertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> wireIndexBuffer;
    Buffer<WebglMultipleScenesUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer;
    BindGroup<WebglMultipleScenesSceneResources> sceneResources;
    RenderClass<WebglMultipleScenesComparisonLeftPass> leftPass;
    RenderClass<WebglMultipleScenesComparisonRightPass> rightPass;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> leftColor;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> rightColor;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> leftDepth;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> rightDepth;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<StorageBinding, TextureBinding, CopySrc>, TextureDimension::e2D> outputColor;
    BindGroup<WebglMultipleScenesResolveResources> resolveResources;
    ComputeClass<WebglMultipleScenesComparisonResolvePass> resolvePass;

public:
    /** Creates all fixed standalone buffers without creating a RenderSet. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        solidVertexBuffer = device->createBuffer("WebglMultipleScenesSolidVertices", WebglMultipleScenesSolidVertexCount);
        solidIndexBuffer = device->createBuffer("WebglMultipleScenesSolidIndices", WebglMultipleScenesSolidIndexCount);
        wireVertexBuffer = device->createBuffer("WebglMultipleScenesWireVertices", WebglMultipleScenesWireVertexCount);
        wireIndexBuffer = device->createBuffer("WebglMultipleScenesWireIndices", WebglMultipleScenesWireIndexCount);
        uniformBuffer = device->createBuffer("WebglMultipleScenesUniforms", 1u);
    }

    /** Allocates the fixed 2x Scene targets and final 800x500 output. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglMultipleScenesOutputWidth || height != WebglMultipleScenesOutputHeight)
        {
            return;
        }
        leftColor = device->createTexture("WebglMultipleScenesLeftColor", WebglMultipleScenesSourceWidth, WebglMultipleScenesSourceHeight, 1u);
        rightColor = device->createTexture("WebglMultipleScenesRightColor", WebglMultipleScenesSourceWidth, WebglMultipleScenesSourceHeight, 1u);
        leftDepth = device->createTexture("WebglMultipleScenesLeftDepth", WebglMultipleScenesSourceWidth, WebglMultipleScenesSourceHeight, 1u);
        rightDepth = device->createTexture("WebglMultipleScenesRightDepth", WebglMultipleScenesSourceWidth, WebglMultipleScenesSourceHeight, 1u);
        outputColor = device->createTexture("WebglMultipleScenesOutput", width, height, 1u);
    }

    /** Uploads both simple Scene geometries and immutable canonical state. */
    void configureScenes(const eastl::vector<WebglMultipleScenesSolidVertex> &solidVertices,
                         const eastl::vector<uint> &solidIndices,
                         const eastl::vector<WebglMultipleScenesWireVertex> &wireVertices,
                         const eastl::vector<uint> &wireIndices,
                         float m00, float m01, float m02, float m03,
                         float m10, float m11, float m12, float m13,
                         float m20, float m21, float m22, float m23,
                         float m30, float m31, float m32, float m33,
                         float lightX, float lightY, float lightZ,
                         float sliderPosition)
    {
        WebglMultipleScenesUniforms uniforms;
        uniforms.modelViewProjectionColumn0 = float4(m00, m01, m02, m03);
        uniforms.modelViewProjectionColumn1 = float4(m10, m11, m12, m13);
        uniforms.modelViewProjectionColumn2 = float4(m20, m21, m22, m23);
        uniforms.modelViewProjectionColumn3 = float4(m30, m31, m32, m33);
        uniforms.hemisphereDirection = float4(lightX, lightY, lightZ, 0.0f);
        uniforms.sliderAndReserved = float4(sliderPosition, 0.0f, 0.0f, 0.0f);
        graphicsQueue->writeBuffer(BufferRange(solidVertexBuffer), solidVertices.data(), uint64_t(solidVertices.size()) * sizeof(WebglMultipleScenesSolidVertex))
            ->writeBuffer(BufferRange(solidIndexBuffer), solidIndices.data(), uint64_t(solidIndices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(wireVertexBuffer), wireVertices.data(), uint64_t(wireVertices.size()) * sizeof(WebglMultipleScenesWireVertex))
            ->writeBuffer(BufferRange(wireIndexBuffer), wireIndices.data(), uint64_t(wireIndices.size()) * sizeof(uint))
            ->writeBuffer(BufferRange(uniformBuffer), &uniforms, sizeof(uniforms))->submit();
        sceneResources = device->createBindGroup<WebglMultipleScenesSceneResources>(uniformBuffer);
        leftPass = device->createRenderClass<WebglMultipleScenesComparisonLeftPass>(sceneResources);
        rightPass = device->createRenderClass<WebglMultipleScenesComparisonRightPass>(sceneResources);
        resolveResources = device->createBindGroup<WebglMultipleScenesResolveResources>(leftColor->createView(), rightColor->createView(), uniformBuffer, outputColor->createView());
        resolvePass = device->createComputeClass<WebglMultipleScenesComparisonResolvePass>(resolveResources);
    }

    /** Draws both independent Scenes and composes their current slider split. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        WebglMultipleScenesFrameBuffer leftFrameBuffer;
        leftFrameBuffer.color = leftColor->createView();
        leftFrameBuffer.color.loadOp = LoadOp::Clear;
        leftFrameBuffer.color.storeOp = StoreOp::Store;
        leftFrameBuffer.color.clearValue = {188.0 / 255.0, 212.0 / 255.0, 143.0 / 255.0, 1.0};
        leftFrameBuffer.depth = leftDepth->createView();
        leftFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        leftFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        leftFrameBuffer.depth.depthClearValue = 1.0f;
        WebglMultipleScenesFrameBuffer rightFrameBuffer;
        rightFrameBuffer.color = rightColor->createView();
        rightFrameBuffer.color.loadOp = LoadOp::Clear;
        rightFrameBuffer.color.storeOp = StoreOp::Store;
        rightFrameBuffer.color.clearValue = {143.0 / 255.0, 188.0 / 255.0, 212.0 / 255.0, 1.0};
        rightFrameBuffer.depth = rightDepth->createView();
        rightFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        rightFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        rightFrameBuffer.depth.depthClearValue = 1.0f;
        graphicsQueue->renderPass("WebglMultipleScenesLeft", leftFrameBuffer,
                                  leftPass->setVertexBuffer(solidVertexBuffer), leftPass->setIndexBuffer(solidIndexBuffer),
                                  leftPass(WebglMultipleScenesSolidIndexCount, 1u, 0u, 0, 0u))
            ->renderPass("WebglMultipleScenesRight", rightFrameBuffer,
                         rightPass->setVertexBuffer(wireVertexBuffer), rightPass->setIndexBuffer(wireIndexBuffer),
                         rightPass(WebglMultipleScenesWireIndexCount, 1u, 0u, 0, 0u))
            ->computePass("WebglMultipleScenesResolve", resolvePass(WebglMultipleScenesOutputWidth, WebglMultipleScenesOutputHeight, 1u))
            ->renderToSwapchain(nextTexture, outputColor, RenderToSwapchainDescriptor{})->submit();
        swapchain->present();
    }

    /** Returns the DSL-created final comparison texture for readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<StorageBinding, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputColor;
    }

    /** Returns the immutable output width. */
    uint getReadbackWidth() const { return WebglMultipleScenesOutputWidth; }

    /** Returns the immutable output height. */
    uint getReadbackHeight() const { return WebglMultipleScenesOutputHeight; }

    /** Releases every private buffer and texture owned by the sample. */
    void destroy() override
    {
        device->freeBuffer(solidVertexBuffer);
        device->freeBuffer(solidIndexBuffer);
        device->freeBuffer(wireVertexBuffer);
        device->freeBuffer(wireIndexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(leftColor);
        device->freeTexture(rightColor);
        device->freeTexture(leftDepth);
        device->freeTexture(rightDepth);
        device->freeTexture(outputColor);
    }
};

#endif
