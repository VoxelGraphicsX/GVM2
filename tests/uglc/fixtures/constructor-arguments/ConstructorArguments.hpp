#pragma once
#include "UGL.h"

using namespace UGL;

/** Stores nested values whose complete type must be exported even when unused by any entry. */
template <typename T> struct UnusedArguments
{
    T values[2];
};

/** Stores ordinary shader values without using the CPU emulation layout as the native ABI. */
struct ArgumentCamera
{
    float4x4 transform;
    float3 offset;
    bool enabled;
    float gain;
};

/** Combines fixed resource arrays, a buffer view, and ordinary material values. */
struct ArgumentMaterial
{
    Texture2D<float4> textures[2];
    Sampler sampler;
    StructuredBuffer<float> factors;
    float4 tint;
};

/** Stores the existing position semantic without repeating constructor arguments on the entry. */
struct ArgumentVertexOutput
{
    float4 position [[Position]];
};

/** Defines the existing framebuffer result used by the fragment entry. */
struct ArgumentFrameBuffer final : IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines one contract shared by graphics entries with different argument use sets. */
class ArgumentMesh final : public IRenderClass
{
public:
    /** Creates a static graphics pipeline from mandatory constructor arguments. */
    constructor(StructuredBuffer<float> vertices [[Args0]], ArgumentCamera camera [[Args1]],
                ArgumentMaterial material [[Args2]], UnusedArguments<int> unused [[Args3]])
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Reads only the buffer and camera arguments of the complete class contract. */
    ArgumentVertexOutput vertex(uint id [[VertexID]])
    {
        ArgumentVertexOutput result;
        result.position = mul(camera.transform, float4(vertices[id], camera.offset.x, 0.0f, 1.0f));
        return result;
    }

    /** Reads material resources without renumbering the graphics contract. */
    ArgumentFrameBuffer fragment(ArgumentVertexOutput input)
    {
        ArgumentFrameBuffer result;
        result.color = half4(material.textures[0]->sample(material.sampler, float2(0.0f, 0.0f)) * material.tint);
        return result;
    }
};

/** Defines a compute class whose resource and ordinary data can be replaced per dispatch. */
class [[LocalWorkGroupSize(8, 1, 1)]] ArgumentCompute final : public IComputeClass
{
public:
    /** Creates the compute pipeline using required initial arguments. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], ArgumentCamera camera [[Args1]]) {}

private:
    /** Writes resource contents while keeping the argument values and handles immutable. */
    void compute(uint3 id [[DispatchThreadID]]) { output[id.x] = uint(camera.gain); }
};

/** Defines a zero-argument contract whose execution method has only operation controls. */
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentZero final : public IComputeClass
{
public:
    /** Creates a compute pipeline without constructor arguments. */
    constructor() {}

private:
    /** Supplies a valid empty compute entry for zero-argument contract analysis. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};

/** Declares a real PixelLocal input attachment and a conventional final color output. */
struct ArgumentPixelFrame final : IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm, PixelLocalAccess::ReadWrite,
                             PixelLocalStorage::Transient, PixelLocalLoad::Clear, PixelLocalStore::Discard> albedo;
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Defines a PixelLocal contract whose draw suffix contains no extent or count controls. */
class ArgumentPixel final : public IPixelLocalRenderClass
{
public:
    /** Creates a PixelLocal pipeline from one ordinary argument. */
    constructor(ArgumentCamera camera [[Args0]]) {}
private:
    /** Preserves the existing PixelLocal entry signature while reading the per-draw argument. */
    ArgumentPixelFrame pixel(ArgumentPixelFrame input [[PixelLocalInput]])
    {
        ArgumentPixelFrame result;
        result.color = half4(camera.gain, 0.0f, 0.0f, 1.0f);
        return result;
    }
};

/** Defines a concrete shader specialization whose constructor contract cannot retain dependent types. */
template <typename T>
class [[LocalWorkGroupSize(1, 1, 1)]] ArgumentTemplate final : public IComputeClass
{
public:
    /** Creates a shader from a value whose concrete type is chosen by class specialization. */
    constructor(T value [[Args0]]) {}
private:
    /** Reads the specialized constructor member without changing the entry signature. */
    void compute(uint3 id [[DispatchThreadID]]) { const T copy = value; }
};

using ArgumentStorage = Buffer<uint, BufferUsage<Storage>>;
using ArgumentIndex = Buffer<uint, BufferUsage<Index>>;
using ArgumentIndirect = Buffer<uint, BufferUsage<Indirect>>;

/** Exercises every operation prefix, including a legal storage view conversion across element types. */
inline void validateArgumentCalls(Device device, ArgumentStorage storage, ArgumentIndex index, ArgumentIndirect indirect,
                                 ArgumentCamera camera, ArgumentMaterial material)
{
    auto mesh = device->createRenderClass<ArgumentMesh>(storage, camera, material, UnusedArguments<int>{});
    mesh->draw(storage, camera, material, UnusedArguments<int>{}, 3, 1, 0, 0);
    BufferRange<uint, BufferUsage<Storage>> range(storage, 16, 16);
    StructuredBuffer<float> converted = range;
    converted = storage;
    mesh->draw(converted, camera, material, UnusedArguments<int>{}, 3, 1, 0, 0);
    mesh->drawIndexed(storage, camera, material, UnusedArguments<int>{}, index, IndexFormat::Uint32, 3, 1, 0, 0, 0);
    mesh->drawIndirect(storage, camera, material, UnusedArguments<int>{}, indirect, 1, 16);
    mesh->drawIndexedIndirect(storage, camera, material, UnusedArguments<int>{}, index, IndexFormat::Uint32, indirect, 1, 20);
    mesh->drawIndirectCount(storage, camera, material, UnusedArguments<int>{}, indirect, indirect, 8u);
    mesh->drawIndirectCount(storage, camera, material, UnusedArguments<int>{}, indirect, indirect, 8u, 24u);
    mesh->drawIndexedIndirectCount(storage, camera, material, UnusedArguments<int>{},
        index, IndexFormat::Uint32, indirect, indirect, 8u);
    mesh->drawIndexedIndirectCount(storage, camera, material, UnusedArguments<int>{},
        index, IndexFormat::Uint16, indirect, indirect, 8u, 24u);
    auto compute = device->createComputeClass<ArgumentCompute>(storage, camera);
    compute->dispatchThreads(storage, camera, 17, 1, 1);
    compute->dispatchWorkgroupsIndirect(storage, camera, indirect);
    auto zero = device->createComputeClass<ArgumentZero>();
    const uint Args1 = 1;
    zero->dispatchThreads(Args1, 1, 1);
    auto specialized = device->createComputeClass<ArgumentTemplate<uint>>(Args1);
    specialized->dispatchThreads(Args1, 1, 1, 1);
    auto pixel = device->createRenderClass<ArgumentPixel>(camera);
    pixel->draw(camera);
}
