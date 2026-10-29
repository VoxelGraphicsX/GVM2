#ifndef GVM_THREE_WEBGPU_COMPUTE_PARTICLES_HPP
#define GVM_THREE_WEBGPU_COMPUTE_PARTICLES_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputeParticlesCount = 200000u;

/** Stores the attribute union for the particle quad and expanded grid lines. */
struct WebgpuComputeParticlesVertex
{
    float4 positionAndCorner [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Stores one Scene entity camera transform and material phase. */
struct WebgpuComputeParticlesObjectData
{
    float4x4 view;
    float4x4 projection;
    float4 viewportPhase;
};

/** Stores one stable instance ordinal required by RenderSet drawing. */
struct WebgpuComputeParticlesInstanceData
{
    float4 ordinal;
};

/** Stores the private grid or sprite material controls. */
struct WebgpuComputeParticlesMaterialData
{
    float4 colorSizePhase;
};

/** Defines the only Scene RenderSet for the particle and grid entities. */
struct WebgpuComputeParticlesSceneRenderSet : public IRenderSet
{
    /** Declares the consolidated geometry and entity component schema. */
    constructor(
        BufferComponent<WebgpuComputeParticlesVertex> vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebgpuComputeParticlesObjectData> objects,
        BufferComponent<WebgpuComputeParticlesInstanceData> instances,
        BufferComponent<WebgpuComputeParticlesMaterialData> materials)
    {
    }
};

/** Stores the non-default physics, click, and Inspector controls. */
struct WebgpuComputeParticlesControls
{
    float4 physics;
    float4 clickPositionAndHit;
    float4 inspectorEnabled;
};

/** Binds all writable particle state to the ordered Compute passes. */
struct WebgpuComputeParticlesComputeResources final : public IBindGroup
{
    /** Declares position, velocity, color, and immutable control storage. */
    constructor(
        RWStructuredBuffer<float4> positions [[Binding0]],
        RWStructuredBuffer<float4> velocities [[Binding1]],
        RWStructuredBuffer<float4> colors [[Binding2]],
        UniformBuffer<WebgpuComputeParticlesControls> controls [[Binding3]])
    {
    }
};

/** Binds computed state to both Scene RenderClasses. */
struct WebgpuComputeParticlesDrawResources final : public IBindGroup
{
    /** Exposes the three state arrays read-only during rasterization. */
    constructor(
        StructuredBuffer<float4> positions [[Binding0]],
        StructuredBuffer<float4> colors [[Binding1]])
    {
    }
};

/** Binds the initial-frame Inspector visibility. */
struct WebgpuComputeParticlesInspectorResources final : public IBindGroup
{
    /** Shares the immutable controls buffer with the screen pass. */
    constructor(
        UniformBuffer<WebgpuComputeParticlesControls> controls [[Binding0]])
    {
    }
};

/** Evaluates the exact unsigned PCG hash used by Three r185 TSL. */
float webgpuComputeParticlesHash(uint seed)
{
    const uint state = seed * 747796405u + 2891336453u;
    const uint word =
        ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    const uint result = (word >> 22u) ^ word;
    return float(result) * (1.0f / 4294967296.0f);
}

/** Converts one accumulated linear-light channel to sRGB output space. */
float webgpuComputeParticlesLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    if (clamped <= 0.0031308f) return clamped * 12.92f;
    return pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Initializes the exact r185 particle grid and hash colors. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeParticlesInitPass final
    : public IComputeClass
{
public:
    /** Binds the sole particle-state resource group. */
    constructor(BindGroup<WebgpuComputeParticlesComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Writes one position and color with the upstream formulas. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= WebgpuComputeParticlesCount) return;
        const uint amount = 447u;
        const float offset =
            sqrt(float(WebgpuComputeParticlesCount)) * 0.5f;
        const uint gridX = index % amount;
        const uint gridZ = index / amount;
        resources->positions[index] = float4(
            (offset - float(gridX)) * 0.2f,
            0.0f,
            (offset - float(gridZ)) * 0.2f,
            1.0f);
        resources->velocities[index] = float4(0.0f);
        resources->colors[index] = float4(
            webgpuComputeParticlesHash(index),
            webgpuComputeParticlesHash(index + 2u),
            0.0f,
            1.0f);
    }
};

/** Applies the one-shot upstream click impulse to every particle. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeParticlesHitPass final
    : public IComputeClass
{
public:
    /** Binds the sole particle-state resource group. */
    constructor(BindGroup<WebgpuComputeParticlesComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Adds the exact distance-weighted and hashed click velocity. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= WebgpuComputeParticlesCount) return;
        const float3 position = resources->positions[index].xyz;
        const float3 clickPosition =
            resources->controls->clickPositionAndHit.xyz;
        const float3 delta = position - clickPosition;
        const float distanceValue = length(delta);
        const float3 direction =
            distanceValue > 0.000001f
                ? delta / distanceValue
                : float3(0.0f);
        const float distanceArea = max(3.0f - distanceValue, 0.0f);
        const float relativePower =
            distanceArea * 0.01f *
            (webgpuComputeParticlesHash(index) * 1.5f + 0.5f);
        resources->velocities[index] +=
            float4(direction * relativePower, 0.0f);
    }
};

