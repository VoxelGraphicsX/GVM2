#ifndef GVM_THREE_WEBGL_GPGPU_PROTOPLANET_HPP
#define GVM_THREE_WEBGL_GPGPU_PROTOPLANET_HPP

#include "UGL.h"
#include "WebglGpgpuProtoplanetData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglGpgpuProtoplanetTextureWidth = 64u;
static const uint WebglGpgpuProtoplanetParticleCount = WebglGpgpuProtoplanetTextureWidth * WebglGpgpuProtoplanetTextureWidth;
static const uint WebglGpgpuProtoplanetVertexCount = WebglGpgpuProtoplanetParticleCount * 6u;
static const uint WebglGpgpuProtoplanetOutputWidth = 800u;
static const uint WebglGpgpuProtoplanetOutputHeight = 500u;

/** Binds the old ping-pong pair and dynamic collision parameters to velocity. */
struct WebglGpgpuProtoplanetVelocityBindGroup final : public IBindGroup
{
    /** Declares the exact sampled resources used by the upstream fragment pass. */
    constructor(UniformBuffer<WebglGpgpuProtoplanetSimulationUniforms> uniforms [[Binding0]], Texture2D<float4> positionTexture [[Binding1]], Texture2D<float4> velocityTexture [[Binding2]])
    {
    }
};

/** Binds the old ping-pong pair to the fixed-step position integration pass. */
struct WebglGpgpuProtoplanetPositionBindGroup final : public IBindGroup
{
    /** Declares both immutable inputs consumed before the ping-pong index changes. */
    constructor(Texture2D<float4> positionTexture [[Binding0]], Texture2D<float4> velocityTexture [[Binding1]])
    {
    }
};

/** Binds the current simulation pair and camera state to the only Scene object. */
struct WebglGpgpuProtoplanetRenderBindGroup final : public IBindGroup
{
    /** Declares camera uniforms and the two current RGBA32Float state textures. */
    constructor(UniformBuffer<WebglGpgpuProtoplanetRenderUniforms> uniforms [[Binding0]], Texture2D<float4> positionTexture [[Binding1]], Texture2D<float4> velocityTexture [[Binding2]])
    {
    }
};

/** Carries one fullscreen triangle through an RGBA32Float simulation pass. */
struct WebglGpgpuProtoplanetSimulationVertexOutput
{
    float4 position [[Position]];
};

/** Defines one full-precision GPUComputationRenderer-equivalent attachment. */
struct WebglGpgpuProtoplanetSimulationFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
};

/** Carries color plus flat axis-affine native point reconstruction data. */
struct WebglGpgpuProtoplanetVertexOutput
{
    float4 position [[Position]];
    float4 particleColor [[Attribute0]];
    uint4 pointCoverageBits [[Attribute1]];
};

/** Defines the direct single-sample canvas color and default depth targets. */
struct WebglGpgpuProtoplanetFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Returns the exact sphere radius used by all three upstream shader stages. */
inline float webglGpgpuProtoplanetRadiusFromMass(float mass, float density)
{
    const float pi = 3.14159265358979323846f;
    return pow((3.0f / (4.0f * pi)) * mass / density, 1.0f / 3.0f);
}

/** Rounds one screen-space value to the requested native subpixel scale. */
inline float webglGpgpuProtoplanetRoundSubpixel(float value, float scale)
{
    return floor(value * scale + 0.5f) / scale;
}

/** Generates the same oversized fullscreen triangle as GPUComputationRenderer. */
inline WebglGpgpuProtoplanetSimulationVertexOutput webglGpgpuProtoplanetFullscreenVertex(uint vertexID)
{
    const float2 textureCoordinate = float2((vertexID << 1u) & 2u, vertexID & 2u);
    WebglGpgpuProtoplanetSimulationVertexOutput outputValue;
    outputValue.position = float4(textureCoordinate * 2.0f - 1.0f, 0.0f, 1.0f);
    return outputValue;
}

/** Reproduces the r185 all-pairs collision and gravity fragment pass. */
class WebglGpgpuProtoplanetVelocityPass final : public IRenderClass
{
public:
    /** Configures a depth-free fullscreen write to one next velocity target. */
    constructor(BindGroup<WebglGpgpuProtoplanetVelocityBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits one fullscreen triangle without standalone geometry buffers. */
    WebglGpgpuProtoplanetSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglGpgpuProtoplanetFullscreenVertex(vertexID);
    }

