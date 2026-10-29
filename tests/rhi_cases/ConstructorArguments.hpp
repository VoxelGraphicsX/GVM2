#pragma once
#include "UGL.h"
#include <array>
using namespace UGL;

/** Produces indirect workgroup counts through the same resource later consumed by dispatch. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentDispatchWriter final : public IComputeClass
{
public:
    /** Selects a writable command range and its three native workgroup counts. */
    constructor(RWStructuredBuffer<uint> commands [[Args0]], uint3 groups [[Args1]])
    {
    }
private:
    /** Writes one native indirect dispatch record without CPU readback. */
    void compute()
    {
        commands[0] = groups.x;
        commands[1] = groups.y;
        commands[2] = groups.z;
    }
};

/** Distinguishes indirect workgroup counts from the direct dispatch thread-count interface. */
class [[LocalWorkGroupSize(4, 1, 1)]] ArgumentWorkgroupCompute final : public IComputeClass
{
public:
    /** Selects an output allocation and an ordinary per-dispatch value. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], uint value [[Args1]])
    {
    }
private:
    /** Makes every invocation in each native workgroup observable. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        output[id.x] = value + id.x;
    }
};

/** Reads a prior GPU result through a replaceable read-only buffer argument. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentReadCompute final : public IComputeClass
{
public:
    /** Defines independent input and output allocations plus an ordinary replacement value. */
    constructor(StructuredBuffer<uint> input [[Args0]], RWStructuredBuffer<uint> output [[Args1]], uint bias [[Args2]]) {}
private:
    /** Observes writes from preceding commands in final queue submission order. */
    void compute(uint3 id [[DispatchThreadID]]) { output[id.x] = input[id.x] + bias; }
};

/** Reads and writes one allocation through independently replaceable constructor buffer ranges. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentAliasedBufferCompute final : public IComputeClass
{
public:
    /** Places the writable range first to exercise alias scopes independently of declaration order. */
    constructor(RWStructuredBuffer<uint> destination [[Args0]], StructuredBuffer<uint> source [[Args1]])
    {
    }
private:
    /** Uses one invocation so overlapping argument ranges do not introduce shader-side data races. */
    void compute()
    {
        destination[1] = source[0] + 1u;
    }
};

/** Writes through a copied resource handle without changing the handle itself. */
void storeArgumentHandle(RWStructuredBuffer<uint> target, uint index, uint value) { target[index] = value + index; }

/** Writes the passed buffer even when its parameter name matches a different top-level Args resource. */
void storeShadowedArgumentHandle(RWStructuredBuffer<uint> first, uint index, uint value)
{
    first[index] = value + index;
}

/** Keeps helper parameters and local resource handles independent from identically named Args fields. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentShadowedHandleCompute final : public IComputeClass
{
public:
    /** Provides a decoy first buffer and a second buffer selected by ordinary C++ value semantics. */
    constructor(RWStructuredBuffer<uint> first [[Args0]], RWStructuredBuffer<uint> second [[Args1]], uint value [[Args2]])
    {
    }
private:
    /** Passes the second buffer through a same-name helper parameter and a same-name local copy. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        storeShadowedArgumentHandle(second, id.x, 17u);
        auto first = second;
        first[id.x + 2u] = value + id.x;
    }
};

/** Shares ordinary fields and a resource between aggregate helpers and ordinary object methods. */
struct ArgumentShadowedPayload
{
    uint value;
    RWStructuredBuffer<uint> output;

    /** Returns this object's value even when a shader Args parameter uses the same name. */
    uint readValue() const
    {
        return value;
    }

    /** Writes this object's resource without substituting the shader's identically named Args fields. */
    void storeValue(uint index) const
    {
        output[index] = value + index;
    }
};

/** Preserves the selected aggregate when a helper parameter shadows a top-level Args aggregate. */
void storeShadowedArgumentPayload(ArgumentShadowedPayload material, uint index)
{
    material.output[index] = material.value + index;
}

/** Preserves nested resource values through ordinary helpers, local copies, and lambda parameters. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentShadowedAggregateCompute final : public IComputeClass
{
public:
    /** Supplies two aggregates whose values and resource allocations must remain distinct. */
    constructor(ArgumentShadowedPayload material [[Args0]], ArgumentShadowedPayload other [[Args1]])
    {
    }
