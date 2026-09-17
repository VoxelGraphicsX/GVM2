#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "UGLBin/generate_result.hpp"

#include "../GaussianSplattingShared/AppRuntimeShared.hpp"
#include "../GaussianSplattingShared/DebugSceneFactory.hpp"
#include "../GaussianSplattingShared/PlyLoader.hpp"
#include "../GaussianSplattingShared/SceneSafety.hpp"
#include "../SampleHostShared/AppWatchdog.hpp"

#include <GVMRHI/GVMRHI.hpp>
#include <SDL.h>
#include <SDL_metal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    static constexpr double kTargetFrameTimeMs = 1000.0 / 120.0;

    double currentTimeMillis()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        return static_cast<double>(microseconds) * 0.001;
    }

    struct AppContext
    {
        SDL_Window *window = nullptr;
        SDL_MetalView metalView = nullptr;
        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device rawDevice = nullptr;
        GVM::RHI::Swapchain swapchain = nullptr;
        int drawableWidth = 1280;
        int drawableHeight = 720;
    };

    void logInfo(const std::string &message)
    {
        std::cerr << "[3dgs-viewer] " << message << std::endl;
        SDL_Log("[3dgs-viewer] %s", message.c_str());
    }

    void updateWindowTitle(SDL_Window *window, const std::string &sceneLabel, double fps, double frameTimeMs)
    {
        if (window == nullptr)
        {
            return;
        }

        std::ostringstream titleBuilder;
        titleBuilder << std::fixed << std::setprecision(1);
        titleBuilder << "GVM 3DGS PLY Compute Viewer | " << fps << " FPS | " << frameTimeMs << " ms";

        if (!sceneLabel.empty())
        {
            titleBuilder << " | " << sceneLabel;
        }

        const std::string title = titleBuilder.str();
        SDL_SetWindowTitle(window, title.c_str());
    }

    using GaussianSplattingShared::formatSceneSummary;
    using GaussianSplattingShared::hasPlyExtension;
    using GaussianSplattingShared::parseEnvFlag;
    using GaussianSplattingShared::parseScenePath;

    /**
     * Stores Test05's optional environment-driven diagnostics capture state.
     *
     * This state is only used for automated readback tests and does not change
     * the default interactive viewer behavior.
     */
    struct DiagnosticsCaptureState
    {
        bool autoCaptureEnabled = false;
        bool autoCaptureCompleted = false;
        bool exitAfterCapture = false;
        std::uint64_t triggerFrame = 1u;
        std::uint64_t captureSequence = 0u;
        std::string captureDirectory = "/tmp";
    };

    /**
     * Writes Test05's stable CPU diagnostics keys for one presented frame.
     *
     * The output intentionally excludes timing and file-path values so it can
     * be compared byte-for-byte across sample refactors.
     */
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

    void initAppContext(AppContext &context)
    {
        logInfo("initializing SDL + Metal window");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
        SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            throw std::runtime_error(SDL_GetError());
        }

        context.window = SDL_CreateWindow("GVM 3DGS PLY Compute Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, context.drawableWidth, context.drawableHeight, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_METAL);
        if (context.window == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        context.metalView = SDL_Metal_CreateView(context.window);
        if (context.metalView == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        context.instance = GVM::RHI::createInstance({
            .preferredBackend = GVM::RHI::GraphicsBackend::Metal,
            .diagnosticsOverlay = {.enabled = GVM::RHI::True},
        });
        if (context.instance == nullptr)
        {
            throw std::runtime_error("Failed to create GVM instance.");
        }

        context.rawDevice = context.instance->createDevice();
        if (context.rawDevice == nullptr)
        {
            throw std::runtime_error("Failed to create GVM device.");
        }

        SDL_Metal_GetDrawableSize(context.window, &context.drawableWidth, &context.drawableHeight);
        GVM::RHI::Extent3D swapchainExtent = {
            .width = static_cast<uint32_t>(std::max(context.drawableWidth, 1)),
            .height = static_cast<uint32_t>(std::max(context.drawableHeight, 1)),
            .depth = 1,
        };
        context.swapchain = context.instance->createSwapchain({
            .A = SDL_Metal_GetLayer(context.metalView),
            .B = &swapchainExtent,
        });
        if (context.swapchain == nullptr)
        {
            throw std::runtime_error("Failed to create swapchain.");
        }

        logInfo("window and swapchain created");
    }

    void destroyAppContext(AppContext &context)
    {
        if (context.swapchain != nullptr)
        {
            context.swapchain->destroy();
            context.swapchain = nullptr;
        }
        if (context.rawDevice != nullptr)
        {
            context.rawDevice->destroy();
            delete context.rawDevice;
            context.rawDevice = nullptr;
        }
        if (context.instance != nullptr)
        {
            delete context.instance;
            context.instance = nullptr;
        }
        if (context.metalView != nullptr && context.window != nullptr)
        {
            SDL_Metal_DestroyView(context.metalView);
            context.metalView = nullptr;
        }
        if (context.window != nullptr)
        {
            SDL_DestroyWindow(context.window);
            context.window = nullptr;
        }
        SDL_Quit();
    }

    void syncDrawableSize(AppContext &context, MyRenderer renderer)
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Metal_GetDrawableSize(context.window, &drawableWidth, &drawableHeight);
        drawableWidth = std::max(drawableWidth, 1);
        drawableHeight = std::max(drawableHeight, 1);
        if (drawableWidth != context.drawableWidth || drawableHeight != context.drawableHeight)
        {
            context.drawableWidth = drawableWidth;
            context.drawableHeight = drawableHeight;
            renderer->resize(static_cast<uint32_t>(drawableWidth), static_cast<uint32_t>(drawableHeight));
            logInfo("resized drawable to " + std::to_string(drawableWidth) + "x" + std::to_string(drawableHeight));
        }
    }

    void uploadDebugScene(MyRenderer renderer, std::string &activeSceneLabel)
    {
        GaussianSplattingShared::uploadDebugScene(renderer, activeSceneLabel, logInfo);
    }

    bool uploadSceneFromPath(MyRenderer renderer, const std::string &scenePath, std::string &activeSceneLabel)
    {
        return GaussianSplattingShared::uploadSceneFromPath(renderer, scenePath, activeSceneLabel, logInfo);
    }

    void uploadInitialScene(MyRenderer renderer, std::optional<std::string> &scenePath, std::string &activeSceneLabel)
    {
        GaussianSplattingShared::uploadInitialScene(renderer, scenePath, activeSceneLabel, logInfo);
    }
} // namespace

