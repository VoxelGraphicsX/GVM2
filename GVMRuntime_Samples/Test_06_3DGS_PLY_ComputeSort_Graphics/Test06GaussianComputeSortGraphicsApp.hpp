#pragma once

#include "../GaussianSplattingShared/AppRuntimeShared.hpp"
#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Runs the Test06 3D Gaussian Splatting compute-sort + billboard-graphics viewer on SampleWindowManager.
    class Test06GaussianComputeSortGraphicsApp final : public SampleApplication
    {
    public:
        /// Creates the Test06 viewer, applies CLI overrides, uploads the initial scene, and arms optional captures.
        explicit Test06GaussianComputeSortGraphicsApp(
            SampleWindowManager &window,
            const GaussianSplattingShared::GaussianViewerCliOptions &options = {});

        /// Stops the watchdog and releases Test06 renderer resources before shared RHI teardown.
        ~Test06GaussianComputeSortGraphicsApp() override;

    private:
        /// Handles interactive camera mouse events, P-key capture, R-key reload, and dropped PLY files.
        void onWindowEvent(const SampleWindowEvent &event) override;

        /// Synchronizes drawable dimensions, WASD camera movement, scene reload, and window title metrics.
        void updateFrame(const SampleFrameContext &context) override;

        /// Appends 3DGS compute-sort + graphics telemetry to the shared sample overlay.
        void drawOverlay(const SampleFrameContext &context) override;

        /// Dispatches one 3DGS compute-sort + billboard-render frame and handles optional frame captures.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
