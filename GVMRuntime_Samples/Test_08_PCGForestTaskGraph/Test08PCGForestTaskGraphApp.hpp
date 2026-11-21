#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Execution engine selectable live in Test08 UI or benchmarked via CLI.
    enum class PCGExecutorMode : uint8_t
    {
        Serial = 0,
        ThreadPool = 1,
        MarlFibers = 2,
    };

    /// Interactive 3D procedural forest terrain and heterogeneous CPU/GPU TaskGraph sample.
    class Test08PCGForestTaskGraphApp final : public SampleApplication
    {
    public:
        explicit Test08PCGForestTaskGraphApp(
            SampleWindowManager &window,
            GVM::RHI::GraphicsBackend preferredBackend = GVM::RHI::GraphicsBackend::Metal);

        ~Test08PCGForestTaskGraphApp() override;

    private:
        void onWindowEvent(const SampleWindowEvent &event) override;
        void updateFrame(const SampleFrameContext &context) override;
        void drawOverlay(const SampleFrameContext &context) override;
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };

    /// Runs deterministic headless PCG + GPU rendering benchmarks across Serial, ThreadExecutor, and MarlTaskExecutor.
    int runPCGForestHeadlessBenchmark(
        GVM::RHI::GraphicsBackend preferredBackend,
        uint32_t warmupIterations,
        uint32_t measuredIterations,
        uint32_t workerThreads);

    /// Renders curated 1280x800 showcase screenshots to captureDir using GPU ReadbackTextureNode.
    int runPCGForestShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir,
        uint32_t workerThreads = 0u);
} // namespace GVM::Samples
