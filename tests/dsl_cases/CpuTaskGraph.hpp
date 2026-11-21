#pragma once

#include "UGL.h"
#include <EASTL/vector.h>

namespace CpuGraphFixture
{
    using namespace UGL;

    /// Describes a bounded CPU dataset without GPU resources or borrowed storage.
    struct Input
    {
        uint count = 256;
        float scale = 1.0f;
    };

    /// Owns the samples passed between ordinary CPU graph operations.
    struct Samples
    {
        eastl::vector<float> values;
    };

    /// Produces the same CPU reference values as the approved graph design.
    Samples generate(IN Input input)
    {
        Samples output;
        output.values.resize(input.count);
        for (uint index = 0; index < input.count; ++index)
        {
            output.values[index] = float(index) / 256.0f;
        }
        return output;
    }

    /// Uses ordinary per-invocation class state without inheriting a task framework interface.
    class ScoreSamples
    {
    public:
        float scale = 1.0f;

        /// Reduces an owned input snapshot in a deterministic order.
        float operator()(IN Samples input) const
        {
            float total = 0.0f;
            for (float value : input.values)
            {
                total += value;
            }
            return total * scale;
        }
    };

    /// Exports a graph that accepts both a function declaration and an ordinary class value.
    [[Export]] auto buildCpuGraph(IN Input input)
    {
        TaskGraph graph;
        ScoreSamples score;
        score.scale = input.scale;
        auto samples = graph.add(generate, input);
        return graph.add(score, samples);
    }

    /// Sums a projected field as an ordinary owned CPU input.
    float sumValues(IN eastl::vector<float> values)
    {
        float total = 0.0f;
        for (uint index = 0; index < uint(values.size()); ++index)
        {
            total += values[index];
        }
        return total;
    }

    /// Projects a record field without synchronously extracting a task result.
    [[Export]] auto buildProjectedGraph(IN Input input)
    {
        TaskGraph graph;
        auto samples = graph.add(generate, input);
        return graph.add(sumValues, samples.values);
    }

    /// Exports a deferred field directly without adding an artificial CPU task.
    [[Export]] auto buildProjectedOutput(IN Input input)
    {
        TaskGraph graph;
        auto samples = graph.add(generate, input);
        return samples.values;
    }

    /// Contains a nested owned record for recursive field projection.
    struct NestedSamples
    {
        Samples samples;
    };

    /// Produces a nested record without sharing its CPU storage.
    NestedSamples generateNested(IN Input input)
    {
        NestedSamples output;
        output.samples = generate(input);
        return output;
    }

    /// Preserves field types through more than one deferred projection.
    [[Export]] auto buildNestedProjection(IN Input input)
    {
        TaskGraph graph;
        auto nested = graph.add(generateNested, input);
        return graph.add(sumValues, nested.samples.values);
    }

    /// Demonstrates the same host API inside DSL code without using a renderer.
    float evaluateCpuGraph(IN Input input)
    {
        TaskRuntime runtime;
        auto execution = runtime.submit(buildCpuGraph, input);
        execution.wait();
        return execution.get();
    }

    /// Evaluates one contiguous leaf slice of the recursive CPU range subgraph.
    float evalRangeLeaf(uint start, uint count, float factor)
    {
        float acc = 0.0f;
        for (uint i = 0u; i < count; ++i)
        {
            acc += (float(start + i) / 256.0f) * factor;
        }
        return acc;
    }

    /// Recursively subdivides a range using explicit TaskGraph& continuation subgraphs and dynamic Task vector fan-in.
    class RecursiveRangeSubgraph
    {
    public:
        float scale = 1.0f;

        Task<float> operator()(TaskGraph &sub, uint start, uint count) const
        {
            if (count <= 32u)
            {
                return sub.add(evalRangeLeaf, start, count, scale);
            }

            const uint half = count / 2u;
            RecursiveRangeSubgraph child;
            child.scale = scale;
            eastl::vector<Task<float>> parts;
            parts.push_back(sub.add(child, start, half));
            parts.push_back(sub.add(child, start + half, count - half));
            return sub.add(sumValues, parts);
        }
    };

    /// Combines two scalar subgraph outputs.
    float combinePair(float a, float b)
    {
        return a + b;
    }

    /// Sums values using a C++ range-based for loop inside a UGL CPU task body.
    float sumValuesRangeFor(IN eastl::vector<float> values)
    {
        float acc = 0.0f;
        for (float v : values)
        {
            acc += v;
        }
        return acc;
    }

    /// Exports a CPU graph exercising range-based for loop translation in UGLC.
    [[Export]] auto buildRangeForCpuGraph(IN Input input)
    {
        TaskGraph graph;
        auto samples = graph.add(generate, input);
        return graph.add(sumValuesRangeFor, samples.values);
    }

    /// Invokes both an [[Export]] entry subgraph and an explicit TaskGraph& recursive subgraph inside one UGL TaskGraph.
    [[Export]] auto buildNestedCpuSubgraph(IN Input input)
    {
        TaskGraph graph;
        RecursiveRangeSubgraph recursive;
        recursive.scale = input.scale;
        auto exportedChild = graph.add(buildCpuGraph, input);
        auto recursiveChild = graph.add(recursive, 0u, input.count);
        return graph.add(combinePair, exportedChild, recursiveChild);
    }
}
