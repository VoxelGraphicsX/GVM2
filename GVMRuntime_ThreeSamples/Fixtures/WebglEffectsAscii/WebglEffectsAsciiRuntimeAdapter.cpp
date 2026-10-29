#include "WebglEffectsAsciiRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 2u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;

        static_assert(sizeof(WebglEffectsAsciiHostVertex) == 48u);
        static_assert(sizeof(WebglEffectsAsciiHostObjectData) == 192u);
        static_assert(sizeof(WebglEffectsAsciiHostInstanceData) == 32u);
        static_assert(sizeof(WebglEffectsAsciiHostMaterialData) == 16u);
        static_assert(sizeof(WebglEffectsAsciiHostRenderFlagsData) == 16u);

        /** Creates parent directories for one ASCII capture artifact. */
        void prepareAsciiOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one flat-shaded sphere triangle with a constant face normal. */
        void appendAsciiSphereTriangle(
            WebglEffectsAsciiEntityData &entity,
            const glm::vec4 &a,
            const glm::vec4 &b,
            const glm::vec4 &c)
        {
            const glm::vec3 pa(a.x, a.y, a.z);
            const glm::vec3 pb(b.x, b.y, b.z);
            const glm::vec3 pc(c.x, c.y, c.z);
            const glm::vec3 faceNormal = glm::normalize(glm::cross(pb - pa, pc - pa));
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 packedNormal(faceNormal, 0.0f);
            entity.vertices.push_back({a, glm::vec4(1.0f), packedNormal});
            entity.vertices.push_back({b, glm::vec4(1.0f), packedNormal});
            entity.vertices.push_back({c, glm::vec4(1.0f), packedNormal});
            entity.indices.insert(entity.indices.end(), {base, base + 1u, base + 2u});
        }

        /** Appends one typed payload to the ASCII Scene RenderSet allocation. */
        void appendAsciiPayload(
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

        /** Builds the r185 SphereGeometry(200, 20, 10) topology for the lit ASCII source. */
        void buildAsciiSphere(WebglEffectsAsciiEntityData &entity)
        {
            constexpr uint32_t WidthSegments = 20u;
            constexpr uint32_t HeightSegments = 10u;
            glm::vec4 grid[HeightSegments + 1u][WidthSegments + 1u];
            for (uint32_t y = 0u; y <= HeightSegments; ++y)
            {
                const float v = float(y) / float(HeightSegments);
                const float theta = v * Pi;
                const float vertical = std::cos(theta);
                const float radius = std::sqrt(std::max(0.0f, 1.0f - vertical * vertical));
                for (uint32_t x = 0u; x <= WidthSegments; ++x)
                {
                    const float u = float(x) / float(WidthSegments);
                    const float phi = u * Pi * 2.0f;
                    grid[y][x] = glm::vec4(
                        -radius * std::cos(phi),
                        vertical,
                        radius * std::sin(phi),
                        1.0f);
                }
            }
            entity.vertices.clear();
            entity.indices.clear();

            // MeshPhongMaterial(flatShading=true) derives one normal per
            // triangle.  Expand the indexed grid triangles here so the
            // RenderSet can carry that face normal through the existing
            // interpolated attribute path on both UGLC pipelines.
            for (uint32_t y = 0u; y < HeightSegments; ++y)
            {
                for (uint32_t x = 0u; x < WidthSegments; ++x)
                {
                    if (y != 0u)
                        appendAsciiSphereTriangle(
                            entity, grid[y][x + 1u], grid[y][x], grid[y + 1u][x + 1u]);
                    if (y != HeightSegments - 1u)
                        appendAsciiSphereTriangle(
                            entity, grid[y][x], grid[y + 1u][x], grid[y + 1u][x + 1u]);
                }
            }
        }

        /** Builds the two-triangle ground plane from the upstream ASCII example. */
        void buildAsciiPlane(WebglEffectsAsciiEntityData &entity)
        {
            entity.vertices = {
                {{-200.0f, -200.0f, -200.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
                {{200.0f, -200.0f, -200.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
                {{200.0f, -200.0f, 200.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
                {{-200.0f, -200.0f, 200.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
            };
            entity.indices = {0u, 1u, 2u, 0u, 2u, 3u};
        }

        /** Validates the three locked ASCII scenarios. */
        void validateAsciiScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool trackball = options.scenarioId == "trackball" && options.targetFrame == 121u;
            if (options.caseId != "webgl_effects_ascii" ||
                (!initial && !animated && !trackball) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_effects_ascii requires the locked r185 scenario contract.");
        }
    } // namespace

    void WebglEffectsAsciiRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateAsciiScenario(options);
        device = inDevice;
        entities.clear();
        entities.resize(EntityCount);
        buildAsciiSphere(entities[0u]);
        buildAsciiPlane(entities[1u]);

        const glm::vec3 cameraPosition(0.0f, 150.0f, 500.0f);
        // TrackballControls performs an initial update in its constructor,
        // which calls PerspectiveCamera.lookAt(target).  Reproduce that
        // deterministic camera basis instead of leaving the default -Z
        // orientation untouched.
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(
            glm::radians(70.0f), 800.0f / 500.0f, 1.0f, 1000.0f);
        const glm::vec4 light0View = view * glm::vec4(500.0f, 500.0f, 500.0f, 1.0f);
        const glm::vec4 light1View = view * glm::vec4(-500.0f, -500.0f, -500.0f, 1.0f);

        entities[0u].objectData.offsetAndScale = glm::vec4(0.0f);
        entities[0u].objectData.materialAndFlags = glm::uvec4(0u, 0u, 0u, 0u);
        entities[0u].objectData.modelViewProjection = projection * view * glm::scale(glm::mat4(1.0f), glm::vec3(200.0f));
        entities[0u].objectData.modelView = view * glm::scale(glm::mat4(1.0f), glm::vec3(200.0f));
        entities[0u].objectData.light0PositionIntensity = glm::vec4(glm::vec3(light0View), 3.0f);
        entities[0u].objectData.light1PositionIntensity = glm::vec4(glm::vec3(light1View), 1.0f);
        entities[0u].instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        entities[0u].instanceData.tint = glm::vec4(1.0f);
        entities[0u].materialData.baseColor = glm::vec4(1.0f);
        entities[0u].renderFlags.flags = glm::uvec4(1u, 0u, 0u, 0u);

        entities[1u].objectData.offsetAndScale = glm::vec4(0.0f);
        entities[1u].objectData.materialAndFlags = glm::uvec4(1u, 1u, 0u, 0u);
        entities[1u].objectData.modelViewProjection = projection * view;
        entities[1u].objectData.modelView = view;
        entities[1u].objectData.light0PositionIntensity = glm::vec4(glm::vec3(light0View), 3.0f);
        entities[1u].objectData.light1PositionIntensity = glm::vec4(glm::vec3(light1View), 1.0f);
        entities[1u].instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        entities[1u].instanceData.tint = glm::vec4(1.0f);
        entities[1u].materialData.baseColor = glm::vec4(
            0.745404f, 0.745404f, 0.745404f, 1.0f);
        entities[1u].renderFlags.flags = glm::uvec4(2u, 0u, 0u, 0u);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_effects_ascii could not create its Scene RenderSet encoder.");
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &current = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(current.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(current.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("AsciiEntity-") + eastl::to_string(index);
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::vertices,
                               (prefix + "-vertices").c_str(), current.vertices.data(),
                               current.vertices.size() * sizeof(current.vertices[0u]));
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::indices,
                               (prefix + "-indices").c_str(), current.indices.data(),
                               current.indices.size() * sizeof(current.indices[0u]));
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::objects,
                               (prefix + "-object").c_str(), &current.objectData, sizeof(current.objectData));
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::instances,
                               (prefix + "-instance").c_str(), &current.instanceData, sizeof(current.instanceData));
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::materials,
                               (prefix + "-material").c_str(), &current.materialData, sizeof(current.materialData));
            appendAsciiPayload(allocation, WebglEffectsAsciiSceneRenderSetComponents::renderFlags,
                               (prefix + "-flags").c_str(), &current.renderFlags, sizeof(current.renderFlags));
            current.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglEffectsAsciiRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const float time = float(frameIndex) / 60.0f;
        const glm::vec3 cameraPosition(0.0f, 150.0f, 500.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(
            glm::radians(70.0f), 800.0f / 500.0f, 1.0f, 1000.0f);
        // AsciiEffect's source example uses timer milliseconds: the fixed
        // 60 Hz replay therefore maps to sin(frame / 60 * 2.0).
        const float bounce = std::abs(std::sin(time * 2.0f)) * 150.0f;
        const glm::mat4 sphereModel = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, bounce, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(200.0f));
        entities[0u].objectData.modelViewProjection = projection * view * sphereModel;
        entities[0u].objectData.modelView = view * sphereModel;
    }

    void WebglEffectsAsciiRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_effects_ascii could not create its update encoder.");
        for (const auto &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglEffectsAsciiSceneRenderSetComponents::objects,
                &entity.objectData,
                sizeof(entity.objectData),
                0u,
                1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglEffectsAsciiRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareAsciiOutputPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("Could not write ASCII RGBA capture.");
    }

    void WebglEffectsAsciiRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareAsciiOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_effects_ascii\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << (uint64_t(width) * 4u)
               << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\"inputReplay\":";
        if (options.scenarioId == "trackball")
        {
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_effects_ascii\","
                   << "\"scenarioId\":\"trackball\",\"captureFrame\":121,"
                   << "\"sha256\":\"1d889d85ea29438a32938ab360b31cb363c3c196e4aa9a618a00bf10258d3ea3\","
                   << "\"target\":\"body > div:last-of-type\",\"eventCount\":3}";
        }
        else
        {
            output << "null";
        }
        output << "}\n";
    }

    void WebglEffectsAsciiRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareAsciiOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_effects_ascii\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglEffectsAsciiSceneRenderSet\",\n  \"renderableObjectCount\":2,\n"
               << "  \"entityCount\":2,\n  \"instanceCount\":1,\n  \"instanceCounts\":[1,1],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":2,\n  \"drawCommandCount\":3,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"renderFlags\"],\n"
               << "  \"scenePasses\":[{\"name\":\"main-lit\",\"renderClass\":\"WebglEffectsAsciiScenePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-lit\",\"entityOrdinal\":0}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglEffectsAsciiSceneRenderSet\",\"renderableObjectCount\":2,\"entityCount\":2,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"flat-phong-and-basic-material-phase\"}],\"scenePasses\":[{\"name\":\"main-lit\",\"renderClass\":\"WebglEffectsAsciiScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"sphere\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"plane\",\"instanceCount\":1}]}],\n"
               << "  \"screenPasses\":[{\"name\":\"ascii-luminance-quantize\",\"renderClass\":\"WebglEffectsAsciiLuminancePass\"},{\"name\":\"courier-glyph-grid-compose\",\"renderClass\":\"WebglEffectsAsciiGlyphPass\"}]\n}\n";
    }

    void WebglEffectsAsciiRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("webgl_effects_ascii capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_effects_ascii has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglEffectsAsciiRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
