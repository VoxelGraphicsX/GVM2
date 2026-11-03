#include "GpuGraphTestNode.hpp"
#include <EASTL/algorithm.h>
#include <chrono>
#include <stdexcept>

namespace
{
    /// Holds recording at a bounded gate and records overlap independently of command submission.
    void recordProbe(const eastl::shared_ptr<GpuGraphRecordingProbe> &probe, uint32_t passIndex = UINT32_MAX)
    {
        std::unique_lock lock(probe->mutex);
        ++probe->started;
        ++probe->active;
        probe->maximumActive = eastl::max(probe->maximumActive, probe->active);
        probe->changed.notify_all();
        const bool released = (probe->gatedPass != UINT32_MAX && probe->gatedPass != passIndex) ||
            probe->changed.wait_for(lock, std::chrono::seconds(5), [&probe] { return probe->released; });
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
    if (mProbe->allocateProbeBuffer)
    {
        auto buffer = node.createBuffer({.label = "GraphLifetimeProbe",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 16});
        std::lock_guard lock(mProbe->mutex);
        mProbe->buffer = buffer;
    }
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
            task.drawFn = [probe = mProbe, index](GVM::RHI::RenderPassEncoder) { recordProbe(probe, index); };
            node.renderPass(eastl::move(descriptor), {eastl::move(task)});
        }
        else
        {
            GVM::Core::ComputePassTaskDescriptor task;
            task.resources.complete = !mOpaque;
            task.dispatchFn = [probe = mProbe, index](GVM::RHI::ComputePassEncoder) { recordProbe(probe, index); };
            node.computePass("GraphComputeProbe", eastl::move(task));
        }
    }
    return count;
}
