#pragma once
#include <GVMCore/GVMCore.Public.hpp>
#include <condition_variable>
#include <mutex>

/// Coordinates bounded recording probes without relying on GPU execution timing.
struct GpuGraphRecordingProbe
{
    std::mutex mutex;
    std::condition_variable changed;
    uint32_t started = 0;
    uint32_t active = 0;
    uint32_t maximumActive = 0;
    bool released = true;
    bool fail = false;
    bool observeBuild = false;
    bool allocateProbeBuffer = false;
    GVM::RHI::Buffer buffer;
};

/// Builds native test subgraphs with instrumented compute and optional ordinary render passes.
class GpuGraphTestNode final : public GVM::Core::IRenderNode<uint32_t, uint32_t>
{
public:
    /// Retains the probe; optional framebuffer views must remain live until the execution completes.
    GpuGraphTestNode(eastl::shared_ptr<GpuGraphRecordingProbe> probe, bool opaque = false,
        GVM::RHI::TextureView target = {});
    /// Emits the requested number of passes, alternating render and compute when a target is supplied.
    uint32_t build(GVM::Core::RenderNode &node, uint32_t count) const;
private:
    eastl::shared_ptr<GpuGraphRecordingProbe> mProbe;
    bool mOpaque;
    GVM::RHI::TextureView mTarget;
};
