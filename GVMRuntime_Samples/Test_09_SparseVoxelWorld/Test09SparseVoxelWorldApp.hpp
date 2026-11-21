#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Execution engine selectable live in Test09 UI or benchmarked via CLI.
    enum class SparseVoxelExecutorMode : uint8_t
    {
        Serial = 0,
        ThreadPool = 1,
        MarlFibers = 2,
    };

    /// Interactive 3D 0.125m Pure Sparse Voxel Open-World Dynamic PCG sample.
    class Test09SparseVoxelWorldApp final : public SampleApplication
    {
    public:
        explicit Test09SparseVoxelWorldApp(
            SampleWindowManager &window,
            GVM::RHI::GraphicsBackend preferredBackend = GVM::RHI::GraphicsBackend::Metal);

        ~Test09SparseVoxelWorldApp() override;

    private:
        void onWindowEvent(const SampleWindowEvent &event) override;
        void updateFrame(const SampleFrameContext &context) override;
        void drawOverlay(const SampleFrameContext &context) override;
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };

    /// Runs deterministic headless sparse-voxel + GPU rendering benchmarks across Serial, ThreadExecutor, and MarlTaskExecutor.
    int runSparseVoxelHeadlessBenchmark(
        GVM::RHI::GraphicsBackend preferredBackend,
        uint32_t warmupIterations,
        uint32_t measuredIterations,
        uint32_t workerThreads);

    /// Captures curated 1280x800 GPU-rendered PNG showcase screenshots using MarlTaskExecutor.
    int runSparseVoxelShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir);
} // namespace GVM::Samples
