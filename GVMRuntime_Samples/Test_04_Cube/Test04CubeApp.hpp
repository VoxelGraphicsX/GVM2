#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Runs the Test04 multi-pass checkerboard + 3D cube renderer against a SampleWindowManager surface.
    class Test04CubeApp final : public SampleApplication
    {
    public:
        /// Creates the Test04 renderer using the shared sample RHI resources and selected backend.
        explicit Test04CubeApp(
            SampleWindowManager &window,
            GVM::RHI::GraphicsBackend preferredBackend = GVM::RHI::GraphicsBackend::Vulkan);

        /// Releases Test04 renderer resources before shared sample RHI resources are destroyed.
        ~Test04CubeApp() override;

    private:
        /// Captures pointer press and motion events for interactive camera rotation.
        void onWindowEvent(const SampleWindowEvent &event) override;

        /// Updates WASD movement state and per-frame timing inputs before rendering.
        void updateFrame(const SampleFrameContext &context) override;

        /// Executes one Test04 multi-pass frame and presents it to the swapchain.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
