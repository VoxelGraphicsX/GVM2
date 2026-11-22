#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "Test05GaussianPlyComputeApp.hpp"

#include "../SampleHostShared/AppWatchdog.hpp"

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
    /// Returns monotonic elapsed time in milliseconds for watchdog and telemetry throttling.
    double currentTimeMillis()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        return static_cast<double>(microseconds) * 0.001;
    }

    /// Emits one Test05 status message to standard error.
    void logInfo(const std::string &message)
    {
        std::cerr << "[3dgs-viewer] " << message << std::endl;
    }

    /// Stores Test05's optional CLI-driven diagnostics capture state.
    struct DiagnosticsCaptureState
    {
        bool autoCaptureEnabled = false;
        bool autoCaptureCompleted = false;
        bool exitAfterCapture = false;
        std::uint64_t triggerFrame = 1u;
        std::uint64_t captureSequence = 0u;
        std::string captureDirectory;
    };

    /// Writes Test05's stable CPU diagnostics keys for one presented frame.
    bool writeDiagnosticsCapture(
        const std::string &diagnosticsCapturePath,
        std::uint64_t frameIndex,
        const Camera &currentCamera,
        bool budgetTelemetryAvailable,
        const GsViewer::BudgetTelemetry &budgetTelemetry)
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
            diagnosticsStream << "telemetry.duplicateOverflowCount=" << budgetTelemetry.duplicateOverflowCount << "\n";
            diagnosticsStream << "telemetry.scatterOverflowCount=" << budgetTelemetry.scatterOverflowCount << "\n";
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

        diagnosticsStream.flush();
        return diagnosticsStream.good();
    }

    /// Formats the desktop window title showing FPS, frame time, and the active 3DGS scene label.
    std::string buildWindowTitle(const std::string &sceneLabel, double fps, double frameTimeMs)
    {
        std::ostringstream titleBuilder;
        titleBuilder << std::fixed << std::setprecision(1);
        titleBuilder << "GVM 3DGS PLY Compute Viewer | " << fps << " FPS | " << frameTimeMs << " ms";
        if (!sceneLabel.empty())
        {
            titleBuilder << " | " << sceneLabel;
        }
        return titleBuilder.str();
    }
} // namespace

namespace GVM::Samples
{
    /// Holds Test05 renderer, watchdog, scene, input, and telemetry state.
    struct Test05GaussianPlyComputeApp::Impl
    {
        MyRenderer renderer;
        SampleHostShared::AppWatchdog watchdog{"3dgs-viewer", 8000.0};
        DiagnosticsCaptureState diagnosticsCapture;
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
        GsViewer::BudgetTelemetry latestTelemetry = {};
        bool hasLatestTelemetry = false;
    };

    Test05GaussianPlyComputeApp::Test05GaussianPlyComputeApp(
        SampleWindowManager &window,
        const GaussianSplattingShared::GaussianViewerCliOptions &options)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test05 3DGS PLY Compute",
                                        .preferredGraphicsBackend = options.backend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        using SampleHostShared::WatchdogPhase;

        mImpl->watchdog.start();
        window.getDrawableSize(mImpl->drawableWidth, mImpl->drawableHeight);
        mImpl->drawableWidth = std::max(mImpl->drawableWidth, 1u);
        mImpl->drawableHeight = std::max(mImpl->drawableHeight, 1u);

        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
        mImpl->renderer->resize(mImpl->drawableWidth, mImpl->drawableHeight);
        logInfo("renderer initialized");
        logInfo("diagnostic mode active: forcing every gaussian peak opacity to 0.30 while preserving gaussian falloff");
        logInfo("controls: WASD move, left mouse drag look, drop .ply to load, R reload current scene");

        mImpl->diagnosticsCapture.autoCaptureEnabled = options.capturePresent;
        mImpl->diagnosticsCapture.exitAfterCapture = options.exitAfterCapture;
        mImpl->diagnosticsCapture.triggerFrame = options.captureFrame;
        mImpl->diagnosticsCapture.captureDirectory = options.captureDirectory.empty()
            ? GaussianSplattingShared::defaultCaptureDirectory()
            : options.captureDirectory;
        if (mImpl->diagnosticsCapture.autoCaptureEnabled)
        {
            GaussianSplattingShared::ensureCaptureDirectoryExists(mImpl->diagnosticsCapture.captureDirectory, logInfo);
            logInfo(
                "diagnostics capture armed: frame=" + std::to_string(mImpl->diagnosticsCapture.triggerFrame) +
                " dir=" + mImpl->diagnosticsCapture.captureDirectory +
                " exitAfterCapture=" + (mImpl->diagnosticsCapture.exitAfterCapture ? std::string("true") : std::string("false")));
        }

