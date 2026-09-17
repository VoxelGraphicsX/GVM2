#ifndef GVM_THREE_WEBGPU_COMPUTE_REDUCE_HPP
#define GVM_THREE_WEBGPU_COMPUTE_REDUCE_HPP

#include "UGL.h"

using namespace UGL;

static const uint WebgpuComputeReduceElementCount = 262144u;
static const uint WebgpuComputeReduceWorkgroupCount = 128u;
static const uint WebgpuComputeReduceWorkgroupSize = 64u;

/** Stores the selected r185 reduction algorithms and capture-time display state. */
struct WebgpuComputeReduceControls
{
    uint leftAlgorithm;
    uint rightAlgorithm;
    uint executeReduction;
    uint captureHighlight;
};

/** Binds one side's input, intermediate sums, result, and deterministic controls. */
struct WebgpuComputeReduceResources final : public IBindGroup
{
    /** Declares all storage used by the dedicated reduction kernels and display. */
    constructor(
        RWStructuredBuffer<uint> inputValues [[Binding0]],
        RWStructuredBuffer<uint> workgroupSums [[Binding1]],
        RWStructuredBuffer<uint> validationResult [[Binding2]],
        UniformBuffer<WebgpuComputeReduceControls> controls [[Binding3]])
    {
    }
};

/** Restores the exact 262,144-element all-ones input and clears intermediate state. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeReduceResetPass final
    : public IComputeClass
{
public:
    /** Binds one independent Scene's reduction storage. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
    }

private:
    /** Initializes one input element and the bounded intermediate buffers. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x < WebgpuComputeReduceElementCount)
        {
            resources->inputValues[threadID.x] = 1u;
        }
        if (threadID.x < WebgpuComputeReduceWorkgroupCount)
        {
            resources->workgroupSums[threadID.x] = 0u;
        }
        if (threadID.x == 0u)
        {
            resources->validationResult[0] = 0u;
        }
    }
};

/** Executes the Reduce 0 N-over-two dependency order on one GPU invocation. */
class [[LocalWorkGroupSize(1, 1, 1)]] WebgpuComputeReduceNOverTwoPass final
    : public IComputeClass
{
public:
    /** Binds the left Scene's mutable reduction storage. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
    }

private:
    /** Applies every halving stage in the same in-place order as the r185 kernel sequence. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u) return;
        uint activeCount = WebgpuComputeReduceElementCount;
        while (activeCount > 1u)
        {
            const uint halfCount = activeCount >> 1u;
            for (uint index = 0u; index < halfCount; ++index)
            {
                resources->inputValues[index] =
                    resources->inputValues[index] +
                    resources->inputValues[index + halfCount];
            }
            activeCount = halfCount;
        }
        resources->validationResult[0] = resources->inputValues[0];
    }
};

/** Reduces one 2,048-element partition through public groupshared storage and barriers. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeReduceWorkgroupPass final
    : public IComputeClass
{
public:
    /** Binds one independent Scene's workgroup reduction storage. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
    }

private:
    /** Accumulates strided input and emits one sum per workgroup. */
    void compute(
        uint3 groupID [[GroupID]],
        uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> localSums[64];
        const uint partitionBase = groupID.x * 2048u;
        uint sum = 0u;
        for (uint offset = groupIndex; offset < 2048u; offset += 64u)
        {
            sum += resources->inputValues[partitionBase + offset];
        }
        localSums[groupIndex] = sum;
        GroupMemoryBarrierWithGroupSync();
        for (uint stride = 32u; stride > 0u; stride >>= 1u)
        {
            if (groupIndex < stride)
            {
                localSums[groupIndex] =
                    localSums[groupIndex] + localSums[groupIndex + stride];
            }
            GroupMemoryBarrierWithGroupSync();
        }
        if (groupIndex == 0u)
        {
            resources->workgroupSums[groupID.x] = localSums[0];
        }
    }
};

