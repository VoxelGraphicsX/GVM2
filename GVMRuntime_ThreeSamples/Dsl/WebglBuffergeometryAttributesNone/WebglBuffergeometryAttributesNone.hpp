#ifndef GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_NONE_HPP
#define GVM_THREE_WEBGL_BUFFERGEOMETRY_ATTRIBUTES_NONE_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebglBuffergeometryAttributesNoneVertexCount = 30000u;

/** Stores the fixed-step animation, viewport aspect, and upstream seed value. */
struct WebglBuffergeometryAttributesNoneUniforms
{
    float4 timeAspectSeedAndReserved;
    float4 samplePositionAndReserved;
};

/** Binds the only uniform buffer used by the attribute-free RawShaderMaterial port. */
struct WebglBuffergeometryAttributesNoneBindGroup final : public IBindGroup
{
    /** Declares the frame-state binding without standalone vertex or index buffers. */
    constructor(UniformBuffer<WebglBuffergeometryAttributesNoneUniforms> uniforms [[Binding0]])
    {
    }
};

/** Carries the procedural vertex color to the fragment stage. */
struct WebglBuffergeometryAttributesNoneVertexOutput
{
    float4 position [[Position]];
    float3 color [[Attribute0]];
};

/** Defines the RGBA8 scene target for the attribute-free geometry sample. */
struct WebglBuffergeometryAttributesNoneFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Applies the scalar Jenkins-style hash used by the r185 source shader. */
uint webglBuffergeometryAttributesNoneHash(uint value)
{
    value += value << 10u;
    value ^= value >> 6u;
    value += value << 3u;
    value ^= value >> 11u;
    value += value << 15u;
    return value;
}

/** Applies the r185 two-component hash composition to scalar bitcasts. */
uint webglBuffergeometryAttributesNoneHashPair(uint x, uint y)
{
    return webglBuffergeometryAttributesNoneHash(
        x ^ webglBuffergeometryAttributesNoneHash(y));
}

/** Converts hashed float-coordinate bits into one value in the half-open unit interval. */
float webglBuffergeometryAttributesNoneHashNoise(float2 coordinates)
{
    const uint ieeeMantissa = 0x007fffffu;
    const uint ieeeOne = 0x3f800000u;
    uint bits = webglBuffergeometryAttributesNoneHashPair(
        asuint(coordinates.x),
        asuint(coordinates.y));
    bits &= ieeeMantissa;
    bits |= ieeeOne;
    return asfloat(bits) - 1.0f;
}

/** Maps the r185 hash noise to a caller-provided scalar interval. */
float webglBuffergeometryAttributesNonePseudoRandom(
    float lower,
    float delta,
    float2 coordinates)
{
    return lower + delta * webglBuffergeometryAttributesNoneHashNoise(coordinates);
}

/** Generates the deterministic r185 pseudo-random vector for one integer index. */
float3 webglBuffergeometryAttributesNonePseudoRandomVector(
    float lower,
    float upper,
    uint index)
{
    const float delta = upper - lower;
    const float indexValue = float(index);
    const float x = webglBuffergeometryAttributesNonePseudoRandom(
        lower, delta, float2(indexValue, 0.0f));
    const float y = webglBuffergeometryAttributesNonePseudoRandom(
        lower, delta, float2(indexValue, 1.0f));
    const float z = webglBuffergeometryAttributesNonePseudoRandom(
        lower, delta, float2(indexValue, 2.0f));
    return float3(x, y, z);
}

