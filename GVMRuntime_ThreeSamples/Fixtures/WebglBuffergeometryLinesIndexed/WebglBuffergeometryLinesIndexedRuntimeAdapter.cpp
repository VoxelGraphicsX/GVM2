#include "WebglBuffergeometryLinesIndexedRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t IterationCount = 4u;
        constexpr float KochAngle = float(Pi / 3.0);

        static_assert(sizeof(WebglBuffergeometryLinesIndexedHostVertex) == 80u);
        static_assert(sizeof(WebglBuffergeometryLinesIndexedHostObjectData) == 80u);
        static_assert(sizeof(WebglBuffergeometryLinesIndexedHostInstanceData) == 16u);
        static_assert(sizeof(WebglBuffergeometryLinesIndexedHostMaterialData) == 16u);

        /** Validates the two fixed indexed-line scenarios and host contract. */
        void validateLinesIndexedScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (options.caseId != "webgl_buffergeometry_lines_indexed" ||
                (!initial && !animated) || options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Indexed lines require the locked case, scenarios, extent, seed, and no input replay.");
            }
        }

        /** Returns one JavaScript-compatible deterministic random double. */
        double nextLinesIndexedRandom(ThreeCompat::DeterministicRandom &random)
        {
            constexpr double InverseTwentyFourBitRange = 1.0 / 16777216.0;
            return double(random.nextUint32() >> 8u) *
                InverseTwentyFourBitRange;
        }

        /** Appends one upstream indexed vertex and its random linear color. */
        uint32_t appendLinesIndexedSourceVertex(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<glm::vec3> &colors,
            const glm::vec3 &position,
            ThreeCompat::DeterministicRandom &random)
        {
            positions.push_back(position);
            const float red =
                float(nextLinesIndexedRandom(random) * 0.5 + 0.5);
            const float green =
                float(nextLinesIndexedRandom(random) * 0.5 + 0.5);
            colors.push_back(glm::vec3(
                red, green, 1.0f));
            return static_cast<uint32_t>(positions.size() - 1u);
        }

        /** Recursively appends one indexed Koch curve interval. */
        void appendLinesIndexedKochIteration(
            const glm::vec3 &p0,
            const glm::vec3 &p4,
            int depth,
            eastl::vector<glm::vec3> &positions,
            eastl::vector<glm::vec3> &colors,
            eastl::vector<uint32_t> &sourceIndices,
            ThreeCompat::DeterministicRandom &random)
        {
            --depth;
            if (depth < 0)
            {
                const uint32_t first =
                    static_cast<uint32_t>(positions.size() - 1u);
                appendLinesIndexedSourceVertex(
                    positions, colors, p4, random);
                sourceIndices.push_back(first);
                sourceIndices.push_back(first + 1u);
                return;
            }
            const glm::vec3 third = (p4 - p0) / 3.0f;
            const glm::vec3 p1 = p0 + third;
            const float angle = std::atan2(third.y, third.x) + KochAngle;
            const float length = glm::length(third);
            const glm::vec3 p2(
                p1.x + std::cos(angle) * length,
                p1.y + std::sin(angle) * length,
                p1.z);
            const glm::vec3 p3 = p0 + third * 2.0f;
            appendLinesIndexedKochIteration(
                p0, p1, depth, positions, colors, sourceIndices, random);
            appendLinesIndexedKochIteration(
                p1, p2, depth, positions, colors, sourceIndices, random);
            appendLinesIndexedKochIteration(
                p2, p3, depth, positions, colors, sourceIndices, random);
            appendLinesIndexedKochIteration(
                p3, p4, depth, positions, colors, sourceIndices, random);
        }

        /** Appends all four translated iterations of one upstream curve family. */
        void appendLinesIndexedSnowflake(
            eastl::vector<glm::vec3> points,
            bool loop,
            float xOffset,
            eastl::vector<glm::vec3> &positions,
            eastl::vector<glm::vec3> &colors,
            eastl::vector<uint32_t> &sourceIndices,
            ThreeCompat::DeterministicRandom &random)
        {
            for (uint32_t iteration = 0u; iteration < IterationCount; ++iteration)
            {
                appendLinesIndexedSourceVertex(
                    positions, colors, points[0u], random);
                for (uint32_t index = 0u; index + 1u < points.size(); ++index)
                {
                    appendLinesIndexedKochIteration(
                        points[index], points[index + 1u], int(iteration),
                        positions, colors, sourceIndices, random);
                }
                if (loop)
                {
                    appendLinesIndexedKochIteration(
                        points.back(), points.front(), int(iteration),
                        positions, colors, sourceIndices, random);
                }
                for (glm::vec3 &point : points) point.x += xOffset;
            }
        }

        /** Builds the complete source indexed stream in exact upstream order. */
        void buildLinesIndexedSource(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<glm::vec3> &colors,
            eastl::vector<uint32_t> &sourceIndices,
            ThreeCompat::DeterministicRandom &random)
        {
            float y = 0.0f;
            appendLinesIndexedSnowflake(
                {{0.0f, y, 0.0f}, {500.0f, y, 0.0f}}, false, 600.0f,
                positions, colors, sourceIndices, random);
            y += 600.0f;
            appendLinesIndexedSnowflake(
                {{0.0f, y, 0.0f}, {250.0f, y + 400.0f, 0.0f},
                 {500.0f, y, 0.0f}},
                true, 600.0f, positions, colors, sourceIndices, random);
            y += 600.0f;
            appendLinesIndexedSnowflake(
                {{0.0f, y, 0.0f}, {500.0f, y, 0.0f},
                 {500.0f, y + 500.0f, 0.0f}, {0.0f, y + 500.0f, 0.0f}},
                true, 600.0f, positions, colors, sourceIndices, random);
            y += 1000.0f;
            appendLinesIndexedSnowflake(
                {{250.0f, y, 0.0f}, {500.0f, y, 0.0f},
                 {250.0f, y, 0.0f}, {250.0f, y + 250.0f, 0.0f},
                 {250.0f, y, 0.0f}, {0.0f, y, 0.0f},
                 {250.0f, y, 0.0f}, {250.0f, y - 250.0f, 0.0f},
                 {250.0f, y, 0.0f}},
                false, 600.0f, positions, colors, sourceIndices, random);
        }

        /** Expands each source index pair into a one-pixel triangle-list segment. */
        void expandLinesIndexedSegments(
            const eastl::vector<glm::vec3> &positions,
            const eastl::vector<glm::vec3> &colors,
            const eastl::vector<uint32_t> &sourceIndices,
            eastl::vector<WebglBuffergeometryLinesIndexedHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const uint32_t segmentCount =
                static_cast<uint32_t>(sourceIndices.size() / 2u);
            vertices.reserve(static_cast<size_t>(segmentCount) * 4u);
            indices.reserve(static_cast<size_t>(segmentCount) * 6u);
            for (uint32_t segment = 0u; segment < segmentCount; ++segment)
            {
                const uint32_t startIndex = sourceIndices[segment * 2u];
                const uint32_t endIndex = sourceIndices[segment * 2u + 1u];
                const glm::vec4 start(positions[startIndex], 1.0f);
                const glm::vec4 end(positions[endIndex], 1.0f);
                const glm::vec4 startColor(colors[startIndex], 1.0f);
                const glm::vec4 endColor(colors[endIndex], 1.0f);
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.push_back({start, end, startColor, endColor,
                                    {0.0f, -0.75f, 0.0f, 0.0f}});
                vertices.push_back({start, end, startColor, endColor,
                                    {0.0f, 0.75f, 0.0f, 0.0f}});
                vertices.push_back({start, end, startColor, endColor,
                                    {1.0f, 0.75f, 0.0f, 0.0f}});
                vertices.push_back({start, end, startColor, endColor,
                                    {1.0f, -0.75f, 0.0f, 0.0f}});
                indices.insert(
                    indices.end(),
                    {base, base + 1u, base + 2u,
                     base, base + 2u, base + 3u});
            }
        }

        /** Builds the OpenGL projection consumed by backend clip conversion. */
        glm::mat4 makeLinesIndexedProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(27.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * (800.0 / 500.0);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = float(2.0 * NearDistance / width);
            projection[1u][1u] = float(2.0 * NearDistance / height);
            projection[2u][2u] = float(
                -(FarDistance + NearDistance) /
                (FarDistance - NearDistance));
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = float(
                -2.0 * FarDistance * NearDistance /
                (FarDistance - NearDistance));
            return projection;
        }

        /** Builds the line child translation and animated parent Z rotation. */
        glm::mat4 makeLinesIndexedModelViewProjection(uint32_t targetFrame)
        {
            constexpr double ReferenceEpochSeconds = 1700000000.0;
            const double angle =
                (ReferenceEpochSeconds + double(targetFrame) / 60.0) * 0.5;
            const float sine = float(std::sin(angle));
            const float cosine = float(std::cos(angle));
            glm::mat4 rotation(1.0f);
            rotation[0u][0u] = cosine;
            rotation[0u][1u] = sine;
            rotation[1u][0u] = -sine;
            rotation[1u][1u] = cosine;
            const glm::mat4 model = glm::translate(
                rotation, glm::vec3(-1200.0f, -1200.0f, 0.0f));
            glm::mat4 view(1.0f);
            view[3u][2u] = -9000.0f;
            return makeLinesIndexedProjection() * view * model;
        }

        /** Appends one typed payload to the RenderSet allocation. */
        void appendLinesIndexedPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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

        /** Creates parent directories for one requested evidence file. */
        void prepareLinesIndexedOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeLinesIndexedText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareLinesIndexedOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write indexed-lines evidence artifact.");
            }
        }
    }

    void WebglBuffergeometryLinesIndexedRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateLinesIndexedScenario(options);
        device = inDevice;
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        eastl::vector<glm::vec3> sourcePositions;
        eastl::vector<glm::vec3> sourceColors;
        eastl::vector<uint32_t> sourceIndices;
        buildLinesIndexedSource(
            sourcePositions, sourceColors, sourceIndices, random);
        sourceVertexCount = static_cast<uint32_t>(sourcePositions.size());
        sourceSegmentCount = static_cast<uint32_t>(sourceIndices.size() / 2u);
        finalRandomState = random.getState();
        expandLinesIndexedSegments(
            sourcePositions, sourceColors, sourceIndices, vertices, indices);

        const WebglBuffergeometryLinesIndexedHostObjectData objectData = {
            .modelViewProjection =
                makeLinesIndexedModelViewProjection(options.targetFrame),
            .viewport = glm::vec4(400.0f, 250.0f, 0.0f, 0.0f),
        };
        const WebglBuffergeometryLinesIndexedHostInstanceData instanceData = {
            .reserved = glm::vec4(0.0f),
        };
        const WebglBuffergeometryLinesIndexedHostMaterialData materialData = {
            .color = glm::vec4(1.0f),
        };
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendLinesIndexedPayload(
            allocation,
            WebglBuffergeometryLinesIndexedSceneRenderSetComponents::vertices,
            "WebglBuffergeometryLinesIndexedVertices", vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendLinesIndexedPayload(
            allocation,
            WebglBuffergeometryLinesIndexedSceneRenderSetComponents::indices,
            "WebglBuffergeometryLinesIndexedIndices", indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendLinesIndexedPayload(
            allocation,
            WebglBuffergeometryLinesIndexedSceneRenderSetComponents::objects,
            "WebglBuffergeometryLinesIndexedObject", &objectData,
            sizeof(objectData));
        appendLinesIndexedPayload(
            allocation,
            WebglBuffergeometryLinesIndexedSceneRenderSetComponents::instances,
            "WebglBuffergeometryLinesIndexedInstance", &instanceData,
            sizeof(instanceData));
        appendLinesIndexedPayload(
            allocation,
            WebglBuffergeometryLinesIndexedSceneRenderSetComponents::materials,
            "WebglBuffergeometryLinesIndexedMaterial", &materialData,
            sizeof(materialData));
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create indexed-lines RenderSet encoder.");
        }
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglBuffergeometryLinesIndexedRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryLinesIndexedRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Indexed-lines capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareLinesIndexedOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write indexed-lines RGBA capture.");
            }
        }

        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgl_buffergeometry_lines_indexed\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
            << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"randomSeed\":" << options.randomSeed << ",\n"
            << "  \"width\":" << width << ",\n"
            << "  \"height\":" << height << ",\n"
            << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
            << "  \"byteCount\":" << byteCount << ",\n"
            << "  \"format\":\"rgba8unorm\"\n}\n";
        writeLinesIndexedText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgl_buffergeometry_lines_indexed\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"implementationLevel\":\"semantic-complete\",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"required\",\n"
            << "  \"sceneRenderSetCount\":1,\n"
            << "  \"renderableObjectCount\":1,\n"
            << "  \"entityCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"vertexCount\":" << vertices.size() << ",\n"
            << "  \"indexCount\":" << indices.size() << ",\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":0,\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"renderSetType\":\"WebglBuffergeometryLinesIndexedSceneRenderSet\",\n"
            << "  \"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
            << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebglBuffergeometryLinesIndexedSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"indexed-koch-lines\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main-indexed-lines\","
            << "\"renderClass\":\"WebglBuffergeometryLinesIndexedMainPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],\n"
            << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main-indexed-lines\",\"entityOrdinal\":0}],\n"
            << "  \"usesRenderEntityID\":true,\n"
            << "  \"usesRenderEntityInstanceID\":true,\n"
            << "  \"directDrawFallback\":false\n}\n";
        writeLinesIndexedText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgl_buffergeometry_lines_indexed\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"sourceVertexCount\":" << sourceVertexCount << ",\n"
            << "  \"sourceSegmentCount\":" << sourceSegmentCount << ",\n"
            << "  \"expandedVertexCount\":" << vertices.size() << ",\n"
            << "  \"finalRandomState\":" << finalRandomState << ",\n"
            << "  \"parentRotationZ\":" << double(frameIndex) / 120.0
            << "\n}\n";
        writeLinesIndexedText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebglBuffergeometryLinesIndexedRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
    }
}