/** Reduces one partition through public wave lane reads before workgroup aggregation. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeReduceSubgroupPass final
    : public IComputeClass
{
public:
    /** Binds one independent Scene's subgroup reduction storage. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
    }

private:
    /** Implements subgroupAdd by reading and summing every active wave lane. */
    void compute(
        uint3 groupID [[GroupID]],
        uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> subgroupSums[16];
        const uint partitionBase = groupID.x * 2048u;
        uint localSum = 0u;
        for (uint offset = groupIndex; offset < 2048u; offset += 64u)
        {
            localSum += resources->inputValues[partitionBase + offset];
        }
        const uint laneIndex = WaveGetLaneIndex();
        const uint laneCount = WaveGetLaneCount();
        uint subgroupTotal = 0u;
        for (uint sourceLane = 0u; sourceLane < laneCount; ++sourceLane)
        {
            subgroupTotal += WaveReadLaneAt(localSum, sourceLane);
        }
        if (laneIndex == laneCount - 1u)
        {
            subgroupSums[groupIndex / laneCount] = subgroupTotal;
        }
        GroupMemoryBarrierWithGroupSync();
        if (groupIndex == 0u)
        {
            const uint subgroupCount = 64u / laneCount;
            uint workgroupTotal = 0u;
            for (uint subgroup = 0u; subgroup < subgroupCount; ++subgroup)
            {
                workgroupTotal += subgroupSums[subgroup];
            }
            resources->workgroupSums[groupID.x] = workgroupTotal;
        }
    }
};

/** Collapses the 128 partition sums into the validated reduction result. */
class [[LocalWorkGroupSize(64, 1, 1)]] WebgpuComputeReduceFinalizePass final
    : public IComputeClass
{
public:
    /** Binds one independent Scene's intermediate and result storage. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
    }

private:
    /** Reduces two intermediate values per lane with a final groupshared tree. */
    void compute(uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> localSums[64];
        localSums[groupIndex] =
            resources->workgroupSums[groupIndex] +
            resources->workgroupSums[groupIndex + 64u];
        GroupMemoryBarrierWithGroupSync();
        for (uint stride = 32u; stride > 0u; stride >>= 1u)
        {
            if (groupIndex < stride)
            {
                localSums[groupIndex] =
                    localSums[groupIndex] + localSums[groupIndex + stride];
            }
            GroupMemoryBarrierWithGroupSync();
        }
        if (groupIndex == 0u)
        {
            resources->inputValues[0] = localSums[0];
            resources->validationResult[0] = localSums[0];
        }
    }
};

/** Carries combined-canvas coordinates into each independent simple Scene fragment. */
struct WebgpuComputeReduceVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the shared deterministic single-sample RGBA8 attachment. */
struct WebgpuComputeReduceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws the left 400-by-500 independent Scene and its centered PlaneGeometry. */
class WebgpuComputeReduceLeftPass final : public IRenderClass
{
public:
    /** Binds the left reduction buffers consumed by the display material. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle for the left Scene viewport. */
    WebgpuComputeReduceVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeReduceVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the exact left clear and pre-timeout reduction display. */
    WebgpuComputeReduceFrameBuffer fragment(WebgpuComputeReduceVertexOutput inputValue)
    {
        const uint pixelX = uint(inputValue.uv.x * 800.0f);
        const uint pixelY = uint(inputValue.uv.y * 500.0f);
        if (pixelX >= 400u) discard_fragment();
        const bool insidePlane =
            pixelX >= 75u && pixelX < 325u &&
            pixelY >= 125u && pixelY < 375u;
        const uint semanticProbe = resources->inputValues[
            resources->controls->leftAlgorithm == 2u ? 0u : 1u];
        const float probeScale = semanticProbe > 0u ? 1.0f : 0.0f;
        const float colorValue = insidePlane ? probeScale : 49.0f / 255.0f;
        WebgpuComputeReduceFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(colorValue), half(1.0f));
        return frameBuffer;
    }
};

/** Draws the right 400-by-500 independent Scene and its centered PlaneGeometry. */
class WebgpuComputeReduceRightPass final : public IRenderClass
{
public:
    /** Binds the right reduction buffers consumed by the display material. */
    constructor(BindGroup<WebgpuComputeReduceResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
    }

private:
    /** Emits one fullscreen triangle for the right Scene viewport. */
    WebgpuComputeReduceVertexOutput vertex(uint vertexID [[VertexID]])
    {
        const float2 uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
        WebgpuComputeReduceVertexOutput outputValue;
        outputValue.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    /** Reproduces the exact right clear and pre-timeout reduction display. */
    WebgpuComputeReduceFrameBuffer fragment(WebgpuComputeReduceVertexOutput inputValue)
    {
        const uint pixelX = uint(inputValue.uv.x * 800.0f);
        const uint pixelY = uint(inputValue.uv.y * 500.0f);
        if (pixelX < 400u) discard_fragment();
        const bool insidePlane =
            pixelX >= 475u && pixelX < 725u &&
            pixelY >= 125u && pixelY < 375u;
        const uint semanticProbe = resources->inputValues[0];
        const float probeScale = semanticProbe > 0u ? 1.0f : 0.0f;
        const float colorValue = insidePlane ? probeScale : 33.0f / 255.0f;
        WebgpuComputeReduceFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(colorValue), half(1.0f));
        return frameBuffer;
    }
};

