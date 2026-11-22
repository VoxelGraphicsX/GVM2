#pragma once

#include "SampleApplication.hpp"

#include <cstdint>

namespace GVM::Samples
{
    /// Holds the command-line configuration for one replay attachment hazard sample run.
    struct ReplayAttachmentHazardSampleConfig
    {
#if defined(__APPLE__)
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Metal;
#else
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
#endif
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t frames = 600;
        uint32_t grassInstanceCount = 178791;
        uint32_t grassVertexCount = 39;
        int consumerMode = 2;
        bool grassEnabled = true;
        bool grassWritesReplay = false;
        bool pixelLocalEnabled = true;
        bool grassDrawIndirect = true;
        bool grassBaryEnabled = true;
    };

    /// Runs the ReplayAttachmentHazard visibility + pixel-local consumer sample on SampleWindowManager.
    class ReplayAttachmentHazardApp final : public SampleApplication
    {
    public:
        /// Creates the hazard renderer on the shared sample RHI device and swapchain.
        explicit ReplayAttachmentHazardApp(
            SampleWindowManager &window,
            const ReplayAttachmentHazardSampleConfig &config = {});

        /// Releases hazard renderer resources before shared sample RHI resources are destroyed.
        ~ReplayAttachmentHazardApp() override;

        /// Prints the aggregated CPU frame-time summary collected across rendered frames.
        void printSummary() const;

    private:
        /// Appends hazard configuration and frame counters to the shared sample overlay.
        void drawOverlay(const SampleFrameContext &context) override;

        /// Executes one visibility + consumer frame and updates timing statistics.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
