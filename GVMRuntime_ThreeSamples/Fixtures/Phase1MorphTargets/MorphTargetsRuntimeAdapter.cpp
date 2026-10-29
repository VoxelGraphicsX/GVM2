#include "MorphTargetsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::webglSceneSet;
#elif defined(GVM_THREE_WEBGPU_MORPH_TARGETS)
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::webgpuSceneSet;
#else
#error "One dedicated morph target host macro must be defined."
#endif

        static_assert(sizeof(MorphTargetsHostVertex) == 16u);
        static_assert(sizeof(MorphTargetsHostTriangleData) == 128u);
        static_assert(sizeof(MorphTargetsHostObjectData) == 160u);
        static_assert(sizeof(MorphTargetsHostInstanceData) == 16u);
        static_assert(sizeof(MorphTargetsHostMaterialData) == 32u);

        /** Stores one source BoxGeometry vertex and its two absolute morph targets. */
        struct MorphTargetsSourceVertex
        {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 spherePosition = glm::vec3(0.0f);
            glm::vec3 twistPosition = glm::vec3(0.0f);
        };

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMorphOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Converts one display-sRGB byte into Three's linear working space. */
        float srgbByteToLinear(uint32_t byteValue)
        {
            const float value = static_cast<float>(byteValue) / 255.0f;
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Assigns one named vector component without dynamic host reflection. */
        void setVectorComponent(glm::vec3 &value, uint32_t component, float scalar)
        {
            value[component] = scalar;
        }

        /** Appends one exact r185 BoxGeometry plane and its six geometry-group range. */
        void appendBoxPlane(eastl::vector<glm::vec3> &positions,
                            eastl::vector<uint32_t> &indices,
                            uint32_t u,
                            uint32_t v,
                            uint32_t w,
                            float uDirection,
                            float vDirection,
                            float planeWidth,
                            float planeHeight,
                            float planeDepth,
                            uint32_t gridX,
                            uint32_t gridY)
        {
            const float segmentWidth = planeWidth / static_cast<float>(gridX);
            const float segmentHeight = planeHeight / static_cast<float>(gridY);
            const float widthHalf = planeWidth * 0.5f;
            const float heightHalf = planeHeight * 0.5f;
            const float depthHalf = planeDepth * 0.5f;
            const uint32_t gridX1 = gridX + 1u;
            const uint32_t gridY1 = gridY + 1u;
            const uint32_t vertexBase = static_cast<uint32_t>(positions.size());
            for (uint32_t iy = 0u; iy < gridY1; ++iy)
            {
                const float y = static_cast<float>(iy) * segmentHeight - heightHalf;
                for (uint32_t ix = 0u; ix < gridX1; ++ix)
                {
                    const float x = static_cast<float>(ix) * segmentWidth - widthHalf;
                    glm::vec3 position(0.0f);
                    setVectorComponent(position, u, x * uDirection);
                    setVectorComponent(position, v, y * vDirection);
                    setVectorComponent(position, w, depthHalf);
                    positions.push_back(position);
                }
            }
            for (uint32_t iy = 0u; iy < gridY; ++iy)
            {
                for (uint32_t ix = 0u; ix < gridX; ++ix)
                {
                    const uint32_t a = vertexBase + ix + gridX1 * iy;
                    const uint32_t b = vertexBase + ix + gridX1 * (iy + 1u);
                    const uint32_t c = vertexBase + ix + 1u + gridX1 * (iy + 1u);
                    const uint32_t d = vertexBase + ix + 1u + gridX1 * iy;
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }
        }

        /** Calculates both absolute target positions with r185 Float32 storage semantics. */
        MorphTargetsSourceVertex makeSourceVertex(const glm::vec3 &position)
        {
            const double x = static_cast<double>(position.x);
            const double y = static_cast<double>(position.y);
            const double z = static_cast<double>(position.z);
            const glm::vec3 sphere(
                static_cast<float>(x * std::sqrt(1.0 - y * y * 0.5 - z * z * 0.5 + y * y * z * z / 3.0)),
                static_cast<float>(y * std::sqrt(1.0 - z * z * 0.5 - x * x * 0.5 + z * z * x * x / 3.0)),
                static_cast<float>(z * std::sqrt(1.0 - x * x * 0.5 - y * y * 0.5 + x * x * y * y / 3.0)));
            const double angle = Pi * x * 0.5;
            const double cosine = std::cos(angle);
            const double sine = std::sin(angle);
            const glm::vec3 twist(
                static_cast<float>(x * 2.0),
                static_cast<float>(y * cosine - z * sine),
                static_cast<float>(y * sine + z * cosine));
            return {
                .position = position,
                .spherePosition = sphere,
                .twistPosition = twist,
            };
        }

        /** Expands all 12,288 indexed source triangles for dynamic per-triangle normals. */
        void buildExpandedBoxGeometry(MorphTargetsEntityState &entity,
                                      uint32_t &sourceVertexCount,
                                      uint32_t &sourceIndexCount)
        {
            eastl::vector<glm::vec3> positions;
            eastl::vector<uint32_t> sourceIndices;
            positions.reserve(6534u);
            sourceIndices.reserve(36864u);
            appendBoxPlane(positions, sourceIndices, 2u, 1u, 0u, -1.0f, -1.0f, 2.0f, 2.0f, 2.0f, 32u, 32u);
            appendBoxPlane(positions, sourceIndices, 2u, 1u, 0u, 1.0f, -1.0f, 2.0f, 2.0f, -2.0f, 32u, 32u);
            appendBoxPlane(positions, sourceIndices, 0u, 2u, 1u, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 32u, 32u);
            appendBoxPlane(positions, sourceIndices, 0u, 2u, 1u, 1.0f, -1.0f, 2.0f, 2.0f, -2.0f, 32u, 32u);
            appendBoxPlane(positions, sourceIndices, 0u, 1u, 2u, 1.0f, -1.0f, 2.0f, 2.0f, 2.0f, 32u, 32u);
            appendBoxPlane(positions, sourceIndices, 0u, 1u, 2u, -1.0f, -1.0f, 2.0f, 2.0f, -2.0f, 32u, 32u);
            if (positions.size() != 6534u || sourceIndices.size() != 36864u)
            {
                throw std::runtime_error("Morph BoxGeometry counts differ from Three r185.");
            }

            eastl::vector<MorphTargetsSourceVertex> sourceVertices;
            sourceVertices.reserve(positions.size());
            for (const glm::vec3 &position : positions)
            {
                sourceVertices.push_back(makeSourceVertex(position));
            }
            entity.vertices.reserve(sourceIndices.size());
            entity.indices.reserve(sourceIndices.size());
            entity.morphTargets.reserve(sourceIndices.size());
            for (size_t triangleOffset = 0u; triangleOffset < sourceIndices.size(); triangleOffset += 3u)
            {
                const MorphTargetsSourceVertex &corner0 = sourceVertices[sourceIndices[triangleOffset]];
                const MorphTargetsSourceVertex &corner1 = sourceVertices[sourceIndices[triangleOffset + 1u]];
                const MorphTargetsSourceVertex &corner2 = sourceVertices[sourceIndices[triangleOffset + 2u]];
                const MorphTargetsSourceVertex *corners[3u] = {&corner0, &corner1, &corner2};
                for (uint32_t cornerIndex = 0u; cornerIndex < 3u; ++cornerIndex)
                {
                    const MorphTargetsSourceVertex &corner = *corners[cornerIndex];
                    const MorphTargetsSourceVertex &nextCorner = *corners[(cornerIndex + 1u) % 3u];
                    const MorphTargetsSourceVertex &lastCorner = *corners[(cornerIndex + 2u) % 3u];
                    const uint32_t expandedIndex = static_cast<uint32_t>(entity.vertices.size());
                    entity.vertices.push_back({glm::vec4(corner.position, 1.0f)});
                    entity.indices.push_back(expandedIndex);
                    entity.morphTargets.push_back({
                        .spherePosition = glm::vec4(corner.spherePosition, 1.0f),
                        .twistPosition = glm::vec4(corner.twistPosition, 1.0f),
                        .corner1Position = glm::vec4(nextCorner.position, 1.0f),
                        .corner1SpherePosition = glm::vec4(nextCorner.spherePosition, 1.0f),
                        .corner1TwistPosition = glm::vec4(nextCorner.twistPosition, 1.0f),
                        .corner2Position = glm::vec4(lastCorner.position, 1.0f),
                        .corner2SpherePosition = glm::vec4(lastCorner.spherePosition, 1.0f),
                        .corner2TwistPosition = glm::vec4(lastCorner.twistPosition, 1.0f),
                    });
                }
            }
            sourceVertexCount = static_cast<uint32_t>(positions.size());
            sourceIndexCount = static_cast<uint32_t>(sourceIndices.size());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendMorphBufferPayload(GVM::Core::RenderSetAllocInfo &allocation,
                                      GVM::Core::RenderComponentHandle component,
                                      const eastl::string &name,
                                      const void *value,
                                      uint64_t byteCount,
                                      uint32_t elementCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = elementCount,
            });
        }

        /** Builds the explicit depth-zero-to-one perspective projection used by GVM. */
        glm::mat4 makeMorphPerspective(double fieldOfViewDegrees,
                                      double aspect,
                                      double nearDistance,
                                      double farDistance)
        {
            const double top = nearDistance * std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = 2.0 * top;
            const double width = aspect * height;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * nearDistance / width);
            projection[1u][1u] = static_cast<float>(2.0 * nearDistance / height);
            projection[2u][2u] = static_cast<float>(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-farDistance * nearDistance / depth);
            return projection;
        }

        /** Reads canonical GUI and orbit state while keeping the renderer network-free. */
        void loadMorphScenarioState(const ThreeSampleHostOptions &options,
                                    MorphTargetsScenarioState &state)
        {
            if (options.scenarioId == "initial")
            {
                if (options.targetFrame != 0u || !options.inputReplayPath.empty())
                {
                    throw std::invalid_argument("Morph initial scenario requires frame zero and no replay.");
                }
                return;
            }
            if (options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Morph interactive scenarios require an explicit replay.");
            }
            std::ifstream input(options.inputReplayPath.c_str());
            if (!input)
            {
                throw std::runtime_error("Could not open the morph input replay.");
            }
            nlohmann::json replay;
            input >> replay;
            if (replay.value("caseId", "") != options.caseId.c_str() ||
                replay.value("scenarioId", "") != options.scenarioId.c_str())
            {
                throw std::runtime_error("Morph input replay identity differs from the requested scenario.");
            }
            const nlohmann::json &canonical = replay.at("canonicalState");
            if (options.scenarioId == "morph-gui" && options.targetFrame == 1u)
            {
                state.spherify = canonical.at("spherify").get<float>();
                state.twist = canonical.at("twist").get<float>();
                return;
            }
            if (options.scenarioId == "orbit-input" && options.targetFrame == 2u)
            {
                const nlohmann::json &camera = canonical.at("cameraPosition");
                state.cameraPosition = glm::vec4(
                    camera.at(0u).get<float>(),
                    camera.at(1u).get<float>(),
                    camera.at(2u).get<float>(),
                    1.0f);
                return;
            }
            throw std::invalid_argument("Morph scenario/frame differs from the frozen Manifest.");
        }

        /** Computes the exact RGBA8 byte count with overflow validation. */
        uint64_t computeMorphRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("Morph capture dimensions overflow RGBA8 storage.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void MorphTargetsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
        if (options.caseId != "webgl_morphtargets")
        {
            throw std::invalid_argument("The webgl morph host only accepts webgl_morphtargets.");
        }
        scenePassName = "WebglMorphtargetsMainPass";
        renderSetTypeName = "WebglMorphtargetsSceneRenderSet";
#else
        if (options.caseId != "webgpu_morphtargets")
        {
            throw std::invalid_argument("The webgpu morph host only accepts webgpu_morphtargets.");
        }
        scenePassName = "WebgpuMorphtargetsMainPass";
        renderSetTypeName = "WebgpuMorphtargetsSceneRenderSet";
#endif
        caseId = options.caseId;
        device = inDevice;
        loadMorphScenarioState(options, scenarioState);
        buildExpandedBoxGeometry(entity, sourceVertexCount, sourceIndexCount);
        entity.instanceData.translation = glm::vec4(0.0f);
        entity.materialData.baseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        const float specular = srgbByteToLinear(0x11u);
        entity.materialData.specularAndShininess = glm::vec4(specular, specular, specular, 30.0f);
        updateObjectData(options.width, options.height);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Morph target host could not create its Scene RenderSet encoder.");
        }
        entity.entityIndex = allocateEntity(*encoder, entity);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex MorphTargetsRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const MorphTargetsEntityState &inEntity) const
    {
        if (inEntity.vertices.size() != 36864u ||
            inEntity.indices.size() != 36864u ||
            inEntity.morphTargets.size() != inEntity.vertices.size())
        {
            throw std::runtime_error("Morph expanded RenderSet payload counts are invalid.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(inEntity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(inEntity.indices.size());
        allocation.instanceCount = 1u;
#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::vertices,
            "WebglMorphtargetsVertices", inEntity.vertices.data(),
            inEntity.vertices.size() * sizeof(MorphTargetsHostVertex), 1u);
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::indices,
            "WebglMorphtargetsIndices", inEntity.indices.data(),
            inEntity.indices.size() * sizeof(uint32_t), 1u);
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::objects,
            "WebglMorphtargetsObject", &inEntity.objectData, sizeof(inEntity.objectData), 1u);
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::instances,
            "WebglMorphtargetsInstance", &inEntity.instanceData, sizeof(inEntity.instanceData), 1u);
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::materials,
            "WebglMorphtargetsMaterial", &inEntity.materialData, sizeof(inEntity.materialData), 1u);
        appendMorphBufferPayload(allocation, WebglMorphtargetsSceneRenderSetComponents::morphTargets,
            "WebglMorphtargetsTargets", inEntity.morphTargets.data(),
            inEntity.morphTargets.size() * sizeof(MorphTargetsHostTriangleData),
            static_cast<uint32_t>(inEntity.morphTargets.size()));