/** Reproduces the single attribute-free r185 mesh with a procedural VertexID shader. */
class WebglBuffergeometryAttributesNoneScenePass final : public IRenderClass
{
public:
    /** Binds only deterministic frame state and disables culling for Three.DoubleSide parity. */
    constructor(BindGroup<WebglBuffergeometryAttributesNoneBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reconstructs positions and colors from VertexID using the exact r185 bitcast hash. */
    WebglBuffergeometryAttributesNoneVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float timeSeconds = bindGroup->uniforms->timeAspectSeedAndReserved.x;
        const float aspect = bindGroup->uniforms->timeAspectSeedAndReserved.y;
        const float upstreamSeed = bindGroup->uniforms->timeAspectSeedAndReserved.z;
        const float2 samplePosition = bindGroup->uniforms->samplePositionAndReserved.xy;
        const float scale = 1.0f / 64.0f;
        float3 position = webglBuffergeometryAttributesNonePseudoRandomVector(
                              -1.0f, 1.0f, vertexID / 3u) +
                          scale * webglBuffergeometryAttributesNonePseudoRandomVector(
                                      -1.0f, 1.0f, vertexID);
        const float3 color = webglBuffergeometryAttributesNonePseudoRandomVector(
            0.25f, 1.0f, vertexID / 3u);

        const float rotationX = timeSeconds * 0.25f;
        const float rotationY = timeSeconds * 0.50f;
        const float sineX = sin(rotationX);
        const float cosineX = cos(rotationX);
        const float sineY = sin(rotationY);
        const float cosineY = cos(rotationY);
        const float3 rotatedY = float3(
            position.x * cosineY + position.z * sineY,
            position.y,
            -position.x * sineY + position.z * cosineY);
        position = float3(
            rotatedY.x,
            rotatedY.y * cosineX - rotatedY.z * sineX,
            rotatedY.y * sineX + rotatedY.z * cosineX);

        const float viewZ = position.z - 4.0f;
        const float clipW = -viewZ;
        const float projectionScale = 1.0f / tan(27.0f * 3.14159265358979323846f / 360.0f);
        const float nearPlane = 1.0f;
        const float farPlane = 3500.0f;
        const float clipZ = ((farPlane + nearPlane) / (nearPlane - farPlane)) * viewZ +
                            (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);

        WebglBuffergeometryAttributesNoneVertexOutput outputValue;
        float4 clipPosition = float4(
            position.x * projectionScale / aspect,
            -position.y * projectionScale,
            clipZ + upstreamSeed * 0.0f,
            clipW);
        clipPosition.x += (1.0f - 2.0f * samplePosition.x) / 800.0f * clipW;
        clipPosition.y += (2.0f * samplePosition.y - 1.0f) / 500.0f * clipW;
        outputValue.position = clipPosition;
        outputValue.color = color;
        return outputValue;
    }

    /** Writes the perspective-interpolated procedural color from the r185 RawShaderMaterial. */
    WebglBuffergeometryAttributesNoneFrameBuffer fragment(
        WebglBuffergeometryAttributesNoneVertexOutput inputValue)
    {
        WebglBuffergeometryAttributesNoneFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(inputValue.color), half(1.0f));
        return frameBuffer;
    }
};

/** Owns and submits the deterministic r185 attribute-free geometry sample. */
class WebglBuffergeometryAttributesNoneRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglBuffergeometryAttributesNoneUniforms, BufferUsage<Uniform, CopyDst>> uniformBuffer0;
    BindGroup<WebglBuffergeometryAttributesNoneBindGroup> bindGroup0;
    RenderClass<WebglBuffergeometryAttributesNoneScenePass> scenePass0;
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
    uint frameIndex = 0u;

public:
    /** Creates one procedural RenderClass and its existing uniform-buffer resources. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        uniformBuffer0 = device->createBuffer("WebglBuffergeometryAttributesNoneUniforms0", 1u);
        bindGroup0 = device->createBindGroup<WebglBuffergeometryAttributesNoneBindGroup>(uniformBuffer0);
        scenePass0 = device->createRenderClass<WebglBuffergeometryAttributesNoneScenePass>(bindGroup0);
    }

    /** Allocates the deterministic RGBA8 target at the host-requested 800 by 500 extent. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglBuffergeometryAttributesNoneRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebglBuffergeometryAttributesNoneDepth32", width, height, 1u);
    }

    /** Rasterizes 10,000 triangles directly into the single-sample output. */
    void render() override
    {
        WebglBuffergeometryAttributesNoneUniforms uniforms0;
        const float4 frameState = float4(
            float(frameIndex) / 60.0f,
            float(readbackWidth) / float(readbackHeight),
            42.0f,
            0.0f);
        uniforms0.timeAspectSeedAndReserved = frameState;
        uniforms0.samplePositionAndReserved = float4(0.5f, 0.5f, 0.0f, 0.0f);

        auto nextTexture = swapchain->queryNextTexture();
        WebglBuffergeometryAttributesNoneFrameBuffer frameBuffer0;
        frameBuffer0.color = outputTexture->createView();
        frameBuffer0.color.loadOp = LoadOp::Clear;
        frameBuffer0.color.storeOp = StoreOp::Store;
        frameBuffer0.color.clearValue = {0.0196078431372549, 0.0196078431372549, 0.0196078431372549, 1.0};
        frameBuffer0.depth = depthTexture->createView();
        frameBuffer0.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer0.depth.depthStoreOp = StoreOp::Store;
        frameBuffer0.depth.depthClearValue = 1.0f;

        graphicsQueue
            ->writeBuffer(BufferRange(uniformBuffer0), &uniforms0, sizeof(uniforms0))
            ->renderPass(
                "WebglBuffergeometryAttributesNoneMain",
                frameBuffer0,
                scenePass0(WebglBuffergeometryAttributesNoneVertexCount, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
        ++frameIndex;
    }

    /** Returns the DSL-created RGBA8 output for deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the uniform and output resources after capture completes. */
    void destroy() override
    {
        device->freeBuffer(uniformBuffer0);
        device->freeTexture(depthTexture);
        device->freeTexture(outputTexture);
    }
};

#endif
