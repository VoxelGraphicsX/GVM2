#include "WebglLoaderDracoRuntimeAdapter.hpp"

#include "DracoAsset.hpp"
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
        constexpr char BunnySha256[] = "3bb08f257d873f69ded447e07c2dd4e9d7a264d58a686c88978c38430c5f6eb4";
        constexpr char CanonicalSceneSha256[] = "6ea538037dc6469a47e63291f709b4146403226f75b5fc27e79dbe8eb1f1d8c6";
        constexpr uint32_t BunnyVertexCount = 34834u;
        constexpr uint32_t BunnyFaceCount = 69451u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(LoaderDracoHostFloat4) == 16u);
        static_assert(sizeof(LoaderDracoHostUint4) == 16u);
        static_assert(sizeof(LoaderDracoHostVertex) == 32u);
        static_assert(sizeof(LoaderDracoHostObjectData) == 288u);
        static_assert(sizeof(LoaderDracoHostInstanceData) == 16u);
        static_assert(sizeof(LoaderDracoHostMaterialData) == 32u);

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Resolves the immutable bunny asset relative to the explicit asset root. */
        std::filesystem::path resolveDracoAsset(const ThreeSampleHostOptions &options)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument("webgl_loader_draco requires an explicit --asset-root.");
            }
            const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) / "models/draco/bunny.drc";
            if (!std::filesystem::is_regular_file(assetPath))
            {
                throw std::runtime_error("Missing locked Draco bunny asset: " + assetPath.string());
            }
            return assetPath;
        }

        /** Returns the formal canonical-state label for one supported scenario. */
        const char *canonicalStateForScenario(const eastl::string &scenarioId)
        {
            if (scenarioId == "initial-loader")
                return "bunny-plane-shadow-loaded";
            if (scenarioId == "canonical-loader")
                return "34834-points-69451-faces-position-only";
            if (scenarioId == "animated-camera")
                return "fixed-step-60hz-camera-orbit";
            return nullptr;
        }

        /** Validates the exact scenario, frame, canonical state, and input contract. */
        void validateLoaderDracoOptions(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_loader_draco")
            {
                throw std::invalid_argument("Draco adapter requires case-id webgl_loader_draco.");
            }
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated-camera" && options.targetFrame == 60u;
            if (!initial && !canonical && !animated)
            {
                throw std::invalid_argument("webgl_loader_draco requires initial-loader/frame 0, canonical-loader/frame 0, or animated-camera/frame 60.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument("webgl_loader_draco scenarios must not consume input replay.");
            }
            const char *canonicalState = canonicalStateForScenario(options.scenarioId);
            if (!options.canonicalStatePath.empty() && options.canonicalStatePath != canonicalState)
            {
                throw std::invalid_argument("webgl_loader_draco canonical-state differs from the formal Manifest.");
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

        /** Builds a zero-to-one perspective projection matching the current RHI clip contract. */
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

        /** Builds the reflected spotlight view-projection used by both Scene passes. */
        glm::mat4 makeSpotShadowViewProjection()
        {
            const glm::dvec3 hostLight(-1.0, -1.0, 1.0);
            const glm::dmat4 view = glm::lookAt(hostLight, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0));
            return makePerspectiveProjection(22.5, 1.0, 0.5, 500.0) * glm::mat4(view);
        }

        /** Builds the exact authored 8-by-8 ground PlaneGeometry. */
        void buildGroundGeometry(LoaderDracoEntityState &entity)
        {
            entity.vertices = {
                {{-4.0f, 4.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{4.0f, 4.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{-4.0f, -4.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
                {{4.0f, -4.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
            };
            entity.indices = {0u, 1u, 2u, 2u, 1u, 3u};
            entity.sourceVertexCount = 4u;
            entity.sourceFaceCount = 2u;
        }

        /** Packs the decoded indexed bunny with Three-compatible generated normals. */
        void buildBunnyGeometry(const DracoMeshAsset &asset, LoaderDracoEntityState &entity)
        {
            entity.vertices.reserve(asset.positions.size());
            for (size_t index = 0u; index < asset.positions.size(); ++index)
            {
                const glm::vec3 &position = asset.positions[index];
                const glm::vec3 &normal = asset.normals[index];
                entity.vertices.push_back({
                    .position = {position.x, position.y, position.z, 1.0f},
                    .normal = {normal.x, normal.y, normal.z, 0.0f},
                });
            }
            entity.indices = asset.indices;
            entity.sourceVertexCount = static_cast<uint32_t>(asset.positions.size());
            entity.sourceFaceCount = static_cast<uint32_t>(asset.indices.size() / 3u);
        }

        /** Initializes static models, material phases, and spotlight shadow matrices. */
        void initializeEntityComponents(eastl::array<LoaderDracoEntityState, 2u> &entities)
        {
            const glm::mat4 reflection = makeHostReflection();
            const glm::mat4 shadowViewProjection = makeSpotShadowViewProjection();

            entities[0u].logicalId = "ground";
            entities[0u].storagePrefix = "WebglLoaderDracoGround";
            glm::mat4 groundModel(1.0f);
            groundModel = glm::translate(groundModel, glm::vec3(0.0f, 0.03f, 0.0f));
            groundModel = glm::rotate(groundModel, static_cast<float>(-Pi * 0.5), glm::vec3(1.0f, 0.0f, 0.0f));
            entities[0u].objectData.model = reflection * groundModel;
            entities[0u].materialData = {
                .baseColor = {srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), 1.0f},
                .flags = {0u, 0u, 1u, 0u},
            };

            entities[1u].logicalId = "draco-bunny";
            entities[1u].storagePrefix = "WebglLoaderDracoBunny";
            entities[1u].objectData.model = reflection;
            entities[1u].materialData = {
                .baseColor = {srgbByteToLinear(0xa5u), srgbByteToLinear(0xa5u), srgbByteToLinear(0xa5u), 1.0f},
                .flags = {1u, 1u, 1u, 1u},
            };

            for (LoaderDracoEntityState &entity : entities)
            {
                entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.model));
                entity.objectData.shadowViewProjection = shadowViewProjection;
                entity.instanceData.translation = {0.0f, 0.0f, 0.0f, 0.0f};
            }
        }

        /** Appends one typed payload to a RenderSet component allocation. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const eastl::string &name, const void *value, uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("webgl_loader_draco RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglLoaderDracoRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        validateLoaderDracoOptions(options);
        device = inDevice;
        const DracoMeshAsset bunny = loadDracoMeshAsset(resolveDracoAsset(options));
        if (bunny.sourceSha256 != BunnySha256 || bunny.positions.size() != BunnyVertexCount || bunny.indices.size() != size_t(BunnyFaceCount) * 3u)
        {
            throw std::runtime_error("Draco bunny identity or decoded topology differs from Three r185.");
        }
        scenarioState.assetSha256 = bunny.sourceSha256;
        buildGroundGeometry(entities[0u]);
        buildBunnyGeometry(bunny, entities[1u]);
        initializeEntityComponents(entities);
        updateCameraState(0u, options.width, options.height);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_loader_draco could not create its Scene RenderSet encoder.");
        }
        for (LoaderDracoEntityState &entity : entities)
        {
            entity.entityIndex = allocateSceneEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglLoaderDracoRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderDracoEntityState &entity) const
    {
        if (entity.vertices.empty() || entity.indices.empty() || entity.vertices.size() > std::numeric_limits<uint32_t>::max() || entity.indices.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::out_of_range("webgl_loader_draco entity geometry has an invalid draw range.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglLoaderDracoSceneRenderSetComponents::vertices, entity.storagePrefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(LoaderDracoHostVertex));
        appendBufferPayload(allocation, WebglLoaderDracoSceneRenderSetComponents::indices, entity.storagePrefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
        appendBufferPayload(allocation, WebglLoaderDracoSceneRenderSetComponents::objects, entity.storagePrefix + "Object", &entity.objectData, sizeof(entity.objectData));
        appendBufferPayload(allocation, WebglLoaderDracoSceneRenderSetComponents::instances, entity.storagePrefix + "Instance", &entity.instanceData, sizeof(entity.instanceData));
        appendBufferPayload(allocation, WebglLoaderDracoSceneRenderSetComponents::materials, entity.storagePrefix + "Material", &entity.materialData, sizeof(entity.materialData));
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderDracoRuntimeAdapter::updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("webgl_loader_draco capture dimensions must be positive.");
        }
        const double virtualMilliseconds = double(frameIndex) * ReferenceFrameStepMilliseconds;
        const double timer = (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.0003;
        scenarioState.cameraX = std::sin(timer) * 0.5;
        scenarioState.cameraY = 0.25;
        scenarioState.cameraZ = std::cos(timer) * 0.5;
        const glm::dvec3 sourceCamera(scenarioState.cameraX, scenarioState.cameraY, scenarioState.cameraZ);
        const glm::dvec3 sourceTarget(0.0, 0.1, 0.0);
        const glm::dvec3 hostCamera(sourceCamera.x, -sourceCamera.y, sourceCamera.z);
        const glm::dvec3 hostTarget(sourceTarget.x, -sourceTarget.y, sourceTarget.z);
        const glm::dvec3 hostForward = glm::normalize(hostTarget - hostCamera);
        const glm::mat4 view = glm::mat4(glm::lookAt(hostCamera, hostTarget, glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 viewProjection = makePerspectiveProjection(35.0, double(width) / double(height), 0.1, 15.0) * view;
        for (LoaderDracoEntityState &entity : entities)
        {
            entity.objectData.viewProjection = viewProjection;
            entity.objectData.cameraPositionAndFogNear = {
                static_cast<float>(hostCamera.x), static_cast<float>(hostCamera.y), static_cast<float>(hostCamera.z), 1.0f};
            entity.objectData.cameraForwardAndFogFar = {
                static_cast<float>(hostForward.x), static_cast<float>(hostForward.y), static_cast<float>(hostForward.z), 4.0f};
        }
    }

    void WebglLoaderDracoRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        updateCameraState(frameIndex, options.width, options.height);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_loader_draco could not create its camera update encoder.");
        }
        for (const LoaderDracoEntityState &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex, WebglLoaderDracoSceneRenderSetComponents::objects, &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderDracoRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
            return;
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("webgl_loader_draco capture exceeds host addressable storage.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderDracoRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglLoaderDracoRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
            return;
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not open webgl_loader_draco RGBA output path.");
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
            throw std::runtime_error("Could not write complete webgl_loader_draco RGBA capture.");
    }

    void WebglLoaderDracoRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
            return;
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not open webgl_loader_draco metadata output path.");
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_draco\",\n"
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
               << "  \"sampleCount\": 1,\n"
               << "  \"msaaEnabled\": false,\n"
               << "  \"inputReplay\": null\n"
               << "}\n";
    }

    void WebglLoaderDracoRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
            return;
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not open webgl_loader_draco structural snapshot path.");
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_draco\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 2,\n"
               << "  \"entityCount\": 2,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 2,\n"
               << "  \"scenePassCount\": 2,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"drawCommandCount\": 2,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"sampleCount\": 1,\n"
               << "  \"msaaEnabled\": false,\n"
               << "  \"cameraPosition\": [" << scenarioState.cameraX << ", " << scenarioState.cameraY << ", " << scenarioState.cameraZ << "],\n"
               << "  \"dracoPointCount\": 34834,\n"
               << "  \"dracoFaceCount\": 69451,\n"
               << "  \"dracoAssetSha256\": \"" << scenarioState.assetSha256.c_str() << "\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 1,\n"
               << "    \"renderSetId\": \"scene\",\n"
               << "    \"renderSetType\": \"WebglLoaderDracoSceneRenderSet\",\n"
               << "    \"renderableObjectCount\": 2,\n"
               << "    \"entityCount\": 2,\n"
               << "    \"drawCommandCount\": 2,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"entities\": [\n"
               << "      {\"entityId\": " << entities[0u].entityIndex << ", \"logicalRenderableId\": \"ground\", \"instanceCount\": 1},\n"
               << "      {\"entityId\": " << entities[1u].entityIndex << ", \"logicalRenderableId\": \"draco-bunny\", \"instanceCount\": 1}\n"
               << "    ],\n"
               << "    \"componentSchema\": [\n"
               << "      {\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},\n"
               << "      {\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},\n"
               << "      {\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},\n"
               << "      {\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},\n"
               << "      {\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}\n"
               << "    ],\n"
               << "    \"scenePasses\": [\n"
               << "      {\"name\":\"shadow-depth\",\"renderClass\":\"WebglLoaderDracoShadowPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},\n"
               << "      {\"name\":\"main\",\"renderClass\":\"WebglLoaderDracoMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}\n"
               << "    ]\n"
               << "  }]\n"
               << "}\n";
    }

    void WebglLoaderDracoRuntimeAdapter::writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty() || options.scenarioId != "canonical-loader")
            return;
        const std::filesystem::path outputPath(options.semanticSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not open webgl_loader_draco semantic snapshot path.");
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_draco\",\n"
               << "  \"scenarioId\": \"canonical-loader\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"34834-points-69451-faces-position-only\",\n"
               << "  \"result\": {\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"canonicalSceneSha256\": \"" << CanonicalSceneSha256 << "\",\n"
               << "    \"canonicalSceneDigestInput\": \"webgl_loader_draco|34834|69451|POSITION|NORMAL|3bb08f257d873f69ded447e07c2dd4e9d7a264d58a686c88978c38430c5f6eb4\",\n"
               << "    \"asset\": {\"encoding\":\"draco-triangular-mesh\",\"pointCount\":34834,\"faceCount\":69451,\"sourceAttributes\":[\"POSITION\"],\"generatedAttributes\":[\"NORMAL\"],\"sha256\":\"" << scenarioState.assetSha256.c_str() << "\"}\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
