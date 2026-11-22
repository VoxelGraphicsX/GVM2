#pragma once

#include "../GaussianSplattingShared/AppRuntimeShared.hpp"
#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Runs the Test05 3D Gaussian Splatting pure-compute tile rasterizer sample on SampleWindowManager.
    class Test05GaussianPlyComputeApp final : public SampleApplication
    {
    public:
        /// Creates the Test05 compute viewer, uploads the initial scene, and arms optional diagnostics capture.
        explicit Test05GaussianPlyComputeApp(
            SampleWindowManager &window,
            const GaussianSplattingShared::GaussianViewerCliOptions &options = {});

        /// Stops the watchdog and releases Test05 renderer resources before shared RHI teardown.
        ~Test05GaussianPlyComputeApp() override;

    private:
        /// Handles interactive camera mouse events, R-key reload, and dropped PLY files.
        void onWindowEvent(const SampleWindowEvent &event) override;

        /// Synchronizes drawable dimensions, WASD camera movement, scene reload, and window title metrics.
        void updateFrame(const SampleFrameContext &context) override;

        /// Appends 3DGS scene and tile-raster telemetry to the shared sample overlay.
        void drawOverlay(const SampleFrameContext &context) override;

        /// Dispatches one 3DGS compute frame and writes optional frame diagnostics capture.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
