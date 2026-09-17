#include "WebglLoaderPlyRuntimeAdapter.hpp"

#include "PlyGeometry.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double ReferenceFrameStepMilliseconds = 1000.0 / 60.0;
        constexpr const char *AsciiAssetSha256 = "1246b1050ebc1e2e6b9de796a004bd3914e525546d402a5e286fbe6f13940c8e";
        constexpr const char *BinaryAssetSha256 = "837f769e67155dc4a6e9a90683a61d6f6b4571a2a97d79586831011f450e66e2";
        constexpr const char *CanonicalSceneSha256 = "5da37adfea8f4e84e2bebc347cf43534e23b55ca6c5ae7019e6006dc1765467f";
        constexpr uint32_t AsciiVertexCount = 855u;
        constexpr uint32_t AsciiFaceCount = 1689u;
        constexpr uint32_t BinaryVertexCount = 50002u;
        constexpr uint32_t BinaryFaceCount = 100000u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(LoaderPlyHostFloat4) == 16u);
        static_assert(sizeof(LoaderPlyHostUint4) == 16u);
        static_assert(sizeof(LoaderPlyHostVertex) == 32u);
        static_assert(sizeof(LoaderPlyHostObjectData) == 352u);
        static_assert(sizeof(LoaderPlyHostInstanceData) == 16u);
        static_assert(sizeof(LoaderPlyHostMaterialData) == 32u);
        static_assert(sizeof(LoaderPlyHostRenderFlags) == 16u);

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Resolves one required asset relative to the explicit immutable asset root. */
        std::filesystem::path resolvePlyAsset(const ThreeSampleHostOptions &options, const std::filesystem::path &relativePath)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument("webgl_loader_ply requires an explicit --asset-root.");
            }
            const std::filesystem::path root(options.assetRoot.c_str());
            const std::filesystem::path assetPath = root / relativePath;
            if (!std::filesystem::is_regular_file(assetPath))
            {
                throw std::runtime_error("Missing locked PLY asset: " + assetPath.string());
            }
            return assetPath;
        }

        /** Returns the exact formal canonical-state label for one supported scenario. */
        const char *canonicalStateForScenario(const eastl::string &scenarioId)
        {
            if (scenarioId == "initial-loader")
            {
                return "ground-dolphins-lucy-two-shadow-lights-camera-time-zero";
            }
            if (scenarioId == "canonical-loader")
            {
                return "ascii-855-vertices-1689-faces-binary-50002-vertices-100000-faces";
            }
            if (scenarioId == "animated-camera")
            {
                return "fixed-step-two-seconds-camera-orbit";
            }
            return nullptr;
        }

        /** Validates the exact formal scenario, frame, and non-interactive input contract. */
        void validateLoaderPlyOptions(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_loader_ply")
            {
                throw std::invalid_argument("PLY adapter requires case-id webgl_loader_ply.");
            }
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated-camera" && options.targetFrame == 120u;
            if (!initial && !canonical && !animated)
            {
                throw std::invalid_argument(
                    "webgl_loader_ply requires initial-loader/frame 0, canonical-loader/frame 0, "
                    "or animated-camera/frame 120.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument("webgl_loader_ply scenarios must not consume input replay.");
            }
            const char *canonicalState = canonicalStateForScenario(options.scenarioId);
            if (!options.canonicalStatePath.empty() && options.canonicalStatePath != canonicalState)
            {
                throw std::invalid_argument("webgl_loader_ply canonical-state differs from the formal Manifest.");
            }
        }

        /** Reproduces Three ColorManagement's sRGB-to-linear transfer for one byte channel. */
        float srgbByteToLinear(uint32_t channel)
        {
            const double srgb = double(channel) / 255.0;
            return static_cast<float>(srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4));
        }

        /** Returns the host reflection that preserves Three's final top-down canvas orientation. */
        glm::mat4 makeHostReflection()
        {
            glm::mat4 reflection(1.0f);
            reflection[1u][1u] = -1.0f;
            return reflection;
        }

        /** Builds the Three camera frustum with the current RHI zero-to-one clip-depth contract. */
        glm::mat4 makePerspectiveProjection(double fieldOfViewDegrees, double aspect, double nearDistance, double farDistance)
        {
            const double top = nearDistance * std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
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

        /** Builds Three's symmetric directional shadow orthographic projection. */
        glm::mat4 makeShadowProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 4.0;
            constexpr double Width = 2.0;
            constexpr double Height = 2.0;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(1.0f);
            projection[0u][0u] = static_cast<float>(2.0 / Width);
            projection[1u][1u] = static_cast<float>(2.0 / Height);
            projection[2u][2u] = static_cast<float>(-1.0 / depth);
            projection[3u][2u] = static_cast<float>(-NearDistance / depth);
            return projection;
        }

        /** Builds one reflected directional-light view-projection matrix looking at the origin. */
        glm::mat4 makeShadowViewProjection(const glm::dvec3 &sourceLightPosition)
        {
            const glm::dvec3 hostLightPosition(sourceLightPosition.x, -sourceLightPosition.y, sourceLightPosition.z);
            const glm::dmat4 view = glm::lookAt(hostLightPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
            return makeShadowProjection() * glm::mat4(view);
        }

        /** Builds the exact ground PlaneGeometry position, normal, and reflected winding arrays. */
        void buildGroundGeometry(LoaderPlyEntityState &entity)
        {
            entity.vertices = {
                {{-20.0f, 20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{20.0f, 20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{-20.0f, -20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{20.0f, -20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
            };
            entity.indices = {0u, 1u, 2u, 2u, 1u, 3u};
            entity.sourceVertexCount = 4u;
            entity.sourceFaceCount = 2u;
        }

        /** Expands parsed PLY triangles to deterministic flat normals and reflected winding. */
        void buildPlyEntityGeometry(const PlyGeometry &geometry, LoaderPlyEntityState &entity)
        {
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve(geometry.indices.size());
            entity.indices.reserve(geometry.indices.size());
            for (size_t indexOffset = 0u; indexOffset < geometry.indices.size(); indexOffset += 3u)
            {
                const uint32_t sourceIndices[3u] = {
                    geometry.indices[indexOffset],
                    geometry.indices[indexOffset + 1u],
                    geometry.indices[indexOffset + 2u],
                };
                const glm::vec3 edge0 = geometry.positions[sourceIndices[1u]] - geometry.positions[sourceIndices[0u]];
                const glm::vec3 edge1 = geometry.positions[sourceIndices[2u]] - geometry.positions[sourceIndices[0u]];
                const glm::vec3 crossNormal = glm::cross(edge0, edge1);
                const float crossNormalLength = glm::length(crossNormal);
                const glm::vec3 faceNormal = crossNormalLength > 0.0f ? crossNormal / crossNormalLength : glm::vec3(0.0f);
                const uint32_t reflectedOrder[3u] = {
                    sourceIndices[0u],
                    sourceIndices[2u],
                    sourceIndices[1u],
                };
                for (uint32_t sourceIndex : reflectedOrder)
                {
                    const glm::vec3 position = geometry.positions[sourceIndex];
                    entity.vertices.push_back({
                        .position = {position.x, position.y, position.z, 1.0f},
                        .normal = {faceNormal.x, faceNormal.y, faceNormal.z, 0.0f},
                    });
                    entity.indices.push_back(static_cast<uint32_t>(entity.indices.size()));
                }
            }
            entity.sourceVertexCount = geometry.sourceVertexCount;
            entity.sourceFaceCount = geometry.sourceFaceCount;
        }

        /** Returns the reflected Three model matrix for one entity transform. */
        glm::mat4 makeEntityModel(const glm::vec3 &position, float rotationX, float uniformScale)
        {
            glm::mat4 sourceModel(1.0f);
            sourceModel = glm::translate(sourceModel, position);
            sourceModel = glm::rotate(sourceModel, rotationX, glm::vec3(1.0f, 0.0f, 0.0f));
            sourceModel = glm::scale(sourceModel, glm::vec3(uniformScale));
            return makeHostReflection() * sourceModel;
        }

        /** Initializes all static transforms, materials, phases, and shadow cameras. */
        void initializeEntityComponents(eastl::array<LoaderPlyEntityState, 3u> &entities)
        {
            const glm::mat4 shadowViewProjection0 = makeShadowViewProjection(glm::dvec3(1.0, 1.0, 1.0));
            const glm::mat4 shadowViewProjection1 = makeShadowViewProjection(glm::dvec3(0.5, 1.0, -1.0));

            entities[0u].logicalId = "ground";
            entities[0u].storagePrefix = "WebglLoaderPlyGround";
            entities[0u].objectData.model = makeEntityModel(glm::vec3(0.0f, -0.5f, 0.0f), static_cast<float>(-Pi * 0.5), 1.0f);
            entities[0u].materialData = {
                .baseColor = {srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), 1.0f},
                .specularAndSurfaceParameter = {srgbByteToLinear(0x47u), srgbByteToLinear(0x47u), srgbByteToLinear(0x47u), 30.0f},
            };
            entities[0u].renderFlags.values = {0u, 0u, 1u, 0u};

            entities[1u].logicalId = "ascii-dolphins";
            entities[1u].storagePrefix = "WebglLoaderPlyAsciiDolphins";
            entities[1u].objectData.model = makeEntityModel(glm::vec3(0.0f, -0.2f, 0.3f), static_cast<float>(-Pi * 0.5), 0.001f);
            entities[1u].materialData = {
                .baseColor = {0.0f, srgbByteToLinear(0x9cu), 1.0f, 1.0f},
                .specularAndSurfaceParameter = {0.04f, 0.04f, 0.04f, 1.0f},
            };
            entities[1u].renderFlags.values = {1u, 1u, 1u, 1u};

            entities[2u].logicalId = "binary-lucy";
            entities[2u].storagePrefix = "WebglLoaderPlyBinaryLucy";
            entities[2u].objectData.model = makeEntityModel(glm::vec3(-0.2f, -0.02f, -0.2f), 0.0f, 0.0006f);
            entities[2u].materialData = entities[1u].materialData;
            entities[2u].renderFlags.values = {1u, 1u, 1u, 2u};

            for (LoaderPlyEntityState &entity : entities)
            {
                entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.model));
                entity.objectData.shadowViewProjection0 = shadowViewProjection0;
                entity.objectData.shadowViewProjection1 = shadowViewProjection1;
                entity.instanceData.translation = {0.0f, 0.0f, 0.0f, 0.0f};
            }
        }

        /** Appends one typed payload to a RenderSet buffer component allocation. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const eastl::string &name, const void *value, uint64_t byteCount, uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("webgl_loader_ply RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglLoaderPlyRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        validateLoaderPlyOptions(options);
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;

        const PlyGeometry dolphins = loadPlyGeometry(resolvePlyAsset(options, "models/ply/ascii/dolphins.ply"), PlyEncoding::Ascii);
        const PlyGeometry lucy = loadPlyGeometry(resolvePlyAsset(options, "models/ply/binary/Lucy100k.ply"), PlyEncoding::BinaryLittleEndian);
        if (dolphins.sourceSha256 != AsciiAssetSha256 || dolphins.sourceVertexCount != AsciiVertexCount || dolphins.sourceFaceCount != AsciiFaceCount)
        {
            throw std::runtime_error("ASCII dolphins PLY identity or parsed counts differ from r185.");
        }
        if (lucy.sourceSha256 != BinaryAssetSha256 || lucy.sourceVertexCount != BinaryVertexCount || lucy.sourceFaceCount != BinaryFaceCount)
        {
            throw std::runtime_error("Binary Lucy PLY identity or parsed counts differ from r185.");
        }
        scenarioState.asciiSha256 = dolphins.sourceSha256;
        scenarioState.binarySha256 = lucy.sourceSha256;

        buildGroundGeometry(entities[0u]);
        buildPlyEntityGeometry(dolphins, entities[1u]);
        buildPlyEntityGeometry(lucy, entities[2u]);
        initializeEntityComponents(entities);
        updateCameraState(0u, options.width, options.height);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_loader_ply could not create its Scene RenderSet encoder.");
        }
        for (LoaderPlyEntityState &entity : entities)
        {
            entity.entityIndex = allocateSceneEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglLoaderPlyRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderPlyEntityState &entity) const
    {
        if (entity.vertices.empty() || entity.indices.empty() || entity.vertices.size() > std::numeric_limits<uint32_t>::max() || entity.indices.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::out_of_range("webgl_loader_ply entity geometry has an invalid draw range.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::vertices, entity.storagePrefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(LoaderPlyHostVertex), 1u);
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::indices, entity.storagePrefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::objects, entity.storagePrefix + "Object", &entity.objectData, sizeof(entity.objectData), 1u);
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::instances, entity.storagePrefix + "Instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::materials, entity.storagePrefix + "Material", &entity.materialData, sizeof(entity.materialData), 1u);
        appendBufferPayload(allocation, WebglLoaderPlySceneRenderSetComponents::renderFlags, entity.storagePrefix + "RenderFlags", &entity.renderFlags, sizeof(entity.renderFlags), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderPlyRuntimeAdapter::updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("webgl_loader_ply capture dimensions must be positive.");
        }
        const double virtualMilliseconds = double(frameIndex) * ReferenceFrameStepMilliseconds;
        const double timer = (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.0005;
        scenarioState.cameraX = std::sin(timer) * 2.5;
        scenarioState.cameraY = 0.15;
        scenarioState.cameraZ = std::cos(timer) * 2.5;

        const glm::dvec3 sourceCamera(scenarioState.cameraX, scenarioState.cameraY, scenarioState.cameraZ);
        const glm::dvec3 sourceTarget(0.0, -0.1, 0.0);
        const glm::dvec3 hostCamera(sourceCamera.x, -sourceCamera.y, sourceCamera.z);
        const glm::dvec3 hostTarget(sourceTarget.x, -sourceTarget.y, sourceTarget.z);
        const glm::dvec3 hostForward = glm::normalize(hostTarget - hostCamera);
        const glm::mat4 view = glm::mat4(glm::lookAt(hostCamera, hostTarget, glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 projection = makePerspectiveProjection(35.0, double(width) / double(height), 1.0, 15.0);
        const glm::mat4 viewProjection = projection * view;
        for (LoaderPlyEntityState &entity : entities)
        {
            entity.objectData.viewProjection = viewProjection;
            entity.objectData.cameraPositionAndFogNear = {
                static_cast<float>(hostCamera.x),
                static_cast<float>(hostCamera.y),
                static_cast<float>(hostCamera.z),
                2.0f,
            };
            entity.objectData.cameraForwardAndFogFar = {
                static_cast<float>(hostForward.x),
                static_cast<float>(hostForward.y),
                static_cast<float>(hostForward.z),
                15.0f,
            };
        }
    }

    void WebglLoaderPlyRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        updateCameraState(frameIndex, options.width, options.height);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_loader_ply could not create its camera update encoder.");
        }
        for (const LoaderPlyEntityState &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex, WebglLoaderPlySceneRenderSetComponents::objects, &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderPlyRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_loader_ply capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderPlyRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglLoaderPlyRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_ply RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete webgl_loader_ply RGBA capture.");
        }
    }

    void WebglLoaderPlyRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_ply metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_ply\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": null\n"
               << "}\n";
    }

    void WebglLoaderPlyRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_ply structural snapshot path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_ply\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 3,\n"
               << "  \"entityCount\": 3,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 2,\n"
               << "  \"scenePassCount\": 2,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"directional-shadow-depth\", \"entityOrdinal\": 0},\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"directional-shadow-depth\", \"entityOrdinal\": 1},\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"main-lit-fog\", \"entityOrdinal\": 0}\n"
               << "  ],\n"
               << "  \"drawCommandCount\": 3,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"antialiasResolve\": \"disabled-single-sample\",\n"
               << "  \"shadowMapCount\": 2,\n"
               << "  \"shadowMapSize\": [1024, 1024],\n"
               << "  \"shadowBias\": -0.001,\n"
               << "  \"cameraFovDegrees\": 35,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 15,\n"
               << "  \"cameraPosition\": [" << scenarioState.cameraX << ", " << scenarioState.cameraY << ", " << scenarioState.cameraZ << "],\n"
               << "  \"asciiPly\": {\"format\": \"ascii\", \"vertexCount\": " << AsciiVertexCount << ", \"faceCount\": " << AsciiFaceCount << ", \"sha256\": \"" << scenarioState.asciiSha256.c_str() << "\"},\n"
               << "  \"binaryPly\": {\"format\": \"binary_little_endian\", \"vertexCount\": " << BinaryVertexCount << ", \"faceCount\": " << BinaryFaceCount << ", \"sha256\": \"" << scenarioState.binarySha256.c_str() << "\"},\n"
               << "  \"normalGeneration\": \"three-r185-computeVertexNormals\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglLoaderPlySceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 3,\n"
               << "      \"entityCount\": 3,\n"
               << "      \"entities\": [\n"
               << "        {\"entityId\": " << entities[0u].entityIndex << ", \"logicalRenderableId\": \"ground\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[1u].entityIndex << ", \"logicalRenderableId\": \"ascii-dolphins\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[2u].entityIndex << ", \"logicalRenderableId\": \"binary-lucy\", \"instanceCount\": 1}\n"
               << "      ],\n"
               << "      \"componentSchema\": [\n"
               << "        {\"name\": \"vertices\", \"kind\": \"buffer\", \"role\": \"vertex\"},\n"
               << "        {\"name\": \"indices\", \"kind\": \"buffer\", \"role\": \"index\"},\n"
               << "        {\"name\": \"objects\", \"kind\": \"buffer\", \"role\": \"object\"},\n"
               << "        {\"name\": \"instances\", \"kind\": \"buffer\", \"role\": \"instance\"},\n"
               << "        {\"name\": \"materials\", \"kind\": \"buffer\", \"role\": \"material\"},\n"
               << "        {\"name\": \"renderFlags\", \"kind\": \"buffer\", \"role\": \"ground-mesh-material-shadow-and-flat-phase\"}\n"
               << "      ],\n"
               << "      \"drawCommandCount\": 3,\n"
               << "      \"directDrawFallback\": false,\n"
               << "      \"scenePasses\": [\n"
               << "        {\"name\": \"directional-shadow-depth\", \"renderClass\": \"WebglLoaderPlyShadowDepthPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 2, \"drawCommandCount\": 2, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-lit-fog\", \"renderClass\": \"WebglLoaderPlyLitPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ]\n"
               << "}\n";
    }

    void WebglLoaderPlyRuntimeAdapter::writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty() || options.scenarioId != "canonical-loader")
        {
            return;
        }
        const std::filesystem::path outputPath(options.semanticSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_ply semantic snapshot path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_ply\",\n"
               << "  \"scenarioId\": \"canonical-loader\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"ascii-855-vertices-1689-faces-binary-50002-vertices-100000-faces\",\n"
               << "  \"result\": {\n"
               << "    \"renderableObjectCount\": 2,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"canonicalSceneSha256\": \"" << CanonicalSceneSha256 << "\",\n"
               << "    \"ascii\": {\"encoding\": \"ascii\", \"vertexCount\": 855, \"faceCount\": 1689, \"sha256\": \"" << AsciiAssetSha256 << "\"},\n"
               << "    \"binary\": {\"encoding\": \"binary_little_endian\", \"vertexCount\": 50002, \"faceCount\": 100000, \"sha256\": \"" << BinaryAssetSha256 << "\"}\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
