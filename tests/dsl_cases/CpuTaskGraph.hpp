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

    /// Demonstrates the same host API inside DSL code without using a renderer.
    float evaluateCpuGraph(IN Input input)
    {
        TaskRuntime runtime;
        auto execution = runtime.submit(buildCpuGraph, input);
        execution.wait();
        return execution.get();
    }
}