    /** Scans all 4,096 old particles in ascending texture order. */
    WebglGpgpuProtoplanetSimulationFrameBuffer fragment(WebglGpgpuProtoplanetSimulationVertexOutput inputValue)
    {
        const uint2 particleCoordinate = uint2(inputValue.position.xy);
        const uint particleID = particleCoordinate.y * WebglGpgpuProtoplanetTextureWidth + particleCoordinate.x;
        const float3 position = bindGroup->positionTexture->read(particleCoordinate).xyz;
        const float4 velocityAndMass = bindGroup->velocityTexture->read(particleCoordinate);
        float3 velocity = velocityAndMass.xyz;
        float mass = velocityAndMass.w;
        const float gravityConstant = bindGroup->uniforms->gravityDensityAndReserved.x;
        const float density = bindGroup->uniforms->gravityDensityAndReserved.y;

        if (mass > 0.0f)
        {
            float radius = webglGpgpuProtoplanetRadiusFromMass(mass, density);
            float3 acceleration = float3(0.0f);
            for (uint y = 0u; y < WebglGpgpuProtoplanetTextureWidth; ++y)
            {
                for (uint x = 0u; x < WebglGpgpuProtoplanetTextureWidth; ++x)
                {
                    const uint2 secondCoordinate = uint2(x, y);
                    const uint secondParticleID = y * WebglGpgpuProtoplanetTextureWidth + x;
                    if (particleID == secondParticleID)
                    {
                        continue;
                    }

                    const float3 secondPosition = bindGroup->positionTexture->read(secondCoordinate).xyz;
                    const float4 secondVelocityAndMass = bindGroup->velocityTexture->read(secondCoordinate);
                    const float3 secondVelocity = secondVelocityAndMass.xyz;
                    const float secondMass = secondVelocityAndMass.w;
                    if (secondMass == 0.0f)
                    {
                        continue;
                    }

                    const float3 positionDelta = secondPosition - position;
                    const float distance = length(positionDelta);
                    const float secondRadius = webglGpgpuProtoplanetRadiusFromMass(secondMass, density);
                    if (distance == 0.0f)
                    {
                        continue;
                    }

                    if (distance < radius + secondRadius)
                    {
                        if (particleID < secondParticleID)
                        {
                            velocity = (velocity * mass + secondVelocity * secondMass) / (mass + secondMass);
                            mass += secondMass;
                            radius = webglGpgpuProtoplanetRadiusFromMass(mass, density);
                        }
                        else
                        {
                            mass = 0.0f;
                            radius = 0.0f;
                            velocity = float3(0.0f);
                            break;
                        }
                    }

                    const float distanceSquared = distance * distance;
                    float gravityField = gravityConstant * secondMass / distanceSquared;
                    gravityField = min(gravityField, 1000.0f);
                    acceleration += gravityField * normalize(positionDelta);
                }

                if (mass == 0.0f)
                {
                    break;
                }
            }
            velocity += (1.0f / 60.0f) * acceleration;
        }

        WebglGpgpuProtoplanetSimulationFrameBuffer frameBuffer;
        frameBuffer.color = float4(velocity, mass);
        return frameBuffer;
    }
};

/** Reproduces the r185 fixed-step position fragment pass. */
class WebglGpgpuProtoplanetPositionPass final : public IRenderClass
{
public:
    /** Configures a depth-free fullscreen write to one next position target. */
    constructor(BindGroup<WebglGpgpuProtoplanetPositionBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(false);
        setDepthCompareFunction(CompareFunction::Always);
    }

private:
    /** Emits one fullscreen triangle without standalone geometry buffers. */
    WebglGpgpuProtoplanetSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        return webglGpgpuProtoplanetFullscreenVertex(vertexID);
    }

    /** Integrates one old position from the old velocity exactly once. */
    WebglGpgpuProtoplanetSimulationFrameBuffer fragment(WebglGpgpuProtoplanetSimulationVertexOutput inputValue)
    {
        const uint2 coordinate = uint2(inputValue.position.xy);
        const float3 position = bindGroup->positionTexture->read(coordinate).xyz;
        float3 velocity = bindGroup->velocityTexture->read(coordinate).xyz;
        const float mass = bindGroup->velocityTexture->read(coordinate).w;
        if (mass == 0.0f)
        {
            velocity = float3(0.0f);
        }

        WebglGpgpuProtoplanetSimulationFrameBuffer frameBuffer;
        frameBuffer.color = float4(position + velocity * (1.0f / 60.0f), 1.0f);
        return frameBuffer;
    }
};

