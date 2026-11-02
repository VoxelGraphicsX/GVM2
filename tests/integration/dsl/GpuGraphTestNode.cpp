#include "GpuGraphTestNode.hpp"
#include <EASTL/algorithm.h>
#include <chrono>
#include <stdexcept>

namespace
{
    /// Holds recording at a bounded gate and records overlap independently of command submission.
    void recordProbe(const eastl::shared_ptr<GpuGraphRecordingProbe> &probe)
    {
        std::unique_lock lock(probe->mutex);
        ++probe->started;
        ++probe->active;
        probe->maximumActive = eastl::max(probe->maximumActive, probe->active);
        probe->changed.notify_all();
        const bool released = probe->changed.wait_for(lock, std::chrono::seconds(5), [&probe] { return probe->released; });
        --probe->active;
        if (!released || probe->fail)
        {
            throw std::runtime_error("Graph recording probe failed.");
        }
    }
}

GpuGraphTestNode::GpuGraphTestNode(eastl::shared_ptr<GpuGraphRecordingProbe> probe, bool opaque,
    GVM::RHI::TextureView target)
    : mProbe(eastl::move(probe)), mOpaque(opaque), mTarget(target)
{
}

uint32_t GpuGraphTestNode::build(GVM::Core::RenderNode &node, uint32_t count) const
{
    if (mProbe->observeBuild)
    {
        recordProbe(mProbe);
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (!mTarget.isNull() && index % 2 == 0)
        {
            GVM::RHI::RenderPassDescriptor descriptor;
            descriptor.label = "GraphRenderProbe";
            descriptor.colorAttachments.push_back({.view = mTarget,
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {float(index == 0), float(index != 0), 0.0f, 1.0f}});
            GVM::Core::RenderPassTaskDescriptor task;
            task.resources.complete = !mOpaque;
            task.drawFn = [probe = mProbe](GVM::RHI::RenderPassEncoder) { recordProbe(probe); };
            node.renderPass(eastl::move(descriptor), {eastl::move(task)});
        }
        else
        {
            GVM::Core::ComputePassTaskDescriptor task;
            task.resources.complete = !mOpaque;
            task.dispatchFn = [probe = mProbe](GVM::RHI::ComputePassEncoder) { recordProbe(probe); };
            node.computePass("GraphComputeProbe", eastl::move(task));
        }
    }
    return count;
}
