#include "WebglUboArraysRuntimeAdapter.hpp"

#include "TexturedBoxSampleData.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 101u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;

        /** Returns the reference JavaScript unit value from one random draw. */
        double nextWebglUboArraysRandom(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one hexadecimal sRGB material byte to linear light. */
        float convertWebglUboArraysColorChannel(uint32_t byte)
        {
            const double srgb = double(byte) / 255.0;
            return static_cast<float>(srgb <= 0.04045
                ? srgb / 12.92
                : std::pow((srgb + 0.055) / 1.055, 2.4));
        }

        /** Builds Three's exact XYZ Euler matrix for one entity. */
        glm::mat4 makeWebglUboArraysRotation(
            double sourceX,
            double sourceY,
            double sourceZ)
        {
            const double x = sourceX;
            const double y = sourceY;
            const double z = sourceZ;
            const double a = std::cos(x);
            const double b = std::sin(x);
            const double c = std::cos(y);
            const double d = std::sin(y);
            const double e = std::cos(z);
            const double f = std::sin(z);
            const double ae = a * e;
            const double af = a * f;
            const double be = b * e;
            const double bf = b * f;
            glm::mat4 result(1.0f);
            result[0u][0u] = static_cast<float>(c * e);
            result[1u][0u] = static_cast<float>(-c * f);
            result[2u][0u] = static_cast<float>(d);
            result[0u][1u] = static_cast<float>(af + be * d);
            result[1u][1u] = static_cast<float>(ae - bf * d);
            result[2u][1u] = static_cast<float>(-b * c);
            result[0u][2u] = static_cast<float>(bf - ae * d);
            result[1u][2u] = static_cast<float>(be + af * d);
            result[2u][2u] = static_cast<float>(a * c);
            return result;
        }

        /** Builds Three r185's non-indexed radius-one TetrahedronGeometry. */
        void buildWebglUboArraysTetrahedron(
            eastl::vector<WebglUboArraysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 source[4u] = {
                glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)),
                glm::normalize(glm::vec3(-1.0f, -1.0f, 1.0f)),
                glm::normalize(glm::vec3(-1.0f, 1.0f, -1.0f)),
                glm::normalize(glm::vec3(1.0f, -1.0f, -1.0f))};
            constexpr uint32_t faceIndices[12u] = {
                2u, 1u, 0u, 0u, 3u, 2u,
                1u, 3u, 0u, 2u, 3u, 1u};
            vertices.clear();
            indices.clear();
            vertices.reserve(12u);
            indices.reserve(12u);
            for (uint32_t face = 0u; face < 4u; ++face)
            {
                const glm::vec3 a = source[faceIndices[face * 3u]];
                const glm::vec3 b = source[faceIndices[face * 3u + 1u]];
                const glm::vec3 c = source[faceIndices[face * 3u + 2u]];
                const glm::vec3 normal = glm::normalize(glm::cross(c - b, a - b));
                for (const glm::vec3 &position : {a, b, c})
                {
                    vertices.push_back({
                        .position = glm::vec4(position, 1.0f),
                        .normal = glm::vec4(normal, 0.0f),
                        .textureCoordinate = glm::vec4(0.0f),
                    });
                    indices.push_back(static_cast<uint32_t>(indices.size()));
                }
            }
        }

        /** Converts the shared BoxGeometry payload to the UBO Scene layout. */
        void buildWebglUboArraysBox(
            eastl::vector<WebglUboArraysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(
                1.0f, 1.0f, 1.0f, sourceVertices, indices);
            glm::vec3 faceNormals[6u] = {};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const TexturedBoxHostFloat4 &sourceA =
                    sourceVertices[indices[face * 6u]].position;
                const TexturedBoxHostFloat4 &sourceB =
                    sourceVertices[indices[face * 6u + 1u]].position;
                const TexturedBoxHostFloat4 &sourceC =
                    sourceVertices[indices[face * 6u + 2u]].position;
                const glm::vec3 a(sourceA.x, sourceA.y, sourceA.z);
                const glm::vec3 b(sourceB.x, sourceB.y, sourceB.z);
                const glm::vec3 c(sourceC.x, sourceC.y, sourceC.z);
                faceNormals[face] = glm::normalize(glm::cross(c - b, a - b));
            }
            vertices.clear();
            vertices.reserve(sourceVertices.size());
            for (uint32_t index = 0u; index < sourceVertices.size(); ++index)
            {
                vertices.push_back({
                    .position = glm::vec4(
                        sourceVertices[index].position.x,
                        sourceVertices[index].position.y,
                        sourceVertices[index].position.z,
                        1.0f),
                    .normal = glm::vec4(faceNormals[index / 4u], 0.0f),
                    .textureCoordinate = glm::vec4(
                        sourceVertices[index].texCoord.x,
                        sourceVertices[index].texCoord.y,
                        0.0f,
                        0.0f),
                });
            }
        }

        /** Builds Three's default SphereGeometry(1,32,16) topology. */
        void buildWebglUboArraysSphere(
            eastl::vector<WebglUboArraysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t widthSegments = 32u;
            constexpr uint32_t heightSegments = 16u;
            vertices.clear();
            indices.clear();
            vertices.reserve((widthSegments + 1u) * (heightSegments + 1u));
            for (uint32_t y = 0u; y <= heightSegments; ++y)
            {
                const double v = double(y) / double(heightSegments);
                const double theta = v * Pi;
                const double sinTheta = std::sin(theta);
                const double cosTheta = std::cos(theta);
                for (uint32_t x = 0u; x <= widthSegments; ++x)
                {
                    const double u = double(x) / double(widthSegments);
                    const double phi = u * Pi * 2.0;
                    const glm::vec3 normal(
                        float(-std::cos(phi) * sinTheta),
                        float(cosTheta),
                        float(std::sin(phi) * sinTheta));
                    vertices.push_back({
                        glm::vec4(normal, 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(float(u), float(1.0 - v), 0.0f, 0.0f)});
                }
            }
            for (uint32_t y = 0u; y < heightSegments; ++y)
            {
                for (uint32_t x = 0u; x < widthSegments; ++x)
                {
                    const uint32_t a = y * (widthSegments + 1u) + x;
                    const uint32_t b = a + 1u;
                    const uint32_t c = (y + 1u) * (widthSegments + 1u) + x;
                    const uint32_t d = c + 1u;
                    if (y != 0u)
                        indices.insert(indices.end(), {a, c, b});
                    if (y != heightSegments - 1u)
                        indices.insert(indices.end(), {b, c, d});
                }
            }
        }

        /** Builds PlaneGeometry(100,100) in its local XY plane. */
        void buildWebglUboArraysPlane(
            eastl::vector<WebglUboArraysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices = {
                {{-50.0f, -50.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
                {{ 50.0f, -50.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
                {{ 50.0f,  50.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}},
                {{-50.0f,  50.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
            };
            indices = {0u, 2u, 1u, 2u, 3u, 1u};
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendWebglUboArraysBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Creates parent folders for an explicitly requested artifact. */
        void prepareWebglUboArraysOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglUboArraysRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        const bool lightCountOrbit =
            options.scenarioId == "light-count-orbit" && options.targetFrame == 121u;
        if (options.caseId != "webgl_ubo_arrays" ||
            (!initial && !animated && !lightCountOrbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "WebGL UBO adapter requires its locked scenarios, extent, seed, and assets.");
        }
        device = inDevice;
        buildWebglUboArraysPlane(planeVertices, planeIndices);
        buildWebglUboArraysSphere(boxVertices, boxIndices);
        projection = makeThreePerspectiveProjection(
            options.width, options.height, 45.0, 0.1, 100.0);

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        const double elapsedSeconds = double(options.targetFrame) / 60.0;
        // Three creates the camera, Scene, and shared SphereGeometry before
        // filling the light arrays; each UUID consumes four xorshift draws.
        // Material/Mesh UUIDs are generated after this loop and cannot affect
        // the light positions or colors visible in the capture.
        for (uint32_t preludeDraw = 0u; preludeDraw < 12u; ++preludeDraw)
            (void)random.nextUint32();
        for (uint32_t light = 0u; light < 300u; ++light)
        {
            const uint32_t packedColor = static_cast<uint32_t>(
                nextWebglUboArraysRandom(random) * 16777215.0);
            lightColors[light] = glm::vec4(
                convertWebglUboArraysColorChannel((packedColor >> 16u) & 255u),
                convertWebglUboArraysColorChannel((packedColor >> 8u) & 255u),
                convertWebglUboArraysColorChannel(packedColor & 255u), 0.0f);
            const float centerX = static_cast<float>(
                nextWebglUboArraysRandom(random) * 50.0 - 25.0);
            const float centerZ = static_cast<float>(
                nextWebglUboArraysRandom(random) * 50.0 - 25.0);
            const float angle = 0.5f * static_cast<float>(elapsedSeconds) +
                static_cast<float>(light) * 0.5f;
            lightPositions[light] = glm::vec4(
                centerX + std::sin(angle) * 5.0f, 1.0f,
                centerZ + std::cos(angle) * 5.0f, 0.0f);
        }
        activeLightCount = glm::vec4(
            options.scenarioId == "light-count-orbit" ? 64.0f : 300.0f,
            0.0f, 0.0f, 0.0f);
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 50.0f, 50.0f),
            glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error("WebGL UBO could not create its Scene Set encoder.");

        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            glm::mat4 model(1.0f);
            const bool plane = entity == 0u;
            if (plane)
            {
                // Preserve the authored PlaneGeometry transform order from
                // the WebGL sample: rotate first, then translate one unit in
                // the local -Z direction (which becomes world -Y).
                model = glm::rotate(model, -float(Pi * 0.5),
                    glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::translate(model, glm::vec3(0.0f, 0.0f, -1.0f));
            }
            else
            {
                const uint32_t grid = entity - 1u;
                const uint32_t x = grid % 10u;
                const uint32_t z = grid / 10u;
                model = glm::translate(model, glm::vec3(
                    float(x * 6u) - 30.0f, 0.0f, float(z * 6u) - 30.0f));
            }
            const glm::mat4 modelView = view * model;
            objects[entity] = {
                .model = model,
                .modelView = modelView,
                .normalTransform = glm::transpose(glm::inverse(modelView)),
                .materialPhase = glm::uvec4(0u, 0u, 0u, 0u),
            };
            instances[entity].reserved = glm::vec4(0.0f);
            materials[entity].baseColor = glm::vec4(1.0f);

            const auto &vertices = plane ? planeVertices : boxVertices;
            const auto &indices = plane ? planeIndices : boxIndices;
            const eastl::string prefix =
                "WebglUboArraysEntity" + eastl::to_string(entity);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendWebglUboArraysBuffer(allocation, WebglUboArraysSceneRenderSetComponents::vertices,
                prefix + "Vertices", vertices.data(), vertices.size() * sizeof(vertices[0u]));
            appendWebglUboArraysBuffer(allocation, WebglUboArraysSceneRenderSetComponents::indices,
                prefix + "Indices", indices.data(), indices.size() * sizeof(indices[0u]));
            appendWebglUboArraysBuffer(allocation, WebglUboArraysSceneRenderSetComponents::objects,
                prefix + "Object", &objects[entity], sizeof(objects[entity]));
            appendWebglUboArraysBuffer(allocation, WebglUboArraysSceneRenderSetComponents::instances,
                prefix + "Instance", &instances[entity], sizeof(instances[entity]));
            appendWebglUboArraysBuffer(allocation, WebglUboArraysSceneRenderSetComponents::materials,
                prefix + "Material", &materials[entity], sizeof(materials[entity]));
            encoder->allocEntity(allocation);
        }
        // Complete the post-light UUID stream so metadata remains identical
        // to the browser harness while preserving the light data above.
        for (uint32_t postludeDraw = 0u; postludeDraw < 928u; ++postludeDraw)
            (void)random.nextUint32();
        finalRandomState = random.getState();
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglUboArraysRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglUboArraysRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        eastl::vector<uint8_t> rgba(size_t(width) * height * 4u);
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareWebglUboArraysOutputPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write WebGL UBO RGBA.");
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareWebglUboArraysOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_ubo_arrays\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"randomSeed\": " << options.randomSeed << ",\n"
                << "  \"randomState\": " << finalRandomState << ",\n"
                << "  \"width\": " << width << ", \"height\": " << height << ",\n"
                << "  \"rowStrideBytes\": " << width * 4u << ",\n"
                << "  \"byteCount\": " << rgba.size() << ",\n"
                << "  \"format\": \"rgba8unorm\", \"sampleCount\": 1,\n"
                << "  \"msaaEnabled\": false, \"simulateMsaa\": false,\n"
                << "  \"sceneRenderSetCount\": 1, \"renderableObjectCount\": 101,\n"
                << "  \"entityCount\": 101, \"instanceCounts\": [1],\n"
                << "  \"scenePassCount\": 1, \"drawCommandCount\": 101,\n"
                << "  \"directDrawFallback\": false, \"gpuWorkDslOnly\": true";
            if (!options.inputReplayPath.empty())
            {
                output << ",\n  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgl_ubo_arrays\",\"scenarioId\":\"light-count-orbit\",\"captureFrame\":121,\"sha256\":\"e361e921f629d7848d666a47b5494b0de5aaa22731a27ba5d1ddbe873f7901cc\",\"target\":\"#container > canvas\",\"eventCount\":3}";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareWebglUboArraysOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            std::ostringstream snapshot;
            snapshot << "{\n  \"caseId\": \"webgl_ubo_arrays\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"implementationLevel\": \"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\": true,\n"
                << "  \"renderSetPolicy\": \"required\",\n"
                << "  \"sceneRenderSetCount\": 1, \"renderableObjectCount\": 101,\n"
                << "  \"entityCount\": 101, \"instanceCounts\": [";
            for (uint32_t entity = 0u; entity < EntityCount; ++entity)
            {
                if (entity != 0u) snapshot << ',';
                snapshot << 1u;
            }
            snapshot
                << "],\n"
                << "  \"scenePassCount\": 1, \"screenPassCount\": 0,\n"
                << "  \"drawCommandCount\": 1, \"sampleCount\": 1,\n"
                << "  \"renderSetType\": \"WebglUboArraysSceneRenderSet\",\n"
                << "  \"directDrawFallback\": false,\n"
                << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebglUboArraysSceneRenderSet\",\"renderableObjectCount\":101,\"entityCount\":101,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"scenePasses\":[{\"name\":\"main-ubo-arrays\",\"renderClass\":\"WebglUboArraysScenePass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
            for (uint32_t entity = 0u; entity < EntityCount; ++entity)
            {
                if (entity != 0u) snapshot << ',';
                snapshot << "{\"entityId\":" << entity
                         << ",\"logicalRenderableId\":\"ubo-array-entity-"
                         << entity << "\",\"instanceCount\":1}";
            }
            snapshot << "]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-ubo-arrays\",\"entityOrdinal\":0}]}\n";
            output << snapshot.str();
        }
        captureWritten = true;
    }

    void WebglUboArraysRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        planeVertices.clear();
        planeIndices.clear();
        boxVertices.clear();
        boxIndices.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
