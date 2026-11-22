#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "Test06GaussianComputeSortGraphicsApp.hpp"

#include "../SampleHostShared/AppWatchdog.hpp"
#include "AppTextureCapture.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "UGLBin/generate_result.hpp"

namespace
{
    static constexpr double kRenderWatchdogTimeoutMs = 2000.0;
    static constexpr double kRenderWarmupWatchdogTimeoutMs = 12000.0;

    /// Returns monotonic elapsed time in milliseconds for watchdog and telemetry throttling.
    double currentTimeMillis()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        return static_cast<double>(microseconds) * 0.001;
    }

    /// Emits one Test06 status message to standard error.
    void logInfo(const std::string &message)
    {
        std::cerr << "[3dgs-viewer] " << message << std::endl;
    }

    /// Formats the desktop window title showing FPS, frame time, and the active 3DGS scene label.
    std::string buildWindowTitle(const std::string &sceneLabel, double fps, double frameTimeMs)
    {
        std::ostringstream titleBuilder;
        titleBuilder << std::fixed << std::setprecision(1);
        titleBuilder << "GVM 3DGS Compute Sort + Graphics Viewer | " << fps << " FPS | " << frameTimeMs << " ms";
        if (!sceneLabel.empty())
        {
            titleBuilder << " | " << sceneLabel;
        }
        return titleBuilder.str();
    }

    /// Stores Test06's optional CLI-driven or P-key-triggered present capture state.
    struct PresentCaptureState
    {
        bool autoCaptureEnabled = false;
        bool autoCaptureCompleted = false;
        bool manualCaptureRequested = false;
        bool exitAfterCapture = false;
        std::uint64_t triggerFrame = 1u;
        std::uint64_t captureSequence = 0u;
        std::string captureDirectory;
    };

    /// Writes Test06's stable CPU diagnostics and indirect-draw readback keys for one captured frame.
    bool writeDiagnosticsCapture(
        const std::string &diagnosticsCapturePath,
        std::uint64_t frameIndex,
        const Camera &currentCamera,
        bool budgetTelemetryAvailable,
        const GsViewer::BudgetTelemetry &budgetTelemetry,
        bool renderIndirectReadSucceeded,
        const GsViewer::GaussianBillboardRenderPassIndirectRenderCommand &renderIndirect,
        const std::string &renderIndirectError)
    {
        std::ofstream diagnosticsStream(diagnosticsCapturePath, std::ios::binary);
        if (!diagnosticsStream.is_open())
        {
            return false;
        }

        diagnosticsStream << "frame=" << frameIndex << "\n";
        diagnosticsStream << "camera.viewInv.translation="
                          << currentCamera.viewInv[3][0] << ","
                          << currentCamera.viewInv[3][1] << ","
                          << currentCamera.viewInv[3][2] << "\n";
        diagnosticsStream << "camera.view.row0="
                          << currentCamera.view[0][0] << ","
                          << currentCamera.view[0][1] << ","
                          << currentCamera.view[0][2] << ","
                          << currentCamera.view[0][3] << "\n";
        diagnosticsStream << "camera.view.row1="
                          << currentCamera.view[1][0] << ","
                          << currentCamera.view[1][1] << ","
                          << currentCamera.view[1][2] << ","
                          << currentCamera.view[1][3] << "\n";
        diagnosticsStream << "camera.view.row2="
                          << currentCamera.view[2][0] << ","
                          << currentCamera.view[2][1] << ","
                          << currentCamera.view[2][2] << ","
                          << currentCamera.view[2][3] << "\n";
        diagnosticsStream << "telemetry.available=" << (budgetTelemetryAvailable ? 1 : 0) << "\n";
        if (budgetTelemetryAvailable)
        {
            diagnosticsStream << "telemetry.frameTag=" << budgetTelemetry.frameTag << "\n";
            diagnosticsStream << "telemetry.requiredEntryCount=" << budgetTelemetry.requiredEntryCount << "\n";
            diagnosticsStream << "telemetry.projectedSplatCount=" << budgetTelemetry.projectedSplatCount << "\n";
            diagnosticsStream << "telemetry.projectedEntryCount=" << budgetTelemetry.projectedEntryCount << "\n";
            diagnosticsStream << "telemetry.nonEmptyTileCount=" << budgetTelemetry.nonEmptyTileCount << "\n";
            diagnosticsStream << "telemetry.lightTileCount=" << budgetTelemetry.lightTileCount << "\n";
            diagnosticsStream << "telemetry.heavyTileCount=" << budgetTelemetry.heavyTileCount << "\n";
            diagnosticsStream << "telemetry.heavySegmentCount=" << budgetTelemetry.heavySegmentCount << "\n";
            diagnosticsStream << "telemetry.maxProjectedTileCount=" << budgetTelemetry.maxProjectedTileCount << "\n";
            diagnosticsStream << "telemetry.maxTileRangeLength=" << budgetTelemetry.maxTileRangeLength << "\n";
            diagnosticsStream << "telemetry.maxHeavyTileSegmentCount=" << budgetTelemetry.maxHeavyTileSegmentCount << "\n";
        }
        diagnosticsStream << "renderIndirect.readbackSucceeded=" << (renderIndirectReadSucceeded ? 1 : 0) << "\n";
        if (renderIndirectReadSucceeded)
        {
            diagnosticsStream << "renderIndirect.vertexCount=" << renderIndirect.vertexCount << "\n";
            diagnosticsStream << "renderIndirect.instanceCount=" << renderIndirect.instanceCount << "\n";
            diagnosticsStream << "renderIndirect.firstVertex=" << renderIndirect.firstVertex << "\n";
            diagnosticsStream << "renderIndirect.firstInstance=" << renderIndirect.firstInstance << "\n";
        }
        else
        {
            diagnosticsStream << "renderIndirect.error=" << renderIndirectError << "\n";
        }

        diagnosticsStream.flush();
        return diagnosticsStream.good();
    }
} // namespace

