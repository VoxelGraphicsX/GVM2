#ifndef UGLC_TEST_SHADER_STATIC_VARIANT_DIAGNOSTICS_RENDER_HPP
#define UGLC_TEST_SHADER_STATIC_VARIANT_DIAGNOSTICS_RENDER_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the ordinary sampled resources used by the no-op diagnostics render variant. */
struct StaticVariantDiagnosticNoOpBindGroup final : public IBindGroup
{
    /** Creates the bind group with only the resources needed by normal rendering. */
    constructor(Texture2D<half4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

/** Provides sampled resources plus writable counters used by the instrumented diagnostics render variant. */
struct StaticVariantDiagnosticInstrumentedBindGroup final : public IBindGroup
{
    /** Creates the bind group with render resources and the diagnostics counter buffer. */
    constructor(Texture2D<half4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]], RWStructuredBuffer<uint> counters [[Binding2]])
    {
    }
};

/** Receives vertex attributes shared by the no-op and instrumented render variants. */
struct StaticVariantDiagnosticVertexInput
{
    float4 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Carries interpolated data from the shared vertex shader to the shared fragment shader. */
struct StaticVariantDiagnosticVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    uint diagnosticIndex [[Attribute1]];
};

/** Defines the color target shared by both diagnostics render variants. */
struct StaticVariantDiagnosticFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Implements the diagnostics policy used by the normal render variant for one bind-group type. */
template <class BindGroupType>
struct NoOpRenderDiagnostics
{
    /** Starts a no-op diagnostics scope without touching any shader resources. */
    void init(BindGroup<BindGroupType> bindGroup)
    {
    }

    /** Leaves the vertex diagnostics point empty for the normal render variant. */
    void markVertex(uint vertexID)
    {
    }

    /** Leaves the fragment diagnostics point empty for the normal render variant. */
    void markFragment(uint diagnosticIndex)
    {
    }

    /** Returns the diagnostic index unchanged for the normal render variant. */
    uint makeDiagnosticIndex(uint seed)
    {
        return seed;
    }
};

/** Implements the diagnostics policy that stores its bind group and writes render-stage counters. */
template <class BindGroupType>
struct InstrumentedRenderDiagnostics
{
    /** Stores the bind group selected by the static shader variant for later diagnostics member calls. */
    BindGroup<BindGroupType> diagnosticBindGroup;

    /** Records that a diagnostics scope was entered for the instrumented render variant. */
    void init(BindGroup<BindGroupType> bindGroup)
    {
        diagnosticBindGroup = bindGroup;
        atomicAdd(diagnosticBindGroup->counters[0], 1u);
    }

    /** Records the vertex diagnostics point for the instrumented render variant. */
    void markVertex(uint vertexID)
    {
        atomicAdd(diagnosticBindGroup->counters[1], 1u);
        diagnosticBindGroup->counters[3] = vertexID;
    }

    /** Records the fragment diagnostics point for the instrumented render variant. */
    void markFragment(uint diagnosticIndex)
    {
        atomicAdd(diagnosticBindGroup->counters[2], 1u);
        diagnosticBindGroup->counters[4] = diagnosticIndex;
    }

    /** Builds a diagnostics index through a stored bind-group read so non-void behavior methods are covered. */
    uint makeDiagnosticIndex(uint seed)
    {
        return diagnosticBindGroup->counters[5] + seed;
    }
};

/** Shares one render shader implementation between no-op and instrumented diagnostics variants. */
template <class DiagnosticPolicy, class BindGroupType>
class StaticVariantDiagnosticRenderPass final : public IRenderClass
{
public:
    /** Creates the render pass specialization with the bind group selected by the variant. */
    constructor(BindGroup<BindGroupType> bindGroup [[Slot0]])
    {
    }

private:
    /** Runs the shared vertex path and dispatches the variant diagnostics policy. */
    StaticVariantDiagnosticVertexOutput vertex(uint vertexID [[VertexID]], StaticVariantDiagnosticVertexInput inputValue [[VertexInput0]])
    {
        DiagnosticPolicy diagnostics;
        diagnostics.init(bindGroup);
        diagnostics.markVertex(vertexID);

        StaticVariantDiagnosticVertexOutput outputValue;
        outputValue.position = inputValue.position;
        outputValue.uv = inputValue.uv;
        outputValue.diagnosticIndex = diagnostics.makeDiagnosticIndex(vertexID);
        return outputValue;
    }

    /** Runs the shared fragment path and dispatches the variant diagnostics policy. */
    StaticVariantDiagnosticFrameBuffer fragment(StaticVariantDiagnosticVertexOutput inputValue)
    {
        DiagnosticPolicy diagnostics;
        diagnostics.init(bindGroup);
        const uint diagnosticIndex = diagnostics.makeDiagnosticIndex(inputValue.diagnosticIndex);
        diagnostics.markFragment(inputValue.diagnosticIndex);

        StaticVariantDiagnosticFrameBuffer frameBuffer;
        frameBuffer.color = bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv);
        return frameBuffer;
    }
};

using NoOpDiagnosticRenderPass = StaticVariantDiagnosticRenderPass<NoOpRenderDiagnostics<StaticVariantDiagnosticNoOpBindGroup>, StaticVariantDiagnosticNoOpBindGroup>;
using InstrumentedDiagnosticRenderPass = StaticVariantDiagnosticRenderPass<InstrumentedRenderDiagnostics<StaticVariantDiagnosticInstrumentedBindGroup>, StaticVariantDiagnosticInstrumentedBindGroup>;

/** Hosts both concrete diagnostics render variants so UGLC emits separate shader artifacts. */
class ShaderStaticVariantDiagnosticsRenderRenderer final : public AbstractRenderer
{
    Device device;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> noOpTexture;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> instrumentedTexture;
    Sampler sampler;
    Buffer<uint, BufferUsage<Storage, CopyDst, CopySrc>> counters;
    BindGroup<StaticVariantDiagnosticNoOpBindGroup> noOpBindGroup;
    BindGroup<StaticVariantDiagnosticInstrumentedBindGroup> instrumentedBindGroup;
    RenderClass<NoOpDiagnosticRenderPass> noOpPass;
    RenderClass<InstrumentedDiagnosticRenderPass> instrumentedPass;

public:
    /** Creates resources, bind groups, and both diagnostics render class variants. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        noOpTexture = device->createTexture("StaticVariantDiagnosticsNoOpTexture", 64, 64, 1);
        instrumentedTexture = device->createTexture("StaticVariantDiagnosticsInstrumentedTexture", 64, 64, 1);
        sampler = device->createSampler({});
        counters = device->createBuffer("StaticVariantDiagnosticsCounters", 128);
        noOpBindGroup = device->createBindGroup<StaticVariantDiagnosticNoOpBindGroup>(noOpTexture->createView(), sampler);
        instrumentedBindGroup = device->createBindGroup<StaticVariantDiagnosticInstrumentedBindGroup>(instrumentedTexture->createView(), sampler, counters);
        noOpPass = device->createRenderClass<NoOpDiagnosticRenderPass>(noOpBindGroup);
        instrumentedPass = device->createRenderClass<InstrumentedDiagnosticRenderPass>(instrumentedBindGroup);
    }

    /** Leaves rendering empty because this fixture validates generated shader variants only. */
    void render() override
    {
    }

    /** Leaves teardown empty because the fixture owns only reference-counted DSL resources. */
    void destroy() override
    {
    }
};

#endif
