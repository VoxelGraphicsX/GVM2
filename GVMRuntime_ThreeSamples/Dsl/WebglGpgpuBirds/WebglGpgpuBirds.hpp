#ifndef GVM_THREE_WEBGL_GPGPU_BIRDS_HPP
#define GVM_THREE_WEBGL_GPGPU_BIRDS_HPP

#include "UGL.h"
#include "WebglGpgpuBirdsVertexData.hpp"

#include <EASTL/vector.h>

using namespace UGL;

static const uint WebglGpgpuBirdsTextureWidth = 32u;
static const uint WebglGpgpuBirdsBirdCount =
    WebglGpgpuBirdsTextureWidth * WebglGpgpuBirdsTextureWidth;
static const uint WebglGpgpuBirdsVertexCount =
    WebglGpgpuBirdsBirdCount * 9u;

/** Binds one immutable ping-pong input pair to the velocity simulation pass. */
struct WebglGpgpuBirdsVelocityBindGroup final : public IBindGroup
{
    /** Declares the exact full-precision inputs sampled by the r185 fragment pass. */
    constructor(
        UniformBuffer<WebglGpgpuBirdsSimulationUniforms> uniforms [[Binding0]],
        Texture2D<float4> positionTexture [[Binding1]],
        Texture2D<float4> velocityTexture [[Binding2]],
        Sampler simulationSampler [[Binding3]])
    {
    }
};

/** Binds one immutable ping-pong input pair to the position simulation pass. */
struct WebglGpgpuBirdsPositionBindGroup final : public IBindGroup
{
    /** Declares the exact full-precision inputs sampled by the r185 fragment pass. */
    constructor(
        UniformBuffer<WebglGpgpuBirdsSimulationUniforms> uniforms [[Binding0]],
        Texture2D<float4> positionTexture [[Binding1]],
        Texture2D<float4> velocityTexture [[Binding2]],
        Sampler simulationSampler [[Binding3]])
    {
    }
};

/** Binds the current simulation result pair to the only ordinary bird material. */
struct WebglGpgpuBirdsRenderBindGroup final : public IBindGroup
{
    /** Declares the camera and full-precision current position/velocity textures. */
    constructor(
        UniformBuffer<WebglGpgpuBirdsRenderUniforms> uniforms [[Binding0]],
        Texture2D<float4> positionTexture [[Binding1]],
        Texture2D<float4> velocityTexture [[Binding2]])
    {
    }
};

/** Carries one fullscreen simulation triangle into a 32 by 32 fragment pass. */
struct WebglGpgpuBirdsSimulationVertexOutput
{
    float4 position [[Position]];
};

/** Defines one full-precision ping-pong attachment matching GPUComputationRenderer. */
struct WebglGpgpuBirdsSimulationFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
};

/** Carries the upstream grayscale input and world-space Z to the fragment shader. */
struct WebglGpgpuBirdsVertexOutput
{
    float4 position [[Position]];
    float3 birdColor [[Attribute0]];
    float worldZ [[Attribute1]];
};

