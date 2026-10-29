#include "SampleWindowManager.hpp"
#include "UGLBin/generate_result.hpp"

#include <GVMRHI/GVMRHI.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace
{
    /// Holds the command-line configuration for one replay attachment hazard sample run.
    struct ReplayAttachmentHazardSampleConfig
    {
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t frames = 600;
        uint32_t grassInstanceCount = 178791;
        uint32_t grassVertexCount = 39;
        int consumerMode = int(ReplayAttachmentHazard::ConsumerModeTerrainGrass);
        bool grassEnabled = true;
        bool grassWritesReplay = false;
        bool pixelLocalEnabled = true;
        bool grassDrawIndirect = true;
        bool grassBaryEnabled = true;
    };

    /// Returns a stable lowercase display name for one graphics backend.
    const char *backendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal: return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan: return "vulkan";
        default: return "undefined";
        }
    }

    /// Parses a non-negative decimal uint32 command-line value.
    uint32_t parseUInt32(const char *value, const char *optionName)
    {
        if (value == nullptr || value[0] == '\0')
        {
            std::fprintf(stderr, "[replay-hazard] missing value for %s\n", optionName);
            std::exit(2);
        }
        char *end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value || *end != '\0')
        {
            std::fprintf(stderr, "[replay-hazard] invalid integer for %s: %s\n", optionName, value);
            std::exit(2);
        }
        return static_cast<uint32_t>(parsed);
    }

    /// Parses an on/off command-line value.
    bool parseOnOff(const char *value, const char *optionName)
    {
        if (std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0)
        {
            return true;
        }
        if (std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "0") == 0)
        {
            return false;
        }
        std::fprintf(stderr, "[replay-hazard] %s expects on/off, got: %s\n", optionName, value);
        std::exit(2);
    }

    /// Applies one named hazard mode to the sample configuration.
    void applyMode(ReplayAttachmentHazardSampleConfig &config, const char *mode)
    {
        if (std::strcmp(mode, "read-only") == 0)
        {
            config.consumerMode = int(ReplayAttachmentHazard::ConsumerModeReadOnly);
            return;
        }
        if (std::strcmp(mode, "terrain-only") == 0)
        {
            config.consumerMode = int(ReplayAttachmentHazard::ConsumerModeTerrainOnly);
            return;
        }
        if (std::strcmp(mode, "terrain-grass") == 0)
        {
            config.consumerMode = int(ReplayAttachmentHazard::ConsumerModeTerrainGrass);
            return;
        }
        if (std::strcmp(mode, "omit-replay-in-grass") == 0)
        {
            config.consumerMode = int(ReplayAttachmentHazard::ConsumerModeTerrainGrass);
            config.grassEnabled = true;
            config.grassWritesReplay = false;
            return;
        }
        if (std::strcmp(mode, "write-replay-in-grass") == 0)
        {
            config.consumerMode = int(ReplayAttachmentHazard::ConsumerModeTerrainGrass);
            config.grassEnabled = true;
            config.grassWritesReplay = true;
            return;
        }
        if (std::strcmp(mode, "no-grass") == 0)
        {
            config.grassEnabled = false;
            return;
        }

        std::fprintf(stderr, "[replay-hazard] unknown mode: %s\n", mode);
        std::exit(2);
    }

    /// Parses the replay hazard sample command line.
    ReplayAttachmentHazardSampleConfig parseCommandLine(int argc, char **argv)
    {
        ReplayAttachmentHazardSampleConfig config;
        for (int i = 1; i < argc; ++i)
        {
            const char *arg = argv[i];
            const auto requireValue = [&](const char *optionName) -> const char * {
                if (i + 1 >= argc)
                {
                    std::fprintf(stderr, "[replay-hazard] missing value for %s\n", optionName);
                    std::exit(2);
                }
                return argv[++i];
            };

            if (std::strcmp(arg, "--backend") == 0)
            {
                const char *value = requireValue(arg);
                if (std::strcmp(value, "metal") == 0)
                {
                    config.backend = GVM::RHI::GraphicsBackend::Metal;
                }
                else if (std::strcmp(value, "vulkan") == 0)
                {
                    config.backend = GVM::RHI::GraphicsBackend::Vulkan;
                }
                else
                {
                    std::fprintf(stderr, "[replay-hazard] --backend expects metal/vulkan, got: %s\n", value);
                    std::exit(2);
                }
            }
            else if (std::strcmp(arg, "--width") == 0)
            {
                config.width = parseUInt32(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--height") == 0)
            {
                config.height = parseUInt32(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--frames") == 0)
            {
                config.frames = parseUInt32(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--grass-instances") == 0)
            {
                config.grassInstanceCount = parseUInt32(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--grass-vertices") == 0)
            {
                config.grassVertexCount = parseUInt32(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--mode") == 0)
            {
                applyMode(config, requireValue(arg));
            }
            else if (std::strcmp(arg, "--grass") == 0)
            {
                config.grassEnabled = parseOnOff(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--pixel-local") == 0)
            {
                config.pixelLocalEnabled = parseOnOff(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--replay-in-grass") == 0)
            {
                const char *value = requireValue(arg);
                if (std::strcmp(value, "write") == 0)
                {
                    config.grassWritesReplay = true;
                }
                else if (std::strcmp(value, "omit") == 0)
                {
                    config.grassWritesReplay = false;
                }
                else
                {
                    std::fprintf(stderr, "[replay-hazard] --replay-in-grass expects write/omit, got: %s\n", value);
                    std::exit(2);
                }
            }
            else if (std::strcmp(arg, "--grass-draw") == 0)
            {
                const char *value = requireValue(arg);
                if (std::strcmp(value, "indirect") == 0)
                {
                    config.grassDrawIndirect = true;
                }
                else if (std::strcmp(value, "direct") == 0)
                {
                    config.grassDrawIndirect = false;
                }
                else
                {
                    std::fprintf(stderr, "[replay-hazard] --grass-draw expects direct/indirect, got: %s\n", value);
                    std::exit(2);
                }
            }
            else if (std::strcmp(arg, "--grass-bary") == 0)
            {
                config.grassBaryEnabled = parseOnOff(requireValue(arg), arg);
            }
            else if (std::strcmp(arg, "--help") == 0)
            {
                std::printf(
                    "ReplayAttachmentHazard sample options:\n"
                    "  --backend vulkan|metal\n"
                    "  --width N --height N --frames N\n"
                    "  --mode read-only|terrain-only|terrain-grass|omit-replay-in-grass|write-replay-in-grass|no-grass\n"
                    "  --grass on|off\n"
                    "  --pixel-local on|off\n"
                    "  --replay-in-grass omit|write\n"
                    "  --grass-draw direct|indirect\n"
                    "  --grass-bary on|off\n"
                    "  --grass-vertices N\n"
                    "  --grass-instances N\n");
                std::exit(0);
            }
            else
            {
                std::fprintf(stderr, "[replay-hazard] unknown argument: %s\n", arg);
                std::exit(2);
            }
        }
        return config;
    }

    /// Returns milliseconds elapsed between two steady-clock time points.
    double elapsedMilliseconds(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const ReplayAttachmentHazardSampleConfig config = parseCommandLine(argc, argv);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = "GVM Replay Attachment Hazard";
        windowInfo.width = config.width;
        windowInfo.height = config.height;
        windowInfo.highDpi = true;
        windowInfo.preferredGraphicsBackend = config.backend;
        GVM::Samples::SampleWindowManager window(windowInfo);

        GVM::RHI::Instance instance = GVM::RHI::createInstance({
            .preferredBackend = config.backend,
            .diagnosticsOverlay = {.enabled = GVM::RHI::True},
        });
        GVM::RHI::Device device = instance->createDevice();
        GVM::RHI::Swapchain swapchain = instance->createSwapchain({
            .nativeSurface = window.getNativeSurface(),
        });

        uint32_t drawableWidth = 1u;
        uint32_t drawableHeight = 1u;
        window.getDrawableSize(drawableWidth, drawableHeight);

        auto renderer = ReplayAttachmentHazard::makeHazardRenderer();
        renderer->init(device, swapchain, drawableWidth, drawableHeight);
        renderer->setConsumerMode(config.consumerMode);
        renderer->setGrassEnabled(config.grassEnabled ? 1 : 0);
        renderer->setGrassWritesReplay(config.grassWritesReplay ? 1 : 0);
        renderer->setPixelLocalEnabled(config.pixelLocalEnabled ? 1 : 0);
        renderer->setGrassInstanceCount(static_cast<int>(config.grassInstanceCount));
        renderer->setGrassVertexCount(static_cast<int>(config.grassVertexCount));
        renderer->setGrassDrawIndirect(config.grassDrawIndirect ? 1 : 0);
        renderer->setGrassBaryEnabled(config.grassBaryEnabled ? 1 : 0);

        const char *activeBackend = backendName(instance->getBackend());
        std::printf(
            "[replay-hazard] active_backend=%s requested_backend=%s drawable=%ux%u frames=%u "
            "grass=%u replay_in_grass=%s pixel_local=%u consumer_mode=%d grass_instances=%u "
            "grass_vertices=%u grass_draw=%s grass_bary=%u\n",
            activeBackend,
            backendName(config.backend),
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

        double totalFrameMs = 0.0;
        double maxFrameMs = 0.0;
        uint32_t renderedFrames = 0u;

        for (uint32_t frame = 0; frame < config.frames; ++frame)
        {
            GVM::Samples::SampleWindowEvent event;
            while (window.pollEvent(event))
            {
                if (event.type == GVM::Samples::SampleWindowEvent::Type::Quit)
                {
                    frame = config.frames;
                    break;
                }
            }
            if (frame >= config.frames)
            {
                break;
            }

            const auto begin = std::chrono::steady_clock::now();
            renderer->render();
            const double frameMs = elapsedMilliseconds(begin, std::chrono::steady_clock::now());
            totalFrameMs += frameMs;
            maxFrameMs = frameMs > maxFrameMs ? frameMs : maxFrameMs;
            ++renderedFrames;

            if ((renderedFrames % 60u) == 0u)
            {
                std::printf("[replay-hazard] frame=%u cpu_frame_ms=%.3f\n", renderedFrames, frameMs);
            }
        }

        const double avgFrameMs = renderedFrames > 0u ? totalFrameMs / double(renderedFrames) : 0.0;
        const double avgFps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;
        std::printf(
            "[replay-hazard-result] active_backend=%s frames=%u avg_frame_ms=%.3f avg_fps=%.2f max_frame_ms=%.3f\n",
            activeBackend,
            renderedFrames,
            avgFrameMs,
            avgFps,
            maxFrameMs);

        renderer = nullptr;
        swapchain = nullptr;
        device = nullptr;
        GVM::RHI::destroyInstance(instance);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[replay-hazard] failed: %s\n", error.what());
        return 1;
    }
}
