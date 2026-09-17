#ifndef GVM_THREE_WEBGPU_COMPUTE_GEOMETRY_HPP
#define GVM_THREE_WEBGPU_COMPUTE_GEOMETRY_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputeGeometryVertexCapacity = 10000u;

/** Stores one immutable indexed Lee Perry Smith mesh vertex. */
struct WebgpuComputeGeometryVertex
{
    float4 basePosition [[Attribute0]];
};

/** Stores camera, pointer, jelly, viewport, and Inspector controls. */
struct WebgpuComputeGeometryControls
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4 pointerPosition;
    float4 physics;
    float4 viewportInspector;
};

/** Binds immutable and writable vertex state for the jelly Compute passes. */
struct WebgpuComputeGeometryComputeResources final : public IBindGroup
{
    /** Declares base position, current position, speed, and control storage. */
    constructor(
        StructuredBuffer<float4> basePositions [[Binding0]],
        RWStructuredBuffer<float4> currentPositions [[Binding1]],
        RWStructuredBuffer<float4> speeds [[Binding2]],
        UniformBuffer<WebgpuComputeGeometryControls> controls [[Binding3]])
    {
    }
};

/** Binds computed positions and controls to the one Scene RenderClass. */
struct WebgpuComputeGeometryDrawResources final : public IBindGroup
{
    /** Exposes current storage positions and immutable camera controls. */
    constructor(
        StructuredBuffer<float4> currentPositions [[Binding0]],
        StructuredBuffer<float4> baseNormals [[Binding1]],
        UniformBuffer<WebgpuComputeGeometryControls> controls [[Binding2]])
    {
    }
};

/** Binds only controls to the background and Inspector screen passes. */
struct WebgpuComputeGeometryScreenResources final : public IBindGroup
{
    /** Shares the immutable viewport and Inspector controls. */
    constructor(
        UniformBuffer<WebgpuComputeGeometryControls> controls [[Binding0]])
    {
    }
};

/** Binds the single-sample scene image for the output copy pass. */
struct WebgpuComputeGeometryResolveResources final : public IBindGroup
{
    /** Declares the scene source, linear sampler, and output controls. */
    constructor(
        Texture2D<float4> sceneColor [[Binding0]],
        Sampler sceneSampler [[Binding1]],
        UniformBuffer<WebgpuComputeGeometryControls> controls [[Binding2]])
    {
    }
};

/** Copies base mesh positions into writable current storage. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeGeometryInitPass final
    : public IComputeClass
{
public:
    /** Binds the dedicated jelly state resource group. */
    constructor(BindGroup<WebgpuComputeGeometryComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Initializes one current position and clears its velocity. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= uint(resources->controls->physics.w)) return;
        resources->currentPositions[index] =
            resources->basePositions[index];
        resources->speeds[index] = float4(0.0f);
    }
};

/** Applies pointer pinch and spring-damped jelly integration. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeGeometryUpdatePass final
    : public IComputeClass
{
public:
    /** Binds the dedicated jelly state resource group. */
    constructor(BindGroup<WebgpuComputeGeometryComputeResources> resources [[Slot0]])
    {
    }

private:
    /** Advances one vertex using the exact r185 statement ordering. */
    void compute(uint3 dispatchThreadID [[DispatchThreadID]])
    {
        const uint index = dispatchThreadID.x;
        if (index >= uint(resources->controls->physics.w)) return;
        const float3 basePosition =
            resources->basePositions[index].xyz;
        float3 currentPosition =
            resources->currentPositions[index].xyz;
        float3 currentSpeed =
            resources->speeds[index].xyz;
        const float4 pointer =
            resources->controls->pointerPosition;
        const float4 physics =
            resources->controls->physics;
        if (pointer.w > 0.5f)
        {
            const float3 worldPosition =
                currentPosition * 0.1f;
            const float3 delta =
                pointer.xyz - worldPosition;
            const float distanceValue = length(delta);
            const float3 direction =
                distanceValue > 0.000001f
                    ? delta / distanceValue
                    : float3(0.0f);
            const float power =
                max(physics.z - distanceValue, 0.0f) * 0.22f;
            currentPosition += direction * power;
        }
        const float distanceValue =
            length(basePosition - currentPosition);
        const float3 force =
            physics.x * distanceValue *
            (basePosition - currentPosition);
        currentSpeed += force;
        currentSpeed *= physics.y;
        currentPosition += currentSpeed;
        resources->currentPositions[index] =
            float4(currentPosition, 1.0f);
        resources->speeds[index] =
            float4(currentSpeed, 0.0f);
    }
};