/** Defines the direct single-sample r185 RGBA8 and default depth attachments. */
struct WebglGpgpuBirdsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Reproduces the r185 fullscreen fragment velocity pass for one bird texel. */
class WebglGpgpuBirdsVelocityPass final : public IRenderClass
{
public:
    /** Binds one frozen ping-pong direction as an RGBA32Float fragment render. */
    constructor(BindGroup<WebglGpgpuBirdsVelocityBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by GPUComputationRenderer. */
    WebglGpgpuBirdsSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuBirdsSimulationVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Scans all 1,024 birds and returns the next full-precision velocity. */
    WebglGpgpuBirdsSimulationFrameBuffer fragment(
        WebglGpgpuBirdsSimulationVertexOutput inputValue)
    {
        const float delta = bindGroup->uniforms->deltaAndTime.x;
        const float separationDistance =
            bindGroup->uniforms->distancesAndFreedom.x;
        const float alignmentDistance =
            bindGroup->uniforms->distancesAndFreedom.y;
        const float cohesionDistance =
            bindGroup->uniforms->distancesAndFreedom.z;
        const float zoneRadius =
            separationDistance + alignmentDistance + cohesionDistance;
        const float separationThreshold = separationDistance / zoneRadius;
        const float alignmentThreshold =
            (separationDistance + alignmentDistance) / zoneRadius;
        const float zoneRadiusSquared = zoneRadius * zoneRadius;

        const float2 resolution = float2(32.0f, 32.0f);
        const float2 selfUv = inputValue.position.xy / resolution;
        const float3 selfPosition =
            bindGroup->positionTexture
                ->sample(bindGroup->simulationSampler, selfUv)
                .xyz;
        const float3 selfVelocity =
            bindGroup->velocityTexture
                ->sample(bindGroup->simulationSampler, selfUv)
                .xyz;
        float3 birdPosition;
        float3 birdVelocity;
        float distance;
        float3 direction;
        float distanceSquared;
        float force;
        float percent;
        float3 velocity = selfVelocity;
        float limit = 9.0f;

        direction = bindGroup->uniforms->predator.xyz * 800.0f - selfPosition;
        direction.z = 0.0f;
        distance = length(direction);
        distanceSquared = distance * distance;
        if (distance < 150.0f)
        {
            force = (distanceSquared / 22500.0f - 1.0f) * delta * 100.0f;
            velocity += normalize(direction) * force;
            limit += 5.0f;
        }

        direction = selfPosition;
        distance = length(direction);
        direction.y *= 2.5f;
        velocity -= normalize(direction) * delta * 5.0f;

        for (float y = 0.0f; y < 32.0f; y += 1.0f)
        {
            for (float x = 0.0f; x < 32.0f; x += 1.0f)
            {
                const float2 reference =
                    float2(x + 0.5f, y + 0.5f) / resolution;
                birdPosition = bindGroup->positionTexture
                                   ->sample(
                                       bindGroup->simulationSampler,
                                       reference)
                                   .xyz;
                direction = birdPosition - selfPosition;
                distance = length(direction);
                if (distance < 0.0001f)
                {
                    continue;
                }

                distanceSquared = distance * distance;
                if (distanceSquared > zoneRadiusSquared)
                {
                    continue;
                }

                percent = distanceSquared / zoneRadiusSquared;
                if (percent < separationThreshold)
                {
                    force = (separationThreshold / percent - 1.0f) * delta;
                    velocity -= normalize(direction) * force;
                }
                else if (percent < alignmentThreshold)
                {
                    const float thresholdDelta =
                        alignmentThreshold - separationThreshold;
                    const float adjustedPercent =
                        (percent - separationThreshold) / thresholdDelta;
                    birdVelocity = bindGroup->velocityTexture
                                       ->sample(
                                           bindGroup->simulationSampler,
                                           reference)
                                       .xyz;
                    force =
                        (0.5f -
                         cos(adjustedPercent * 6.28318530717958647692f) * 0.5f +
                         0.5f) * delta;
                    velocity += normalize(birdVelocity) * force;
                }
                else
                {
                    const float thresholdDelta = 1.0f - alignmentThreshold;
                    float adjustedPercent = 1.0f;
                    if (thresholdDelta != 0.0f)
                    {
                        adjustedPercent =
                            (percent - alignmentThreshold) / thresholdDelta;
                    }
                    force =
                        (0.5f -
                         (cos(adjustedPercent * 6.28318530717958647692f) *
                              -0.5f +
                          0.5f)) * delta;
                    velocity += normalize(direction) * force;
                }
            }
        }

        if (length(velocity) > limit)
        {
            velocity = normalize(velocity) * limit;
        }
        WebglGpgpuBirdsSimulationFrameBuffer frameBuffer;
        frameBuffer.color = float4(velocity, 1.0f);
        return frameBuffer;
    }
};

/** Reproduces the r185 fullscreen fragment position pass for one bird texel. */
class WebglGpgpuBirdsPositionPass final : public IRenderClass
{
public:
    /** Binds one frozen ping-pong direction as an RGBA32Float fragment render. */
    constructor(BindGroup<WebglGpgpuBirdsPositionBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Generates the fullscreen triangle used by GPUComputationRenderer. */
    WebglGpgpuBirdsSimulationVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 texCoord = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebglGpgpuBirdsSimulationVertexOutput outputValue;
        outputValue.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Integrates the old position with the old velocity exactly once per callback. */
    WebglGpgpuBirdsSimulationFrameBuffer fragment(
        WebglGpgpuBirdsSimulationVertexOutput inputValue)
    {
        const float2 uv =
            inputValue.position.xy / float2(32.0f, 32.0f);
        const float4 positionAndPhase =
            bindGroup->positionTexture->sample(
                bindGroup->simulationSampler,
                uv);
        const float3 velocity =
            bindGroup->velocityTexture
                ->sample(bindGroup->simulationSampler, uv)
                .xyz;
        const float delta = bindGroup->uniforms->deltaAndTime.x;
        float phase =
            positionAndPhase.w + delta +
            length(float2(velocity.x, velocity.z)) * delta * 3.0f +
            max(velocity.y, 0.0f) * delta * 6.0f;
        phase = fmod(phase, 62.83f);
        WebglGpgpuBirdsSimulationFrameBuffer frameBuffer;
        frameBuffer.color =
            float4(positionAndPhase.xyz + velocity * delta * 15.0f, phase);
        return frameBuffer;
    }
};

/** Draws all 1,024 physically batched birds as one ordinary non-instanced object. */
class WebglGpgpuBirdsMainPass final : public IRenderClass
{
public:
    /** Configures opaque DoubleSide triangle rendering with Three's default depth state. */
    constructor(BindGroup<WebglGpgpuBirdsRenderBindGroup> bindGroup [[Slot0]])
    {
        setCullMode(CullMode::None);
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Samples the current simulation textures and applies wing and flight orientation. */
    WebglGpgpuBirdsVertexOutput vertex(
        WebglGpgpuBirdsVertex inputValue [[VertexInput0]])
    {
        const uint2 coordinate = uint2(
            uint(inputValue.reference.x * 32.0f),
            uint(inputValue.reference.y * 32.0f));
        const float4 positionAndPhase =
            bindGroup->positionTexture->read(coordinate);
        const float3 sampledVelocity =
            bindGroup->velocityTexture->read(coordinate).xyz;
        float3 velocity = normalize(sampledVelocity);

        float3 localPosition = inputValue.position;
        if (inputValue.birdVertex == 4.0f ||
            inputValue.birdVertex == 7.0f)
        {
            localPosition.y = sin(positionAndPhase.w) * 5.0f;
        }

        localPosition = float3(
            localPosition.z,
            localPosition.y,
            -localPosition.x);
        velocity.z *= -1.0f;
        const float xz = length(float2(velocity.x, velocity.z));
        const float x = sqrt(1.0f - velocity.y * velocity.y);
        const float cosineY = velocity.x / xz;
        const float sineY = velocity.z / xz;
        const float cosineZ = x;
        const float sineZ = velocity.y;

        const float3 zRotatedPosition = float3(
            cosineZ * localPosition.x - sineZ * localPosition.y,
            sineZ * localPosition.x + cosineZ * localPosition.y,
            localPosition.z);
        float3 worldPosition = float3(
            cosineY * zRotatedPosition.x + sineY * zRotatedPosition.z,
            zRotatedPosition.y,
            -sineY * zRotatedPosition.x + cosineY * zRotatedPosition.z);
        worldPosition += positionAndPhase.xyz;

        const float4 viewPosition = mul(
            bindGroup->uniforms->viewMatrix,
            float4(worldPosition, 1.0f));
        float4 clipPosition = mul(
            bindGroup->uniforms->projectionMatrix,
            viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;

        WebglGpgpuBirdsVertexOutput outputValue;
        outputValue.position = clipPosition;
        outputValue.birdColor = inputValue.birdColor;
        outputValue.worldZ = worldPosition.z;
        return outputValue;
    }

    /** Writes the upstream raw grayscale ShaderMaterial result without fog or transfer. */
    WebglGpgpuBirdsFrameBuffer fragment(
        WebglGpgpuBirdsVertexOutput inputValue)
    {
        const float grayscale =
            0.2f + (1000.0f - inputValue.worldZ) / 1000.0f *
                       inputValue.birdColor.x;
        WebglGpgpuBirdsFrameBuffer frameBuffer;
        frameBuffer.color = half4(float4(grayscale, grayscale, grayscale, 1.0f));
        return frameBuffer;
    }
};

/** Owns both RGBA32Float ping-pong pairs and the sole ordinary Scene draw. */
class WebglGpgpuBirdsRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebglGpgpuBirdsVertex, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<WebglGpgpuBirdsSimulationUniforms, BufferUsage<Uniform, CopyDst>>
        simulationUniformBuffer;
    Buffer<WebglGpgpuBirdsRenderUniforms, BufferUsage<Uniform, CopyDst>>
        renderUniformBuffer;
    Sampler simulationSampler;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        positionTexture0;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        positionTexture1;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        velocityTexture0;
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
        velocityTexture1;
    BindGroup<WebglGpgpuBirdsVelocityBindGroup> velocityBindGroup0To1;
    BindGroup<WebglGpgpuBirdsVelocityBindGroup> velocityBindGroup1To0;
    BindGroup<WebglGpgpuBirdsPositionBindGroup> positionBindGroup0To1;
    BindGroup<WebglGpgpuBirdsPositionBindGroup> positionBindGroup1To0;
    BindGroup<WebglGpgpuBirdsRenderBindGroup> renderBindGroup0;
    BindGroup<WebglGpgpuBirdsRenderBindGroup> renderBindGroup1;
    RenderClass<WebglGpgpuBirdsVelocityPass> velocityPass0To1;
    RenderClass<WebglGpgpuBirdsVelocityPass> velocityPass1To0;
    RenderClass<WebglGpgpuBirdsPositionPass> positionPass0To1;
    RenderClass<WebglGpgpuBirdsPositionPass> positionPass1To0;
    RenderClass<WebglGpgpuBirdsMainPass> mainPass0;
    RenderClass<WebglGpgpuBirdsMainPass> mainPass1;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
        outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D>
        depthTexture;
    uint frameIndex = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;

public:
    /** Creates only resources representable by the frozen public DSL surface. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebglGpgpuBirdsVertices",
            WebglGpgpuBirdsVertexCount);
        simulationUniformBuffer = device->createBuffer(
            "WebglGpgpuBirdsSimulationUniforms",
            1u);
        renderUniformBuffer = device->createBuffer(
            "WebglGpgpuBirdsRenderUniforms",
            1u);
        simulationSampler = device->createSampler({
            .label = "WebglGpgpuBirdsNearestSampler",
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
        positionTexture0 = device->createTexture(
            "WebglGpgpuBirdsPosition0RGBA32Float",
            WebglGpgpuBirdsTextureWidth,
            WebglGpgpuBirdsTextureWidth,
            1u);
        positionTexture1 = device->createTexture(
            "WebglGpgpuBirdsPosition1RGBA32Float",
            WebglGpgpuBirdsTextureWidth,
            WebglGpgpuBirdsTextureWidth,
            1u);
        velocityTexture0 = device->createTexture(
            "WebglGpgpuBirdsVelocity0RGBA32Float",
            WebglGpgpuBirdsTextureWidth,
            WebglGpgpuBirdsTextureWidth,
            1u);
        velocityTexture1 = device->createTexture(
            "WebglGpgpuBirdsVelocity1RGBA32Float",
            WebglGpgpuBirdsTextureWidth,
            WebglGpgpuBirdsTextureWidth,
            1u);
    }

    /** Allocates the direct 800 by 500 color output and default depth attachment. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebglGpgpuBirdsOutputRGBA8",
            width,
            height,
            1u);
        depthTexture = device->createTexture(
            "WebglGpgpuBirdsDepth32",
            width,
            height,
            1u);
    }

    /** Uploads exact r185 Float32 arrays to both initial ping-pong states. */
    void configureScene(
        const eastl::vector<WebglGpgpuBirdsVertex> &vertices,
        const eastl::vector<float4> &initialPosition,
        const eastl::vector<float4> &initialVelocity)
    {
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(WebglGpgpuBirdsVertexCount) *
                    sizeof(WebglGpgpuBirdsVertex))
            ->writeTexture(
                positionTexture0,
                initialPosition.data(),
                uint64_t(WebglGpgpuBirdsBirdCount) * sizeof(float4))
            ->writeTexture(
                positionTexture1,
                initialPosition.data(),
                uint64_t(WebglGpgpuBirdsBirdCount) * sizeof(float4))
            ->writeTexture(
                velocityTexture0,
                initialVelocity.data(),
                uint64_t(WebglGpgpuBirdsBirdCount) * sizeof(float4))
            ->writeTexture(
                velocityTexture1,
                initialVelocity.data(),
                uint64_t(WebglGpgpuBirdsBirdCount) * sizeof(float4))
            ->submit();

        velocityBindGroup0To1 =
            device->createBindGroup<WebglGpgpuBirdsVelocityBindGroup>(
                simulationUniformBuffer,
                positionTexture0->createView(),
                velocityTexture0->createView(),
                simulationSampler);
        velocityBindGroup1To0 =
            device->createBindGroup<WebglGpgpuBirdsVelocityBindGroup>(
                simulationUniformBuffer,
                positionTexture1->createView(),
                velocityTexture1->createView(),
                simulationSampler);
        positionBindGroup0To1 =
            device->createBindGroup<WebglGpgpuBirdsPositionBindGroup>(
                simulationUniformBuffer,
                positionTexture0->createView(),
                velocityTexture0->createView(),
                simulationSampler);
        positionBindGroup1To0 =
            device->createBindGroup<WebglGpgpuBirdsPositionBindGroup>(
                simulationUniformBuffer,
                positionTexture1->createView(),
                velocityTexture1->createView(),
                simulationSampler);
        renderBindGroup0 =
            device->createBindGroup<WebglGpgpuBirdsRenderBindGroup>(
                renderUniformBuffer,
                positionTexture0->createView(),
                velocityTexture0->createView());
        renderBindGroup1 =
            device->createBindGroup<WebglGpgpuBirdsRenderBindGroup>(
                renderUniformBuffer,
                positionTexture1->createView(),
                velocityTexture1->createView());

        velocityPass0To1 =
            device->createRenderClass<WebglGpgpuBirdsVelocityPass>(
                velocityBindGroup0To1);
        velocityPass1To0 =
            device->createRenderClass<WebglGpgpuBirdsVelocityPass>(
                velocityBindGroup1To0);
        positionPass0To1 =
            device->createRenderClass<WebglGpgpuBirdsPositionPass>(
                positionBindGroup0To1);
        positionPass1To0 =
            device->createRenderClass<WebglGpgpuBirdsPositionPass>(
                positionBindGroup1To0);
        mainPass0 = device->createRenderClass<WebglGpgpuBirdsMainPass>(
            renderBindGroup0);
        mainPass1 = device->createRenderClass<WebglGpgpuBirdsMainPass>(
            renderBindGroup1);
    }

