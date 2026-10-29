#include "ColladaAsset.hpp"

#include <glm/common.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Throws when one frozen COLLADA asset contract is not satisfied. */
    void requireColladaCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Validates the exact Elf geometry groups, bounds, materials, and hierarchy transform. */
    void testFrozenElfAsset()
    {
        const GVM::ThreeSamples::ColladaElfAsset asset =
            GVM::ThreeSamples::loadColladaElfAsset(
                std::filesystem::path(GVM_THREE_COLLADA_ASSET_ROOT) /
                "models" / "collada" / "elf" / "elf.dae");
        requireColladaCondition(asset.vertices.size() == 42624u,
                                "Elf resolved vertex count is invalid.");
        const uint32_t expectedGroupCounts[] = {1446u, 26478u, 8040u, 6660u};
        requireColladaCondition(asset.groups.size() == 4u,
                                "Elf material group count is invalid.");
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            requireColladaCondition(
                asset.groups[index].vertexCount == expectedGroupCounts[index] &&
                    asset.groups[index].materialIndex == index,
                "Elf material group layout differs from Three.js r185.");
        }
        const uint32_t sampleIndices[] = {0u, 1446u, 27924u, 35964u};
        const glm::vec3 samplePositions[] = {
            {-14.1650105f, 1.6738380f, -5.6116672f},
            {2.5320561f, 24.9853802f, -6.7002330f},
            {-0.9111570f, 38.1067009f, -3.0742159f},
            {-0.6541440f, 41.0388184f, -2.3481121f},
        };
        const glm::vec2 sampleUvs[] = {
            {0.01878499f, 0.43678901f},
            {0.47419101f, 0.12214100f},
            {0.71935099f, 0.85444701f},
            {0.07721495f, 0.47339800f},
        };
        for (uint32_t sample = 0u; sample < 4u; ++sample)
        {
            const GVM::ThreeSamples::ColladaAssetVertex &vertex =
                asset.vertices[sampleIndices[sample]];
            requireColladaCondition(
                glm::all(glm::lessThan(
                    glm::abs(vertex.position - samplePositions[sample]),
                    glm::vec3(1.0e-5f))) &&
                    glm::all(glm::lessThan(
                        glm::abs(vertex.uv - sampleUvs[sample]),
                        glm::vec2(1.0e-5f))),
                "Elf resolved vertex samples differ from Three.js r185.");
        }
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (const GVM::ThreeSamples::ColladaAssetVertex &vertex : asset.vertices)
        {
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
        }
        requireColladaCondition(
            glm::all(glm::lessThan(glm::abs(
                minimum - glm::vec3(-16.5599804f, -0.9689130f, -16.5830593f)),
                glm::vec3(1.0e-4f))) &&
                glm::all(glm::lessThan(glm::abs(
                    maximum - glm::vec3(11.6611404f, 42.7836304f, 11.6380701f)),
                    glm::vec3(1.0e-4f))),
            "Elf resolved bounds differ from Three.js r185.");
        const glm::mat4 world = GVM::ThreeSamples::makeColladaElfWorldMatrix(asset, 0u);
        requireColladaCondition(
            std::abs(world[3u][0u] - 0.3918409f) < 1.0e-6f &&
                std::abs(world[3u][1u]) < 1.0e-5f &&
                std::abs(world[3u][2u] - 0.5659924f) < 1.0e-6f,
            "Elf hierarchy transform differs from Three.js r185.");
    }

    /** Validates the exact ABB link counts and the zero-pose hierarchy. */
    void testFrozenRobotAsset()
    {
        const GVM::ThreeSamples::ColladaRobotAsset asset =
            GVM::ThreeSamples::loadColladaRobotAsset(
                std::filesystem::path(GVM_THREE_COLLADA_ASSET_ROOT) /
                "models" / "collada" / "abb_irb52_7_120.dae");
        const uint32_t counts[] = {
            6474u, 27120u, 4716u, 13884u, 19098u, 1632u, 5256u,
        };
        requireColladaCondition(asset.links.size() == 7u,
                                "ABB robot link count is invalid.");
        for (uint32_t index = 0u; index < 7u; ++index)
            requireColladaCondition(asset.links[index].vertices.size() == counts[index],
                                    "ABB robot link vertex count is invalid.");
        const eastl::vector<float> joints(7u, 0.0f);
        const eastl::vector<glm::mat4> transforms =
            GVM::ThreeSamples::evaluateColladaRobotPose(asset, joints);
        const glm::vec3 translations[] = {
            {0.0f, 0.0f, 0.0f}, {0.0f, 4.865f, 0.0f},
            {1.5f, 4.865f, 0.0f}, {1.5f, 9.615f, 0.0f},
            {7.5f, 9.615f, 0.0f}, {7.5f, 9.615f, 0.0f},
            {8.15f, 9.615f, 0.0f},
        };
        for (uint32_t index = 0u; index < 7u; ++index)
        {
            requireColladaCondition(
                glm::all(glm::lessThan(
                    glm::abs(glm::vec3(transforms[index][3u]) - translations[index]),
                    glm::vec3(1.0e-4f))),
                "ABB robot zero-pose hierarchy differs from Three.js r185.");
        }
    }
} // namespace

/** Executes the sample-private frozen COLLADA regression suite. */
int main()
{
    try
    {
        testFrozenElfAsset();
        testFrozenRobotAsset();
        std::cout << "COLLADA asset tests passed for the frozen r185 assets.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "COLLADA Elf test failed: " << error.what() << '\n';
        return 1;
    }
}