/** Draws the sole Points object as one non-instanced triangle-expanded object. */
class WebglGpgpuProtoplanetMainPass final : public IRenderClass
{
public:
    /** Reproduces opaque ShaderMaterial depth behavior without triangle culling. */
    constructor(BindGroup<WebglGpgpuProtoplanetRenderBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one logical native point over its quantized coverage square. */
    WebglGpgpuProtoplanetVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const uint particleIndex = vertexID / 6u;
        const uint localVertex = vertexID - particleIndex * 6u;
        float2 corner = float2(-1.0f, -1.0f);
        if (localVertex == 1u || localVertex == 4u || localVertex == 5u)
        {
            corner.x = 1.0f;
        }
        if (localVertex == 2u || localVertex == 3u || localVertex == 5u)
        {
            corner.y = 1.0f;
        }

        const uint2 coordinate = uint2(particleIndex & (WebglGpgpuProtoplanetTextureWidth - 1u), particleIndex / WebglGpgpuProtoplanetTextureWidth);
        const float3 worldPosition = bindGroup->positionTexture->read(coordinate).xyz;
        const float mass = bindGroup->velocityTexture->read(coordinate).w;
        const float density = bindGroup->uniforms->cameraDensityAndViewport.y;
        const float radius = webglGpgpuProtoplanetRadiusFromMass(mass, density);
        const float4 viewPosition = mul(bindGroup->uniforms->viewMatrix, float4(worldPosition, 1.0f));
        float4 clipPosition = mul(bindGroup->uniforms->projectionMatrix, viewPosition);

        const float rawPointSize = radius * bindGroup->uniforms->cameraDensityAndViewport.x / (-viewPosition.z);
        const float pointSize = clamp(rawPointSize, 1.0f, 511.0f);
        const float2 pointCenter = (clipPosition.xy / clipPosition.w + float2(1.0f)) * float2(400.0f, 250.0f);
        const float2 topPointCenter = float2(pointCenter.x, float(WebglGpgpuProtoplanetOutputHeight) - pointCenter.y);

        // A 4,096-particle Chrome/ANGLE probe establishes that native
        // PointCoord rounds both axis endpoints independently to 1/16 pixel.
        const float2 halfPointSize = float2(pointSize * 0.5f);
        const float2 roundedCoverageLow = float2(webglGpgpuProtoplanetRoundSubpixel(topPointCenter.x - halfPointSize.x, 16.0f), webglGpgpuProtoplanetRoundSubpixel(topPointCenter.y - halfPointSize.y, 16.0f));
        const float2 roundedCoverageHigh = float2(webglGpgpuProtoplanetRoundSubpixel(topPointCenter.x + halfPointSize.x, 16.0f), webglGpgpuProtoplanetRoundSubpixel(topPointCenter.y + halfPointSize.y, 16.0f));
        const float2 coverageSize = roundedCoverageHigh - roundedCoverageLow;
        const float2 pointCoverageEdge = float2(webglGpgpuProtoplanetRoundSubpixel(topPointCenter.x - halfPointSize.x, 256.0f), webglGpgpuProtoplanetRoundSubpixel(topPointCenter.y - (coverageSize.y - halfPointSize.y), 256.0f));
        const float2 pointCoverageCenter = pointCoverageEdge + coverageSize * 0.5f;
        const float2 conservativeCoverageSize = coverageSize + float2(0.125f);
        clipPosition.x = (pointCoverageCenter.x / 400.0f - 1.0f) * clipPosition.w;
        clipPosition.y = ((float(WebglGpgpuProtoplanetOutputHeight) - pointCoverageCenter.y) / 250.0f - 1.0f) * clipPosition.w;
        clipPosition.x += corner.x * conservativeCoverageSize.x / float(WebglGpgpuProtoplanetOutputWidth) * clipPosition.w;
        clipPosition.y += corner.y * conservativeCoverageSize.y / float(WebglGpgpuProtoplanetOutputHeight) * clipPosition.w;
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglGpgpuProtoplanetVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.particleColor = float4(1.0f, mass / 250.0f, 0.0f, 1.0f);
        outputValue.pointCoverageBits = uint4(asuint(pointCoverageEdge.x), asuint(pointCoverageEdge.y), asuint(coverageSize.x), asuint(coverageSize.y));
        return outputValue;
    }