    /** Uploads one deterministic callback state and selects its ping-pong direction. */
    void updateFrame(
        WebglGpgpuBirdsSimulationUniforms simulationUniforms,
        float4x4 projectionMatrix,
        float4x4 viewMatrix,
        uint currentFrameIndex)
    {
        WebglGpgpuBirdsRenderUniforms renderUniforms;
        renderUniforms.projectionMatrix = projectionMatrix;
        renderUniforms.viewMatrix = viewMatrix;
        frameIndex = currentFrameIndex;
        graphicsQueue
            ->writeBuffer(
                BufferRange(simulationUniformBuffer),
                &simulationUniforms,
                sizeof(simulationUniforms))
            ->writeBuffer(
                BufferRange(renderUniformBuffer),
                &renderUniforms,
                sizeof(renderUniforms))
            ->submit();
    }

    /** Renders velocity then position and draws the resulting state exactly once. */
    void render() override
    {
        WebglGpgpuBirdsSimulationFrameBuffer velocityFrameBuffer;
        velocityFrameBuffer.color.loadOp = LoadOp::Clear;
        velocityFrameBuffer.color.storeOp = StoreOp::Store;
        velocityFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglGpgpuBirdsSimulationFrameBuffer positionFrameBuffer;
        positionFrameBuffer.color.loadOp = LoadOp::Clear;
        positionFrameBuffer.color.storeOp = StoreOp::Store;
        positionFrameBuffer.color.clearValue = {0.0, 0.0, 0.0, 0.0};
        WebglGpgpuBirdsFrameBuffer frameBuffer;
        frameBuffer.color = outputTexture->createView();
        frameBuffer.color.loadOp = LoadOp::Clear;
        frameBuffer.color.storeOp = StoreOp::Store;
        frameBuffer.color.clearValue = {1.0, 1.0, 1.0, 1.0};
        frameBuffer.depth = depthTexture->createView();
        frameBuffer.depth.depthLoadOp = LoadOp::Clear;
        frameBuffer.depth.depthStoreOp = StoreOp::Store;
        frameBuffer.depth.depthClearValue = 1.0f;
        auto nextTexture = swapchain->queryNextTexture();

        if ((frameIndex & 1u) == 0u)
        {
            velocityFrameBuffer.color = velocityTexture1->createView();
            positionFrameBuffer.color = positionTexture1->createView();
            graphicsQueue
                ->renderPass(
                    "WebglGpgpuBirdsVelocity0To1",
                    velocityFrameBuffer,
                    velocityPass0To1(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglGpgpuBirdsPosition0To1",
                    positionFrameBuffer,
                    positionPass0To1(3u, 1u, 0u, 0u))
                ->renderPass(
                    "main-double-sided-flock",
                    frameBuffer,
                    mainPass1->setVertexBuffer(vertexBuffer),
                    mainPass1(WebglGpgpuBirdsVertexCount, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        else
        {
            velocityFrameBuffer.color = velocityTexture0->createView();
            positionFrameBuffer.color = positionTexture0->createView();
            graphicsQueue
                ->renderPass(
                    "WebglGpgpuBirdsVelocity1To0",
                    velocityFrameBuffer,
                    velocityPass1To0(3u, 1u, 0u, 0u))
                ->renderPass(
                    "WebglGpgpuBirdsPosition1To0",
                    positionFrameBuffer,
                    positionPass1To0(3u, 1u, 0u, 0u))
                ->renderPass(
                    "main-double-sided-flock",
                    frameBuffer,
                    mainPass0->setVertexBuffer(vertexBuffer),
                    mainPass0(WebglGpgpuBirdsVertexCount, 1u, 0u, 0u))
                ->renderToSwapchain(
                    nextTexture,
                    outputTexture,
                    RenderToSwapchainDescriptor{})
                ->submit();
        }
        swapchain->present();
    }

    /** Returns the final direct DSL-created RGBA8 target for host readback. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the explicitly configured capture width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the explicitly configured capture height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Returns the current position/phase ping-pong target after the selected frame. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
    getCurrentPositionTextureHandle() const
    {
        return (frameIndex & 1u) == 0u ? positionTexture1 : positionTexture0;
    }

    /** Returns the current velocity ping-pong target after the selected frame. */
    Texture<TextureFormat::RGBA32Float,
            TextureUsage<TextureBinding, RenderAttachment, CopyDst, CopySrc>,
            TextureDimension::e2D>
    getCurrentVelocityTextureHandle() const
    {
        return (frameIndex & 1u) == 0u ? velocityTexture1 : velocityTexture0;
    }

    /** Releases every ordinary buffer, ping-pong texture, and attachment. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
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
