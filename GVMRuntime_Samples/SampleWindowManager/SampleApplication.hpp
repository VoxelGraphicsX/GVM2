#pragma once

#include "SampleWindowManager.hpp"

#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace GVM::Samples
{
    /// Defines the platform-independent host contract implemented by one windowed sample.
    class ISampleApplication
    {
    public:
        /// Releases sample-owned renderer and runtime resources.
        virtual ~ISampleApplication() = default;

        /// Processes pending SampleWindowManager events for this sample.
        virtual void processPendingEvents() = 0;

        /// Renders one frame when the platform surface is available.
        virtual void renderFrame() = 0;

        /// Reports whether the sample has requested shutdown.
        virtual bool shouldQuit() const = 0;
    };

    /// Describes the common RHI and overlay settings used by a windowed sample application.
    struct SampleApplicationCreateInfo
    {
        const char *sampleName = "GVM Sample";
        GVM::RHI::GraphicsBackend preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;
        uint32_t imguiFramesInFlight = 3;
        /// Controls whether the RHI diagnostics overlay is composited into the sample output.
        bool diagnosticsOverlayEnabled = true;
    };

    /// Provides frame timing and drawable metadata passed to overridable sample hooks.
    struct SampleFrameContext
    {
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
        const char *backendName = "Vulkan";
        uint32_t windowWidth = 1;
        uint32_t windowHeight = 1;
        uint32_t drawableWidth = 1;
        uint32_t drawableHeight = 1;
        float deltaSeconds = 0.0f;
        float frameTimeMs = 0.0f;
    };

    /// Owns the default windowed-sample RHI lifecycle and leaves sample-specific rendering to subclasses.
    class SampleApplication : public ISampleApplication
    {
    public:
        /// Creates the RHI instance, device, swapchain, and ImGui context for one windowed sample.
        SampleApplication(SampleWindowManager &window, const SampleApplicationCreateInfo &createInfo);

        /// Releases shared ImGui and RHI resources after subclass-owned renderer resources are destroyed.
        ~SampleApplication() override;

        /// Processes common window events, forwards input to ImGui, and updates the quit state.
        void processPendingEvents() final;

        /// Starts a common sample frame, emits overlay UI, and asks the subclass to render.
        void renderFrame() final;

        /// Reports whether the common event processing has requested shutdown.
        bool shouldQuit() const final;

    protected:
        /// Returns the sample window wrapper owned by the platform host.
        SampleWindowManager &window() const;

        /// Returns the GVMRHI instance shared by the windowed sample.
        GVM::RHI::Instance instance() const;

        /// Returns the GVMRHI device shared by the windowed sample.
        GVM::RHI::Device device() const;

        /// Returns the GVMRHI swapchain created from the sample window surface.
        GVM::RHI::Swapchain swapchain() const;

        /// Returns the stable display name used by the default sample overlay.
        const char *sampleName() const;

        /// Requests shutdown from a subclass without depending on platform-specific exit APIs.
        void requestQuit();

        /// Lets subclasses inspect or consume normalized input and window events.
        virtual void onWindowEvent(const SampleWindowEvent &event);

        /// Lets subclasses update CPU-side state before overlay UI is finalized.
        virtual void updateFrame(const SampleFrameContext &context);

        /// Draws the sample overlay after ImGui has started a new frame.
        virtual void drawOverlay(const SampleFrameContext &context);

        /// Renders the sample frame after common ImGui draw data has been finalized.
        virtual void renderSampleFrame() = 0;

    private:
        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };

    /// Creates the sample application bound to the supplied SampleWindowManager.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window);
} // namespace GVM::Samples