namespace GVM::Samples
{
    /// Holds Test06 renderer, watchdog, scene, capture, input, and telemetry state.
    struct Test06GaussianComputeSortGraphicsApp::Impl
    {
        MyRenderer renderer;
        SampleHostShared::AppWatchdog watchdog{
            "3dgs-viewer",
            kRenderWatchdogTimeoutMs,
            SampleHostShared::WatchdogPhase::LoadingScene,
            kRenderWarmupWatchdogTimeoutMs};
        PresentCaptureState presentCapture;
        std::optional<std::string> activeScenePath;
        std::string activeSceneLabel;
        uint32_t drawableWidth = 1280u;
        uint32_t drawableHeight = 720u;
        bool leftMouseDown = false;
        bool hasMouseMotion = false;
        int mouseX = 0;
        int mouseY = 0;
        bool reloadScene = false;
        std::uint64_t frameIndex = 0u;
        double fpsAccumulatedMs = 0.0;
        std::uint64_t fpsAccumulatedFrames = 0u;
        double lastRasterTelemetryLogMs = 0.0;
        uint32_t lastLoggedRasterTelemetryFrameTag = GsViewer::InvalidTelemetryFrameTag;
        double renderWarmupDeadlineMs = 0.0;
        GsViewer::BudgetTelemetry latestTelemetry = {};
        bool hasLatestTelemetry = false;
    };

    Test06GaussianComputeSortGraphicsApp::Test06GaussianComputeSortGraphicsApp(
        SampleWindowManager &window,
        const GaussianSplattingShared::GaussianViewerCliOptions &options)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test06 3DGS ComputeSort Graphics",
                                        .preferredGraphicsBackend = options.backend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        using SampleHostShared::WatchdogPhase;

        window.getDrawableSize(mImpl->drawableWidth, mImpl->drawableHeight);
        mImpl->drawableWidth = std::max(mImpl->drawableWidth, 1u);
        mImpl->drawableHeight = std::max(mImpl->drawableHeight, 1u);

        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
        mImpl->renderer->resize(mImpl->drawableWidth, mImpl->drawableHeight);

