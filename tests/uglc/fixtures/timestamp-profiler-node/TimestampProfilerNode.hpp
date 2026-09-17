#ifndef UGLC_TEST_TIMESTAMP_PROFILER_NODE_HPP
#define UGLC_TEST_TIMESTAMP_PROFILER_NODE_HPP

#include "UGL.h"

using namespace UGL;

struct TimestampProfilerNodeBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] TimestampProfilerNodePass final : public IComputeClass
{
public:
    constructor(BindGroup<TimestampProfilerNodeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x + 1u;
    }
};

class TimestampProfilerNodeRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Buffer<uint, BufferUsage<Storage, CopyDst, CopySrc>> values;
    BindGroup<TimestampProfilerNodeBindGroup> bindGroup;
    ComputeClass<TimestampProfilerNodePass> timestampPass;
    GpuTimestampFrameProfiler frameProfiler;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        values = device->createBuffer("TimestampProfilerValues", 64);
        bindGroup = device->createBindGroup<TimestampProfilerNodeBindGroup>(values);
        timestampPass = device->createComputeClass<TimestampProfilerNodePass>(bindGroup);
        frameProfiler = device->createTimestampFrameProfiler(2u, "TimestampNodeProfiler");
    }

    void render() override
    {
        Queue queue = device->graphicsQueue(0);
        frameProfiler.reset();
        auto scope = frameProfiler.writePass("TimestampProfilerCompute");
        queue->computePass("TimestampProfilerCompute", scope, timestampPass(64u, 1u, 1u));
        queue->resolveTimestampProfiler(frameProfiler);
        queue->submit();
    }

    void destroy() override
    {
    }
};

#endif