    /** Applies the upstream dead-particle and circular PointCoord discards. */
    WebglGpgpuProtoplanetFrameBuffer fragment(WebglGpgpuProtoplanetVertexOutput inputValue)
    {
        if (inputValue.particleColor.y == 0.0f)
        {
            discard_fragment();
        }
        const float4 pointCoverage = float4(asfloat(inputValue.pointCoverageBits.x), asfloat(inputValue.pointCoverageBits.y), asfloat(inputValue.pointCoverageBits.z), asfloat(inputValue.pointCoverageBits.w));
        const float2 pointCoord = (inputValue.position.xy - pointCoverage.xy) / pointCoverage.zw;
        const float radialDistance = length(pointCoord - float2(0.5f));
        if (radialDistance > 0.5f)
        {
            discard_fragment();
        }

        WebglGpgpuProtoplanetFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.particleColor);
        return frameBuffer;
    }
};

/** Owns the fragment ping-pong simulation and sole ordinary Scene draw. */
class WebglGpgpuProtoplanetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglGpgpuProtoplanetSimulationUniforms, BufferUsage<Uniform, CopyDst>> simulationUniformBuffer;
    Buffer<WebglGpgpuProtoplanetRenderUniforms, BufferUsage<Uniform, CopyDst>> renderUniformBuffer;
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> positionTexture0;
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> positionTexture1;
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> velocityTexture0;
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> velocityTexture1;
    BindGroup<WebglGpgpuProtoplanetVelocityBindGroup> velocityBindGroup0To1;
    BindGroup<WebglGpgpuProtoplanetVelocityBindGroup> velocityBindGroup1To0;
    BindGroup<WebglGpgpuProtoplanetPositionBindGroup> positionBindGroup0To1;
    BindGroup<WebglGpgpuProtoplanetPositionBindGroup> positionBindGroup1To0;
    BindGroup<WebglGpgpuProtoplanetRenderBindGroup> renderBindGroup0;
    BindGroup<WebglGpgpuProtoplanetRenderBindGroup> renderBindGroup1;
    RenderClass<WebglGpgpuProtoplanetVelocityPass> velocityPass0To1;
    RenderClass<WebglGpgpuProtoplanetVelocityPass> velocityPass1To0;
    RenderClass<WebglGpgpuProtoplanetPositionPass> positionPass0To1;
    RenderClass<WebglGpgpuProtoplanetPositionPass> positionPass1To0;
    RenderClass<WebglGpgpuProtoplanetMainPass> mainPass0;
    RenderClass<WebglGpgpuProtoplanetMainPass> mainPass1;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float, TextureUsage<RenderAttachment>, TextureDimension::e2D> depthTexture;
    uint frameIndex = 0u;
    uint readbackWidth = WebglGpgpuProtoplanetOutputWidth;
    uint readbackHeight = WebglGpgpuProtoplanetOutputHeight;

