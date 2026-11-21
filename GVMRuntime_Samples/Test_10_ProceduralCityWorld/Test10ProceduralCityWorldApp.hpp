#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Execution engine selectable live in Test10 UI or benchmarked via CLI.
    enum class CityExecutorMode : uint8_t
    {
        Serial = 0,
        ThreadPool = 1,
        MarlFibers = 2,
    };

    /// Interactive 480m x 480m Open-World Procedural Metropolis + Data-Dependent Nested TaskGraph sample.
    class Test10ProceduralCityWorldApp final : public SampleApplication
    {
    public:
        explicit Test10ProceduralCityWorldApp(
            SampleWindowManager &window,
            GVM::RHI::GraphicsBackend preferredBackend = GVM::RHI::GraphicsBackend::Metal);

        ~Test10ProceduralCityWorldApp() override;

    private:
        void onWindowEvent(const SampleWindowEvent &event) override;
        void updateFrame(const SampleFrameContext &context) override;
        void drawOverlay(const SampleFrameContext &context) override;
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };

    /// Runs deterministic headless city generation + GPU rendering benchmarks across Serial, ThreadPool, and Marl.
    int runProceduralCityHeadlessBenchmark(
        GVM::RHI::GraphicsBackend preferredBackend,
        uint32_t warmupIterations,
        uint32_t measuredIterations,
        uint32_t workerThreads);

    /// Renders 3 curated 1280x800 showcase screenshots to captureDir using MarlTaskExecutor.
    int runProceduralCityShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir);
} // namespace GVM::Samples
