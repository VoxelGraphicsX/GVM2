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

}