private:
    /** Uses the selected aggregate through three independent scopes that shadow the root material. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        storeShadowedArgumentPayload(other, id.x);
        auto material = other;
        material.output[id.x + 2u] = material.value + 10u + id.x;
        auto store = [](ArgumentShadowedPayload material, uint index)
        {
            material.output[index + 4u] = material.value + 20u + index;
        };
        store(other, id.x);
    }
};

/** Distinguishes an ordinary object's receiver from the shader receiver for identically named fields. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentShadowedMemberCompute final : public IComputeClass
{
public:
    /** Supplies an object, a different root value, and a different root output allocation. */
    constructor(ArgumentShadowedPayload payload [[Args0]], uint value [[Args1]], RWStructuredBuffer<uint> output [[Args2]])
    {
    }
private:
    /** Observes both the ordinary object's fields and the actual shader Args fields. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        payload.storeValue(id.x);
        output[id.x] = payload.readValue();
        output[id.x + 2u] = value;
    }
};

/** Writes compare-exchange results to physical buffer elements and ordinary local variables. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentAtomicCompute final : public IComputeClass
{
public:
    /** Captures the allocation used for the atomic target and original-value observations. */
    constructor(RWStructuredBuffer<uint> output [[Args0]])
    {
    }
private:
    /** Covers both atomic address spaces, local output, and an output index evaluated exactly once. */
    void compute()
    {
        atomicCompareExchange(output[0], 0u, 7u, output[1]);
        atomicCompareExchange(output[0], 0u, 11u, output[2]);
        uint original = 99u;
        atomicCompareExchange(output[0], 7u, 13u, original);
        output[3] = original;
        uint index = 4u;
        atomicCompareExchange(output[0], 13u, 17u, output[index++]);
        output[5] = index;
        GroupShared<uint> shared;
        atomicStore(shared, 23u);
        atomicCompareExchange(shared, 23u, 31u, output[6]);
        output[7] = atomicLoad(shared);
    }
};

/** Carries copied and assigned resource handles through ordinary C++ local values and helper parameters. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentHandleCompute final : public IComputeClass
{
public:
    /** Defines two replaceable buffers and the runtime selection used by this invocation. */
    constructor(RWStructuredBuffer<uint> first [[Args0]], RWStructuredBuffer<uint> second [[Args1]], uint selection [[Args2]]) {}
private:
    /** Copies a handle, replaces the local selection, and passes the selected resource to a helper. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto target = first;
        if (selection != 0)
        {
            target = second;
        }
        storeArgumentHandle(target, id.x, selection);
    }
};

/** Exercises ordinary Args fields whose CPU and Metal storage representations differ. */
struct ArgumentData
{
    uint base;
    bool enabled;
    half weight;
    float3 direction;
    float4x4 matrix;
    uint fixed[2];
    std::array<uint, 2> sequence;
};