public:
    /** Creates only existing DSL buffers and full-precision ping-pong textures. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        simulationUniformBuffer = device->createBuffer("WebglGpgpuProtoplanetSimulationUniforms", 1u);
        renderUniformBuffer = device->createBuffer("WebglGpgpuProtoplanetRenderUniforms", 1u);
        positionTexture0 = device->createTexture("WebglGpgpuProtoplanetPosition0RGBA32Float", WebglGpgpuProtoplanetTextureWidth, WebglGpgpuProtoplanetTextureWidth, 1u);
        positionTexture1 = device->createTexture("WebglGpgpuProtoplanetPosition1RGBA32Float", WebglGpgpuProtoplanetTextureWidth, WebglGpgpuProtoplanetTextureWidth, 1u);
        velocityTexture0 = device->createTexture("WebglGpgpuProtoplanetVelocity0RGBA32Float", WebglGpgpuProtoplanetTextureWidth, WebglGpgpuProtoplanetTextureWidth, 1u);
        velocityTexture1 = device->createTexture("WebglGpgpuProtoplanetVelocity1RGBA32Float", WebglGpgpuProtoplanetTextureWidth, WebglGpgpuProtoplanetTextureWidth, 1u);
    }

    /** Allocates the exact direct 800 by 500 color and depth outputs. */
    void configureOutput(uint width, uint height)
    {
        if (width != WebglGpgpuProtoplanetOutputWidth || height != WebglGpgpuProtoplanetOutputHeight)
        {
            return;
        }
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebglGpgpuProtoplanetOutputRGBA8", width, height, 1u);
        depthTexture = device->createTexture("WebglGpgpuProtoplanetDepth32", width, height, 1u);
    }

    /** Uploads one exact initial or restarted Float32 state to both ping-pong sides. */
    void configureScene(const eastl::vector<float4> &initialPosition, const eastl::vector<float4> &initialVelocity)
    {
        graphicsQueue->writeTexture(positionTexture0, initialPosition.data(), uint64_t(WebglGpgpuProtoplanetParticleCount) * sizeof(float4))
            ->writeTexture(positionTexture1, initialPosition.data(), uint64_t(WebglGpgpuProtoplanetParticleCount) * sizeof(float4))
            ->writeTexture(velocityTexture0, initialVelocity.data(), uint64_t(WebglGpgpuProtoplanetParticleCount) * sizeof(float4))
            ->writeTexture(velocityTexture1, initialVelocity.data(), uint64_t(WebglGpgpuProtoplanetParticleCount) * sizeof(float4))
            ->submit();

        velocityBindGroup0To1 = device->createBindGroup<WebglGpgpuProtoplanetVelocityBindGroup>(simulationUniformBuffer, positionTexture0->createView(), velocityTexture0->createView());
        velocityBindGroup1To0 = device->createBindGroup<WebglGpgpuProtoplanetVelocityBindGroup>(simulationUniformBuffer, positionTexture1->createView(), velocityTexture1->createView());
        positionBindGroup0To1 = device->createBindGroup<WebglGpgpuProtoplanetPositionBindGroup>(positionTexture0->createView(), velocityTexture0->createView());
        positionBindGroup1To0 = device->createBindGroup<WebglGpgpuProtoplanetPositionBindGroup>(positionTexture1->createView(), velocityTexture1->createView());
        renderBindGroup0 = device->createBindGroup<WebglGpgpuProtoplanetRenderBindGroup>(renderUniformBuffer, positionTexture0->createView(), velocityTexture0->createView());
        renderBindGroup1 = device->createBindGroup<WebglGpgpuProtoplanetRenderBindGroup>(renderUniformBuffer, positionTexture1->createView(), velocityTexture1->createView());

        velocityPass0To1 = device->createRenderClass<WebglGpgpuProtoplanetVelocityPass>(velocityBindGroup0To1);
        velocityPass1To0 = device->createRenderClass<WebglGpgpuProtoplanetVelocityPass>(velocityBindGroup1To0);
        positionPass0To1 = device->createRenderClass<WebglGpgpuProtoplanetPositionPass>(positionBindGroup0To1);
        positionPass1To0 = device->createRenderClass<WebglGpgpuProtoplanetPositionPass>(positionBindGroup1To0);
        mainPass0 = device->createRenderClass<WebglGpgpuProtoplanetMainPass>(renderBindGroup0);
        mainPass1 = device->createRenderClass<WebglGpgpuProtoplanetMainPass>(renderBindGroup1);
    }

    /** Uploads one callback's dynamic parameters and frozen camera state. */
    void updateFrame(WebglGpgpuProtoplanetSimulationUniforms simulationUniforms, float4x4 projectionMatrix, float4x4 viewMatrix, float cameraConstant, float density, uint currentFrameIndex)
    {
        WebglGpgpuProtoplanetRenderUniforms renderUniforms;
        renderUniforms.projectionMatrix = projectionMatrix;
        renderUniforms.viewMatrix = viewMatrix;
        renderUniforms.cameraDensityAndViewport = float4(cameraConstant, density, float(WebglGpgpuProtoplanetOutputWidth), float(WebglGpgpuProtoplanetOutputHeight));
        frameIndex = currentFrameIndex;
        graphicsQueue->writeBuffer(BufferRange(simulationUniformBuffer), &simulationUniforms, sizeof(simulationUniforms))->writeBuffer(BufferRange(renderUniformBuffer), &renderUniforms, sizeof(renderUniforms))->submit();
    }

    /** Runs velocity then position fragments and draws the resulting state once. */
    void render() override
    {
        WebglGpgpuProtoplanetSimulationFrameBuffer velocityFrameBuffer;
        WebglGpgpuProtoplanetSimulationFrameBuffer positionFrameBuffer;
        WebglGpgpuProtoplanetFrameBuffer sceneFrameBuffer;
        sceneFrameBuffer.color = outputTexture->createView();
        sceneFrameBuffer.color.loadOp = LoadOp::Clear;
        sceneFrameBuffer.color.storeOp = StoreOp::Store;
        sceneFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};
        sceneFrameBuffer.depth = depthTexture->createView();
        sceneFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        sceneFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        sceneFrameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();

        if ((frameIndex & 1u) == 0u)
        {
            velocityFrameBuffer.color = velocityTexture1->createView();
            velocityFrameBuffer.color.loadOp = LoadOp::Clear;
            velocityFrameBuffer.color.storeOp = StoreOp::Store;
            velocityFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
            positionFrameBuffer.color = positionTexture1->createView();
            positionFrameBuffer.color.loadOp = LoadOp::Clear;
            positionFrameBuffer.color.storeOp = StoreOp::Store;
            positionFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
            graphicsQueue->renderPass("WebglGpgpuProtoplanetVelocity0To1", velocityFrameBuffer, velocityPass0To1(3u, 1u, 0u, 0u))->renderPass("WebglGpgpuProtoplanetPosition0To1", positionFrameBuffer, positionPass0To1(3u, 1u, 0u, 0u))->renderPass("main-expanded-circular-particles", sceneFrameBuffer, mainPass1(WebglGpgpuProtoplanetVertexCount, 1u, 0u, 0u))->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})->submit();
        }
        else
        {
            velocityFrameBuffer.color = velocityTexture0->createView();
            velocityFrameBuffer.color.loadOp = LoadOp::Clear;
            velocityFrameBuffer.color.storeOp = StoreOp::Store;
            velocityFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
            positionFrameBuffer.color = positionTexture0->createView();
            positionFrameBuffer.color.loadOp = LoadOp::Clear;
            positionFrameBuffer.color.storeOp = StoreOp::Store;
            positionFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
            graphicsQueue->renderPass("WebglGpgpuProtoplanetVelocity1To0", velocityFrameBuffer, velocityPass1To0(3u, 1u, 0u, 0u))->renderPass("WebglGpgpuProtoplanetPosition1To0", positionFrameBuffer, positionPass1To0(3u, 1u, 0u, 0u))->renderPass("main-expanded-circular-particles", sceneFrameBuffer, mainPass0(WebglGpgpuProtoplanetVertexCount, 1u, 0u, 0u))->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})->submit();
        }
        swapchain->present();
    }

    /** Returns the direct RGBA8 target selected for deterministic host readback. */
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D> getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the fixed reference-capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the fixed reference-capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Returns the current position texture after the selected callback. */
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> getCurrentPositionTextureHandle() const
    {
        return (frameIndex & 1u) == 0u ? positionTexture1 : positionTexture0;
    }

    /** Returns the current velocity texture after the selected callback. */
    Texture<TextureFormat::RGBA32Float, TextureUsage<RenderAttachment, TextureBinding, CopyDst, CopySrc>, TextureDimension::e2D> getCurrentVelocityTextureHandle() const
    {
        return (frameIndex & 1u) == 0u ? velocityTexture1 : velocityTexture0;
    }

    /** Releases every buffer, ping-pong texture, and attachment. */
    void destroy() override
    {
        device->freeBuffer(simulationUniformBuffer);
        device->freeBuffer(renderUniformBuffer);
        device->freeTexture(positionTexture0);
        device->freeTexture(positionTexture1);
        device->freeTexture(velocityTexture0);
        device->freeTexture(velocityTexture1);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
