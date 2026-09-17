#pragma once

#include "DebugSceneFactory.hpp"
#include "PlyLoader.hpp"
#include "SceneSafety.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

namespace GaussianSplattingShared
{
    using LogFunction = void (*)(const std::string &message);

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
     * Parses one boolean environment flag using the sample convention for truthy values.
     *
     * Valid true values are 1, true, yes, and on after case normalization; any missing or other value is false.
     */
    inline bool parseEnvFlag(const char *name)
    {
        const char *value = std::getenv(name);
        if (value == nullptr)
        {
            return false;
        }

        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
    }

    /**
     * Parses one optional unsigned integer from the process environment.
     *
     * Invalid non-empty values are ignored and reported through the provided sample log sink.
     */
    inline std::optional<std::uint64_t> parseEnvUint64(const char *name, LogFunction logFunction = nullptr)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return std::nullopt;
        }

        std::istringstream valueStream(value);
        std::uint64_t parsedValue = 0u;
        char trailingCharacter = '\0';
        if (!(valueStream >> parsedValue) || (valueStream >> trailingCharacter))
        {
            logMessage(logFunction, "ignored invalid integer env " + std::string(name) + "=" + value);
            return std::nullopt;
        }

        return parsedValue;
    }

    /**
     * Parses one optional double-precision value from the process environment.
     *
     * Invalid non-empty values are ignored and reported through the provided sample log sink.
     */
    inline std::optional<double> parseEnvDouble(const char *name, LogFunction logFunction = nullptr)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return std::nullopt;
        }

        std::istringstream valueStream(value);
        double parsedValue = 0.0;
        char trailingCharacter = '\0';
        if (!(valueStream >> parsedValue) || (valueStream >> trailingCharacter))
        {
            logMessage(logFunction, "ignored invalid float env " + std::string(name) + "=" + value);
            return std::nullopt;
        }

        return parsedValue;
    }

    /**
     * Parses one optional single-precision value from the process environment.
     *
     * Invalid non-empty values are ignored and reported through the provided sample log sink.
     */
    inline std::optional<float> parseEnvFloat(const char *name, LogFunction logFunction = nullptr)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return std::nullopt;
        }

        std::istringstream valueStream(value);
        float parsedValue = 0.0f;
        char trailingCharacter = '\0';
        if (!(valueStream >> parsedValue) || (valueStream >> trailingCharacter))
        {
            logMessage(logFunction, "ignored invalid float env " + std::string(name) + "=" + value);
            return std::nullopt;
        }

        return parsedValue;
    }

    /**
     * Returns a capture directory from an environment variable or /tmp when the variable is missing.
     *
     * The returned path is not created by this function; call ensureCaptureDirectoryExists when creation is required.
     */
    inline std::string getCaptureDirectory(const char *environmentName)
    {
        const char *directory = std::getenv(environmentName);
        if (directory == nullptr || directory[0] == '\0')
        {
            return "/tmp";
        }
        return std::string(directory);
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
        std::string normalizedDirectory = captureDirectory.empty() ? std::string("/tmp") : captureDirectory;
        while (normalizedDirectory.size() > 1u && normalizedDirectory.back() == '/')
        {
            normalizedDirectory.pop_back();
        }

        const std::string fileName = samplePrefix + "_" + stageLabel + "_frame_" + std::to_string(frameIndex) + "_capture_" + std::to_string(captureSequence) + extension;
        if (normalizedDirectory == "/")
        {
            return normalizedDirectory + fileName;
        }

        return normalizedDirectory + "/" + fileName;
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
     * Parses the first sample scene path from command-line arguments.
     *
     * The parser accepts --scene <path> or the first non-option argument and leaves all other options to the caller.
     */
    inline std::optional<std::string> parseScenePath(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (std::strcmp(argv[index], "--scene") == 0 && index + 1 < argc)
            {
                return std::string(argv[index + 1]);
            }
            if (argv[index][0] != '-')
            {
                return std::string(argv[index]);
            }
        }
        return std::nullopt;
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
