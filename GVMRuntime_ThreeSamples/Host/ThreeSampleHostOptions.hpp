#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/string.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /// Defines the repository-wide random seed passed explicitly to GVM and Three reference captures.
    static constexpr uint32_t DefaultThreeRandomSeed = 0x12345678u;

    /// Stores deterministic command-line settings shared by every Three.js compatibility sample host.
    struct ThreeSampleHostOptions
    {
        eastl::string caseId;
        eastl::string scenarioId;
        eastl::string pipeline;
        eastl::string assetRoot;
        eastl::string captureRgbaPath;
        eastl::string captureMetadataPath;
        eastl::string sceneSnapshotPath;
        eastl::string semanticSnapshotPath;
        eastl::string inputReplayPath;
        eastl::string canonicalStatePath;
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Undefined;
        uint32_t width = 800;
        uint32_t height = 500;
        uint32_t targetFrame = 0;
        uint32_t frameCount = 1;
        uint32_t randomSeed = DefaultThreeRandomSeed;
        bool showHelp = false;
    };

    /// Parses the explicit runtime configuration accepted by a Three.js compatibility sample host.
    ThreeSampleHostOptions parseThreeSampleHostOptions(int argumentCount, const char *const *arguments);

    /// Returns the stable lowercase command-line name for a graphics backend.
    const char *threeSampleBackendName(GVM::RHI::GraphicsBackend backend);

    /// Prints the command-line contract shared by all generated Three.js compatibility sample hosts.
    void printThreeSampleHostUsage(const char *executableName);
} // namespace GVM::ThreeSamples