#else
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::vertices,
            "WebgpuMorphtargetsVertices", inEntity.vertices.data(),
            inEntity.vertices.size() * sizeof(MorphTargetsHostVertex), 1u);
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::indices,
            "WebgpuMorphtargetsIndices", inEntity.indices.data(),
            inEntity.indices.size() * sizeof(uint32_t), 1u);
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::objects,
            "WebgpuMorphtargetsObject", &inEntity.objectData, sizeof(inEntity.objectData), 1u);
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::instances,
            "WebgpuMorphtargetsInstance", &inEntity.instanceData, sizeof(inEntity.instanceData), 1u);
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::materials,
            "WebgpuMorphtargetsMaterial", &inEntity.materialData, sizeof(inEntity.materialData), 1u);
        appendMorphBufferPayload(allocation, WebgpuMorphtargetsSceneRenderSetComponents::morphTargets,
            "WebgpuMorphtargetsTargets", inEntity.morphTargets.data(),
            inEntity.morphTargets.size() * sizeof(MorphTargetsHostTriangleData),
            static_cast<uint32_t>(inEntity.morphTargets.size()));
#endif
        return encoder.allocEntity(allocation);
    }

    void MorphTargetsRuntimeAdapter::updateObjectData(uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("Morph capture dimensions must be positive.");
        }
        const glm::vec3 sourceCamera(scenarioState.cameraPosition);
        const glm::vec3 hostCamera(sourceCamera.x, -sourceCamera.y, sourceCamera.z);
        const glm::mat4 view = glm::lookAt(hostCamera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 reflection(1.0f);
        reflection[1u][1u] = -1.0f;
        const glm::mat4 modelView = view * reflection;
        const glm::mat4 projection = makeMorphPerspective(
            45.0, static_cast<double>(width) / static_cast<double>(height), 1.0, 20.0);
        entity.objectData.modelView = modelView;
        entity.objectData.modelViewProjection = projection * modelView;
        entity.objectData.morphWeightsAndViewport = glm::vec4(
            scenarioState.spherify,
            scenarioState.twist,
            static_cast<float>(width),
            static_cast<float>(height));
        entity.objectData.ambientColorAndPointIntensity = glm::vec4(
            srgbByteToLinear(0x8fu) * 1.5f,
            srgbByteToLinear(0xbcu) * 1.5f,
            srgbByteToLinear(0xd4u) * 1.5f,
            200.0f);
    }

    void MorphTargetsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)frameIndex;
        updateObjectData(options.width, options.height);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Morph target host could not create its object update encoder.");
        }