/** Carries computed view position into derivative normal shading. */
struct WebgpuComputeGeometryVertexOutput
{
    float4 position [[Position]];
    float3 viewPosition [[Attribute0]];
    float3 viewNormal [[Attribute1]];
};

/** Defines the final color and depth Scene attachments. */
struct WebgpuComputeGeometryFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only screen attachment. */
struct WebgpuComputeGeometryColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Carries fullscreen coordinates to background and Inspector passes. */
struct WebgpuComputeGeometryScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Converts one linear channel to the r185 sRGB output transfer. */
float webgpuComputeGeometryLinearToSrgb(float value)
{
    const float clamped = max(value, 0.0f);
    return clamped <= 0.0031308f
        ? clamped * 12.92f
        : pow(clamped, 0.41666f) * 1.055f - 0.055f;
}

/** Draws the one ordinary non-RenderSet jelly mesh from computed storage. */
class WebgpuComputeGeometryMainPass final : public IRenderClass
{
public:
    /** Binds computed positions and enables double-sided depth testing. */
    constructor(BindGroup<WebgpuComputeGeometryDrawResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Reads the indexed vertex's computed storage position. */
    WebgpuComputeGeometryVertexOutput vertex(
        WebgpuComputeGeometryVertex inputValue [[VertexInput0]],
        uint vertexID [[VertexID]])
    {
        const float4 position =
            resources->currentPositions[vertexID] +
            inputValue.basePosition * 0.0f;
        WebgpuComputeGeometryVertexOutput outputValue;
        outputValue.position =
            mul(resources->controls->modelViewProjection, position);
        outputValue.viewPosition =
            mul(resources->controls->modelView, position).xyz;
        const float4 transformedNormal = mul(
            resources->controls->modelView,
            float4(resources->baseNormals[vertexID].xyz, 0.0f));
        outputValue.viewNormal = normalize(
            float3(
                transformedNormal.x,
                transformedNormal.y,
                transformedNormal.z));
        return outputValue;
    }

    /** Reproduces MeshNormalNodeMaterial through screen derivatives. */
    WebgpuComputeGeometryFrameBuffer fragment(
        WebgpuComputeGeometryVertexOutput inputValue)
    {
        const float3 normal =
            normalize(inputValue.viewNormal);
        const float3 linearColor =
            normal * 0.5f + float3(0.5f);
        WebgpuComputeGeometryFrameBuffer frameBuffer;
        frameBuffer.color =
            half4(half3(linearColor), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the exact procedural gradient and vignette behind the mesh. */
class WebgpuComputeGeometryBackgroundPass final : public IRenderClass
{
public:
    /** Disables culling for one fullscreen triangle. */
    constructor(BindGroup<WebgpuComputeGeometryScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle. */
    WebgpuComputeGeometryScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeGeometryScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Evaluates the r185 screenUV mix and radial vignette. */
    WebgpuComputeGeometryColorFrameBuffer fragment(
        WebgpuComputeGeometryScreenOutput inputValue)
    {
        const float3 bottom =
            float3(0.346704f, 0.242281f, 0.930111f);
        const float3 top =
            float3(0.887923f, 0.610496f, 0.610496f);
        const float3 tint =
            float3(0.386430f, 0.274677f, 0.921582f) * 4.0f;
        const float vignette =
            1.0f -
            clamp(
                (distance(inputValue.uv, float2(0.5f)) - 0.3f) /
                    0.5f,
                0.0f,
                1.0f);
        const float3 linearColor =
            lerp(bottom, top, inputValue.uv.y) *
            tint * vignette;
        WebgpuComputeGeometryColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(
                webgpuComputeGeometryLinearToSrgb(linearColor.x),
                webgpuComputeGeometryLinearToSrgb(linearColor.y),
                webgpuComputeGeometryLinearToSrgb(linearColor.z)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Composites the locked initial-frame Inspector chrome. */
class WebgpuComputeGeometryInspectorPass final : public IRenderClass
{
public:
    /** Configures source-alpha screen blending. */
    constructor(BindGroup<WebgpuComputeGeometryScreenResources> resources [[Slot0]])
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
    WebgpuComputeGeometryScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeGeometryScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the deterministic rounded Inspector bar. */
    WebgpuComputeGeometryColorFrameBuffer fragment(
        WebgpuComputeGeometryScreenOutput inputValue)
    {
        if (resources->controls->viewportInspector.z < 0.5f)
        {
            discard_fragment();
        }
        const float2 pixel =
            float2(inputValue.uv.x * 800.0f, inputValue.uv.y * 500.0f);
        const float2 center = float2(699.5f, 33.5f);
        const float2 halfExtent = float2(85.5f, 18.5f);
        const float radius = pixel.x < center.x ? 12.0f : 6.0f;
        const float2 delta =
            abs(pixel - center) - (halfExtent - float2(radius));
        const float distanceValue =
            length(max(delta, float2(0.0f))) +
            min(max(delta.x, delta.y), 0.0f) - radius;
        const float2 shadowCenter = center + float2(0.0f, 4.0f);
        const float shadowRadius =
            pixel.x < shadowCenter.x ? 12.0f : 6.0f;
        const float2 shadowDelta =
            abs(pixel - shadowCenter) -
            (halfExtent - float2(shadowRadius));
        const float shadowDistance =
            length(max(shadowDelta, float2(0.0f))) +
            min(max(shadowDelta.x, shadowDelta.y), 0.0f) -
            shadowRadius;
        const float shadowAlpha =
            0.3f / (1.0f + exp(shadowDistance / 4.5f));
        const float shapeCoverage =
            clamp(0.5f - distanceValue, 0.0f, 1.0f);
        const float3 barColor =
            pixel.x < 663.0f
                ? float3(20.0f, 60.0f, 80.0f) / 255.0f
                : float3(30.0f, 30.0f, 36.0f) / 255.0f;
        const float barAlpha = 0.85f * shapeCoverage;
        float outputAlpha =
            barAlpha + shadowAlpha * (1.0f - barAlpha);
        float3 premultipliedColor = barColor * barAlpha;
        const float borderCoverage =
            shapeCoverage *
            clamp(distanceValue + 1.5f, 0.0f, 1.0f);
        const float borderAlpha =
            (0x54u / 255.0f) * borderCoverage;
        premultipliedColor =
            float3(74.0f, 74.0f, 90.0f) / 255.0f *
                borderAlpha +
            premultipliedColor * (1.0f - borderAlpha);
        outputAlpha =
            borderAlpha + outputAlpha * (1.0f - borderAlpha);
        if (outputAlpha < 0.001f) discard_fragment();
        WebgpuComputeGeometryColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half3(premultipliedColor / outputAlpha),
            half(outputAlpha));
        return frameBuffer;
    }
};

/** Copies the centered single-sample result into the canonical RGBA8 image. */
class WebgpuComputeGeometryResolvePass final : public IRenderClass
{
public:
    /** Binds the single-sample scene and disables fullscreen culling. */
    constructor(
        BindGroup<WebgpuComputeGeometryResolveResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen resolve triangle. */
    WebgpuComputeGeometryScreenOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv =
            float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeGeometryScreenOutput outputValue;
        outputValue.position =
            float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reads the centered Scene sample in encoded output space. */
    WebgpuComputeGeometryColorFrameBuffer fragment(
        WebgpuComputeGeometryScreenOutput inputValue)
    {
        WebgpuComputeGeometryColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(resources->sceneColor->sample(
            resources->sceneSampler,
            inputValue.uv));
        return frameBuffer;
    }
};

/** Owns the dedicated ordinary mesh, Compute state, and screen passes. */
class WebgpuComputeGeometryRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<WebgpuComputeGeometryVertex, BufferUsage<Vertex, CopyDst>>
        vertexBuffer;
    Buffer<uint, BufferUsage<Index, CopyDst>> indexBuffer;
    Buffer<float4, BufferUsage<Storage, CopyDst>> basePositions;
    Buffer<float4, BufferUsage<Storage, CopyDst>> currentPositions;
    Buffer<float4, BufferUsage<Storage, CopyDst>> speeds;
    Buffer<float4, BufferUsage<Storage, CopyDst>> baseNormals;
    Buffer<WebgpuComputeGeometryControls, BufferUsage<Uniform, CopyDst>>
        controls;
    BindGroup<WebgpuComputeGeometryComputeResources> computeResources;
    BindGroup<WebgpuComputeGeometryDrawResources> drawResources;
    BindGroup<WebgpuComputeGeometryScreenResources> screenResources;
    BindGroup<WebgpuComputeGeometryResolveResources> resolveResources;
    ComputeClass<WebgpuComputeGeometryInitPass> initPass;
    ComputeClass<WebgpuComputeGeometryUpdatePass> updatePass;
    RenderClass<WebgpuComputeGeometryMainPass> mainPass;
    RenderClass<WebgpuComputeGeometryBackgroundPass> backgroundPass;
    RenderClass<WebgpuComputeGeometryInspectorPass> inspectorPass;
    RenderClass<WebgpuComputeGeometryResolvePass> resolvePass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> depthTexture;
    Sampler sceneSampler;
    uint indexCount = 0u;
    uint vertexCount = 0u;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    bool firstFrame = true;

public:
    /** Creates standalone geometry, storage, Compute, and control buffers. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        vertexBuffer = device->createBuffer(
            "WebgpuComputeGeometryVertices",
            WebgpuComputeGeometryVertexCapacity);
        indexBuffer = device->createBuffer(
            "WebgpuComputeGeometryIndices", 60000u);
        basePositions = device->createBuffer(
            "WebgpuComputeGeometryBasePositions",
            WebgpuComputeGeometryVertexCapacity);
        currentPositions = device->createBuffer(
            "WebgpuComputeGeometryCurrentPositions",
            WebgpuComputeGeometryVertexCapacity);
        speeds = device->createBuffer(
            "WebgpuComputeGeometrySpeeds",
            WebgpuComputeGeometryVertexCapacity);
        baseNormals = device->createBuffer(
            "WebgpuComputeGeometryBaseNormals",
            WebgpuComputeGeometryVertexCapacity);
        controls = device->createBuffer(
            "WebgpuComputeGeometryControls", 1u);
        computeResources =
            device->createBindGroup<WebgpuComputeGeometryComputeResources>(
                basePositions, currentPositions, speeds, controls);
        drawResources =
            device->createBindGroup<WebgpuComputeGeometryDrawResources>(
                currentPositions, baseNormals, controls);
        screenResources =
            device->createBindGroup<WebgpuComputeGeometryScreenResources>(
                controls);
        initPass =
            device->createComputeClass<WebgpuComputeGeometryInitPass>(
                computeResources);
        updatePass =
            device->createComputeClass<WebgpuComputeGeometryUpdatePass>(
                computeResources);
        mainPass =
            device->createRenderClass<WebgpuComputeGeometryMainPass>(
                drawResources);
        backgroundPass =
            device->createRenderClass<WebgpuComputeGeometryBackgroundPass>(
                screenResources);
        inspectorPass =
            device->createRenderClass<WebgpuComputeGeometryInspectorPass>(
                screenResources);
        sceneSampler = device->createSampler({
            .label = "WebgpuComputeGeometryResolveSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
        });
    }

    /** Allocates the host-sized DSL-owned output attachments. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        sceneTexture = device->createTexture(
            "WebgpuComputeGeometrySceneRGBA8",
            width,
            height,
            1u);
        outputTexture = device->createTexture(
            "WebgpuComputeGeometryRGBA8", width, height, 1u);
        depthTexture = device->createTexture(
            "WebgpuComputeGeometryDepth32",
            width,
            height,
            1u);
        resolveResources =
            device->createBindGroup<WebgpuComputeGeometryResolveResources>(
                sceneTexture->createView(),
                sceneSampler,
                controls);
        resolvePass =
            device->createRenderClass<WebgpuComputeGeometryResolvePass>(
                resolveResources);
    }

    /** Uploads the decoded GLB geometry and locked scenario controls. */
    void configureScene(
        const eastl::vector<float4> &vertices,
        const eastl::vector<uint> &indices,
        const eastl::vector<float4> &positions,
        const eastl::vector<float4> &normals,
        float4 mvp0,
        float4 mvp1,
        float4 mvp2,
        float4 mvp3,
        float4 modelView0,
        float4 modelView1,
        float4 modelView2,
        float4 modelView3,
        float4 pointerPosition,
        float4 physics,
        float inspectorEnabled)
    {
        vertexCount = uint(vertices.size());
        indexCount = uint(indices.size());
        WebgpuComputeGeometryControls controlValue;
        controlValue.modelViewProjection =
            float4x4(mvp0, mvp1, mvp2, mvp3);
        controlValue.modelView =
            float4x4(modelView0, modelView1, modelView2, modelView3);
        controlValue.pointerPosition = pointerPosition;
        controlValue.physics = physics;
        controlValue.viewportInspector =
            float4(
                float(readbackWidth),
                float(readbackHeight),
                inspectorEnabled,
                0.0f);
        graphicsQueue
            ->writeBuffer(
                BufferRange(vertexBuffer),
                vertices.data(),
                uint64_t(vertices.size()) *
                    sizeof(float4))
            ->writeBuffer(
                BufferRange(indexBuffer),
                indices.data(),
                uint64_t(indices.size()) * sizeof(uint))
            ->writeBuffer(
                BufferRange(basePositions),
                positions.data(),
                uint64_t(positions.size()) * sizeof(float4))
            ->writeBuffer(
                BufferRange(baseNormals),
                normals.data(),
                uint64_t(normals.size()) * sizeof(float4))
            ->writeBuffer(
                BufferRange(controls),
                &controlValue,
                sizeof(controlValue))
            ->submit();
    }

    /** Runs ordered jelly Compute and draws background, mesh, and Inspector. */
    void render() override
    {
        if (firstFrame)
        {
            graphicsQueue
                ->computePass(
                    "WebgpuComputeGeometryInit",
                    initPass(vertexCount, 1u, 1u))
                ->submit();
            firstFrame = false;
        }
        auto nextTexture = swapchain->queryNextTexture();
        WebgpuComputeGeometryColorFrameBuffer backgroundFrameBuffer;
        backgroundFrameBuffer.color = sceneTexture->createView();
        backgroundFrameBuffer.color.loadOp = LoadOp::Clear;
        backgroundFrameBuffer.color.storeOp = StoreOp::Store;
        backgroundFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        WebgpuComputeGeometryFrameBuffer meshFrameBuffer;
        meshFrameBuffer.color = sceneTexture->createView();
        meshFrameBuffer.color.loadOp = LoadOp::Load;
        meshFrameBuffer.color.storeOp = StoreOp::Store;
        meshFrameBuffer.depth = depthTexture->createView();
        meshFrameBuffer.depth.depthLoadOp = LoadOp::Clear;
        meshFrameBuffer.depth.depthStoreOp = StoreOp::Store;
        meshFrameBuffer.depth.depthClearValue = 1.0f;
        WebgpuComputeGeometryColorFrameBuffer resolveFrameBuffer;
        resolveFrameBuffer.color = outputTexture->createView();
        resolveFrameBuffer.color.loadOp = LoadOp::Clear;
        resolveFrameBuffer.color.storeOp = StoreOp::Store;
        resolveFrameBuffer.color.clearValue =
            {0.0f, 0.0f, 0.0f, 1.0f};
        graphicsQueue
            ->computePass(
                "WebgpuComputeGeometryUpdate",
                updatePass(vertexCount, 1u, 1u))
            ->renderPass(
                "WebgpuComputeGeometryBackground",
                backgroundFrameBuffer,
                backgroundPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebgpuComputeGeometryMesh",
                meshFrameBuffer,
                mainPass->setVertexBuffer(vertexBuffer),
                mainPass->setIndexBuffer(indexBuffer),
                mainPass(indexCount, 1u, 0u, 0, 0u))
            ->renderPass(
                "WebgpuComputeGeometryResolve",
                resolveFrameBuffer,
                resolvePass(3u, 1u, 0u, 0u))
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

    /** Releases all standalone buffers and DSL-owned output attachments. */
    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(indexBuffer);
        device->freeBuffer(basePositions);
        device->freeBuffer(currentPositions);
        device->freeBuffer(speeds);
        device->freeBuffer(baseNormals);
        device->freeBuffer(controls);
        device->freeTexture(sceneTexture);
        device->freeTexture(outputTexture);
        device->freeTexture(depthTexture);
    }
};

#endif
