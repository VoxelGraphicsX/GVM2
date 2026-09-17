#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one fully resolved non-indexed COLLADA triangle vertex. */
    struct ColladaAssetVertex final
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec2 uv = glm::vec2(0.0f);
        uint32_t materialIndex = 0u;
    };

    /** Describes one contiguous material range in the resolved triangle stream. */
    struct ColladaAssetGroup final
    {
        uint32_t firstVertex = 0u;
        uint32_t vertexCount = 0u;
        uint32_t materialIndex = 0u;
    };

    /** Describes one r185 COLLADA Phong material and its diffuse image. */
    struct ColladaAssetMaterial final
    {
        eastl::string name;
        eastl::string textureFile;
        glm::vec3 specular = glm::vec3(0.0f);
        float shininess = 30.0f;
    };

    /** Owns the validated Elf mesh, material groups, hierarchy transform, and asset lock. */
    struct ColladaElfAsset final
    {
        eastl::vector<ColladaAssetVertex> vertices;
        eastl::vector<ColladaAssetGroup> groups;
        eastl::vector<ColladaAssetMaterial> materials;
        glm::mat4 nodeMatrix = glm::mat4(1.0f);
        eastl::string sha256;
    };

    /** Stores one expanded robot-link triangle with an explicit flat normal. */
    struct ColladaRobotVertex final
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    };

    /** Owns one validated robot-link mesh and its zero-pose local transform. */
    struct ColladaRobotLink final
    {
        eastl::string name;
        eastl::vector<ColladaRobotVertex> vertices;
        glm::mat4 localTransform = glm::mat4(1.0f);
        glm::vec3 jointAxis = glm::vec3(0.0f);
        int32_t parentLink = -1;
        float minimumDegrees = 0.0f;
        float maximumDegrees = 0.0f;
    };

    /** Owns the seven-link ABB robot, fixed hierarchy, material colors, and asset lock. */
    struct ColladaRobotAsset final
    {
        eastl::vector<ColladaRobotLink> links;
        eastl::string sha256;
    };

    /** Parses and validates the exact Three.js r185 Elf COLLADA asset without GPU work. */
    [[nodiscard]] ColladaElfAsset loadColladaElfAsset(
        const std::filesystem::path &path);

    /** Builds the r185 Z-up conversion and animated root rotation for one fixed frame. */
    [[nodiscard]] glm::mat4 makeColladaElfWorldMatrix(
        const ColladaElfAsset &asset,
        uint32_t frameIndex);

    /** Parses and validates the exact Three.js r185 ABB kinematics COLLADA asset. */
    [[nodiscard]] ColladaRobotAsset loadColladaRobotAsset(
        const std::filesystem::path &path);

    /** Evaluates the seven robot-link world transforms for one joint pose. */
    [[nodiscard]] eastl::vector<glm::mat4> evaluateColladaRobotPose(
        const ColladaRobotAsset &asset,
        const eastl::vector<float> &jointDegrees);
} // namespace GVM::ThreeSamples
