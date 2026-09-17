#pragma once

#include "RgbaImageData.hpp"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one skinned Michelle vertex decoded from the pinned r185 GLB. */
    struct MichelleGlbVertex final
    {
        glm::vec4 position = glm::vec4(0.0f);
        glm::vec4 normal = glm::vec4(0.0f);
        glm::vec4 tangent = glm::vec4(0.0f);
        glm::vec4 uv = glm::vec4(0.0f);
        glm::uvec4 joints = glm::uvec4(0u);
        glm::vec4 weights = glm::vec4(0.0f);
    };

    /** Stores one node's immutable rest transform and parent relation. */
    struct MichelleGlbNode final
    {
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        int32_t parent = -1;
    };

    /** Identifies the transform component driven by one animation channel. */
    enum class MichelleAnimationPath : uint8_t
    {
        Translation,
        Rotation,
        Scale,
    };

    /** Stores one linear SambaDance transform channel on the shared time grid. */
    struct MichelleAnimationChannel final
    {
        uint32_t nodeIndex = 0u;
        MichelleAnimationPath path = MichelleAnimationPath::Translation;
        eastl::vector<glm::vec4> values;
    };

    /** Owns Michelle geometry, skinning data, animation, and embedded textures. */
    struct MichelleGlbAsset final
    {
        eastl::vector<MichelleGlbVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<MichelleGlbNode> nodes;
        eastl::vector<uint32_t> jointNodeIndices;
        eastl::vector<glm::mat4> inverseBindMatrices;
        eastl::vector<float> animationTimes;
        eastl::vector<MichelleAnimationChannel> animationChannels;
        eastl::vector<RgbaImageData> textures;
        eastl::string sha256;
        uint32_t meshNodeIndex = 0u;
        float animationDuration = 0.0f;
    };

    /** Loads and validates the exact Michelle.glb layout locked for Three.js r185. */
    [[nodiscard]] MichelleGlbAsset loadMichelleGlbAsset(
        const std::filesystem::path &path);

    /** Evaluates SambaDance and produces the 65 object-local skin matrices. */
    void sampleMichelleSkinPalette(
        const MichelleGlbAsset &asset,
        float timeSeconds,
        eastl::vector<glm::mat4> &palette);

    /** Evaluates SambaDance and emits mesh-local positions and normals for one frame. */
    void sampleMichelleSkinnedVertices(
        const MichelleGlbAsset &asset,
        float timeSeconds,
        eastl::vector<glm::vec4> &positions,
        eastl::vector<glm::vec4> &normals);
} // namespace GVM::ThreeSamples