/** Advances gravity, position, friction, and floor bounce in r185 order. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeParticlesUpdatePass final
    : public IComputeClass
{
public:
    /** Binds the sole particle-state resource group. */
    constructor(BindGroup<WebgpuComputeParticlesComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Advances one particle with the exact upstream update ordering. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= WebgpuComputeParticlesCount) return;
        float3 position = resources->positions[index].xyz;
        float3 velocity = resources->velocities[index].xyz;
        const float4 physics = resources->controls->physics;
        velocity += float3(0.0f, physics.x, 0.0f);
        position += velocity;
        velocity *= physics.z;
        if (position.y < 0.0f)
        {
            position.y = 0.0f;
            velocity.y = -velocity.y * physics.y;
            velocity.x *= 0.9f;
            velocity.z *= 0.9f;
        }
        resources->positions[index] = float4(position, 1.0f);
        resources->velocities[index] = float4(velocity, 0.0f);
    }
};

/** Carries Scene material phase, UV, and entity identity. */
struct WebgpuComputeParticlesVertexOutput
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
    float2 uv [[Attribute1]];
    uint entityID [[Attribute2]];
};

/** Defines the encoded Scene color and depth attachments. */
struct WebgpuComputeParticlesFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines the color-only Inspector screen attachment. */
struct WebgpuComputeParticlesColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen coordinates into the Inspector pass. */
struct WebgpuComputeParticlesScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Draws only the opaque expanded GridHelper entity through the Scene RenderSet. */
class WebgpuComputeParticlesGridPass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet with opaque depth state. */
    constructor(RenderSet<WebgpuComputeParticlesSceneRenderSet> sceneSet [[Slot0]])
    {
        setPrimitiveTopology(PrimitiveTopology::LineList);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Transforms native grid lines and preserves mandatory entity identity. */
    WebgpuComputeParticlesVertexOutput vertex(
        WebgpuComputeParticlesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuComputeParticlesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuComputeParticlesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const float4 currentClip =
            mul(
                objectData.projection,
                mul(
                    objectData.view,
                    float4(inputValue.positionAndCorner.xyz, 1.0f)));
        WebgpuComputeParticlesVertexOutput outputValue;
        outputValue.position = currentClip;
        outputValue.color = float4(1.0f);
        outputValue.uv =
            float2(instanceData.ordinal.x * 0.0f);
        outputValue.entityID = renderEntityID;
        return outputValue;
    }

    /** Emits only material phase zero and rejects the particle entity. */
    WebgpuComputeParticlesFrameBuffer fragment(
        WebgpuComputeParticlesVertexOutput inputValue)
    {
        const WebgpuComputeParticlesMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (material.colorSizePhase.w > 0.5f) discard_fragment();
        WebgpuComputeParticlesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuComputeParticlesLinearToSrgb(
                    material.colorSizePhase.x),
                webgpuComputeParticlesLinearToSrgb(
                    material.colorSizePhase.y),
                webgpuComputeParticlesLinearToSrgb(
                    material.colorSizePhase.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Draws only the 200,000 transparent circle sprites through the same RenderSet. */
class WebgpuComputeParticlesSpritePass final : public IRenderClass
{
public:
    /** Binds the unique Scene RenderSet and computed particle state. */
    constructor(
        RenderSet<WebgpuComputeParticlesSceneRenderSet> sceneSet [[Slot0]],
        BindGroup<WebgpuComputeParticlesDrawResources> resources [[Slot1]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Expands one computed world position as a camera-facing Sprite quad. */
    WebgpuComputeParticlesVertexOutput vertex(
        WebgpuComputeParticlesVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebgpuComputeParticlesObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebgpuComputeParticlesInstanceData instanceData =
            sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
        const WebgpuComputeParticlesMaterialData material =
            sceneSet->materials->get(renderEntityID, 0u);
        const uint particleIndex =
            uint(instanceData.ordinal.x);
        float4 viewPosition = mul(
            objectData.view,
            float4(resources->positions[particleIndex].xyz, 1.0f));
        viewPosition.xy += inputValue.positionAndCorner.xy *
            material.colorSizePhase.x;
        WebgpuComputeParticlesVertexOutput outputValue;
        outputValue.position = mul(objectData.projection, viewPosition);
        outputValue.color =
            resources->colors[particleIndex];
        outputValue.uv =
            inputValue.positionAndCorner.zw;
        outputValue.entityID =
            renderEntityID + uint(instanceData.ordinal.x * 0.0f);
        return outputValue;
    }

    /** Evaluates uv-color modulation and the upstream circular opacity. */
    WebgpuComputeParticlesFrameBuffer fragment(
        WebgpuComputeParticlesVertexOutput inputValue)
    {
        const WebgpuComputeParticlesMaterialData material =
            sceneSet->materials->get(inputValue.entityID, 0u);
        if (material.colorSizePhase.w < 0.5f) discard_fragment();
        const float2 centered = inputValue.uv * 2.0f - float2(1.0f);
        const float opacity =
            length(centered) <= 1.0f ? 1.0f : 0.0f;
        const float3 linearColor =
            inputValue.color.xyz * float3(inputValue.uv, 1.0f);
        WebgpuComputeParticlesFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuComputeParticlesLinearToSrgb(linearColor.x),
                webgpuComputeParticlesLinearToSrgb(linearColor.y),
                webgpuComputeParticlesLinearToSrgb(linearColor.z)),
            half(opacity));
        return frameBuffer;
    }
};

/** Composites the deterministic Inspector bar without Scene geometry. */
class WebgpuComputeParticlesInspectorPass final : public IRenderClass
{
public:
    /** Configures standard source-alpha screen blending. */
    constructor(
        BindGroup<WebgpuComputeParticlesInspectorResources> resources [[Slot0]])
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuComputeParticlesScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeParticlesScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the locked initial-frame rounded Inspector chrome. */
    WebgpuComputeParticlesColorFrameBuffer fragment(
        WebgpuComputeParticlesScreenOutput inputValue)
    {
        if (resources->controls->inspectorEnabled.x < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel =
            float2(inputValue.uv.x * 800.0f, inputValue.uv.y * 500.0f);
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float cornerRadius = pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta =
            abs(pixel - center) -
            (halfExtent - float2(cornerRadius));
        const float distanceValue =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) -
            cornerRadius;
        if (distanceValue > 0.5f) discard_fragment();
        WebgpuComputeParticlesColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                pixel.x < 663.0f
                    ? float3(23.1818f, 61.8182f, 85.7727f) / 255.0f
                    : float3(30.0f, 30.0f, 36.0f) / 255.0f),
            half(0.88f * clamp(0.5f - distanceValue, 0.0f, 1.0f)));
        return frameBuffer;
    }
};

/** Owns the dedicated Compute pipeline and single Scene RenderSet. */
class WebgpuComputeParticlesRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebgpuComputeParticlesSceneRenderSet> sceneSet;
    Buffer<float4, BufferUsage<Storage, CopyDst>> positions;
    Buffer<float4, BufferUsage<Storage, CopyDst>> velocities;
    Buffer<float4, BufferUsage<Storage, CopyDst>> colors;
    Buffer<WebgpuComputeParticlesControls, BufferUsage<Uniform, CopyDst>>
        controls;
    BindGroup<WebgpuComputeParticlesComputeResources> computeResources;
    BindGroup<WebgpuComputeParticlesDrawResources> drawResources;
    BindGroup<WebgpuComputeParticlesInspectorResources> inspectorResources;
    ComputeClass<WebgpuComputeParticlesInitPass> initPass;
    ComputeClass<WebgpuComputeParticlesHitPass> hitPass;
    ComputeClass<WebgpuComputeParticlesUpdatePass> updatePass;
    RenderClass<WebgpuComputeParticlesGridPass> gridPass;
    RenderClass<WebgpuComputeParticlesSpritePass> spritePass;
    RenderClass<WebgpuComputeParticlesInspectorPass> inspectorPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    bool firstFrame = true;
    bool hitEnabled = false;

public:
    /** Creates dedicated state buffers, Compute classes, and Scene passes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebgpuComputeParticlesSceneRenderSet>();
        positions = device->createBuffer(
            "WebgpuComputeParticlesPositions",
            WebgpuComputeParticlesCount);
        velocities = device->createBuffer(
            "WebgpuComputeParticlesVelocities",
            WebgpuComputeParticlesCount);
        colors = device->createBuffer(
            "WebgpuComputeParticlesColors",
            WebgpuComputeParticlesCount);
        controls = device->createBuffer(
            "WebgpuComputeParticlesControls", 1u);
        computeResources =
            device->createBindGroup<WebgpuComputeParticlesComputeResources>(
                positions, velocities, colors, controls);
        drawResources =
            device->createBindGroup<WebgpuComputeParticlesDrawResources>(
                positions, colors);
        inspectorResources =
            device->createBindGroup<WebgpuComputeParticlesInspectorResources>(
                controls);
        initPass =
            device->createComputeClass<WebgpuComputeParticlesInitPass>(
                computeResources);
        hitPass =
            device->createComputeClass<WebgpuComputeParticlesHitPass>(
                computeResources);
        updatePass =
            device->createComputeClass<WebgpuComputeParticlesUpdatePass>(
                computeResources);
        gridPass =
            device->createRenderClass<WebgpuComputeParticlesGridPass>(
                sceneSet);
        spritePass =
            device->createRenderClass<WebgpuComputeParticlesSpritePass>(
                sceneSet, drawResources);
        inspectorPass =
            device->createRenderClass<WebgpuComputeParticlesInspectorPass>(
                inspectorResources);
    }

    /** Allocates the host-sized DSL-owned output attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture(
            "WebgpuComputeParticlesRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuComputeParticlesDepth32", width, height, 1u);
    }

    /** Uploads deterministic scenario controls and one-shot hit state. */
    void configureControls(
        float4 physics,
        float4 clickPositionAndHit,
        float inspectorEnabled)
    {
        WebgpuComputeParticlesControls value;
        value.physics = physics;
        value.clickPositionAndHit = clickPositionAndHit;
        value.inspectorEnabled =
            float4(inspectorEnabled, 0.0f, 0.0f, 0.0f);
        hitEnabled = clickPositionAndHit.w > 0.5f;
        graphicsQueue
            ->writeBuffer(BufferRange(controls), &value, sizeof(value))
            ->submit();
    }

    /** Executes ordered initialization, optional hit, update, and Scene passes. */
    void render() override
    {
        sceneSet->update();
        if (firstFrame)
        {
            if (hitEnabled)
            {
                graphicsQueue
                    ->computePass(
                        "WebgpuComputeParticlesInit",
                        initPass(WebgpuComputeParticlesCount, 1u, 1u))
                    ->computePass(
                        "WebgpuComputeParticlesHit",
                        hitPass(WebgpuComputeParticlesCount, 1u, 1u))
                    ->submit();
            }
            else
            {
                graphicsQueue
                    ->computePass(
                        "WebgpuComputeParticlesInit",
                        initPass(WebgpuComputeParticlesCount, 1u, 1u))
                    ->submit();
            }
            firstFrame = false;
        }
        auto nextTexture = swapchain->queryNextTexture();
        WebgpuComputeParticlesFrameBuffer clearFrameBuffer;
        clearFrameBuffer.color = outputTexture->createView();
        clearFrameBuffer.color.loadOp = LoadOp::Clear;
        clearFrameBuffer.color.storeOp = StoreOp::Store;
        clearFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        clearFrameBuffer.depth = depthTexture->createView();
        clearFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        clearFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        clearFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuComputeParticlesFrameBuffer loadFrameBuffer = clearFrameBuffer;
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        loadFrameBuffer.depth.depthLoadOp = LoadOp::Load;
        WebgpuComputeParticlesColorFrameBuffer inspectorFrameBuffer;
        inspectorFrameBuffer.color = outputTexture->createView();
        inspectorFrameBuffer.color.loadOp = LoadOp::Load;
        inspectorFrameBuffer.color.storeOp = StoreOp::Store;
        graphicsQueue
            ->computePass(
                "WebgpuComputeParticlesUpdate",
                updatePass(WebgpuComputeParticlesCount, 1u, 1u))
            ->renderPass(
                "WebgpuComputeParticlesGrid",
                clearFrameBuffer,
                gridPass())
            ->renderPass(
                "WebgpuComputeParticlesSprites",
                loadFrameBuffer,
                spritePass())
            ->renderToSwapchain(
                nextTexture,
                outputTexture,
                RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned readback texture. */
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D>
    getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured readback width. */
    uint getReadbackWidth() const
    {
        return readbackWidth;
    }

    /** Returns the configured readback height. */
    uint getReadbackHeight() const
    {
        return readbackHeight;
    }

    /** Releases the sole Scene RenderSet and output attachments. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
