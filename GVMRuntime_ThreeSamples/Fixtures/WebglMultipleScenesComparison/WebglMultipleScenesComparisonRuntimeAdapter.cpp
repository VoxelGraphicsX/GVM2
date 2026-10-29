#include "WebglMultipleScenesComparisonRuntimeAdapter.hpp"

#include <EASTL/array.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t Detail = 3u;
        constexpr uint32_t SolidVertexCount = 960u;
        constexpr uint32_t WireVertexCount = 3840u;
        constexpr uint32_t WireIndexCount = 5760u;
        constexpr double Pi = 3.14159265358979323846;

        /** Creates parent directories for one requested comparison artifact. */
        void prepareComparisonOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes one bounded tightly packed RGBA8 capture size. */
        uint64_t computeComparisonRgbaByteCount(uint32_t width, uint32_t height)
        {
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error("Multiple-scenes RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * 4u;
        }

        /** Returns the exact r185 zero-to-one perspective projection used by GVM. */
        glm::mat4 makeComparisonProjection()
        {
            constexpr double FieldOfViewDegrees = 35.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / width);
            projection[1u][1u] = static_cast<float>(2.0 * NearDistance / height);
            projection[2u][2u] = static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return projection;
        }

        /** Appends one radius-one smooth vertex using Three's Float32 attribute conversion. */
        void appendComparisonSolidVertex(eastl::vector<WebglMultipleScenesSolidVertex> &vertices,
                                         const glm::dvec3 &unscaledPosition)
        {
            const glm::dvec3 normalizedPosition = glm::normalize(unscaledPosition);
            const glm::vec3 position(static_cast<float>(normalizedPosition.x),
                                     static_cast<float>(normalizedPosition.y),
                                     static_cast<float>(normalizedPosition.z));
            const glm::dvec3 normalizedNormal = glm::normalize(glm::dvec3(position));
            vertices.push_back({
                .position = {position.x, position.y, position.z, 1.0f},
                .normal = {static_cast<float>(normalizedNormal.x),
                           static_cast<float>(normalizedNormal.y),
                           static_cast<float>(normalizedNormal.z), 0.0f},
            });
        }

        /** Builds Three r185's non-indexed radius-one detail-3 IcosahedronGeometry. */
        void buildComparisonIcosahedron(eastl::vector<WebglMultipleScenesSolidVertex> &vertices,
                                        eastl::vector<uint32_t> &indices)
        {
            const double goldenRatio = (1.0 + std::sqrt(5.0)) * 0.5;
            const eastl::array<glm::dvec3, 12u> baseVertices = {
                glm::dvec3(-1.0, goldenRatio, 0.0), glm::dvec3(1.0, goldenRatio, 0.0),
                glm::dvec3(-1.0, -goldenRatio, 0.0), glm::dvec3(1.0, -goldenRatio, 0.0),
                glm::dvec3(0.0, -1.0, goldenRatio), glm::dvec3(0.0, 1.0, goldenRatio),
                glm::dvec3(0.0, -1.0, -goldenRatio), glm::dvec3(0.0, 1.0, -goldenRatio),
                glm::dvec3(goldenRatio, 0.0, -1.0), glm::dvec3(goldenRatio, 0.0, 1.0),
                glm::dvec3(-goldenRatio, 0.0, -1.0), glm::dvec3(-goldenRatio, 0.0, 1.0),
            };
            constexpr eastl::array<uint32_t, 60u> BaseIndices = {
                0u, 11u, 5u, 0u, 5u, 1u, 0u, 1u, 7u, 0u, 7u, 10u, 0u, 10u, 11u,
                1u, 5u, 9u, 5u, 11u, 4u, 11u, 10u, 2u, 10u, 7u, 6u, 7u, 1u, 8u,
                3u, 9u, 4u, 3u, 4u, 2u, 3u, 2u, 6u, 3u, 6u, 8u, 3u, 8u, 9u,
                4u, 9u, 5u, 2u, 4u, 11u, 6u, 2u, 10u, 8u, 6u, 7u, 9u, 8u, 1u,
            };
            constexpr uint32_t Columns = Detail + 1u;
            vertices.clear();
            indices.clear();
            vertices.reserve(SolidVertexCount);
            indices.reserve(SolidVertexCount);
            for (size_t faceOffset = 0u; faceOffset < BaseIndices.size(); faceOffset += 3u)
            {
                const glm::dvec3 a = baseVertices[BaseIndices[faceOffset]];
                const glm::dvec3 b = baseVertices[BaseIndices[faceOffset + 1u]];
                const glm::dvec3 c = baseVertices[BaseIndices[faceOffset + 2u]];
                glm::dvec3 lattice[Columns + 1u][Columns + 1u] = {};
                for (uint32_t column = 0u; column <= Columns; ++column)
                {
                    const double columnAlpha = double(column) / double(Columns);
                    const glm::dvec3 aToC = a + (c - a) * columnAlpha;
                    const glm::dvec3 bToC = b + (c - b) * columnAlpha;
                    const uint32_t rowCount = Columns - column;
                    for (uint32_t row = 0u; row <= rowCount; ++row)
                    {
                        lattice[column][row] = row == 0u && column == Columns
                            ? aToC
                            : aToC + (bToC - aToC) * (double(row) / double(rowCount));
                    }
                }
                for (uint32_t column = 0u; column < Columns; ++column)
                {
                    for (uint32_t rowTriangle = 0u; rowTriangle < 2u * (Columns - column) - 1u; ++rowTriangle)
                    {
                        const uint32_t row = rowTriangle / 2u;
                        if ((rowTriangle & 1u) == 0u)
                        {
                            appendComparisonSolidVertex(vertices, lattice[column][row + 1u]);
                            appendComparisonSolidVertex(vertices, lattice[column + 1u][row]);
                            appendComparisonSolidVertex(vertices, lattice[column][row]);
                        }
                        else
                        {
                            appendComparisonSolidVertex(vertices, lattice[column][row + 1u]);
                            appendComparisonSolidVertex(vertices, lattice[column + 1u][row + 1u]);
                            appendComparisonSolidVertex(vertices, lattice[column + 1u][row]);
                        }
                    }
                }
            }
            if (vertices.size() != SolidVertexCount)
            {
                throw std::logic_error("Detail-3 comparison icosahedron produced an invalid vertex count.");
            }
            for (uint32_t vertexIndex = 0u; vertexIndex < SolidVertexCount; vertexIndex += 3u)
            {
                indices.push_back(vertexIndex);
                indices.push_back(vertexIndex + 2u);
                indices.push_back(vertexIndex + 1u);
            }
        }

        /** Appends one triangle-list wire quad for an original mesh edge. */
        void appendComparisonWireSegment(eastl::vector<WebglMultipleScenesWireVertex> &vertices,
                                         eastl::vector<uint32_t> &indices,
                                         const WebglMultipleScenesSolidVertex &start,
                                         const WebglMultipleScenesSolidVertex &end)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            const WebglMultipleScenesWireVertex startNegative = {
                start.position, end.position, start.normal, end.normal, {0.0f, -1.0f, 0.0f, 0.0f}};
            const WebglMultipleScenesWireVertex startPositive = {
                start.position, end.position, start.normal, end.normal, {0.0f, 1.0f, 0.0f, 0.0f}};
            const WebglMultipleScenesWireVertex endNegative = {
                start.position, end.position, start.normal, end.normal, {1.0f, -1.0f, 0.0f, 0.0f}};
            const WebglMultipleScenesWireVertex endPositive = {
                start.position, end.position, start.normal, end.normal, {1.0f, 1.0f, 0.0f, 0.0f}};
            vertices.push_back(startNegative);
            vertices.push_back(startPositive);
            vertices.push_back(endNegative);
            vertices.push_back(endPositive);
            const uint32_t segmentIndices[6u] = {
                baseVertex, baseVertex + 2u, baseVertex + 1u,
                baseVertex + 1u, baseVertex + 2u, baseVertex + 3u};
            indices.insert(indices.end(), segmentIndices, segmentIndices + 6u);
        }

        /** Expands every triangle edge in Three's non-indexed wireframe index order. */
        void buildComparisonWireGeometry(const eastl::vector<WebglMultipleScenesSolidVertex> &solidVertices,
                                         eastl::vector<WebglMultipleScenesWireVertex> &wireVertices,
                                         eastl::vector<uint32_t> &wireIndices)
        {
            wireVertices.clear();
            wireIndices.clear();
            wireVertices.reserve(WireVertexCount);
            wireIndices.reserve(WireIndexCount);
            for (uint32_t triangle = 0u; triangle < SolidVertexCount; triangle += 3u)
            {
                appendComparisonWireSegment(wireVertices, wireIndices, solidVertices[triangle], solidVertices[triangle + 1u]);
                appendComparisonWireSegment(wireVertices, wireIndices, solidVertices[triangle + 1u], solidVertices[triangle + 2u]);
                appendComparisonWireSegment(wireVertices, wireIndices, solidVertices[triangle + 2u], solidVertices[triangle]);
            }
            if (wireVertices.size() != WireVertexCount || wireIndices.size() != WireIndexCount)
            {
                throw std::logic_error("Comparison wire expansion produced an invalid element count.");
            }
        }
    } // namespace

    void WebglMultipleScenesComparisonRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool slider = options.scenarioId == "slider" && options.targetFrame == 1u;
        const bool orbit = options.scenarioId == "orbit" && options.targetFrame == 2u;
        if (options.caseId != "webgl_multiple_scenes_comparison" || (!initial && !slider && !orbit))
        {
            throw std::invalid_argument("Multiple-scenes adapter requires one locked example scenario/frame pair.");
        }
        if (options.width != 800u || options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument("Multiple-scenes adapter requires the locked extent and random seed.");
        }
        if (initial != options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only multiple-scenes interaction scenarios require an input replay.");
        }
        device = inDevice;
        sliderPosition = (slider || orbit) ? 200.0f : 400.0f;
        if (orbit)
        {
            const double theta = -2.0 * Pi * 100.0 / 500.0;
            cameraX = 6.0 * std::sin(theta);
            cameraZ = 6.0 * std::cos(theta);
        }
        buildComparisonIcosahedron(solidVertices, solidIndices);
        buildComparisonWireGeometry(solidVertices, wireVertices, wireIndices);
        glm::mat4 reflection(1.0f);
        reflection[1u][1u] = -1.0f;
        const glm::dvec3 hostCamera(cameraX, -cameraY, cameraZ);
        const glm::mat4 view(glm::lookAtRH(hostCamera, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        modelViewProjection = makeComparisonProjection() * view * reflection;
        const glm::vec3 lightDirection = glm::normalize(glm::vec3(-2.0f, 2.0f, 2.0f));
        hemisphereDirection = glm::vec4(lightDirection, 0.0f);
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeComparisonRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareComparisonOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("Could not write the multiple-scenes RGBA capture.");
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareComparisonOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_multiple_scenes_comparison\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n";
        if (options.scenarioId == "slider")
        {
            output << "  \"inputReplay\": {\"schemaVersion\":1,\"caseId\":\"webgl_multiple_scenes_comparison\",\"scenarioId\":\"slider\",\"captureFrame\":1,\"sha256\":\"1e98c95076e373a1fc3e7841b95b914ccb243c71bd3bb522c0d659e3c074a621\",\"target\":\".slider\",\"eventCount\":12}\n";
        }
        else if (options.scenarioId == "orbit")
        {
            output << "  \"inputReplay\": {\"schemaVersion\":1,\"caseId\":\"webgl_multiple_scenes_comparison\",\"scenarioId\":\"orbit\",\"captureFrame\":2,\"sha256\":\"00df46702cd216c197b16bac17fafb5a1c7e7d7ff0b3f73c6e4095fe1968d7de\",\"target\":\".container\",\"eventCount\":3}\n";
        }
        else
        {
            output << "  \"inputReplay\": null\n";
        }
        output << "}\n";
    }

    void WebglMultipleScenesComparisonRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareComparisonOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_multiple_scenes_comparison\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 2,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 2,\n"
               << "  \"screenPassCount\": 1,\n"
               << "  \"drawCommandCount\": 2,\n"
               << "  \"sliderPosition\": " << sliderPosition << ",\n"
               << "  \"cameraPosition\": [" << cameraX << ", " << cameraY << ", " << cameraZ << "],\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"sceneL\",\"scenePass\":\"main-solid\",\"entityOrdinal\":0},\n"
               << "    {\"sceneRoot\":\"sceneR\",\"scenePass\":\"main-wireframe-expanded\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
