#include "ThreeSampleHostOptions.hpp"

#include <EASTL/string.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace
{
    /// Parses a non-negative 32-bit integer used by deterministic frame selection.
    uint32_t parseUint32(const char *optionName, const char *value)
    {
        if (value == nullptr || value[0] == '\0' || value[0] == '-')
        {
            const eastl::string message = eastl::string(optionName) +
                " requires a non-negative 32-bit integer.";
            throw std::invalid_argument(message.c_str());
        }

        errno = 0;
        char *end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (errno != 0 || end == value || *end != '\0' ||
            parsed > std::numeric_limits<uint32_t>::max())
        {
            const eastl::string message = eastl::string(optionName) +
                " requires a non-negative 32-bit integer.";
            throw std::invalid_argument(message.c_str());
        }
        return static_cast<uint32_t>(parsed);
    }

    /// Parses a positive 32-bit integer used by deterministic host settings.
    uint32_t parsePositiveUint32(const char *optionName, const char *value)
    {
        if (value == nullptr || value[0] == '\0')
        {
            const eastl::string message = eastl::string(optionName) + " requires a value.";
            throw std::invalid_argument(message.c_str());
        }

        errno = 0;
        char *end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
            parsed > std::numeric_limits<uint32_t>::max())
        {
            const eastl::string message =
                eastl::string(optionName) + " requires a positive 32-bit integer.";
            throw std::invalid_argument(message.c_str());
        }
        return static_cast<uint32_t>(parsed);
    }

    /// Returns the value following one command-line option and advances the parser index.
    const char *consumeOptionValue(
        const char *optionName,
        int argumentCount,
        const char *const *arguments,
        int &argumentIndex)
    {
        if (argumentIndex + 1 >= argumentCount)
        {
            const eastl::string message = eastl::string(optionName) + " requires a value.";
            throw std::invalid_argument(message.c_str());
        }
        ++argumentIndex;
        return arguments[argumentIndex];
    }

    /// Converts the public backend spelling into the corresponding GVMRHI enum.
    GVM::RHI::GraphicsBackend parseBackend(const char *value)
    {
        if (value != nullptr && eastl::string(value) == "metal")
        {
            return GVM::RHI::GraphicsBackend::Metal;
        }
        if (value != nullptr && eastl::string(value) == "vulkan")
        {
            return GVM::RHI::GraphicsBackend::Vulkan;
        }
        throw std::invalid_argument("--backend must be either 'metal' or 'vulkan'.");
    }

    /// Parses the required UGLC pipeline identity compiled into one generated host.
    eastl::string parsePipeline(const char *value)
    {
        if (value != nullptr && (eastl::string(value) == "legacy" ||
                                 eastl::string(value) == "experimental"))
        {
            return value;
        }
        throw std::invalid_argument("--pipeline must be either 'legacy' or 'experimental'.");
    }

    /// Parses one required non-empty string option.
    eastl::string parseNonEmptyString(const char *optionName, const char *value)
    {
        if (value == nullptr || value[0] == '\0')
        {
            const eastl::string message = eastl::string(optionName) + " requires a non-empty value.";
            throw std::invalid_argument(message.c_str());
        }
        return value;
    }
} // namespace

