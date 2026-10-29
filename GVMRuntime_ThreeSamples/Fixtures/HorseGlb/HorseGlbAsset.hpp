#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one base Horse mesh vertex decoded from the frozen Three.js r185 GLB. */
    struct HorseGlbVertex final
    {
        glm::vec4 position = glm::vec4(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
    };

    /** Owns the validated Horse mesh, relative morph targets, and animation clip. */
    struct HorseGlbAsset final
    {
        eastl::vector<HorseGlbVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<glm::vec4> morphPositions;
        eastl::vector<float> animationTimes;
        eastl::vector<float> animationWeights;
        eastl::string sha256;
        uint32_t morphTargetCount = 0u;
    };

    /** Loads and validates the exact Horse.glb layout locked for Three.js r185 samples. */
    [[nodiscard]] HorseGlbAsset loadHorseGlbAsset(
        const std::filesystem::path &path);

    /** Evaluates the repeating linear Horse morph clip at one time in seconds. */
    void sampleHorseMorphWeights(
        const HorseGlbAsset &asset,
        float timeSeconds,
        eastl::vector<float> &weights);
} // namespace GVM::ThreeSamples