/** Writes independently captured argument values into a replaceable buffer range. */
struct [[LocalWorkGroupSize(1, 1, 1)]] ArgumentCompute final : public IComputeClass
{
public:
    uint creations = 0;
    /** Counts creation separately from each deferred invocation. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], const ArgumentData data [[Args1]]) { creations += 1; }
private:
    /** Combines each distinct value shape so the GPU readback checks field encoding and matrix orientation. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        output[id.x] = data.base + uint(data.direction[2]) + uint(data.matrix[2][1]) +
            uint(float(data.weight) * 10.0f) + data.fixed[1] + data.sequence[1] + (data.enabled ? 100u : 0u) + id.x;
    }
};

/** Supplies a uniform resource with the existing buffer element ABI. */
struct ArgumentUniform { uint value; };

/** Mixes ordinary values with writable and uniform resource fields. */
struct ArgumentMaterial
{
    uint base;
    RWStructuredBuffer<uint> output;
    UniformBuffer<ArgumentUniform> uniform;
    /** Writes resource contents through a const input while preserving all handles and ordinary fields. */
    void store(uint index) const { output[index] = base + uniform->value + index; atomicAdd(output[index], 1u); }
};

/** Uses nested resource fields and a const resource-only method in the Args ABI. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentMaterialCompute final : public IComputeClass
{
public:
    /** Captures a mixed input without introducing a user-visible argument wrapper. */
    constructor(ArgumentMaterial material [[Args0]]) {}
private:
    /** Invokes the same resource-only helper through a local copy and a readonly input. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = material;
        local.store(id.x);
        material.store(id.x);
    }
};

/** Preserves a buffer-only bind group for the existing Slot ABI. */
struct ArgumentBindings final : public IBindGroup
{
    /** Stores one immutable resource entry for Slot pipelines. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]]) {}
};

/** Carries the ordinary raster stage output unchanged by constructor Args. */
struct ArgumentRasterVertex { float4 position [[Position]]; };

/** Defines a readback-compatible color target for Args raster execution. */
struct ArgumentRasterTarget final : public IFrameBuffer { ColorAttachment<TextureFormat::RGBA8Unorm> color; };

/** Generates native indirect records and an aliased uint32 counter in one GPU dispatch. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentIndirectCountWriter final : public IComputeClass
{
public:
    /** Selects the destination, raw GPU count, record capacity, stride, and native record width. */
    constructor(RWStructuredBuffer<uint> data [[Args0]], uint count [[Args1]], uint capacity [[Args2]],
        uint strideWords [[Args3]], uint commandWords [[Args4]])
    {
    }
private:
    /** Stores the counter at byte four and native commands beginning at byte sixteen. */
    void compute()
    {
        data[1] = count;
        for (uint index = 0; index < capacity; ++index)
        {
            for (uint field = 0; field < commandWords; ++field)
            {
                data[4 + index * strideWords + field] = field == 0 ? 3u : field == 1 ? 1u : 0u;
            }
        }
    }
};

/** Counts actual one-pixel raster draws with fragment atomics, independently of vertex-cache behavior. */
class ArgumentCountRaster final : public IRenderClass
{
public:
    /** Selects an atomic output and the contribution of each executed draw. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], uint contribution [[Args1]])
    {
    }
private:
    /** Emits a fullscreen triangle using native vertex indices. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Adds one contribution per draw to an output shared safely by all indirect commands. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        atomicAdd(output[0], contribution);
        ArgumentRasterTarget result;
        result.color = half4(1);
        return result;
    }
};

/** Consumes a compute result through fragment-stage Args without a descriptor set. */
class ArgumentReadRaster final : public IRenderClass
{
public:
    /** Selects the shader-readable buffer independently for each direct draw. */
    constructor(StructuredBuffer<uint> input [[Args0]]) {}
private:
    /** Produces a fullscreen triangle with the ordinary vertex signature. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Exposes the preceding compute write as an exactly observable color channel. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex inputVertex)
    {
        ArgumentRasterTarget result;
        result.color = half4(float(input[0]) / 255.0f, 0, 0, 1);
        return result;
    }
};

/** Writes through a fragment-stage Args buffer using the enabled native graphics-store capability. */
class ArgumentWriteRaster final : public IRenderClass
{
public:
    /** Requires the fragment output allocation and replacement value at creation and draw. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], uint value [[Args1]]) {}
private:
    /** Produces a fullscreen triangle for a single-pixel fragment write. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Writes resource contents while leaving every Args handle and ordinary value unchanged. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex inputVertex)
    {
        output[0] = value;
        ArgumentRasterTarget result;
        result.color = half4(0, 1, 0, 1);
        return result;
    }
};

/** Writes one element through the existing Slot ABI alongside an Args reader in the same pass. */
class ArgumentSlotWriteRaster final : public IRenderClass
{
public:
    /** Captures an immutable group whose second buffer element receives the fragment output. */
    constructor(BindGroup<ArgumentBindings> entries [[Slot0]])
    {
    }
private:
    /** Produces a fullscreen triangle without vertex buffer dependencies. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Writes independently of the first element read by another draw in this render pass. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex inputVertex)
    {
        entries->output[1] = 99u;
        ArgumentRasterTarget result;
        result.color = half4(0, 1, 0, 1);
        return result;
    }
};

/** Uses a shared root layout for vertex and fragment stages of a procedural draw. */
class ArgumentRaster final : public IRenderClass
{
public:
    /** Accepts per-invocation color and a vertex-stage observation buffer. */
    constructor(float4 color [[Args0]], RWStructuredBuffer<uint> output [[Args1]], uint value [[Args2]]) {}
private:
    /** Records vertex-stage replacement values and produces a fullscreen triangle. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        output[id] = value + id;
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Reads the same invocation block without changing the public fragment signature. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(color);
        return result;
    }
};

/** Observes indexed and indirect vertex and instance controls through replaceable Args. */
class ArgumentIndexedRaster final : public IRenderClass
{
public:
    /** Creates a procedural graphics pipeline with independent per-invocation output. */
    constructor(float4 color [[Args0]], RWStructuredBuffer<uint> output [[Args1]], uint value [[Args2]])
    {
    }
private:
    /** Records native vertex and instance indices while generating a fullscreen triangle. */
    ArgumentRasterVertex vertex(uint id [[VertexID]], uint instance [[InstanceID]])
    {
        output[id] = value + instance * 16u + id;
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id % 3u], 0, 1);
        return result;
    }
    /** Makes each task's fragment Args visible in the color attachment. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(color);
        return result;
    }
};

/** Shares one transient pixel-local color with an ordinary readback attachment. */
struct ArgumentPixelFrame final : IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm, PixelLocalAccess::ReadWrite,
        PixelLocalStorage::Transient, PixelLocalLoad::Clear, PixelLocalStore::Discard> albedo;
    ColorAttachment<TextureFormat::RGBA8Unorm> present;
};

/** Writes the first pixel-local phase from replaceable constructor values. */
class ArgumentPixelRaster final : public IRenderClass
{
public:
    /** Captures the color written by this raster task. */
    constructor(float4 color [[Args0]])
    {
    }
private:
    /** Generates a fullscreen triangle without external vertex inputs. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Publishes the captured color for the following pixel-local phase. */
    ArgumentPixelFrame fragment(ArgumentRasterVertex input)
    {
        ArgumentPixelFrame result;
        result.albedo = half4(color);
        return result;
    }
};

/** Combines a compiler-owned input attachment with a constant-only Args root. */
class ArgumentPixelResolve final : public IPixelLocalRenderClass
{
public:
    /** Captures an ordinary color added during this pixel-local invocation. */
    constructor(float4 bias [[Args0]])
    {
    }
private:
    /** Reads the preceding phase while leaving its attachment unchanged. */
    ArgumentPixelFrame pixel(ArgumentPixelFrame input [[PixelLocalInput]])
    {
        ArgumentPixelFrame result;
        result.present = input.albedo.read() + half4(bias);
        return result;
    }
};

/** Combines an Args resource table with a separate compiler-owned input-attachment set. */
class ArgumentPixelTextureResolve final : public IPixelLocalRenderClass
{
public:
    /** Captures the sampled allocation, sampler, and ordinary color for this invocation. */
    constructor(Texture2D<float4> image [[Args0]], Sampler sampling [[Args1]], float4 bias [[Args2]])
    {
    }
private:
    /** Reads both the previous pixel-local phase and the invocation's sampled resource. */
    ArgumentPixelFrame pixel(ArgumentPixelFrame input [[PixelLocalInput]])
    {
        ArgumentPixelFrame result;
        result.present = input.albedo.read() + half4(image->sampleLevel(sampling, float2(0.5f, 0.5f), 0.0f)) + half4(bias);
        return result;
    }
};

/** Supplies an ordinary vertex buffer that must survive root argument slot changes. */
struct ArgumentVertexInput
{
    float2 position [[Attribute0]];
    uint code [[Attribute1]];
};

/** Exercises execution-buffer capture without allocating a shader Args root. */
class ArgumentZeroRaster final : public IRenderClass
{
public:
    /** Creates a graphics pipeline without constructor arguments. */
    constructor()
    {
    }
private:
    /** Reads the ordinary vertex buffer while a control-only snapshot is bound. */
    ArgumentRasterVertex vertex(ArgumentVertexInput input [[VertexInput0]])
    {
        ArgumentRasterVertex result;
        result.position = float4(input.position, 0, 1);
        return result;
    }
    /** Makes a successful zero-argument indexed draw visible in readback. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(0, 0, 1, 1);
        return result;
    }
};

/** Uses the Args root slot following its vertex input buffer. */
class ArgumentVertexRaster final : public IRenderClass
{
public:
    /** Accepts the same per-invocation shapes as a procedural Args draw. */
    constructor(float4 color [[Args0]], RWStructuredBuffer<uint> output [[Args1]], uint value [[Args2]]) {}
private:
    /** Reads a restored ordinary vertex buffer while accessing the distinct Args root. */
    ArgumentRasterVertex vertex(ArgumentVertexInput input [[VertexInput0]], uint id [[VertexID]])
    {
        output[id] = value + input.code;
        ArgumentRasterVertex result;
        result.position = float4(input.position, 0, 1);
        return result;
    }
    /** Reads the shared per-draw root from the fragment stage. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(color);
        return result;
    }
};

/** Exercises the existing Slot ABI when a pass switches between Args pipelines and a Slot pipeline. */
class ArgumentSlotRaster final : public IRenderClass
{
public:
    /** Keeps the compatibility pipeline on its existing immutable BindGroup interface. */
    constructor(BindGroup<ArgumentBindings> entries [[Slot0]]) {}
private:
    /** Observes the ordinary vertex buffer after the pass switches away from an Args pipeline. */
    ArgumentRasterVertex vertex(ArgumentVertexInput input [[VertexInput0]], uint id [[VertexID]])
    {
        entries->output[id] = input.code;
        ArgumentRasterVertex result;
        result.position = float4(input.position, 0, 1);
        return result;
    }
    /** Provides a normal compatibility fragment output. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(0, 1, 0, 1);
        return result;
    }
};

/** Samples a replaceable texture and sampler without changing the compute entry signature. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentTextureCompute final : public IComputeClass
{
public:
    /** Captures a concrete texture view, sampler, output range, and ordinary value for every invocation. */
    constructor(Texture2D<float4> image [[Args0]], Sampler sampling [[Args1]], RWStructuredBuffer<uint> output [[Args2]], uint bias [[Args3]])
    {
    }
private:
    /** Makes both the sampled resource and the selected sampler observable in GPU readback. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        const auto color = image->sampleLevel(sampling, float2(0.5f, 0.5f), 0.0f);
        output[id.x] = uint(color.x * 255.0f + 0.5f) + bias;
    }
};

/** Writes a replaceable storage view for ordered compute and render consumers. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentStorageTextureCompute final : public IComputeClass
{
public:
    /** Selects the exact writable view and color at both creation and dispatch. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> image [[Args0]], float4 color [[Args1]])
    {
    }
private:
    /** Writes only the selected view's subresource. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        image->write(id.xy, half4(color));
    }
};

/** Carries typed image, sampler, and buffer arrays through ordinary aggregate value semantics. */
struct ArgumentResourceArrays
{
    std::array<Texture2D<float4>, 2> images;
    std::array<Sampler, 2> samplers;
    std::array<RWStructuredBuffer<uint>, 2> outputs;
    uint selection;
};

/** Samples resource values passed through a normal C++ helper rather than static resource aliases. */
float4 sampleArgumentTexture(Texture2D<float4> image, Sampler sampling)
{
    return image->sampleLevel(sampling, float2(0.5f, 0.5f), 0.0f);
}

/** Exercises dynamic resource selection, local copies, assignment, arrays, and helper parameters. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentResourceArrayCompute final : public IComputeClass
{
public:
    /** Captures all fixed-array elements independently in the constructor Args layout. */
    constructor(ArgumentResourceArrays resources [[Args0]])
    {
    }
private:
    /** Uses an invocation-varying index and a root-uniform index while retaining first-class resource values. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = resources;
        auto image = local.images[0];
        image = local.images[id.x & 1u];
        auto sampling = local.samplers[resources.selection];
        const auto color = sampleArgumentTexture(image, sampling);
        local.outputs[id.x & 1u][id.x] = uint(color.x * 255.0f + 0.5f);
    }
};

/** Captures top-level image, sampler, and buffer arrays with their native lifetime and access scopes. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentTopLevelArrayCompute final : public IComputeClass
{
public:
    /** Requires complete fixed arrays while keeping the ordinary output buffer independently replaceable. */
    constructor((std::array<Texture2D<float4>, 2> images [[Args0]]), (std::array<Sampler, 2> sampling [[Args1]]),
        (std::array<StructuredBuffer<uint>, 2> inputs [[Args2]]), RWStructuredBuffer<uint> output [[Args3]])
    {
    }
private:
    /** Observes every top-level array through invocation-varying resource selection. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        const auto index = id.x & 1u;
        const auto color = images[index]->sampleLevel(sampling[index], float2(0.5f), 0.0f);
        output[id.x] = uint(color.x * 255.0f + 0.5f) + inputs[index][0];
    }
};

/** Samples the current draw's exact image view through fragment-stage Args. */
class ArgumentTextureRaster final : public IRenderClass
{
public:
    /** Keeps texture and sampler replacement independent from the vertex and fragment interfaces. */
    constructor(Texture2D<float4> image [[Args0]], Sampler sampling [[Args1]])
    {
    }
private:
    /** Produces a fullscreen triangle using the unchanged procedural vertex interface. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Reads the texture and sampler selected for this immutable draw task. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(image->sampleLevel(sampling, float2(0.5f, 0.5f), 0.0f));
        return result;
    }
};

/** Selects an integer texture from a top-level fixed array using a root-uniform index. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentIntegerTextureCompute final : public IComputeClass
{
public:
    /** Keeps integer image tables distinct from floating-point sampled-image tables. */
    constructor((std::array<Texture2D<uint4>, 2> images [[Args0]]), RWStructuredBuffer<uint> output [[Args1]], uint selection [[Args2]])
    {
    }
private:
    /** Reads an exact unsigned texel without a sampler or floating-point conversion. */
    void compute()
    {
        output[0] = images[selection]->read(uint2(0), 0u).x;
    }
};

/** Writes an invocation-selected storage image from a fixed resource array. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentStorageArrayCompute final : public IComputeClass
{
public:
    /** Captures a fixed array of independently replaceable writable image views. */
    constructor((std::array<RWTexture2D<TextureFormat::RGBA8Unorm>, 2> images [[Args0]]))
    {
    }
private:
    /** Requires non-uniform storage-image indexing when neighboring invocations choose different descriptors. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto image = images[id.x & 1u];
        image->write(uint2(0), half4(float(id.x + 1u) * 0.25f, 0, 0, 1));
    }
};

/** Exercises distinct typed tables for sampled and storage 3D images and 2D array views. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentImageShapesCompute final : public IComputeClass
{
public:
    /** Captures compatible views while preserving their separate dimensions and storage formats. */
    constructor(Texture3D<float4> volume [[Args0]], Texture2DArray<float4> layers [[Args1]],
        RWTexture3D<TextureFormat::RGBA8Unorm> outputVolume [[Args2]],
        RWTexture2DArray<TextureFormat::R16Float> outputLayers [[Args3]], RWStructuredBuffer<uint> output [[Args4]])
    {
    }
private:
    /** Reads selected layers, writes the returned volume depth and array layer count, and writes both storage shapes. */
    void compute()
    {
        const uint3 volumeSize = volume->getDimensions();
        const uint3 layerSize = layers->getDimensions();
        const auto first = volume->read(uint3(0, 0, 1), 0u);
        const auto second = layers->read(uint2(0), 1u, 0u);
        output[0] = uint((first.x + second.x) * 255.0f + 0.5f);
        output[1] = volumeSize.z;
        output[2] = layerSize.z;
        outputVolume->write(uint3(0, 0, 1), half4(first));
        outputLayers->write(uint2(0), 1u, half(0.5f));
    }
};

/** Keeps the existing Slot ABI available between Args draws that use internal resource tables. */
struct ArgumentTextureBindings final : public IBindGroup
{
    /** Creates immutable sampled image and sampler entries for an ordinary Slot pipeline. */
    constructor(Texture2D<float4> image [[Binding0]], Sampler sampling [[Binding1]])
    {
    }
};

/** Samples through a legacy Slot group so tests can switch descriptor-set ABIs in one render pass. */
class ArgumentSlotTextureRaster final : public IRenderClass
{
public:
    /** Accepts an existing immutable group through the unchanged Slot constructor. */
    constructor(BindGroup<ArgumentTextureBindings> resources [[Slot0]])
    {
    }
private:
    /** Produces a fullscreen triangle with the same ordinary stage interface as the Args counterpart. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Samples through the old native descriptor set after or before an Args resource table. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(resources->image->sampleLevel(resources->sampling, float2(0.5f), 0.0f));
        return result;
    }
};

/** Exposes fragment storage-image writes so render-pass dependency boundaries can be tested. */
class ArgumentStorageTextureRaster final : public IRenderClass
{
public:
    /** Captures a writable image distinct from the active framebuffer attachment. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> image [[Args0]])
    {
    }
private:
    /** Produces one fullscreen triangle without changing the ordinary vertex signature. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Makes each draw a storage-image writer requiring an ordered boundary before an overlapping later draw. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        image->write(uint2(0), half4(1));
        ArgumentRasterTarget result;
        result.color = half4(1);
        return result;
    }
};

/** Exposes a fragment storage-image read so access narrowing records the Args view as read-only. */
class ArgumentReadStorageTextureRaster final : public IRenderClass
{
public:
    /** Captures a storage image that the fragment stage reads but never writes. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> image [[Args0]])
    {
    }
private:
    /** Produces one fullscreen triangle without changing the ordinary vertex signature. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Reads one texel so the storage image participates in tracking without a write. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = image->read(uint2(0));
        return result;
    }
};

/** Captures a storage array view for a Slot vertex writer in a mixed-ABI render batch. */
struct ArgumentOverlapTextureBindings final : IBindGroup
{
    /** Preserves the actual array view and its layer range in the legacy binding. */
    constructor(RWTexture2DArray<TextureFormat::R32Uint> image [[Binding0]])
    {
    }
};

/** Writes one texel through a Slot array view without racing the fragment writer. */
class ArgumentSlotArrayWriteRaster final : public IRenderClass
{
public:
    /** Selects the Slot storage view whose physical layer overlaps a later Args view. */
    constructor(BindGroup<ArgumentOverlapTextureBindings> entries [[Slot0]])
    {
    }
private:
    /** Writes the first texel of physical layer one from exactly one vertex invocation. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        if (id == 0u)
        {
            entries->image->write(uint2(0), 1u, 31u);
        }
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Produces an ordinary color attachment independently of the storage write. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        ArgumentRasterTarget result;
        result.color = half4(1);
        return result;
    }
};

/** Writes a disjoint texel of the same physical layer through a replaceable Args view. */
class ArgumentArrayWriteRaster final : public IRenderClass
{
public:
    /** Captures a view beginning at physical layer one. */
    constructor(RWTexture2DArray<TextureFormat::R32Uint> image [[Args0]])
    {
    }
private:
    /** Generates a fullscreen triangle without accessing storage resources in the vertex stage. */
    ArgumentRasterVertex vertex(uint id [[VertexID]])
    {
        const float2 points[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
        ArgumentRasterVertex result;
        result.position = float4(points[id], 0, 1);
        return result;
    }
    /** Writes only the second texel, so both stage writes are independent within the pass. */
    ArgumentRasterTarget fragment(ArgumentRasterVertex input)
    {
        image->write(uint2(1, 0), 0u, 47u);
        ArgumentRasterTarget result;
        result.color = half4(1);
        return result;
    }
};

/** Writes a fixed pattern through the existing Slot ABI so deferred Slot tasks stay observable. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentSlotCompute final : public IComputeClass
{
public:
    /** Captures an immutable group whose buffer range receives the dispatch output. */
    constructor(BindGroup<ArgumentBindings> entries [[Slot0]])
    {
    }
private:
    /** Writes each invocation's marker value into the bound range so offset handling stays observable. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        entries->output[id.x] = 77u + id.x;
    }
};