namespace GVM::ThreeSamples
{
    ThreeSampleHostOptions parseThreeSampleHostOptions(
        int argumentCount,
        const char *const *arguments)
    {
        ThreeSampleHostOptions options;
        bool targetFrameSpecified = false;
        bool frameCountSpecified = false;
        bool randomSeedSpecified = false;
        for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex)
        {
            const eastl::string argument = arguments[argumentIndex] != nullptr ? arguments[argumentIndex] : "";
            if (argument == "--help" || argument == "-h")
            {
                options.showHelp = true;
                continue;
            }
            if (argument == "--case-id")
            {
                options.caseId = parseNonEmptyString(
                    "--case-id",
                    consumeOptionValue("--case-id", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--scenario-id")
            {
                options.scenarioId = parseNonEmptyString(
                    "--scenario-id",
                    consumeOptionValue("--scenario-id", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--backend")
            {
                options.backend = parseBackend(consumeOptionValue(
                    "--backend", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--pipeline")
            {
                options.pipeline = parsePipeline(consumeOptionValue(
                    "--pipeline", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--width")
            {
                options.width = parsePositiveUint32(
                    "--width",
                    consumeOptionValue("--width", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--height")
            {
                options.height = parsePositiveUint32(
                    "--height",
                    consumeOptionValue("--height", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--frame")
            {
                if (frameCountSpecified)
                {
                    throw std::invalid_argument("--frame and --frame-count are mutually exclusive.");
                }
                options.targetFrame = parseUint32(
                    "--frame",
                    consumeOptionValue("--frame", argumentCount, arguments, argumentIndex));
                if (options.targetFrame == std::numeric_limits<uint32_t>::max())
                {
                    throw std::invalid_argument("--frame is too large to execute inclusively.");
                }
                options.frameCount = options.targetFrame + 1u;
                targetFrameSpecified = true;
                continue;
            }
            if (argument == "--frame-count")
            {
                if (targetFrameSpecified)
                {
                    throw std::invalid_argument("--frame and --frame-count are mutually exclusive.");
                }
                options.frameCount = parsePositiveUint32(
                    "--frame-count",
                    consumeOptionValue("--frame-count", argumentCount, arguments, argumentIndex));
                options.targetFrame = options.frameCount - 1u;
                frameCountSpecified = true;
                continue;
            }
            if (argument == "--random-seed")
            {
                options.randomSeed = parseUint32(
                    "--random-seed",
                    consumeOptionValue("--random-seed", argumentCount, arguments, argumentIndex));
                randomSeedSpecified = true;
                continue;
            }
            if (argument == "--asset-root")
            {
                options.assetRoot = parseNonEmptyString(
                    "--asset-root",
                    consumeOptionValue("--asset-root", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--capture-rgba")
            {
                options.captureRgbaPath = parseNonEmptyString(
                    "--capture-rgba",
                    consumeOptionValue("--capture-rgba", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--capture-metadata")
            {
                options.captureMetadataPath = parseNonEmptyString(
                    "--capture-metadata",
                    consumeOptionValue("--capture-metadata", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--scene-snapshot")
            {
                options.sceneSnapshotPath = parseNonEmptyString(
                    "--scene-snapshot",
                    consumeOptionValue("--scene-snapshot", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--semantic-snapshot")
            {
                options.semanticSnapshotPath = parseNonEmptyString(
                    "--semantic-snapshot",
                    consumeOptionValue(
                        "--semantic-snapshot", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--input-replay")
            {
                options.inputReplayPath = parseNonEmptyString(
                    "--input-replay",
                    consumeOptionValue("--input-replay", argumentCount, arguments, argumentIndex));
                continue;
            }
            if (argument == "--canonical-state")
            {
                options.canonicalStatePath = parseNonEmptyString(
                    "--canonical-state",
                    consumeOptionValue("--canonical-state", argumentCount, arguments, argumentIndex));
                continue;
            }
            const eastl::string message = eastl::string("Unknown command-line option: ") + argument;
            throw std::invalid_argument(message.c_str());
        }

        if (!options.showHelp && options.backend == GVM::RHI::GraphicsBackend::Undefined)
        {
            throw std::invalid_argument("--backend is required.");
        }
        if (!options.showHelp && options.pipeline.empty())
        {
            throw std::invalid_argument("--pipeline is required.");
        }
        if (!options.showHelp && options.caseId.empty())
        {
            throw std::invalid_argument("--case-id is required.");
        }
        if (!options.showHelp && options.scenarioId.empty())
        {
            throw std::invalid_argument("--scenario-id is required.");
        }
        if (!options.showHelp && !randomSeedSpecified)
        {
            throw std::invalid_argument("--random-seed is required.");
        }
        return options;
    }

    const char *threeSampleBackendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal: return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan: return "vulkan";
        default: return "undefined";
        }
    }

    void printThreeSampleHostUsage(const char *executableName)
    {
        std::fprintf(
            stdout,
            "Usage: %s --case-id <id> --scenario-id <id> --pipeline <legacy|experimental> "
            "--backend <metal|vulkan> "
            "--random-seed <uint32> [--width <pixels>] [--height <pixels>] "
            "[--frame <index>|--frame-count <count>] "
            "[--asset-root <path>] [--capture-rgba <path>] [--capture-metadata <path>] "
            "[--scene-snapshot <path>] [--semantic-snapshot <path>] "
            "[--input-replay <path>] [--canonical-state <path>]\n",
            executableName != nullptr ? executableName : "gvm-three-sample");
    }
} // namespace GVM::ThreeSamples
