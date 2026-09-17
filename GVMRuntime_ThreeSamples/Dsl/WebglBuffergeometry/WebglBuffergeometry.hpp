#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_HPP

#include "UGL.h"
#include "WebglBuffergeometryVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglBuffergeometryVertexCount = 480000u;

/** Stores the frozen Three transforms and one deterministic coverage position. */
struct WebglBuffergeometryUniforms
{
    float4x4 projection;
    float4x4 modelView;
    float4 samplePositionAndReserved;
};

/** Binds the immutable frame transforms used by both transparent side passes. */
struct WebglBuffergeometryBindGroup final : public IBindGroup
{
    /** Declares one current-surface uniform buffer through the frozen DSL interface. */
    constructor(UniformBuffer<WebglBuffergeometryUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries view-space Phong inputs and authored Float32 RGBA to the fragment stage. */
struct WebglBuffergeometryVertexOutput
{
    float4 position [[Position]];
    float3 viewNormal [[Attribute0]];
    float4 vertexColor [[Attribute1]];
    float3 viewDirectionVector [[Attribute2]];
    float fogDepth [[Attribute3]];
};

/** Defines the deterministic single-sample color and depth target. */
struct WebglBuffergeometryFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Converts one linear-light channel with Three r185's exact output constants. */
inline float webglBuffergeometryLinearToSrgb(float value)
{
    if (value <= 0.0031308f)
    {
        return value * 12.92f;
    }
    return pow(value, 0.41666f) * 1.055f - 0.055f;
}

/** Evaluates Three r185's optimized Schlick Fresnel approximation. */
inline float3 webglBuffergeometryFresnelSchlick(
    float3 f0,
    float f90,
    float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(f90) * fresnel;
}

/** Evaluates Three r185's normalized Blinn-Phong direct specular BRDF. */
inline float3 webglBuffergeometryPhongSpecular(
    float3 lightDirection,
    float3 viewDirection,
    float3 normal)
{
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float dotNormalHalf =
        clamp(dot(normal, halfDirection), 0.0f, 1.0f);
    const float dotViewHalf =
        clamp(dot(viewDirection, halfDirection), 0.0f, 1.0f);
    const float3 fresnel = webglBuffergeometryFresnelSchlick(
        float3(1.0f),
        1.0f,
        dotViewHalf);
    const float distribution =
        0.3183098861837907f * 126.0f * pow(dotNormalHalf, 250.0f);
    return fresnel * (0.25f * distribution);
}

/** Applies one fixed sample-position translation in clip space. */
inline float4 webglBuffergeometryApplyCoverageOffset(
    float4 clipPosition,
    float2 samplePosition)
{
    clipPosition.x +=
        (1.0f - 2.0f * samplePosition.x) / 800.0f * clipPosition.w;
    clipPosition.y +=
        (2.0f * samplePosition.y - 1.0f) / 500.0f * clipPosition.w;
    return clipPosition;
}

/** Evaluates the shared MeshPhong, output-transfer, fog, and vertex-alpha equations. */
inline float4 webglBuffergeometryShade(
    float3 viewNormal,
    float4 vertexColor,
    float3 viewDirectionVector,
    float fogDepth)
{
    const float inversePi = 0.3183098861837907f;
    const float3 normal = normalize(viewNormal);
    const float3 viewDirection = normalize(viewDirectionVector);
    const float3 lightDirection0 = normalize(float3(1.0f, 1.0f, 1.0f));
    const float3 lightDirection1 = float3(0.0f, -1.0f, 0.0f);
    const float dotLight0 =
        clamp(dot(normal, lightDirection0), 0.0f, 1.0f);
    const float dotLight1 =
        clamp(dot(normal, lightDirection1), 0.0f, 1.0f);
    const float irradiance0 = dotLight0 * 1.5f;
    const float irradiance1 = dotLight1 * 4.5f;
    const float3 diffuseColor =
        vertexColor.xyz * 0.6653872728347778f;
    float3 linearColor =
        diffuseColor *
        ((float3(0.6038273572921753f) +
          float3(irradiance0) +
          float3(irradiance1)) * inversePi);
    linearColor += float3(irradiance0) *
                   webglBuffergeometryPhongSpecular(
                       lightDirection0,
                       viewDirection,
                       normal);
    linearColor += float3(irradiance1) *
                   webglBuffergeometryPhongSpecular(
                       lightDirection1,
                       viewDirection,
                       normal);

    float3 displayColor = float3(
        webglBuffergeometryLinearToSrgb(linearColor.x),
        webglBuffergeometryLinearToSrgb(linearColor.y),
        webglBuffergeometryLinearToSrgb(linearColor.z));
    const float fogFactor = smoothstep(2000.0f, 3500.0f, fogDepth);
    displayColor = lerp(
        displayColor,
        float3(0.0196078431372549f),
        fogFactor);
    return float4(displayColor, vertexColor.w);
}

/** Reproduces the first transparent DoubleSide draw that renders back faces. */
class WebglBuffergeometryBackSidePass final : public IRenderClass
{
public:
    /** Preserves Three's BackSide culling, normal blending, and depth-write state. */
    constructor(BindGroup<WebglBuffergeometryBindGroup> bindGroup [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::Back);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Applies FLIP_SIDED normal negation before preparing view-space varyings. */
    WebglBuffergeometryVertexOutput vertex(
        WebglBuffergeometryVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.position, 1.0f));
        const float3 viewNormal = -mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.normal, 0.0f)).xyz;

