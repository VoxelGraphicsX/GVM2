#pragma once

#include "DebugSceneFactory.hpp"
#include "PlyLoader.hpp"
#include "SceneSafety.hpp"

#include <GVMRHI/GVMRHI.hpp>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GaussianSplattingShared
{
    using LogFunction = void (*)(const std::string &message);

    /**
     * Holds the explicit command-line configuration shared by the 3DGS viewer samples.
     *
     * Use this reusable structure in both Test05 and Test06 host entry points so backend,
     * frame limit, scene selection, and diagnostics capture options follow one CLI contract.
     */
    struct GaussianViewerCliOptions
    {
#if defined(__APPLE__)
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Metal;
#else
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
#endif
        std::uint32_t maxFrames = 0u;
        std::optional<std::string> scenePath;
        bool capturePresent = false;
        bool exitAfterCapture = false;
        std::uint64_t captureFrame = 1u;
        std::string captureDirectory;
        std::optional<double> sortEntryFrustumDilation;
    };

    /**
     * Sends one diagnostic message to the caller-provided log sink when one is available.
     *
     * Use this helper from shared sample code that must preserve each sample's own logging prefix and backend.
     */
    inline void logMessage(LogFunction logFunction, const std::string &message)
    {
        if (logFunction != nullptr)
        {
            logFunction(message);
        }
    }

    /**
     * Returns a lowercase copy of an ASCII option value for case-insensitive CLI parsing.
     */
    inline std::string toLowerAscii(const std::string &input)
    {
        std::string result = input;
        for (char &character : result)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return result;
    }

    /**
     * Parses a boolean CLI token (`1`, `true`, `yes`, `on`, `0`, `false`, `no`, `off`).
     */
    inline bool parseCliBoolValue(const std::string &rawValue, const char *optionName)
    {
        const std::string normalized = toLowerAscii(rawValue);
        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
        {
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
        {
            return false;
        }
        throw std::runtime_error(std::string("Invalid boolean value for ") + optionName + ": " + rawValue);
    }

    /**
     * Parses an unsigned 64-bit integer CLI option value.
     */
    inline std::uint64_t parseCliUint64Value(const std::string &rawValue, const char *optionName)
    {
        std::istringstream valueStream(rawValue);
        std::uint64_t parsedValue = 0u;
        char trailingCharacter = '\0';
        if (rawValue.empty() || rawValue[0] == '-' || !(valueStream >> parsedValue) || (valueStream >> trailingCharacter))
        {
            throw std::runtime_error(std::string("Invalid unsigned integer for ") + optionName + ": " + rawValue);
        }
        return parsedValue;
    }

    /**
     * Parses a double-precision floating-point CLI option value.
     */
    inline double parseCliDoubleValue(const std::string &rawValue, const char *optionName)
    {
        std::istringstream valueStream(rawValue);
        double parsedValue = 0.0;
        char trailingCharacter = '\0';
        if (rawValue.empty() || !(valueStream >> parsedValue) || (valueStream >> trailingCharacter))
        {
            throw std::runtime_error(std::string("Invalid floating-point value for ") + optionName + ": " + rawValue);
        }
        return parsedValue;
    }

    /**
     * Resolves the platform temporary directory used when `--capture-dir` is not explicitly specified.
     */
    inline std::string defaultCaptureDirectory()
    {
        std::error_code errorCode;
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path(errorCode);
        if (errorCode || tempDir.empty())
        {
            return ".";
        }
        return tempDir.string();
    }

    /**
     * Splits a `--flag=value` or `--flag value` argument when `argv[index]` matches `optionName`.
     */
    inline bool tryConsumeCliValue(
        int argc,
        char **argv,
        int &index,
        const char *optionName,
        std::string &outValue)
    {
        const std::size_t nameLength = std::strlen(optionName);
        if (std::strncmp(argv[index], optionName, nameLength) != 0)
        {
            return false;
        }
        if (argv[index][nameLength] == '=')
        {
            outValue = argv[index] + nameLength + 1u;
            return true;
        }
        if (argv[index][nameLength] == '\0')
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error(std::string("Missing value for ") + optionName);
            }
            outValue = argv[++index];
            return true;
        }
        return false;
    }

    /**
     * Parses explicit command-line options for the Test05 and Test06 3DGS sample viewers.
     *
     * Supported options:
     *   --backend metal|vulkan
     *   --frames <count>
     *   --scene <ply-path> (or first positional argument)
     *   --capture-present[=on|off]
     *   --capture-exit-after[=on|off]
     *   --capture-frame <frame-index>
     *   --capture-dir <directory>
     *   --frustum-dilation <float>
     */
    inline GaussianViewerCliOptions parseGaussianViewerCliOptions(int argc, char **argv)
    {
        GaussianViewerCliOptions options;
        options.captureDirectory = defaultCaptureDirectory();

        for (int index = 1; index < argc; ++index)
        {
            const char *arg = argv[index];
            std::string value;

            if (tryConsumeCliValue(argc, argv, index, "--backend", value))
            {
                const std::string normalized = toLowerAscii(value);
                if (normalized == "metal")
                {
                    options.backend = GVM::RHI::GraphicsBackend::Metal;
                }
                else if (normalized == "vulkan")
                {
                    options.backend = GVM::RHI::GraphicsBackend::Vulkan;
                }
                else
                {
                    throw std::runtime_error("Unsupported --backend value (expected metal or vulkan): " + value);
                }
            }
            else if (tryConsumeCliValue(argc, argv, index, "--frames", value))
            {
                options.maxFrames = static_cast<std::uint32_t>(parseCliUint64Value(value, "--frames"));
            }
            else if (tryConsumeCliValue(argc, argv, index, "--scene", value))
            {
                options.scenePath = value;
            }
            else if (std::strcmp(arg, "--capture-present") == 0)
            {
                options.capturePresent = true;
            }
            else if (std::strncmp(arg, "--capture-present=", 18) == 0)
            {
                options.capturePresent = parseCliBoolValue(arg + 18, "--capture-present");
            }
            else if (std::strcmp(arg, "--capture-exit-after") == 0)
            {
                options.exitAfterCapture = true;
            }
            else if (std::strncmp(arg, "--capture-exit-after=", 21) == 0)
            {
                options.exitAfterCapture = parseCliBoolValue(arg + 21, "--capture-exit-after");
            }
            else if (tryConsumeCliValue(argc, argv, index, "--capture-frame", value))
            {
                options.captureFrame = parseCliUint64Value(value, "--capture-frame");
            }
            else if (tryConsumeCliValue(argc, argv, index, "--capture-dir", value))
            {
                options.captureDirectory = value;
            }
            else if (tryConsumeCliValue(argc, argv, index, "--frustum-dilation", value))
            {
                options.sortEntryFrustumDilation = parseCliDoubleValue(value, "--frustum-dilation");
            }
            else if (arg[0] != '-' && !options.scenePath.has_value())
            {
                options.scenePath = std::string(arg);
            }
            else
            {
                throw std::runtime_error(std::string("Unknown command-line argument: ") + arg);
            }
        }

        return options;
    }

    /**
     * Formats a CPU Gaussian scene summary using the stable text shape expected by sample diagnostics.
     *
     * Use this for user-facing scene upload logs; it does not mutate or validate the scene.
     */
    inline std::string formatSceneSummary(const CpuGaussianScene &scene)
    {
        return "splats=" + std::to_string(scene.splats.size()) + " shDegree=" + std::to_string(scene.shDegree) + " hasFocusHint=" + std::string(scene.hasFocusHint ? "true" : "false") + " focusCenter=(" + std::to_string(scene.focusCenter[0]) + ", " + std::to_string(scene.focusCenter[1]) + ", " + std::to_string(scene.focusCenter[2]) + ")" + " focusRadius=" + std::to_string(scene.focusRadius) + " hasSuggestedCamera=" + std::string(scene.hasSuggestedCamera ? "true" : "false") + " hasSuggestedSceneUp=" + std::string(scene.hasSuggestedSceneUp ? "true" : "false") + " boundsMin=(" +
               std::to_string(scene.boundsMin[0]) + ", " + std::to_string(scene.boundsMin[1]) + ", " + std::to_string(scene.boundsMin[2]) + ")" + " boundsMax=(" + std::to_string(scene.boundsMax[0]) + ", " + std::to_string(scene.boundsMax[1]) + ", " + std::to_string(scene.boundsMax[2]) + ")";
    }

    /**
     * Returns true when a path has a case-insensitive .ply extension.
     *
     * Use this for drag-and-drop filtering only; it does not prove that the target file is a valid PLY scene.
     */
    inline bool hasPlyExtension(const std::string &path)
    {
        if (path.size() < 4u)
        {
            return false;
        }

        const char dot = path[path.size() - 4u];
        const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(path[path.size() - 3u])));
        const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(path[path.size() - 2u])));
        const char y = static_cast<char>(std::tolower(static_cast<unsigned char>(path[path.size() - 1u])));
        return dot == '.' && p == 'p' && l == 'l' && y == 'y';
    }

    /**
     * Creates a capture directory if it is non-empty and logs filesystem errors without throwing.
     *
     * This is intended for optional diagnostics output; failure should not prevent normal rendering.
     */
    inline void ensureCaptureDirectoryExists(const std::string &captureDirectory, LogFunction logFunction = nullptr)
    {
        if (captureDirectory.empty())
        {
            return;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(captureDirectory, errorCode);
        if (errorCode)
        {
            logMessage(logFunction, "failed to create capture directory " + captureDirectory + ": " + errorCode.message());
        }
    }

    /**
     * Builds a stable sample capture path under a normalized directory.
     *
     * samplePrefix must be an English token such as test06; extension must include the leading dot.
     */
    inline std::string buildCapturePath(
        const std::string &captureDirectory,
        const std::string &samplePrefix,
        const std::string &stageLabel,
        std::uint64_t frameIndex,
        std::uint64_t captureSequence,
        const std::string &extension)
    {
        const std::filesystem::path baseDirectory = captureDirectory.empty()
            ? std::filesystem::path(defaultCaptureDirectory())
            : std::filesystem::path(captureDirectory);
        const std::string fileName = samplePrefix + "_" + stageLabel + "_frame_" + std::to_string(frameIndex) + "_capture_" + std::to_string(captureSequence) + extension;
        return (baseDirectory / fileName).string();
    }

    /**
     * Builds the PPM capture path for one rendered stage.
     *
     * Use this for outputTexture and swapchainTexture captures that keep the existing sample filename convention.
     */
    inline std::string buildStageCapturePath(
        const std::string &captureDirectory,
        const std::string &samplePrefix,
        const std::string &stageLabel,
        std::uint64_t frameIndex,
        std::uint64_t captureSequence)
    {
        return buildCapturePath(captureDirectory, samplePrefix, stageLabel, frameIndex, captureSequence, ".ppm");
    }

    /**
     * Builds the text diagnostics capture path for one sample frame.
     *
     * Use this for frame diagnostics that must be compared across refactors.
     */
    inline std::string buildDiagnosticsCapturePath(
        const std::string &captureDirectory,
        const std::string &samplePrefix,
        std::uint64_t frameIndex,
        std::uint64_t captureSequence)
    {
        return buildCapturePath(captureDirectory, samplePrefix, "diagnostics", frameIndex, captureSequence, ".txt");
    }

    /**
     * Uploads the shared default calibration scene through the provided renderer.
     *
     * Use this fallback when no external PLY path is provided or when the initial PLY load fails.
     */
    template <typename Renderer>
    inline void uploadDebugScene(Renderer renderer, std::string &activeSceneLabel, LogFunction logFunction)
    {
        logMessage(logFunction, "no external scene loaded, generating default calibration scene with exact-center blend probe");
        CpuGaussianScene scene = DebugSceneFactory::makeCalibrationScene();
        renderer->uploadScene(scene);
        activeSceneLabel = "debug:calibration";
        logMessage(logFunction, "default debug scene uploaded, " + formatSceneSummary(scene));
    }

    /**
     * Loads, sanitizes, uploads, and logs one external 3DGS PLY scene.
     *
     * The renderer must expose uploadScene(CpuGaussianScene), and exceptions from loading or upload are propagated.
     */
    template <typename Renderer>
    inline bool uploadSceneFromPath(Renderer renderer, const std::string &scenePath, std::string &activeSceneLabel, LogFunction logFunction)
    {
        logMessage(logFunction, "loading PLY scene: " + scenePath);
        SceneSafety::PreparedScene prepared = PlyLoader::loadPreparedForStableBaseline(scenePath);
        logMessage(logFunction, SceneSafety::formatSummary(prepared));
        renderer->uploadScene(prepared.scene);
        activeSceneLabel = scenePath;
        logMessage(logFunction, "scene uploaded successfully, " + formatSceneSummary(prepared.scene));
        return true;
    }

    /**
     * Uploads either an external PLY scene or the calibration fallback scene.
     *
     * Initial external load failures are logged and converted to the calibration fallback while clearing scenePath.
     */
    template <typename Renderer>
    inline void uploadInitialScene(Renderer renderer, std::optional<std::string> &scenePath, std::string &activeSceneLabel, LogFunction logFunction)
    {
        if (!scenePath.has_value())
        {
            uploadDebugScene(renderer, activeSceneLabel, logFunction);
            return;
        }

        try
        {
            uploadSceneFromPath(renderer, scenePath.value(), activeSceneLabel, logFunction);
        }
        catch (const std::exception &error)
        {
            logMessage(logFunction, "initial scene load failed, falling back to debug calibration scene: " + std::string(error.what()));
            scenePath.reset();
            uploadDebugScene(renderer, activeSceneLabel, logFunction);
        }
    }
} // namespace GaussianSplattingShared