int main(int argc, char **argv)
{
    AppContext context;
    using SampleHostShared::WatchdogPhase;
    SampleHostShared::AppWatchdog watchdog("3dgs-viewer", 8000.0);

    try
    {
        watchdog.start();
        initAppContext(context);

        MyRenderer renderer = makeMyRenderer();
        renderer->init(context.rawDevice, context.swapchain);
        renderer->resize(static_cast<uint32_t>(context.drawableWidth), static_cast<uint32_t>(context.drawableHeight));
        logInfo("renderer initialized");
        logInfo("diagnostic mode active: forcing every gaussian peak opacity to 0.30 while preserving gaussian falloff");
        logInfo("controls: WASD move, left mouse drag look, drop .ply to load, R reload current scene");

        DiagnosticsCaptureState diagnosticsCapture;
        diagnosticsCapture.autoCaptureEnabled = parseEnvFlag("GVM_TEST05_CAPTURE_PRESENT");
        diagnosticsCapture.exitAfterCapture = parseEnvFlag("GVM_TEST05_CAPTURE_EXIT_AFTER");
        diagnosticsCapture.captureDirectory = GaussianSplattingShared::getCaptureDirectory("GVM_TEST05_CAPTURE_DIR");
        if (const auto triggerFrame = GaussianSplattingShared::parseEnvUint64("GVM_TEST05_CAPTURE_FRAME", logInfo))
        {
            diagnosticsCapture.triggerFrame = *triggerFrame;
        }
        if (diagnosticsCapture.autoCaptureEnabled)
        {
            GaussianSplattingShared::ensureCaptureDirectoryExists(diagnosticsCapture.captureDirectory, logInfo);
            logInfo("diagnostics capture armed: frame=" + std::to_string(diagnosticsCapture.triggerFrame) + " dir=" + diagnosticsCapture.captureDirectory + " exitAfterCapture=" + (diagnosticsCapture.exitAfterCapture ? std::string("true") : std::string("false")));
        }

        std::optional<std::string> activeScenePath = parseScenePath(argc, argv);
        std::string activeSceneLabel;
        watchdog.touch(WatchdogPhase::LoadingScene);
        uploadInitialScene(renderer, activeScenePath, activeSceneLabel);
        watchdog.touch(WatchdogPhase::Idle);

        bool quitRequested = false;
        bool leftMouseDown = false;
        bool reloadScene = false;
        double lastTicks = currentTimeMillis();
        std::uint64_t frameIndex = 0;
        double fpsAccumulatedMs = 0.0;
        std::uint64_t fpsAccumulatedFrames = 0;
        double displayedFps = 0.0;
        double displayedFrameTimeMs = 0.0;
        double lastRasterTelemetryLogMs = 0.0;
        uint32_t lastLoggedRasterTelemetryFrameTag = GsViewer::InvalidTelemetryFrameTag;

        while (!quitRequested)
        {
            const double frameStart = currentTimeMillis();
            syncDrawableSize(context, renderer);

            SDL_Event event;
            int mouseX = 0;
            int mouseY = 0;
            bool hasMouseMotion = false;
            while (SDL_PollEvent(&event) != 0)
            {
                if (event.type == SDL_QUIT)
                {
                    quitRequested = true;
                }
                else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    syncDrawableSize(context, renderer);
                }
                else if (event.type == SDL_KEYDOWN)
                {
                    switch (event.key.keysym.sym)
                    {
                    case SDLK_r:
                        reloadScene = true;
                        break;
                    default:
                        break;
                    }
                }
                else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
                {
                    leftMouseDown = true;
                }
                else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
                {
                    leftMouseDown = false;
                }
                else if (event.type == SDL_MOUSEMOTION)
                {
                    mouseX = event.motion.x;
                    mouseY = event.motion.y;
                    hasMouseMotion = true;
                }
                else if (event.type == SDL_DROPFILE)
                {
                    const char *droppedFile = event.drop.file;
                    if (droppedFile != nullptr)
                    {
                        const std::string droppedPath = droppedFile;
                        SDL_free(event.drop.file);
                        if (hasPlyExtension(droppedPath))
                        {
                            try
                            {
                                watchdog.touch(WatchdogPhase::LoadingScene);
                                uploadSceneFromPath(renderer, droppedPath, activeSceneLabel);
                                activeScenePath = droppedPath;
                                watchdog.touch(WatchdogPhase::Idle);
                            }
                            catch (const std::exception &sceneError)
                            {
                                watchdog.touch(WatchdogPhase::Idle);
                                logInfo("drag-drop load failed: " + std::string(sceneError.what()));
                            }
                        }
                        else
                        {
                            logInfo("ignored dropped file (not .ply): " + droppedPath);
                        }
                    }
                }
            }

            int cameraMoveMask = 0;
            const Uint8 *keyboardState = SDL_GetKeyboardState(nullptr);
            if (keyboardState[SDL_SCANCODE_W] != 0)
            {
                cameraMoveMask |= 1;
            }
            if (keyboardState[SDL_SCANCODE_S] != 0)
            {
                cameraMoveMask |= 2;
            }
            if (keyboardState[SDL_SCANCODE_A] != 0)
            {
                cameraMoveMask |= 4;
            }
            if (keyboardState[SDL_SCANCODE_D] != 0)
            {
                cameraMoveMask |= 8;
            }

            if (reloadScene)
            {
                watchdog.touch(WatchdogPhase::LoadingScene);
                if (activeScenePath.has_value())
                {
                    try
                    {
                        uploadSceneFromPath(renderer, activeScenePath.value(), activeSceneLabel);
                    }
                    catch (const std::exception &sceneError)
                    {
                        logInfo("reload failed: " + std::string(sceneError.what()));
                    }
                }
                else
                {
                    uploadDebugScene(renderer, activeSceneLabel);
                }
                reloadScene = false;
                watchdog.touch(WatchdogPhase::Idle);
            }

            renderer->setMouseLeftClick(leftMouseDown ? 1 : 0);
            if (hasMouseMotion)
            {
                renderer->setMouseMoveX(mouseX);
                renderer->setMouseMoveY(mouseY);
            }
            renderer->setCameraMove(cameraMoveMask);
            renderer->setDurationTicks(frameStart - lastTicks);
            watchdog.touch(WatchdogPhase::Rendering);
            renderer->render();
            watchdog.touch(WatchdogPhase::Idle);
            ++frameIndex;

            const bool autoCaptureTriggered = diagnosticsCapture.autoCaptureEnabled && !diagnosticsCapture.autoCaptureCompleted && frameIndex >= diagnosticsCapture.triggerFrame;
            if (autoCaptureTriggered)
            {
                diagnosticsCapture.captureSequence += 1u;
                const std::string diagnosticsCapturePath = GaussianSplattingShared::buildDiagnosticsCapturePath(diagnosticsCapture.captureDirectory, "test05", frameIndex, diagnosticsCapture.captureSequence);
                GsViewer::BudgetTelemetry budgetTelemetry = {};
                const bool budgetTelemetryAvailable = renderer->getLatestBudgetTelemetry(budgetTelemetry);
                const Camera currentCamera = renderer->getCurrentCamera();
                const bool diagnosticsWriteSucceeded = writeDiagnosticsCapture(diagnosticsCapturePath, frameIndex, currentCamera, budgetTelemetryAvailable, budgetTelemetry);
                diagnosticsCapture.autoCaptureCompleted = true;
                if (diagnosticsWriteSucceeded)
                {
                    logInfo("captured diagnostics to " + diagnosticsCapturePath + " at frame=" + std::to_string(frameIndex));
                }
                else
                {
                    logInfo("diagnostics capture failed for " + diagnosticsCapturePath + " at frame=" + std::to_string(frameIndex));
                }
                if (diagnosticsCapture.exitAfterCapture)
                {
                    quitRequested = true;
                }
            }

            if ((frameIndex % 240u) == 0u)
            {
                logInfo("heartbeat frame=" + std::to_string(frameIndex) + " scene=" + activeSceneLabel + " size=" + std::to_string(context.drawableWidth) + "x" + std::to_string(context.drawableHeight));
            }

            const double frameTime = currentTimeMillis() - frameStart;
            if (frameTime < kTargetFrameTimeMs)
            {
                SDL_Delay(static_cast<Uint32>(kTargetFrameTimeMs - frameTime));
            }

            const double presentedFrameTimeMs = currentTimeMillis() - frameStart;
            fpsAccumulatedMs += presentedFrameTimeMs;
            fpsAccumulatedFrames += 1u;

            GsViewer::BudgetTelemetry telemetry = {};
            if (renderer->getLatestBudgetTelemetry(telemetry))
            {
                const double nowMs = currentTimeMillis();
                if (telemetry.frameTag != lastLoggedRasterTelemetryFrameTag && nowMs - lastRasterTelemetryLogMs >= 1000.0)
                {
                    const double projectedAverageEntries = telemetry.projectedSplatCount > 0u ? double(telemetry.projectedEntryCount) / double(telemetry.projectedSplatCount) : 0.0;

                    std::ostringstream telemetryBuilder;
                    telemetryBuilder << std::fixed << std::setprecision(2);
                    telemetryBuilder << "tile-raster telemetry"
                                     << " projected_splats=" << telemetry.projectedSplatCount << " projected_entries=" << telemetry.projectedEntryCount << " projected_avg_entries=" << projectedAverageEntries << " non_empty_tiles=" << telemetry.nonEmptyTileCount << " light_tiles=" << telemetry.lightTileCount << " heavy_tiles=" << telemetry.heavyTileCount << " heavy_segments=" << telemetry.heavySegmentCount << " max_projected_tiles=" << telemetry.maxProjectedTileCount
                                     << " max_tile_range=" << telemetry.maxTileRangeLength << " max_heavy_segments_per_tile=" << telemetry.maxHeavyTileSegmentCount;
                    logInfo(telemetryBuilder.str());
                    lastLoggedRasterTelemetryFrameTag = telemetry.frameTag;
                    lastRasterTelemetryLogMs = nowMs;
                }
            }

            if (fpsAccumulatedMs >= 250.0)
            {
                displayedFps = (double)fpsAccumulatedFrames * 1000.0 / fpsAccumulatedMs;
                displayedFrameTimeMs = fpsAccumulatedMs / (double)fpsAccumulatedFrames;
                updateWindowTitle(context.window, activeSceneLabel, displayedFps, displayedFrameTimeMs);
                fpsAccumulatedMs = 0.0;
                fpsAccumulatedFrames = 0u;
            }

            lastTicks = frameStart;
        }

        watchdog.touch(WatchdogPhase::Shutdown);
        watchdog.stop();
        destroyAppContext(context);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[3dgs-viewer] fatal: " << error.what() << std::endl;
        watchdog.stop();
        destroyAppContext(context);
        return 1;
    }
}
