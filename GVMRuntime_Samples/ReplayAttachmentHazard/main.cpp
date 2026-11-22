#include "ReplayAttachmentHazardApp.hpp"
#include "SampleWindowManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace
{
    static constexpr int kConsumerModeReadOnly = 0;
    static constexpr int kConsumerModeTerrainOnly = 1;
    static constexpr int kConsumerModeTerrainGrass = 2;

    /// Consumes a `--flag=value` or `--flag value` argument when `argv[index]` matches `optionName`.
    bool tryConsumeOptionValue(
        int argc,
        char **argv,
        int &index,
        const char *optionName,
        const char *&outValue)
    {
        const size_t optionLength = std::strlen(optionName);
        if (std::strncmp(argv[index], optionName, optionLength) != 0)
        {
            return false;
        }
        if (argv[index][optionLength] == '=')
        {
            outValue = argv[index] + optionLength + 1u;
            return true;
        }
        if (argv[index][optionLength] == '\0')
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "[replay-hazard] missing value for %s\n", optionName);
                std::exit(2);
            }
            outValue = argv[++index];
            return true;
        }
        return false;
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
    void applyMode(GVM::Samples::ReplayAttachmentHazardSampleConfig &config, const char *mode)
    {
        if (std::strcmp(mode, "read-only") == 0)
        {
            config.consumerMode = kConsumerModeReadOnly;
            return;
        }
        if (std::strcmp(mode, "terrain-only") == 0)
        {
            config.consumerMode = kConsumerModeTerrainOnly;
            return;
        }
        if (std::strcmp(mode, "terrain-grass") == 0)
        {
            config.consumerMode = kConsumerModeTerrainGrass;
            return;
        }
        if (std::strcmp(mode, "omit-replay-in-grass") == 0)
        {
            config.consumerMode = kConsumerModeTerrainGrass;
            config.grassEnabled = true;
            config.grassWritesReplay = false;
            return;
        }
        if (std::strcmp(mode, "write-replay-in-grass") == 0)
        {
            config.consumerMode = kConsumerModeTerrainGrass;
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
    GVM::Samples::ReplayAttachmentHazardSampleConfig parseCommandLine(int argc, char **argv)
    {
        GVM::Samples::ReplayAttachmentHazardSampleConfig config;
        for (int index = 1; index < argc; ++index)
        {
            const char *arg = argv[index];
            const char *value = nullptr;

            if (tryConsumeOptionValue(argc, argv, index, "--backend", value))
            {
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
            else if (tryConsumeOptionValue(argc, argv, index, "--width", value))
            {
                config.width = parseUInt32(value, "--width");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--height", value))
            {
                config.height = parseUInt32(value, "--height");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--frames", value))
            {
                config.frames = parseUInt32(value, "--frames");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--grass-instances", value))
            {
                config.grassInstanceCount = parseUInt32(value, "--grass-instances");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--grass-vertices", value))
            {
                config.grassVertexCount = parseUInt32(value, "--grass-vertices");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--mode", value))
            {
                applyMode(config, value);
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--grass", value))
            {
                config.grassEnabled = parseOnOff(value, "--grass");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--pixel-local", value))
            {
                config.pixelLocalEnabled = parseOnOff(value, "--pixel-local");
            }
            else if (tryConsumeOptionValue(argc, argv, index, "--replay-in-grass", value))
            {
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
            else if (tryConsumeOptionValue(argc, argv, index, "--grass-draw", value))
            {
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
            else if (tryConsumeOptionValue(argc, argv, index, "--grass-bary", value))
            {
                config.grassBaryEnabled = parseOnOff(value, "--grass-bary");
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
} // namespace

/// Runs the desktop ReplayAttachmentHazard sample through the shared SampleWindowManager host.
int main(int argc, char **argv)
{
    try
    {
        const GVM::Samples::ReplayAttachmentHazardSampleConfig config = parseCommandLine(argc, argv);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = config.backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM Replay Attachment Hazard (Metal)"
            : "GVM Replay Attachment Hazard (Vulkan)";
        windowInfo.width = config.width;
        windowInfo.height = config.height;
        windowInfo.highDpi = true;
        windowInfo.preferredGraphicsBackend = config.backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::ReplayAttachmentHazardApp>(window, config);

        uint32_t renderedFrames = 0u;
        while (!app->shouldQuit())
        {
            app->processPendingEvents();
            app->renderFrame();
            ++renderedFrames;
            if (config.frames > 0u && renderedFrames >= config.frames)
            {
                break;
            }
        }

        app->printSummary();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[replay-hazard] failed: %s\n", error.what());
        return 1;
    }
}
