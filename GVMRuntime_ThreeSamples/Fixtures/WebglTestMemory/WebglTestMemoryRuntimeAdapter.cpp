#include "WebglTestMemoryRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/trigonometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t PreFrameRandomDrawCount = 120u;
        constexpr uint32_t GeometryUuidDrawCount = 4u;
        constexpr uint32_t FirstFramePostColorDrawCount = 24u;
        constexpr uint32_t LaterFramePostColorDrawCount = 16u;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglTestMemoryHostVertex) == 48u);
        static_assert(sizeof(WebglTestMemoryHostObjectData) == 144u);
        static_assert(sizeof(WebglTestMemoryHostInstanceData) == 16u);
        static_assert(sizeof(WebglTestMemoryHostMaterialData) == 16u);
        static_assert(sizeof(WebglTestMemoryHostRenderFlags) == 16u);

        /** Creates parent directories for one requested evidence artifact. */
        void prepareWebglTestMemoryOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional UTF-8 evidence file. */
        void writeWebglTestMemoryText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglTestMemoryOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL memory-test evidence.");
        }

        /** Appends one generated component payload to an allocation. */
        void appendWebglTestMemoryPayload(
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

        /** Builds Three r185 SphereGeometry positions and triangle indices. */
        void buildWebglTestMemorySphere(
            uint32_t widthSegments,
            uint32_t heightSegments,
            eastl::vector<glm::vec4> &positions,
            eastl::vector<uint32_t> &indices)
        {
            constexpr double Radius = 50.0;
            positions.clear();
            indices.clear();
            positions.reserve(
                static_cast<size_t>(widthSegments + 1u) *
                static_cast<size_t>(heightSegments + 1u));
            for (uint32_t row = 0u; row <= heightSegments; ++row)
            {
                const double v = double(row) / double(heightSegments);
                const double theta = v * Pi;
                const double y = Radius * std::cos(theta);
                const double ringRadius =
                    std::sqrt(Radius * Radius - y * y);
                for (uint32_t column = 0u;
                     column <= widthSegments;
                     ++column)
                {
                    const double u = double(column) / double(widthSegments);
                    const double phi = u * Pi * 2.0;
                    positions.emplace_back(
                        static_cast<float>(-ringRadius * std::cos(phi)),
                        static_cast<float>(y),
                        static_cast<float>(ringRadius * std::sin(phi)),
                        1.0f);
                }
            }
            const uint32_t rowWidth = widthSegments + 1u;
            for (uint32_t row = 0u; row < heightSegments; ++row)
            {
                for (uint32_t column = 0u;
                     column < widthSegments;
                     ++column)
                {
                    const uint32_t a = row * rowWidth + column + 1u;
                    const uint32_t b = row * rowWidth + column;
                    const uint32_t c = (row + 1u) * rowWidth + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u)
                        indices.insert(indices.end(), {a, b, d});
                    if (row != heightSegments - 1u)
                        indices.insert(indices.end(), {b, c, d});
                }
            }
        }

        /** Expands every generated wireframe line index into one triangle quad. */
        void buildWebglTestMemoryWireframe(
            const eastl::vector<glm::vec4> &positions,
            const eastl::vector<uint32_t> &triangleIndices,
            eastl::vector<WebglTestMemoryHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(triangleIndices.size() * 4u);
            indices.reserve(triangleIndices.size() * 6u);
            for (size_t triangle = 0u;
                 triangle + 2u < triangleIndices.size();
                 triangle += 3u)
            {
                const uint32_t corners[3u] = {
                    triangleIndices[triangle],
                    triangleIndices[triangle + 1u],
                    triangleIndices[triangle + 2u]};
                for (uint32_t edgeIndex = 0u;
                     edgeIndex < 3u;
                     ++edgeIndex)
                {
                    const glm::vec4 start = positions[corners[edgeIndex]];
                    const glm::vec4 end =
                        positions[corners[(edgeIndex + 1u) % 3u]];
                    const uint32_t base =
                        static_cast<uint32_t>(vertices.size());
                    vertices.push_back({start, end, {0.0f, -0.75f, 0.0f, 0.0f}});
                    vertices.push_back({start, end, {0.0f, 0.75f, 0.0f, 0.0f}});
                    vertices.push_back({start, end, {1.0f, 0.75f, 0.0f, 0.0f}});
                    vertices.push_back({start, end, {1.0f, -0.75f, 0.0f, 0.0f}});
                    indices.insert(indices.end(), {
                        base, base + 1u, base + 2u,
                        base, base + 2u, base + 3u});
                }
            }
        }

        /** Creates Three's WebGL perspective projection without global GLM switches. */
        glm::mat4 makeWebglTestMemoryProjection()
        {
            constexpr float Near = 1.0f;
            constexpr float Far = 10000.0f;
            const float inverseTangent =
                1.0f / std::tan(glm::radians(60.0f) * 0.5f);
            glm::mat4 result(0.0f);
            result[0u][0u] = inverseTangent / 1.6f;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0f;
            result[3u][2u] = (2.0f * Far * Near) / (Near - Far);
            return result;
        }

        /** Advances the exact r185 stream and returns the target frame parameters. */
        void resolveWebglTestMemoryFrame(
            uint32_t targetFrame,
            ThreeCompat::DeterministicRandom &random,
            uint32_t &widthSegments,
            uint32_t &heightSegments,
            uint8_t color[4u])
        {
            for (uint32_t draw = 0u;
                 draw < PreFrameRandomDrawCount;
                 ++draw)
                (void)random.nextUint32();
            for (uint32_t frame = 0u; frame <= targetFrame; ++frame)
            {
                widthSegments = eastl::max(
                    3u,
                    static_cast<uint32_t>(
                        double(random.nextFloat()) * 64.0));
                heightSegments = eastl::max(
                    2u,
                    static_cast<uint32_t>(
                        double(random.nextFloat()) * 32.0));
                for (uint32_t draw = 0u;
                     draw < GeometryUuidDrawCount;
                     ++draw)
                    (void)random.nextUint32();
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                    color[channel] = static_cast<uint8_t>(
                        double(random.nextFloat()) * 256.0);
                color[3u] = 255u;
                const uint32_t postColorDrawCount = frame == 0u
                    ? FirstFramePostColorDrawCount
                    : LaterFramePostColorDrawCount;
                for (uint32_t draw = 0u;
                     draw < postColorDrawCount;
                     ++draw)
                    (void)random.nextUint32();
            }
        }
    }

    void WebglTestMemoryRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-allocation" &&
            options.targetFrame == 0u;
        memoryChurn =
            options.scenarioId == "memory-churn" &&
            options.targetFrame == 120u;
        if (options.caseId != "webgl_test_memory" ||
            (!initial && !memoryChurn) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "WebGL memory test requires its locked scenarios, extent, seed, and no replay.");
        }
        device = inDevice;
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        uint8_t color[4u] = {};
        resolveWebglTestMemoryFrame(
            options.targetFrame,
            random,
            widthSegments,
            heightSegments,
            color);
        finalRandomState = random.getState();
        const uint32_t expectedRandomState =
            initial ? 2752946622u : 84978506u;
        if (finalRandomState != expectedRandomState)
            throw std::runtime_error(
                "WebGL memory-test random stream diverged from the oracle.");

        eastl::vector<glm::vec4> positions;
        eastl::vector<uint32_t> triangleIndices;
        buildWebglTestMemorySphere(
            widthSegments,
            heightSegments,
            positions,
            triangleIndices);
        buildWebglTestMemoryWireframe(
            positions,
            triangleIndices,
            entity.vertices,
            entity.indices);
        entity.objectData.modelView = glm::mat4(1.0f);
        entity.objectData.modelView[3u][2u] = -200.0f;
        entity.objectData.projection = makeWebglTestMemoryProjection();
        entity.objectData.viewport = glm::vec4(400.0f, 250.0f, 0.0f, 0.0f);
        entity.instanceData.reserved = glm::vec4(0.0f);
        entity.materialData.colorAndOpacity = glm::vec4(1.0f);
        entity.renderFlags.generationAndVisibility =
            glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        entity.textureBytes.assign(color, color + 4u);
        entity.mipOffsets.push_back(0u);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGL memory-test Scene Set encoder.");
        entity.entityIndex = allocateEntity(*encoder, 0u);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex
    WebglTestMemoryRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        uint32_t generation) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        WebglTestMemoryHostRenderFlags flags = entity.renderFlags;
        flags.generationAndVisibility.x = static_cast<float>(generation);
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::vertices,
            "WebglTestMemoryVertices",
            entity.vertices.data(),
            entity.vertices.size() * sizeof(WebglTestMemoryHostVertex));
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::indices,
            "WebglTestMemoryIndices",
            entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t));
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::objects,
            "WebglTestMemoryObject",
            &entity.objectData,
            sizeof(entity.objectData));
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::instances,
            "WebglTestMemoryInstance",
            &entity.instanceData,
            sizeof(entity.instanceData));
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::materials,
            "WebglTestMemoryMaterial",
            &entity.materialData,
            sizeof(entity.materialData));
        appendWebglTestMemoryPayload(
            allocation,
            WebglTestMemorySceneRenderSetComponents::renderFlags,
            "WebglTestMemoryRenderFlags",
            &flags,
            sizeof(flags));
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglTestMemorySceneRenderSetComponents::textures;
        textureComponent.textures.push_back({
            .textureName = "WebglTestMemoryCanvasTexture",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .width = 1u,
            .height = 1u,
            .data = entity.textureBytes.data(),
            .dataStorageBytes = entity.textureBytes.size(),
            .mipmapOffsetBytes = entity.mipOffsets,
        });
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        return encoder.allocEntity(allocation);
    }

    void WebglTestMemoryRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        if (!memoryChurn || frameIndex == 0u) return;
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create a WebGL memory-test churn encoder.");
        encoder->removeEntity(entity.entityIndex);
        entity.entityIndex = allocateEntity(*encoder, frameIndex);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
        ++reallocationCount;
    }

    void WebglTestMemoryRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        if (memoryChurn && reallocationCount != 120u)
            throw std::runtime_error(
                "WebGL memory-test churn did not perform 120 reallocations.");
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebglTestMemoryOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL memory-test RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_test_memory\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"randomState\":" << finalRandomState
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"}\n";
        writeWebglTestMemoryText(
            options.captureMetadataPath, metadata.str());
        const uint32_t generation = memoryChurn ? 120u : 0u;
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_test_memory\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":1,"
            << "\"vertexCount\":" << entity.vertices.size()
            << ",\"indexCount\":" << entity.indices.size()
            << ",\"scenePassCount\":1,\"screenPassCount\":2,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebglTestMemorySceneRenderSet\",\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"per-frame-solid-color-fixed-slot\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"wireframe-and-lifetime-generation\"}],"
            << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebglTestMemorySceneRenderSet\",\"entityCount\":1,"
            << "\"renderableObjectCount\":1,\"entities\":[{\"entityId\":"
            << entity.entityIndex
            << ",\"logicalRenderableId\":\"temporary-wireframe-sphere\","
            << "\"instanceCount\":1}],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"per-frame-solid-color-fixed-slot\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"wireframe-and-lifetime-generation\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"dynamic-wireframe\","
            << "\"renderClass\":\"WebglTestMemoryScenePass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"dynamic-wireframe\",\"entityOrdinal\":0}],"
            << "\"widthSegments\":" << widthSegments
            << ",\"heightSegments\":" << heightSegments
            << ",\"lifetimeGeneration\":" << generation
            << ",\"reallocationCount\":" << reallocationCount
            << ",\"finalRandomState\":" << finalRandomState << "}\n";
        writeWebglTestMemoryText(
            options.sceneSnapshotPath, snapshot.str());
        writeWebglTestMemoryText(
            options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglTestMemoryRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entity.vertices.clear();
        entity.indices.clear();
        entity.textureBytes.clear();
        entity.mipOffsets.clear();
        device = {};
    }
}