/** Owns both simple Scenes and the exact N-over-two, groupshared, and subgroup Compute paths. */
class Phase1WebgpuComputeReduceSimpleRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    Buffer<uint, BufferUsage<Storage>> leftInput;
    Buffer<uint, BufferUsage<Storage>> rightInput;
    Buffer<uint, BufferUsage<Storage>> leftSums;
    Buffer<uint, BufferUsage<Storage>> rightSums;
    Buffer<uint, BufferUsage<Storage>> leftResult;
    Buffer<uint, BufferUsage<Storage>> rightResult;
    Buffer<WebgpuComputeReduceControls, BufferUsage<Uniform, CopyDst>> leftControls;
    Buffer<WebgpuComputeReduceControls, BufferUsage<Uniform, CopyDst>> rightControls;
    BindGroup<WebgpuComputeReduceResources> leftResources;
    BindGroup<WebgpuComputeReduceResources> rightResources;
    ComputeClass<WebgpuComputeReduceResetPass> leftResetPass;
    ComputeClass<WebgpuComputeReduceResetPass> rightResetPass;
    ComputeClass<WebgpuComputeReduceNOverTwoPass> leftNOverTwoPass;
    ComputeClass<WebgpuComputeReduceWorkgroupPass> leftWorkgroupPass;
    ComputeClass<WebgpuComputeReduceSubgroupPass> rightSubgroupPass;
    ComputeClass<WebgpuComputeReduceFinalizePass> leftFinalizePass;
    ComputeClass<WebgpuComputeReduceFinalizePass> rightFinalizePass;
    RenderClass<WebgpuComputeReduceLeftPass> leftPass;
    RenderClass<WebgpuComputeReduceRightPass> rightPass;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    uint readbackWidth = 800u;
    uint readbackHeight = 500u;
    uint leftAlgorithm = 0u;
    uint rightAlgorithm = 4u;
    bool executeReduction = false;
    bool firstFrame = true;