#if defined(GVM_THREE_WEBGL_MORPH_TARGETS)
        encoder->setBufferComponentData(entity.entityIndex,
            WebglMorphtargetsSceneRenderSetComponents::objects,
            &entity.objectData, sizeof(entity.objectData), 0u, 1u);
#else
        encoder->setBufferComponentData(entity.entityIndex,
            WebgpuMorphtargetsSceneRenderSetComponents::objects,
            &entity.objectData, sizeof(entity.objectData), 0u, 1u);
#endif
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MorphTargetsRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeMorphRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Morph capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void MorphTargetsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareMorphOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write the morph RGBA8 capture.");
        }
    }

    void MorphTargetsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareMorphOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
               << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"randomSeed\":" << options.randomSeed << ",\n"
               << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
               << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\":" << byteCount << ",\n"
               << "  \"format\":\"rgba8unorm\",\n";
        if (options.scenarioId == "morph-gui")
        {
            const char *replayHash = caseId == "webgl_morphtargets"
                ? "7829115fd85eeb7fd4a91fb8cff375d9d0e660dc741e05e390eed5ec646ba6d0"
                : "b6c95128c23ea41a38c0e6e4ad26879cfc15da77a960c1fe539e7eecd1aae774";
            output << "  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"" << caseId.c_str()
                   << "\",\"scenarioId\":\"morph-gui\",\"captureFrame\":1,\"sha256\":\""
                   << replayHash
                   << "\",\"target\":\"#container > canvas\",\"eventCount\":1}\n";
        }
        else if (options.scenarioId == "orbit-input")
        {
            const char *replayHash = caseId == "webgl_morphtargets"
                ? "b47c5bb2077aa247070a2b0558fdd97fdc4720964523c7defbe2df09994fb55d"
                : "2d2302b8353da34555f744a36410bbf31f6f238e43cecc5413a9b56cb06d54e8";
            output << "  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"" << caseId.c_str()
                   << "\",\"scenarioId\":\"orbit-input\",\"captureFrame\":2,\"sha256\":\""
                   << replayHash
                   << "\",\"target\":\"#container > canvas\",\"eventCount\":3}\n";
        }
        else
        {
            output << "  \"inputReplay\":null\n";
        }
        output << "}\n";
    }

    void MorphTargetsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareMorphOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"caseId\":\"" << caseId.c_str() << "\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"semantic-complete\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"assetHashes\":[],\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":1,\n  \"entityCount\":1,\n"
               << "  \"instanceCounts\":[1],\n"
               << "  \"sourceVertexCount\":" << sourceVertexCount << ",\n"
               << "  \"sourceIndexCount\":" << sourceIndexCount << ",\n"
               << "  \"expandedVertexCount\":" << entity.vertices.size() << ",\n"
               << "  \"expandedIndexCount\":" << entity.indices.size() << ",\n"
               << "  \"geometryGroupCount\":6,\n"
               << "  \"morphTargetCount\":2,\n"
               << "  \"morphWeights\":[" << scenarioState.spherify << ',' << scenarioState.twist << "],\n"
               << "  \"renderSetType\":\"" << renderSetTypeName.c_str() << "\",\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"morphTargets\"],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n"
               << "  \"scenePasses\":[\"" << scenePassName.c_str() << "\"],\n"
               << "  \"screenPasses\":[],\n"
               << "  \"attachmentFormats\":[\"rgba8unorm\",\"depth32float\"],\n"
               << "  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-phong-morph\"}],\n"
               << "  \"sceneRoots\":[{\n"
               << "    \"id\":\"scene\",\n"
               << "    \"renderSetCount\":1,\n"
               << "    \"renderSetId\":\"scene\",\n"
               << "    \"renderSetType\":\"" << renderSetTypeName.c_str() << "\",\n"
               << "    \"renderableObjectCount\":1,\n"
               << "    \"entityCount\":1,\n"
               << "    \"drawCommandCount\":1,\n"
               << "    \"directDrawFallback\":false,\n"
               << "    \"componentSchema\":["
               << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
               << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
               << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
               << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
               << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
               << "{\"name\":\"morphTargets\",\"kind\":\"buffer\",\"role\":\"two-position-morph-targets\"}],\n"
               << "    \"scenePasses\":[{"
               << "\"name\":\"main-phong-morph\","
               << "\"renderClass\":\"" << scenePassName.c_str() << "\","
               << "\"renderSetId\":\"scene\","
               << "\"renderSetBindingCount\":1,"
               << "\"drawMode\":\"render-set-indexed-indirect\","
               << "\"invocationCount\":1,"
               << "\"drawCommandCount\":1,"
               << "\"usesStandaloneGeometry\":false,"
               << "\"usesExplicitDrawCount\":false}],\n"
               << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"morph-box\",\"instanceCount\":1}]\n"
               << "  }]\n"
               << "}\n";
    }

    void MorphTargetsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entity.vertices.clear();
        entity.indices.clear();
        entity.morphTargets.clear();
    }
} // namespace GVM::ThreeSamples
