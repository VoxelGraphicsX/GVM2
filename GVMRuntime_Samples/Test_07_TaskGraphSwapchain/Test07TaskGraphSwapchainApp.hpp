#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Runs the minimal Test07 TaskGraph + RenderNode + Swapchain sample without AbstractRenderer boilerplate.
    class Test07TaskGraphSwapchainApp final : public SampleApplication
    {
    public:
        /// Initializes the sample application and its TaskRuntime on the window's preferred graphics backend.
        explicit Test07TaskGraphSwapchainApp(
            SampleWindowManager &window,
            GVM::RHI::GraphicsBackend preferredBackend = GVM::RHI::GraphicsBackend::Vulkan);

        /// Drains TaskRuntime executions before shared sample RHI resources are released.
        ~Test07TaskGraphSwapchainApp() override;

    private:
        void updateFrame(const SampleFrameContext &context) override;
        void drawOverlay(const SampleFrameContext &context) override;
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