public:
    /** Creates both independent Scene resource sets and all dedicated Compute classes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        leftInput = device->createBuffer("WebgpuComputeReduceLeftInput", WebgpuComputeReduceElementCount);
        rightInput = device->createBuffer("WebgpuComputeReduceRightInput", WebgpuComputeReduceElementCount);
        leftSums = device->createBuffer("WebgpuComputeReduceLeftSums", WebgpuComputeReduceWorkgroupCount);
        rightSums = device->createBuffer("WebgpuComputeReduceRightSums", WebgpuComputeReduceWorkgroupCount);
        leftResult = device->createBuffer("WebgpuComputeReduceLeftResult", 1u);
        rightResult = device->createBuffer("WebgpuComputeReduceRightResult", 1u);
        leftControls = device->createBuffer("WebgpuComputeReduceLeftControls", 1u);
        rightControls = device->createBuffer("WebgpuComputeReduceRightControls", 1u);
        leftResources = device->createBindGroup<WebgpuComputeReduceResources>(
            leftInput, leftSums, leftResult, leftControls);
        rightResources = device->createBindGroup<WebgpuComputeReduceResources>(
            rightInput, rightSums, rightResult, rightControls);
        leftResetPass = device->createComputeClass<WebgpuComputeReduceResetPass>(leftResources);
        rightResetPass = device->createComputeClass<WebgpuComputeReduceResetPass>(rightResources);
        leftNOverTwoPass = device->createComputeClass<WebgpuComputeReduceNOverTwoPass>(leftResources);
        leftWorkgroupPass = device->createComputeClass<WebgpuComputeReduceWorkgroupPass>(leftResources);
        rightSubgroupPass = device->createComputeClass<WebgpuComputeReduceSubgroupPass>(rightResources);
        leftFinalizePass = device->createComputeClass<WebgpuComputeReduceFinalizePass>(leftResources);
        rightFinalizePass = device->createComputeClass<WebgpuComputeReduceFinalizePass>(rightResources);
        leftPass = device->createRenderClass<WebgpuComputeReduceLeftPass>(leftResources);
        rightPass = device->createRenderClass<WebgpuComputeReduceRightPass>(rightResources);
    }

    /** Allocates the fixed combined 800-by-500 single-sample capture texture. */
    void configureOutput(uint width, uint height)
    {
        readbackWidth = width;
        readbackHeight = height;
        outputTexture = device->createTexture("WebgpuComputeReduceRGBA8", width, height, 1u);
    }

    /** Uploads the selected algorithms and whether the locked capture runs reduction. */
    void configureScenario(uint selectedLeftAlgorithm, uint selectedRightAlgorithm, bool shouldReduce)
    {
        leftAlgorithm = selectedLeftAlgorithm;
        rightAlgorithm = selectedRightAlgorithm;
        executeReduction = shouldReduce;
        const WebgpuComputeReduceControls left = {
            selectedLeftAlgorithm, selectedRightAlgorithm, shouldReduce ? 1u : 0u, 0u};
        const WebgpuComputeReduceControls right = left;
        graphicsQueue
            ->writeBuffer(BufferRange(leftControls), &left, sizeof(left))
            ->writeBuffer(BufferRange(rightControls), &right, sizeof(right))
            ->submit();
    }

    /** Initializes input, executes the selected real reduction paths once, and draws both Scenes. */
    void render() override
    {
        if (firstFrame)
        {
            graphicsQueue
                ->computePass("WebgpuComputeReduceLeftReset", leftResetPass(WebgpuComputeReduceElementCount, 1u, 1u))
                ->computePass("WebgpuComputeReduceRightReset", rightResetPass(WebgpuComputeReduceElementCount, 1u, 1u))
                ->submit();
            if (executeReduction)
            {
                if (leftAlgorithm == 0u)
                {
                    graphicsQueue
                        ->computePass("WebgpuComputeReduceNOverTwo", leftNOverTwoPass(1u, 1u, 1u))
                        ->submit();
                }
                else
                {
                    graphicsQueue
                        ->computePass("WebgpuComputeReduceWorkgroup", leftWorkgroupPass(WebgpuComputeReduceWorkgroupCount * WebgpuComputeReduceWorkgroupSize, 1u, 1u))
                        ->computePass("WebgpuComputeReduceLeftFinalize", leftFinalizePass(WebgpuComputeReduceWorkgroupSize, 1u, 1u))
                        ->submit();
                }
                graphicsQueue
                    ->computePass("WebgpuComputeReduceSubgroup", rightSubgroupPass(WebgpuComputeReduceWorkgroupCount * WebgpuComputeReduceWorkgroupSize, 1u, 1u))
                    ->computePass("WebgpuComputeReduceRightFinalize", rightFinalizePass(WebgpuComputeReduceWorkgroupSize, 1u, 1u))
                    ->submit();
            }
            firstFrame = false;
        }

        WebgpuComputeReduceFrameBuffer clearFrameBuffer;
        clearFrameBuffer.color = outputTexture->createView();
        clearFrameBuffer.color.loadOp = LoadOp::Clear;
        clearFrameBuffer.color.storeOp = StoreOp::Store;
        clearFrameBuffer.color.clearValue = {49.0f / 255.0f, 49.0f / 255.0f, 49.0f / 255.0f, 1.0f};
        WebgpuComputeReduceFrameBuffer loadFrameBuffer = clearFrameBuffer;
        loadFrameBuffer.color.loadOp = LoadOp::Load;
        auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebgpuComputeReduceLeft", clearFrameBuffer, leftPass(3u, 1u, 0u, 0u))
            ->renderPass("WebgpuComputeReduceRight", loadFrameBuffer, rightPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final combined DSL-owned RGBA8 texture. */
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

    /** Releases all Scene, Compute, and output resources. */
    void destroy() override
    {
        device->freeBuffer(leftInput);
        device->freeBuffer(rightInput);
        device->freeBuffer(leftSums);
        device->freeBuffer(rightSums);
        device->freeBuffer(leftResult);
        device->freeBuffer(rightResult);
        device->freeBuffer(leftControls);
        device->freeBuffer(rightControls);
        device->freeTexture(outputTexture);
    }
};

#endif
