#include "Test07TaskGraphSwapchainApp.hpp"

#include <GVMCore/Public/TaskRuntime.hpp>
#include <imgui.h>
#include <imgui_impl_GVM.h>
#include <stdexcept>

#include "UGLBin/generate_result.hpp"

namespace GVM::Samples
{
    struct Test07TaskGraphSwapchainApp::Impl
    {
        eastl::unique_ptr<GVM::Core::TaskRuntime> runtime;
        uint32_t workerCount = 2u;
        float elapsedSeconds = 0.0f;
        uint32_t drawableWidth = 800u;
        uint32_t drawableHeight = 600u;
        uint64_t frameIndex = 0u;
    };

    Test07TaskGraphSwapchainApp::Test07TaskGraphSwapchainApp(
        SampleWindowManager &window,
        GVM::RHI::GraphicsBackend preferredBackend)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test07 TaskGraph Swapchain",
                                        .preferredGraphicsBackend = preferredBackend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->runtime = eastl::make_unique<GVM::Core::TaskRuntime>(
            *device(),
            GVM::Core::TaskRuntimeDescriptor{
                .workerCount = mImpl->workerCount,
                .enablePassTimestampFeedback = true,
            });
    }

    Test07TaskGraphSwapchainApp::~Test07TaskGraphSwapchainApp()
    {
        mImpl = nullptr;
    }

    void Test07TaskGraphSwapchainApp::updateFrame(const SampleFrameContext &context)
    {
        mImpl->elapsedSeconds += context.deltaSeconds;
        mImpl->drawableWidth = context.drawableWidth > 0u ? context.drawableWidth : 1u;
        mImpl->drawableHeight = context.drawableHeight > 0u ? context.drawableHeight : 1u;
    }

    void Test07TaskGraphSwapchainApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 128.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.76f);
        if (ImGui::Begin("TaskGraph Telemetry", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Workers: %u", mImpl->workerCount);
            ImGui::Text("Submitted Frames: %llu", static_cast<unsigned long long>(mImpl->frameIndex));
            const auto snapshot = mImpl->runtime->passProfileSnapshot();
            if (!snapshot.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("GPU Pass Feedback:");
                for (const auto &entry : snapshot)
                {
                    ImGui::Text("  %s: %.3f ms (w=%u)",
                        entry.label.c_str(),
                        entry.smoothedDurationNs * 1e-6,
                        static_cast<unsigned>(entry.learnedWeight));
                }
            }
        }
        ImGui::End();
    }

    void Test07TaskGraphSwapchainApp::renderSampleFrame()
    {
        auto nextTexture = swapchain()->queryNextTexture();
        TaskGraphSwapchainSample::FrameRequest request;
        request.swapchainTarget = nextTexture;
        request.params.extent = glm::uvec2(mImpl->drawableWidth, mImpl->drawableHeight);
        request.params.timeSeconds = mImpl->elapsedSeconds;
        request.params.aspectRatio = float(mImpl->drawableWidth) / float(mImpl->drawableHeight);

        auto execution = mImpl->runtime->submit(
            TaskGraphSwapchainSample::Entries::renderSwapchainFrame,
            request);
        if (!execution.wait(10'000'000'000ull) ||
            execution.status() != GVM::Core::ExecutionStatus::Succeeded)
        {
            throw std::runtime_error(execution.error().empty()
                ? "Test07 TaskGraph frame execution timed out."
                : execution.error().c_str());
        }

        swapchain()->present();
        ++mImpl->frameIndex;
    }

    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test07TaskGraphSwapchainApp>(window);
    }
} // namespace GVM::Samples