        mImpl->activeScenePath = options.scenePath;
        mImpl->watchdog.touch(WatchdogPhase::LoadingScene);
        GaussianSplattingShared::uploadInitialScene(mImpl->renderer, mImpl->activeScenePath, mImpl->activeSceneLabel, logInfo);
        mImpl->watchdog.touch(WatchdogPhase::Idle);
    }

    Test05GaussianPlyComputeApp::~Test05GaussianPlyComputeApp()
    {
        if (mImpl != nullptr)
        {
            mImpl->watchdog.touch(SampleHostShared::WatchdogPhase::Shutdown);
            mImpl->watchdog.stop();
            mImpl = nullptr;
        }
    }

    void Test05GaussianPlyComputeApp::onWindowEvent(const SampleWindowEvent &event)
    {
        using SampleHostShared::WatchdogPhase;

        if (event.type == SampleWindowEvent::Type::KeyDown && (event.key == 'r' || event.key == 'R'))
        {
            mImpl->reloadScene = true;
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

    void Test05GaussianPlyComputeApp::updateFrame(const SampleFrameContext &context)
    {
        using SampleHostShared::WatchdogPhase;

        const uint32_t nextWidth = std::max(context.drawableWidth, 1u);
        const uint32_t nextHeight = std::max(context.drawableHeight, 1u);
        if (nextWidth != mImpl->drawableWidth || nextHeight != mImpl->drawableHeight)
        {
            mImpl->drawableWidth = nextWidth;
            mImpl->drawableHeight = nextHeight;
            mImpl->renderer->resize(nextWidth, nextHeight);
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
                }
                catch (const std::exception &sceneError)
                {
                    logInfo("reload failed: " + std::string(sceneError.what()));
                }
            }
            else
            {
                GaussianSplattingShared::uploadDebugScene(mImpl->renderer, mImpl->activeSceneLabel, logInfo);
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

    void Test05GaussianPlyComputeApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 128.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.76f);
        if (ImGui::Begin("3DGS Compute Telemetry", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Scene: %s", mImpl->activeSceneLabel.c_str());
            if (mImpl->hasLatestTelemetry)
            {
                ImGui::Text("Projected Splats: %u", mImpl->latestTelemetry.projectedSplatCount);
                ImGui::Text("Projected Entries: %u", mImpl->latestTelemetry.projectedEntryCount);
                ImGui::Text(
                    "Tiles (Light / Heavy): %u / %u",
                    mImpl->latestTelemetry.lightTileCount,
                    mImpl->latestTelemetry.heavyTileCount);
            }
        }
        ImGui::End();
    }

    void Test05GaussianPlyComputeApp::renderSampleFrame()
    {
        using SampleHostShared::WatchdogPhase;

        mImpl->watchdog.touch(WatchdogPhase::Rendering);
        mImpl->renderer->render();
        mImpl->watchdog.touch(WatchdogPhase::Idle);
        mImpl->frameIndex += 1u;

        const bool autoCaptureTriggered =
            mImpl->diagnosticsCapture.autoCaptureEnabled &&
            !mImpl->diagnosticsCapture.autoCaptureCompleted &&
            mImpl->frameIndex >= mImpl->diagnosticsCapture.triggerFrame;
        if (autoCaptureTriggered)
        {
            mImpl->diagnosticsCapture.captureSequence += 1u;
            const std::string diagnosticsCapturePath = GaussianSplattingShared::buildDiagnosticsCapturePath(
                mImpl->diagnosticsCapture.captureDirectory,
                "test05",
                mImpl->frameIndex,
                mImpl->diagnosticsCapture.captureSequence);
            GsViewer::BudgetTelemetry budgetTelemetry = {};
            const bool budgetTelemetryAvailable = mImpl->renderer->getLatestBudgetTelemetry(budgetTelemetry);
            const Camera currentCamera = mImpl->renderer->getCurrentCamera();
            const bool diagnosticsWriteSucceeded = writeDiagnosticsCapture(
                diagnosticsCapturePath,
                mImpl->frameIndex,
                currentCamera,
                budgetTelemetryAvailable,
                budgetTelemetry);
            mImpl->diagnosticsCapture.autoCaptureCompleted = true;
            if (diagnosticsWriteSucceeded)
            {
                logInfo("captured diagnostics to " + diagnosticsCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }
            else
            {
                logInfo("diagnostics capture failed for " + diagnosticsCapturePath + " at frame=" + std::to_string(mImpl->frameIndex));
            }
            if (mImpl->diagnosticsCapture.exitAfterCapture)
            {
                requestQuit();
            }
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

    /// Creates the Test05 3DGS compute sample application bound to the supplied SampleWindowManager.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test05GaussianPlyComputeApp>(window);
    }
} // namespace GVM::Samples
