#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace GpuGraphFixture
{
    using namespace UGL;

    using SampleBuffer = Buffer<float, BufferUsage<Storage, CopySrc, CopyDst>>;
    using SampleRange = BufferRange<float, BufferUsage<Storage, CopySrc, CopyDst>>;

    /// Describes CPU-owned inputs to the mixed CPU/GPU graph.
    struct Input
    {
        uint count = 256u;
        float scale = 2.0f;
        float bias = 0.25f;
    };

    /// Generates ordinary CPU values without GPU resources or borrowed storage.
    eastl::vector<float> generate(IN Input input)
    {
        eastl::vector<float> values(input.count);
        for (uint index = 0; index < input.count; ++index)
        {
            values[index] = float(index) / 256.0f;
        }
        return values;
    }

    /// Scales one immutable input range into a separate GPU allocation.
    class [[LocalWorkGroupSize(64, 1, 1)]] Scale final : public IComputeClass
    {
    public:
        /// Captures the complete resource and scalar arguments for one dispatch.
        constructor(StructuredBuffer<float> source [[Args0]], RWStructuredBuffer<float> target [[Args1]],
            uint count [[Args2]], float scale [[Args3]])
        {
        }
    private:
        /// Writes one scaled element for each valid invocation.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = source[id.x] * scale;
            }
        }
    };

    /// Biases an independent branch of the input dataset.
    class [[LocalWorkGroupSize(64, 1, 1)]] Bias final : public IComputeClass
    {
    public:
        /// Captures independent output storage and the bias value.
        constructor(StructuredBuffer<float> source [[Args0]], RWStructuredBuffer<float> target [[Args1]],
            uint count [[Args2]], float bias [[Args3]])
        {
        }
    private:
        /// Writes one biased element for each valid invocation.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = source[id.x] + bias;
            }
        }
    };

    /// Combines two independently produced GPU ranges.
    class [[LocalWorkGroupSize(64, 1, 1)]] Add final : public IComputeClass
    {
    public:
        /// Captures both producers and the destination allocation.
        constructor(StructuredBuffer<float> left [[Args0]], StructuredBuffer<float> right [[Args1]],
            RWStructuredBuffer<float> target [[Args2]], uint count [[Args3]])
        {
        }
    private:
        /// Writes the sum after the resource dependency solver orders both producers.
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                target[id.x] = left[id.x] + right[id.x];
            }
        }
    };

    /// Uploads CPU values into an existing Buffer managed internally by the execution.
    class Upload final : public IRenderNode<eastl::vector<float>, SampleBuffer>
    {
    public:
        /// Returns an existing buffer handle while GPU execution remains deferred.
        SampleBuffer build(IN eastl::vector<float> values) const
        {
            auto output = createBuffer<SampleBuffer>("GraphInput", uint(values.size()));
            writeBuffer(SampleRange(output, 0u, uint(values.size()) * sizeof(float)), values);
            return output;
        }
    };

    /// Expands one configuration object into three independently recordable compute passes.
    class Filter final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        float scale = 2.0f;
        float bias = 0.25f;
        uint elementCount = 256u;
        /// Builds two independent branches and a resource-dependent combine pass.
        SampleBuffer build(IN SampleBuffer input) const
        {
            uint count = elementCount;
            auto scaled = scaleInput(input, count);
            auto biased = createBuffer<SampleBuffer>("Biased", count);
            auto combined = createBuffer<SampleBuffer>("Combined", count);
            computePass("Bias", dispatchThreads<Bias>(input, biased, count, bias, count, 1u, 1u));
            computePass("Combine", dispatchThreads<Add>(scaled, biased, combined, count, count, 1u, 1u));
            return combined;
        }
    private:
        /// Verifies synchronous helper methods receive the same hidden invocation state.
        SampleBuffer scaleInput(IN SampleBuffer input, uint count) const
        {
            auto output = createBuffer<SampleBuffer>("Scaled", count);
            computePass("Scale", dispatchThreads<Scale>(input, output, count, scale, count, 1u, 1u));
            return output;
        }
    };

    /// Reduces the completed readback using ordinary CPU code.
    float sum(IN eastl::vector<float> values)
    {
        float result = 0.0f;
        for (uint index = 0; index < values.size(); ++index)
        {
            result += values[index];
        }
        return result;
    }

    /// Exports a renderer-independent CPU/upload/compute/readback/CPU graph using one add API.
    [[Export]] auto buildGraph(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        filter.scale = input.scale;
        filter.bias = input.bias;
        auto source = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, source);
        auto filtered = graph.add(filter, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, filtered);
        return graph.add(sum, copied);
    }

    /// Reuses a first-wave buffer after a CPU readback dependency has completed.
    class ReuseBuffer final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        /// Reads the retained original allocation while deriving dispatch size from ordinary CPU values.
        SampleBuffer build(IN SampleBuffer input, IN eastl::vector<float> values) const
        {
            uint count = uint(values.size());
            auto output = createBuffer<SampleBuffer>("Reused", count);
            computePass("Reuse", dispatchThreads<Scale>(input, output, count, 1.0f, count, 1u, 1u));
            return output;
        }
    };

    /// Builds a second GPU wave from the first wave's completed owned CPU readback.
    [[Export]] auto buildTwoWaves(IN Input input)
    {
        TaskGraph graph;
        auto source = graph.add(generate, input);
        auto first = graph.add(Upload{}, source);
        auto intermediate = graph.add(ReadbackNode<float>{}, first);
        auto second = graph.add(ReuseBuffer{}, first, intermediate);
        auto result = graph.add(ReadbackNode<float>{}, second);
        return graph.add(sum, result);
    }

    /// Exposes independently produced buffers through ordinary named output fields.
    struct BranchOutputs
    {
        SampleBuffer scaled;
        SampleBuffer biased;
    };

    /// Builds two outputs without imposing a RenderNode-wide GPU completion fence.
    class Branches final : public IRenderNode<SampleBuffer, BranchOutputs>
    {
    public:
        uint count = 256u;
        /// Produces distinct resource versions that can be projected independently.
        BranchOutputs build(IN SampleBuffer input) const
        {
            BranchOutputs output;
            output.scaled = createBuffer<SampleBuffer>("ProjectedScale", count);
            output.biased = createBuffer<SampleBuffer>("ProjectedBias", count);
            computePass("ProjectedScale", dispatchThreads<Scale>(input, output.scaled, count, 2.0f, count, 1u, 1u));
            computePass("ProjectedBias", dispatchThreads<Bias>(input, output.biased, count, 0.25f, count, 1u, 1u));
            return output;
        }
    };

    /// Connects only one projected GPU output to a typed readback adapter.
    [[Export]] auto buildProjectedGpuGraph(IN Input input)
    {
        TaskGraph graph;
        Branches branches;
        branches.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto outputs = graph.add(branches, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, outputs.scaled);
        return graph.add(sum, copied);
    }

    using PreviewTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>;

    /// Reuses the ordinary framebuffer declaration for graph raster passes.
    struct PreviewFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    /// Carries procedural clip-space positions between raster stages.
    struct PreviewVertex
    {
        float4 position [[Position]];
    };

    /// Writes a GPU-computed input into an ordinary RGBA8 attachment.
    class Preview final : public IRenderClass
    {
    public:
        /// Captures the compute result through immutable draw arguments.
        constructor(StructuredBuffer<float> source [[Args0]])
        {
        }
    private:
        /// Emits a full-screen triangle without vertex buffers.
        PreviewVertex vertex(uint id [[VertexID]])
        {
            PreviewVertex output;
            if (id == 0u)
            {
                output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            }
            else if (id == 1u)
            {
                output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
            }
            else
            {
                output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            }
            return output;
        }
        /// Stores exact green when the upstream filter produced its expected first element.
        PreviewFrameBuffer fragment(PreviewVertex input)
        {
            PreviewFrameBuffer output;
            output.color = half4(0.0f, source[0] * 4.0f, 0.0f, 1.0f);
            return output;
        }
    };

    /// Converts a raster attachment into an ordinary buffer for the existing readback adapter.
    class [[LocalWorkGroupSize(1, 1, 1)]] ReadPreview final : public IComputeClass
    {
    public:
        /// Captures a sampled view and the one-element destination.
        constructor(Texture2D<float4> source [[Args0]], RWStructuredBuffer<float> target [[Args1]])
        {
        }
    private:
        /// Reads the attachment through its declared sampled-texture role.
        void compute()
        {
            target[0] = source->read(uint2(0u, 0u), 0u).y;
        }
    };

    /// Uses existing texture, view, framebuffer and shader types in a render subgraph.
    class PreviewNode final : public IRenderNode<SampleBuffer, SampleBuffer>
    {
    public:
        /// Records a raster pass followed by a dependent texture-reading compute pass.
        SampleBuffer build(IN SampleBuffer input) const
        {
            auto texture = createTexture<PreviewTexture>("GraphPreview", uint2(1u, 1u));
            auto textureView = view(texture);
            PreviewFrameBuffer framebuffer;
            framebuffer.color = textureView;
            framebuffer.color.loadOp = LoadOp::Clear;
            framebuffer.color.storeOp = StoreOp::Store;
            renderPass("Preview", framebuffer, draw<Preview>(input, 3u, 1u, 0u, 0u));
            auto output = createBuffer<SampleBuffer>("PreviewValue", 1u);
            computePass("ReadPreview", dispatchThreads<ReadPreview>(textureView, output, 1u, 1u, 1u));
            return output;
        }
    };

    /// Exercises upload, compute, raster, sampled-texture compute and CPU readback in one graph.
    [[Export]] auto buildRasterGraph(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto filtered = graph.add(filter, uploaded);
        auto preview = graph.add(PreviewNode{}, filtered);
        auto copied = graph.add(ReadbackNode<float>{}, preview);
        return graph.add(sum, copied);
    }

    /// Exports an existing buffer handle whose allocation is explicitly destroyed by the host.
    [[Export]] auto buildBuffer(IN Input input)
    {
        TaskGraph graph;
        auto values = graph.add(generate, input);
        return graph.add(Upload{}, values);
    }

    /// Exports named GPU allocations using ordinary output fields and existing resource types.
    [[Export]] auto buildBufferOutputs(IN Input input)
    {
        TaskGraph graph;
        Branches branches;
        branches.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        return graph.add(branches, uploaded);
    }

    /// Builds an exported image without adding a graph-specific resource owner type.
    class ImageOutput final : public IRenderNode<SampleBuffer, PreviewTexture>
    {
    public:
        /// Records the image producer while leaving explicit destruction to its successful recipient.
        PreviewTexture build(IN SampleBuffer input) const
        {
            auto texture = createTexture<PreviewTexture>("ExportedPreview", uint2(1u, 1u));
            PreviewFrameBuffer framebuffer;
            framebuffer.color = view(texture);
            renderPass("ExportedPreview", framebuffer, draw<Preview>(input, 3u, 1u, 0u, 0u));
            return texture;
        }
    };

    /// Exports a raster result that remains valid after its execution and runtime are released.
    [[Export]] auto buildImage(IN Input input)
    {
        TaskGraph graph;
        Filter filter;
        filter.elementCount = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto filtered = graph.add(filter, uploaded);
        return graph.add(ImageOutput{}, filtered);
    }

    /// Returns two handles to one allocation after its final in-place write.
    class AliasedOutputs final : public IRenderNode<SampleBuffer, BranchOutputs>
    {
    public:
        uint count = 256u;
        /// Copies resource descriptions without claiming to preserve old GPU contents.
        BranchOutputs build(IN SampleBuffer input) const
        {
            BranchOutputs output;
            output.scaled = input;
            computePass("InPlaceScale", dispatchThreads<Scale>(input, input, count, 2.0f, count, 1u, 1u));
            output.biased = input;
            return output;
        }
    };

    /// Reads an aliased output only after the allocation's final producing write.
    [[Export]] auto buildAliasedOutputGraph(IN Input input)
    {
        TaskGraph graph;
        AliasedOutputs aliases;
        aliases.count = input.count;
        auto values = graph.add(generate, input);
        auto uploaded = graph.add(Upload{}, values);
        auto outputs = graph.add(aliases, uploaded);
        auto copied = graph.add(ReadbackNode<float>{}, outputs.scaled);
        return graph.add(sum, copied);
    }

}
