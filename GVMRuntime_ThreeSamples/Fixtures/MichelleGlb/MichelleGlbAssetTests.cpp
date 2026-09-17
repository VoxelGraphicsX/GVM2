#include "MichelleGlbAsset.hpp"

#include <EASTL/vector.h>

#include <glm/common.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

#ifndef GVM_THREE_MICHELLE_ASSET_ROOT
#error "GVM_THREE_MICHELLE_ASSET_ROOT must identify the locked r185 examples directory."
#endif

namespace
{
    /** Fails the decoder test with one concrete contract message. */
    void requireMichelle(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Returns true when every skin-matrix coefficient is finite. */
    bool isFiniteMichelleMatrix(const glm::mat4 &matrix)
    {
        for (uint32_t column = 0u; column < 4u; ++column)
        {
            for (uint32_t row = 0u; row < 4u; ++row)
            {
                if (!std::isfinite(matrix[column][row])) return false;
            }
        }
        return true;
    }
} // namespace

/** Validates the shared Michelle geometry, images, hierarchy, and two frozen poses. */
int main()
{
    try
    {
        const std::filesystem::path path =
            std::filesystem::path(GVM_THREE_MICHELLE_ASSET_ROOT) /
            "models/gltf/Michelle.glb";
        const GVM::ThreeSamples::MichelleGlbAsset asset =
            GVM::ThreeSamples::loadMichelleGlbAsset(path);
        requireMichelle(asset.vertices.size() == 16340u,
                         "Michelle vertex count differs from r185.");
        requireMichelle(asset.indices.size() == 84318u,
                         "Michelle index count differs from r185.");
        requireMichelle(asset.nodes.size() == 67u &&
                         asset.jointNodeIndices.size() == 65u,
                         "Michelle hierarchy or joint count differs from r185.");
        requireMichelle(asset.animationTimes.size() == 547u &&
                         asset.animationChannels.size() == 195u,
                         "Michelle SambaDance channel layout differs from r185.");
        requireMichelle(asset.textures.size() == 4u,
                         "Michelle embedded texture count differs from r185.");
        for (const GVM::ThreeSamples::RgbaImageData &texture : asset.textures)
        {
            requireMichelle(texture.width == 512u && texture.height == 512u &&
                             texture.pixels.size() == 512u * 512u * 4u,
                             "Michelle embedded texture extent differs from r185.");
        }
        eastl::vector<glm::mat4> initialPalette;
        eastl::vector<glm::mat4> animatedPalette;
        GVM::ThreeSamples::sampleMichelleSkinPalette(
            asset, 0.0f, initialPalette);
        GVM::ThreeSamples::sampleMichelleSkinPalette(
            asset, 1.0f, animatedPalette);
        requireMichelle(initialPalette.size() == 65u &&
                         animatedPalette.size() == 65u,
                         "Michelle skin palette length differs from r185.");
        bool poseChanged = false;
        for (size_t joint = 0u; joint < initialPalette.size(); ++joint)
        {
            requireMichelle(isFiniteMichelleMatrix(initialPalette[joint]) &&
                             isFiniteMichelleMatrix(animatedPalette[joint]),
                             "Michelle skin palette contains a non-finite value.");
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    poseChanged = poseChanged ||
                        std::abs(initialPalette[joint][column][row] -
                                 animatedPalette[joint][column][row]) > 1.0e-5f;
                }
            }
        }
        requireMichelle(poseChanged,
                         "Michelle one-second pose did not change from frame zero.");
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        GVM::ThreeSamples::sampleMichelleSkinnedVertices(
            asset, 0.0f, positions, normals);
        constexpr size_t ProbeVertexIndices[] = {
            0u, 1000u, 5000u, 10000u, 16000u};
        const glm::vec3 ExpectedProbePositions[] = {
            {8.24872949f, 2.60910449f, -143.65863006f},
            {19.31553673f, 0.59231625f, -88.11728880f},
            {-9.51027046f, 11.63799473f, -88.04579678f},
            {-18.20127401f, 30.53180366f, -4.28157313f},
            {6.45032817f, 3.29137472f, -141.91860231f},
        };
        for (size_t probe = 0u; probe < std::size(ProbeVertexIndices); ++probe)
        {
            requireMichelle(
                glm::length(
                    glm::vec3(positions[ProbeVertexIndices[probe]]) -
                    ExpectedProbePositions[probe]) < 2.0e-4f,
                "Michelle frame-zero skinning differs from Three r185.");
        }
        requireMichelle(positions.size() == asset.vertices.size() &&
                         normals.size() == asset.vertices.size(),
                         "Michelle sampled vertex count differs from the mesh.");
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (size_t vertex = 0u; vertex < positions.size(); ++vertex)
        {
            minimum = glm::min(minimum, glm::vec3(positions[vertex]));
            maximum = glm::max(maximum, glm::vec3(positions[vertex]));
            requireMichelle(std::abs(glm::length(glm::vec3(normals[vertex])) -
                                     1.0f) < 1.0e-4f,
                             "Michelle sampled normal is not normalized.");
        }
        const glm::vec3 extent = maximum - minimum;
        const float largestExtent = glm::max(
            extent.x, glm::max(extent.y, extent.z));
        requireMichelle(largestExtent > 150.0f && largestExtent < 170.0f,
                         "Michelle sampled frame-zero height is not mesh-local.");
        std::cout << "Michelle GLB asset tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Michelle GLB asset tests failed: " << error.what() << '\n';
        return 1;
    }
}