        if (options.sortEntryFrustumDilation.has_value())
        {
            mImpl->renderer->setSortEntryFrustumDilation(static_cast<float>(*options.sortEntryFrustumDilation));
            logInfo("sort-entry frustum dilation override applied: " + std::to_string(*options.sortEntryFrustumDilation));
        }

        logInfo("renderer initialized");
        logInfo("controls: WASD move, left mouse drag look, drop .ply to load, R reload current scene, P capture outputTexture+swapchainTexture");

        mImpl->presentCapture.autoCaptureEnabled = options.capturePresent;
        mImpl->presentCapture.exitAfterCapture = options.exitAfterCapture;
        mImpl->presentCapture.triggerFrame = options.captureFrame;
        mImpl->presentCapture.captureDirectory = options.captureDirectory.empty()
            ? GaussianSplattingShared::defaultCaptureDirectory()
            : options.captureDirectory;
        if (mImpl->presentCapture.autoCaptureEnabled)
        {
            GaussianSplattingShared::ensureCaptureDirectoryExists(mImpl->presentCapture.captureDirectory, logInfo);
            logInfo(
                "present capture armed: frame=" + std::to_string(mImpl->presentCapture.triggerFrame) +
                " dir=" + mImpl->presentCapture.captureDirectory +
                " exitAfterCapture=" + (mImpl->presentCapture.exitAfterCapture ? std::string("true") : std::string("false")));
        }

