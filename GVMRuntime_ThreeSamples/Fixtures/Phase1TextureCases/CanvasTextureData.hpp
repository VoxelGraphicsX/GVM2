#pragma once

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Describes the semantic pointer replay reconstructed by the DSL CanvasTexture fragment path. */
    struct CanvasTextureReplayResult
    {
        eastl::array<glm::vec4, 4u> segments = {};
        eastl::string sha256;
        eastl::string target;
        uint32_t eventCount = 0u;
        uint32_t captureFrame = 0u;
        uint32_t activeSegmentCount = 0u;
        bool painted = false;
    };

    /** Creates an empty replay whose DSL fragment reconstruction produces the initial white canvas. */
    [[nodiscard]] CanvasTextureReplayResult createInitialCanvasTextureReplay();

    /** Validates and converts the locked pointer replay into four semantic line segments. */
    [[nodiscard]] CanvasTextureReplayResult parseCanonicalCanvasTextureReplay(const std::filesystem::path &inputReplayPath);

} // namespace GVM::ThreeSamples
