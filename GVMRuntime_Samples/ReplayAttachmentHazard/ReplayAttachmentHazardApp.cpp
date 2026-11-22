#include "ReplayAttachmentHazardApp.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <chrono>
#include <cstdio>

#include "UGLBin/generate_result.hpp"

namespace
{
    /// Returns a stable lowercase display name for one graphics backend.
    const char *hazardBackendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal:
            return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan:
            return "vulkan";
        default:
            return "undefined";
        }
    }

    /// Returns milliseconds elapsed between two steady-clock time points.
    double elapsedMilliseconds(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }
} // namespace

namespace GVM::Samples
{
    /// Holds ReplayAttachmentHazard renderer state and frame-timing accumulators.
    struct ReplayAttachmentHazardApp::Impl
    {
        ReplayAttachmentHazardSampleConfig config;
        ReplayAttachmentHazard::HazardRenderer renderer;
        const char *activeBackend = "undefined";
        double totalFrameMs = 0.0;
        double maxFrameMs = 0.0;
        uint32_t renderedFrames = 0u;
    };

    ReplayAttachmentHazardApp::ReplayAttachmentHazardApp(
        SampleWindowManager &window,
        const ReplayAttachmentHazardSampleConfig &config)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Replay Attachment Hazard",
                                        .preferredGraphicsBackend = config.backend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->config = config;

        uint32_t drawableWidth = 1u;
        uint32_t drawableHeight = 1u;
        window.getDrawableSize(drawableWidth, drawableHeight);

        mImpl->renderer = ReplayAttachmentHazard::makeHazardRenderer();
        mImpl->renderer->init(device(), swapchain(), drawableWidth, drawableHeight);
        mImpl->renderer->setConsumerMode(config.consumerMode);
        mImpl->renderer->setGrassEnabled(config.grassEnabled ? 1 : 0);
        mImpl->renderer->setGrassWritesReplay(config.grassWritesReplay ? 1 : 0);
        mImpl->renderer->setPixelLocalEnabled(config.pixelLocalEnabled ? 1 : 0);
        mImpl->renderer->setGrassInstanceCount(static_cast<int>(config.grassInstanceCount));
        mImpl->renderer->setGrassVertexCount(static_cast<int>(config.grassVertexCount));
        mImpl->renderer->setGrassDrawIndirect(config.grassDrawIndirect ? 1 : 0);
        mImpl->renderer->setGrassBaryEnabled(config.grassBaryEnabled ? 1 : 0);

        mImpl->activeBackend = hazardBackendName(instance()->getBackend());
        std::printf(
            "[replay-hazard] active_backend=%s requested_backend=%s drawable=%ux%u frames=%u "
            "grass=%u replay_in_grass=%s pixel_local=%u consumer_mode=%d grass_instances=%u "
            "grass_vertices=%u grass_draw=%s grass_bary=%u\n",
            mImpl->activeBackend,
            hazardBackendName(config.backend),
            drawableWidth,
            drawableHeight,
            config.frames,
            config.grassEnabled ? 1u : 0u,
            config.grassWritesReplay ? "write" : "omit",
            config.pixelLocalEnabled ? 1u : 0u,
            config.consumerMode,
            config.grassInstanceCount,
            config.grassVertexCount,
            config.grassDrawIndirect ? "indirect" : "direct",
            config.grassBaryEnabled ? 1u : 0u);
        std::printf(
            "[replay-hazard] gpu_scope_legend=no-grass:scope0=BaseVisibilityPass,scope1=ConsumerPass; "
            "grass:scope0=BaseVisibilityPass,scope1=GrassVisibilityPass,scope2=ConsumerPass\n");
    }

    ReplayAttachmentHazardApp::~ReplayAttachmentHazardApp()
    {
        mImpl = nullptr;
    }

    void ReplayAttachmentHazardApp::printSummary() const
    {
        const double avgFrameMs = mImpl->renderedFrames > 0u
            ? mImpl->totalFrameMs / static_cast<double>(mImpl->renderedFrames)
            : 0.0;
        const double avgFps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;
        std::printf(
            "[replay-hazard-result] active_backend=%s frames=%u avg_frame_ms=%.3f avg_fps=%.2f max_frame_ms=%.3f\n",
            mImpl->activeBackend,
            mImpl->renderedFrames,
            avgFrameMs,
            avgFps,
            mImpl->maxFrameMs);
    }

    void ReplayAttachmentHazardApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 128.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.76f);
        if (ImGui::Begin("Replay Hazard Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Consumer Mode: %d", mImpl->config.consumerMode);
            ImGui::Text("Grass: %s (%s)", mImpl->config.grassEnabled ? "on" : "off", mImpl->config.grassWritesReplay ? "write" : "omit");
            ImGui::Text("PixelLocal: %s", mImpl->config.pixelLocalEnabled ? "on" : "off");
            ImGui::Text("Rendered Frames: %u", mImpl->renderedFrames);
        }
        ImGui::End();
    }

    void ReplayAttachmentHazardApp::renderSampleFrame()
    {
        const auto begin = std::chrono::steady_clock::now();
        mImpl->renderer->render();
        const double frameMs = elapsedMilliseconds(begin, std::chrono::steady_clock::now());
        mImpl->totalFrameMs += frameMs;
        mImpl->maxFrameMs = frameMs > mImpl->maxFrameMs ? frameMs : mImpl->maxFrameMs;
        ++mImpl->renderedFrames;

        if ((mImpl->renderedFrames % 60u) == 0u)
        {
            std::printf("[replay-hazard] frame=%u cpu_frame_ms=%.3f\n", mImpl->renderedFrames, frameMs);
        }
    }

    /// Creates the ReplayAttachmentHazard sample application bound to the supplied SampleWindowManager.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<ReplayAttachmentHazardApp>(window);
    }
} // namespace GVM::Samples