        mImpl->watchdog.start();
        mImpl->activeScenePath = options.scenePath;
        mImpl->watchdog.touch(WatchdogPhase::LoadingScene);
        GaussianSplattingShared::uploadInitialScene(mImpl->renderer, mImpl->activeScenePath, mImpl->activeSceneLabel, logInfo);
        mImpl->watchdog.touch(WatchdogPhase::Idle);
        mImpl->renderWarmupDeadlineMs = currentTimeMillis() + kRenderWarmupWatchdogTimeoutMs;
    }

    Test06GaussianComputeSortGraphicsApp::~Test06GaussianComputeSortGraphicsApp()
    {
        if (mImpl != nullptr)
        {
            mImpl->watchdog.touch(SampleHostShared::WatchdogPhase::Shutdown);
            mImpl->watchdog.stop();
            mImpl = nullptr;
        }
    }

    void Test06GaussianComputeSortGraphicsApp::onWindowEvent(const SampleWindowEvent &event)
    {
        using SampleHostShared::WatchdogPhase;

        if (event.type == SampleWindowEvent::Type::KeyDown)
        {
            if (event.key == 'p' || event.key == 'P')
            {
                mImpl->presentCapture.manualCaptureRequested = true;
            }
            else if (event.key == 'r' || event.key == 'R')
            {
                mImpl->reloadScene = true;
            }
        }
        else if (event.type == SampleWindowEvent::Type::PointerDown && event.pointerId == 1)
        {
            mImpl->leftMouseDown = true;
        }
        else if (event.type == SampleWindowEvent::Type::PointerUp && event.pointerId == 1)
        {
            mImpl->leftMouseDown = false;
        }
        else if (event.type == SampleWindowEvent::Type::PointerMove)
        {
            mImpl->mouseX = static_cast<int>(event.x);
            mImpl->mouseY = static_cast<int>(event.y);
            mImpl->hasMouseMotion = true;
        }
        else if (event.type == SampleWindowEvent::Type::FileDrop)
        {
            const std::string droppedPath(event.droppedFilePath.c_str());
            if (GaussianSplattingShared::hasPlyExtension(droppedPath))
            {
                try
                {
                    mImpl->watchdog.touch(WatchdogPhase::LoadingScene);
                    GaussianSplattingShared::uploadSceneFromPath(mImpl->renderer, droppedPath, mImpl->activeSceneLabel, logInfo);
                    mImpl->activeScenePath = droppedPath;
                    mImpl->renderWarmupDeadlineMs = currentTimeMillis() + kRenderWarmupWatchdogTimeoutMs;
                    mImpl->watchdog.touch(WatchdogPhase::Idle);
                }
                catch (const std::exception &sceneError)
                {
                    mImpl->watchdog.touch(WatchdogPhase::Idle);
                    logInfo("drag-drop load failed: " + std::string(sceneError.what()));
                }
            }
            else
            {
                logInfo("ignored dropped file (not .ply): " + droppedPath);
            }
        }
    }

    void Test06GaussianComputeSortGraphicsApp::updateFrame(const SampleFrameContext &context)
    {
        using SampleHostShared::WatchdogPhase;

        const uint32_t nextWidth = std::max(context.drawableWidth, 1u);
        const uint32_t nextHeight = std::max(context.drawableHeight, 1u);
        if (nextWidth != mImpl->drawableWidth || nextHeight != mImpl->drawableHeight)
        {
            mImpl->drawableWidth = nextWidth;
            mImpl->drawableHeight = nextHeight;
            mImpl->renderer->resize(nextWidth, nextHeight);
            mImpl->renderWarmupDeadlineMs = currentTimeMillis() + kRenderWarmupWatchdogTimeoutMs;
            logInfo("resized drawable to " + std::to_string(nextWidth) + "x" + std::to_string(nextHeight));
        }

        if (mImpl->reloadScene)
        {
            mImpl->watchdog.touch(WatchdogPhase::LoadingScene);
            if (mImpl->activeScenePath.has_value())
            {
                try
                {
                    GaussianSplattingShared::uploadSceneFromPath(
                        mImpl->renderer,
                        mImpl->activeScenePath.value(),
                        mImpl->activeSceneLabel,
                        logInfo);
                    mImpl->renderWarmupDeadlineMs = currentTimeMillis() + kRenderWarmupWatchdogTimeoutMs;
                }
                catch (const std::exception &sceneError)
                {
                    logInfo("reload failed: " + std::string(sceneError.what()));
                }
            }
            else
            {
                GaussianSplattingShared::uploadDebugScene(mImpl->renderer, mImpl->activeSceneLabel, logInfo);
                mImpl->renderWarmupDeadlineMs = currentTimeMillis() + kRenderWarmupWatchdogTimeoutMs;
            }
            mImpl->reloadScene = false;
            mImpl->watchdog.touch(WatchdogPhase::Idle);
        }

        int cameraMoveMask = 0;
        if (window().isKeyDown('w'))
        {
            cameraMoveMask |= 1;
        }
        if (window().isKeyDown('s'))
        {
            cameraMoveMask |= 2;
        }
        if (window().isKeyDown('a'))
        {
            cameraMoveMask |= 4;
        }
        if (window().isKeyDown('d'))
        {
            cameraMoveMask |= 8;
        }

        mImpl->renderer->setMouseLeftClick(mImpl->leftMouseDown ? 1 : 0);
        if (mImpl->hasMouseMotion)
        {
            mImpl->renderer->setMouseMoveX(mImpl->mouseX);
            mImpl->renderer->setMouseMoveY(mImpl->mouseY);
            mImpl->hasMouseMotion = false;
        }
        mImpl->renderer->setCameraMove(cameraMoveMask);
        mImpl->renderer->setDurationTicks(static_cast<double>(context.frameTimeMs));

        mImpl->fpsAccumulatedMs += static_cast<double>(context.frameTimeMs);
        mImpl->fpsAccumulatedFrames += 1u;
        if (mImpl->fpsAccumulatedMs >= 250.0)
        {
            const double displayedFps = static_cast<double>(mImpl->fpsAccumulatedFrames) * 1000.0 / mImpl->fpsAccumulatedMs;
            const double displayedFrameTimeMs = mImpl->fpsAccumulatedMs / static_cast<double>(mImpl->fpsAccumulatedFrames);
            const std::string title = buildWindowTitle(mImpl->activeSceneLabel, displayedFps, displayedFrameTimeMs);
            window().setTitle(title.c_str());
            mImpl->fpsAccumulatedMs = 0.0;
            mImpl->fpsAccumulatedFrames = 0u;
        }
    }

    void Test06GaussianComputeSortGraphicsApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 128.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.76f);
        if (ImGui::Begin("3DGS ComputeSort + Graphics", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Scene: %s", mImpl->activeSceneLabel.c_str());
            if (mImpl->hasLatestTelemetry)
            {
                ImGui::Text("Projected Splats: %u", mImpl->latestTelemetry.projectedSplatCount);
                ImGui::Text("Required Entries: %u", mImpl->latestTelemetry.requiredEntryCount);
            }
        }
        ImGui::End();
    }

    void Test06GaussianComputeSortGraphicsApp::renderSampleFrame()
    {
        using SampleHostShared::WatchdogPhase;

        const double frameStart = currentTimeMillis();
        const WatchdogPhase renderPhase = frameStart < mImpl->renderWarmupDeadlineMs
            ? WatchdogPhase::RenderingWarmup
            : WatchdogPhase::Rendering;
        mImpl->watchdog.touch(renderPhase);
        mImpl->renderer->render();
        mImpl->watchdog.touch(WatchdogPhase::Idle);
        mImpl->frameIndex += 1u;

        const bool autoCaptureTriggered =
            mImpl->presentCapture.autoCaptureEnabled &&
            !mImpl->presentCapture.autoCaptureCompleted &&
            mImpl->frameIndex >= mImpl->presentCapture.triggerFrame;
        if (autoCaptureTriggered || mImpl->presentCapture.manualCaptureRequested)
        {
            GaussianSplattingShared::ensureCaptureDirectoryExists(mImpl->presentCapture.captureDirectory, logInfo);
            mImpl->presentCapture.captureSequence += 1u;

            const std::string outputCapturePath = GaussianSplattingShared::buildStageCapturePath(
                mImpl->presentCapture.captureDirectory,
                "test06",
                "output",
                mImpl->frameIndex,
                mImpl->presentCapture.captureSequence);
            const std::string swapchainCapturePath = GaussianSplattingShared::buildStageCapturePath(
                mImpl->presentCapture.captureDirectory,
                "test06",
                "swapchain",
                mImpl->frameIndex,
                mImpl->presentCapture.captureSequence);
            const std::string diagnosticsCapturePath = GaussianSplattingShared::buildDiagnosticsCapturePath(
                mImpl->presentCapture.captureDirectory,
                "test06",
                mImpl->frameIndex,
                mImpl->presentCapture.captureSequence);

            std::string outputCaptureError;
            const bool outputCaptureSucceeded = GaussianSplattingCapture::dumpRgba16FloatTextureToPpm(
                device(),
                mImpl->renderer->getOutputTextureHandle(),
                mImpl->renderer->getOutputTextureWidth(),
                mImpl->renderer->getOutputTextureHeight(),
                outputCapturePath,
                &outputCaptureError);

            std::string swapchainCaptureError;
            const bool swapchainCaptureSucceeded = GaussianSplattingCapture::dumpRgba8TextureToPpm(
                device(),
                mImpl->renderer->getSwapchainStagingTextureHandle(),
                mImpl->renderer->getOutputTextureWidth(),
                mImpl->renderer->getOutputTextureHeight(),
                swapchainCapturePath,
                &swapchainCaptureError);

            GsViewer::GaussianBillboardRenderPassIndirectRenderCommand renderIndirect = {};
            std::string renderIndirectError;
            const bool renderIndirectReadSucceeded = GaussianSplattingCapture::readBufferStruct(
                device(),
                mImpl->renderer->getGaussianRenderIndirectBufferHandle(),
                &renderIndirect,
                &renderIndirectError);

            GsViewer::BudgetTelemetry budgetTelemetry = {};
            const bool budgetTelemetryAvailable = mImpl->renderer->getLatestBudgetTelemetry(budgetTelemetry);
            const Camera currentCamera = mImpl->renderer->getCurrentCamera();
            const bool diagnosticsWriteSucceeded = writeDiagnosticsCapture(
                diagnosticsCapturePath,
                mImpl->frameIndex,
                currentCamera,
                budgetTelemetryAvailable,
                budgetTelemetry,
                renderIndirectReadSucceeded,
                renderIndirect,
                renderIndirectError);

            if (autoCaptureTriggered)
            {
                mImpl->presentCapture.autoCaptureCompleted = true;
            }

            if (outputCaptureSucceeded)
            {
                logInfo("captured outputTexture to " + outputCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }
            else
            {
                logInfo("outputTexture capture failed for " + outputCapturePath + " at frame=" + std::to_string(mImpl->frameIndex) + " reason=" + outputCaptureError);
            }

            if (swapchainCaptureSucceeded)
            {
                logInfo("captured swapchainTexture to " + swapchainCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }
            else
            {
                logInfo("swapchainTexture capture failed for " + swapchainCapturePath + " at frame=" + std::to_string(mImpl->frameIndex) + " reason=" + swapchainCaptureError);
            }

            if (diagnosticsWriteSucceeded)
            {
                logInfo("captured diagnostics to " + diagnosticsCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }
            else
            {
                logInfo("diagnostics capture failed for " + diagnosticsCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }

            if (autoCaptureTriggered && mImpl->presentCapture.exitAfterCapture)
            {
                requestQuit();
            }
            mImpl->presentCapture.manualCaptureRequested = false;
        }

        if ((mImpl->frameIndex % 240u) == 0u)
        {
            logInfo(
                "heartbeat frame=" + std::to_string(mImpl->frameIndex) +
                " scene=" + mImpl->activeSceneLabel +
                " size=" + std::to_string(mImpl->drawableWidth) + "x" + std::to_string(mImpl->drawableHeight));
        }

        GsViewer::BudgetTelemetry telemetry = {};
        if (mImpl->renderer->getLatestBudgetTelemetry(telemetry))
        {
            mImpl->latestTelemetry = telemetry;
            mImpl->hasLatestTelemetry = true;

            const double nowMs = currentTimeMillis();
            if (telemetry.frameTag != mImpl->lastLoggedRasterTelemetryFrameTag &&
                nowMs - mImpl->lastRasterTelemetryLogMs >= 1000.0)
            {
                const double projectedAverageEntries = telemetry.projectedSplatCount > 0u
                    ? static_cast<double>(telemetry.projectedEntryCount) / static_cast<double>(telemetry.projectedSplatCount)
                    : 0.0;

                std::ostringstream telemetryBuilder;
                telemetryBuilder << std::fixed << std::setprecision(2);
                telemetryBuilder << "tile-raster telemetry"
                                 << " projected_splats=" << telemetry.projectedSplatCount
                                 << " projected_entries=" << telemetry.projectedEntryCount
                                 << " projected_avg_entries=" << projectedAverageEntries
                                 << " non_empty_tiles=" << telemetry.nonEmptyTileCount
                                 << " light_tiles=" << telemetry.lightTileCount
                                 << " heavy_tiles=" << telemetry.heavyTileCount
                                 << " heavy_segments=" << telemetry.heavySegmentCount
                                 << " max_projected_tiles=" << telemetry.maxProjectedTileCount
                                 << " max_tile_range=" << telemetry.maxTileRangeLength
                                 << " max_heavy_segments_per_tile=" << telemetry.maxHeavyTileSegmentCount;
                logInfo(telemetryBuilder.str());
                mImpl->lastLoggedRasterTelemetryFrameTag = telemetry.frameTag;
                mImpl->lastRasterTelemetryLogMs = nowMs;
            }
        }
    }

    /// Creates the Test06 3DGS compute-sort + graphics sample application bound to the supplied SampleWindowManager.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test06GaussianComputeSortGraphicsApp>(window);
    }
} // namespace GVM::Samples