        WebglBuffergeometryVertexOutput outputValue;
        float4 clipPosition = mul(
            bindGroup->uniforms->projection,
            viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = webglBuffergeometryApplyCoverageOffset(
            clipPosition,
            bindGroup->uniforms->samplePositionAndReserved.xy);
        outputValue.viewNormal = viewNormal;
        outputValue.vertexColor = inputValue.color;
        outputValue.viewDirectionVector = -viewPosition.xyz;
        outputValue.fogDepth = -viewPosition.z;
        return outputValue;
    }

    /** Shades and blends one upstream BackSide fragment with authored vertex alpha. */
    WebglBuffergeometryFrameBuffer fragment(
        WebglBuffergeometryVertexOutput inputValue)
    {
        const float4 color = webglBuffergeometryShade(
            inputValue.viewNormal,
            inputValue.vertexColor,
            inputValue.viewDirectionVector,
            inputValue.fogDepth);
        WebglBuffergeometryFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Reproduces the second transparent DoubleSide draw that renders front faces. */
class WebglBuffergeometryFrontSidePass final : public IRenderClass
{
public:
    /** Preserves Three's FrontSide culling, normal blending, and depth-write state. */
    constructor(BindGroup<WebglBuffergeometryBindGroup> bindGroup [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::Front);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Preserves the authored normal while preparing view-space Phong varyings. */
    WebglBuffergeometryVertexOutput vertex(
        WebglBuffergeometryVertex inputValue [[VertexInput0]])
    {
        const float4 viewPosition = mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.position, 1.0f));
        const float3 viewNormal = mul(
            bindGroup->uniforms->modelView,
            float4(inputValue.normal, 0.0f)).xyz;

        WebglBuffergeometryVertexOutput outputValue;
        float4 clipPosition = mul(
            bindGroup->uniforms->projection,
            viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        outputValue.position = webglBuffergeometryApplyCoverageOffset(
            clipPosition,
            bindGroup->uniforms->samplePositionAndReserved.xy);
        outputValue.viewNormal = viewNormal;
        outputValue.vertexColor = inputValue.color;
        outputValue.viewDirectionVector = -viewPosition.xyz;
        outputValue.fogDepth = -viewPosition.z;
        return outputValue;
    }

    /** Shades and blends one upstream FrontSide fragment with authored vertex alpha. */
    WebglBuffergeometryFrameBuffer fragment(
        WebglBuffergeometryVertexOutput inputValue)
    {
        const float4 color = webglBuffergeometryShade(
            inputValue.viewNormal,
            inputValue.vertexColor,
            inputValue.viewDirectionVector,
            inputValue.fogDepth);
        WebglBuffergeometryFrameBuffer frameBuffer;
        frameBuffer.color = half4(color);
        return frameBuffer;
    }
};

/** Owns the standalone Float32 geometry and all private DSL rendering resources. */
class WebglBuffergeometryRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglBuffergeometryUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglBuffergeometryBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometryBackSidePass> backSidePass0;
    RenderClass<WebglBuffergeometryFrontSidePass> frontSidePass0;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    bool sceneRendered = false;

public:
    /** Creates standalone Float32 vertex and uniform buffers through generated DSL methods. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglBuffergeometryVertices",
            WebglBuffergeometryVertexCount);
        uniformBuffer0 = device->createBuffer(
            "WebglBuffergeometryUniforms0", 1u);
    }

    /** Allocates deterministic single-sample color and depth targets. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryOutput", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryDepth", width, height, 1u);
    }

    /** Uploads immutable Float32 geometry and the single-sample transforms. */
    void configureScene(
        const eastl::vector<WebglBuffergeometryVertex> &vertices,
        float4x4 projection,
        float4x4 modelView)
    {
        WebglBuffergeometryUniforms uniforms0;
        uniforms0.projection = projection;
        uniforms0.modelView = modelView;
        uniforms0.samplePositionAndReserved =
            float4(0.5f, 0.5f, 0.0f, 0.0f);

        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) * sizeof(WebglBuffergeometryVertex))
            ->writeBuffer(
                BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->submit();

        bindGroup0 = device->createBindGroup<WebglBuffergeometryBindGroup>(
            uniformBuffer0);
        backSidePass0 =
            device->createRenderClass<WebglBuffergeometryBackSidePass>(
                bindGroup0);
        frontSidePass0 =
            device->createRenderClass<WebglBuffergeometryFrontSidePass>(
                bindGroup0);
    }

    /** Rasterizes BackSide then FrontSide directly into the single-sample output. */
    void render() override
    {
        auto nextTexture = swapchain->queryNextTexture();
        if (!sceneRendered)
        {
            WebglBuffergeometryFrameBuffer frameBuffer0;
            frameBuffer0.color = outputTexture->createView();
            frameBuffer0.color.loadOp = LoadOp::Clear;
            frameBuffer0.color.storeOp = StoreOp::Store;
            frameBuffer0.color.clearValue = {
                5.0 / 255.0, 5.0 / 255.0, 5.0 / 255.0, 1.0};
            frameBuffer0.depth = depthTexture->createView();
            frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
            frameBuffer0.depth.depthStoreOp = StoreOp::Store;
            frameBuffer0.depth.depthClearValue = 1.0f;

            graphicsQueue
                ->renderPass(
                    "WebglBuffergeometryMain",
                    frameBuffer0,
                    backSidePass0->setVertexBuffer(vertexBuffer),
                    backSidePass0(WebglBuffergeometryVertexCount, 1u, 0u, 0u),
                    frontSidePass0->setVertexBuffer(vertexBuffer),
                    frontSidePass0(WebglBuffergeometryVertexCount, 1u, 0u, 0u))
                ->submit();
            sceneRendered = true;
        }

        graphicsQueue
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-created RGBA8 output for deterministic readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the locked output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the locked output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases all standalone buffers and private sample textures. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
