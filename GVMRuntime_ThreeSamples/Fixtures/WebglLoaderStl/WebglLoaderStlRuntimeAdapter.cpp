#include "WebglLoaderStlRuntimeAdapter.hpp"

#include "StlAsset.hpp"
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
        constexpr const char *AssetSha256[4u] = {
            "5c0d95ca55352ccf5cca12197a5f9fa17eb2e905d1bb3a45c8ba51c9a22699a4",
            "c4ebcc17156642a8e4df8564a09f730b54db236e783320d71af3e51de17e8315",
            "4c98307430589fbb86f0310b25b1896bbb0ac4c4f5b97d9dc4f03d3250003533",
            "0012b62d4bb485f902a972192eef29d6ce4b97a9deb2de67a02d30a5b738f461",
        };
        constexpr uint32_t AssetFacetCounts[4u] = {288u, 1000u, 1052u, 2156u};
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(LoaderStlHostFloat4) == 16u);
        static_assert(sizeof(LoaderStlHostUint4) == 16u);
        static_assert(sizeof(LoaderStlHostVertex) == 48u);
        static_assert(sizeof(LoaderStlHostObjectData) == 352u);
        static_assert(sizeof(LoaderStlHostInstanceData) == 16u);
        static_assert(sizeof(LoaderStlHostMaterialData) == 32u);
        static_assert(sizeof(LoaderStlHostRenderFlags) == 16u);

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Resolves one required asset relative to the explicit immutable asset root. */
        std::filesystem::path resolveStlAsset(const ThreeSampleHostOptions &options, const std::filesystem::path &relativePath)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument("webgl_loader_stl requires an explicit --asset-root.");
            }
            const std::filesystem::path root(options.assetRoot.c_str());
            const std::filesystem::path assetPath = root / relativePath;
            if (!std::filesystem::is_regular_file(assetPath))
            {
                throw std::runtime_error("Missing locked STL asset: " + assetPath.string());
            }
            return assetPath;
        }

        /** Returns the exact formal canonical-state label for one supported scenario. */
        const char *canonicalStateForScenario(const eastl::string &scenarioId)
        {
            if (scenarioId == "initial-loader")
            {
                return "ground-four-stl-meshes-two-shadow-lights-camera-time-zero";
            }
            if (scenarioId == "canonical-loader")
            {
                return "ascii-288-binary-1000-1052-colored-2156-facets";
            }
            if (scenarioId == "animated-camera")
            {
                return "fixed-step-two-seconds-camera-orbit";
            }
            return nullptr;
        }

        /** Validates the exact formal scenario, frame, and non-interactive input contract. */
        void validateLoaderStlOptions(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_loader_stl")
            {
                throw std::invalid_argument("STL adapter requires case-id webgl_loader_stl.");
            }
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated-camera" && options.targetFrame == 120u;
            if (!initial && !canonical && !animated)
            {
                throw std::invalid_argument(
                    "webgl_loader_stl requires initial-loader/frame 0, canonical-loader/frame 0, "
                    "or animated-camera/frame 120.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument("webgl_loader_stl scenarios must not consume input replay.");
            }
            const char *canonicalState = canonicalStateForScenario(options.scenarioId);
            if (!options.canonicalStatePath.empty() && options.canonicalStatePath != canonicalState)
            {
                throw std::invalid_argument("webgl_loader_stl canonical-state differs from the formal Manifest.");
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

        /** Builds the exact ground PlaneGeometry position, normal, and source winding arrays. */
        void buildGroundGeometry(LoaderStlEntityState &entity)
        {
            entity.vertices = {
                {{-20.0f, 20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                {{20.0f, 20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                {{-20.0f, -20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                {{20.0f, -20.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
            };
            entity.indices = {0u, 2u, 1u, 2u, 3u, 1u};
            entity.sourceVertexCount = 4u;
            entity.sourceFaceCount = 2u;
        }

        /** Packs one parsed non-indexed STL asset with source winding and preserved facet attributes. */
        void buildStlEntityGeometry(const StlAsset &asset, LoaderStlEntityState &entity)
        {
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve(asset.vertices.size());
            entity.indices.reserve(asset.vertices.size());
            for (size_t faceOffset = 0u; faceOffset < asset.vertices.size(); faceOffset += 3u)
            {
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const StlAssetVertex &source = asset.vertices[faceOffset + corner];
                    entity.vertices.push_back({
                        .position = {source.position.x, source.position.y, source.position.z, 1.0f},
                        .normal = {source.normal.x, source.normal.y, source.normal.z, 0.0f},
                        .color = {source.color.x, source.color.y, source.color.z, 1.0f},
                    });
                    entity.indices.push_back(static_cast<uint32_t>(entity.indices.size()));
                }
            }
            entity.sourceVertexCount = static_cast<uint32_t>(asset.vertices.size());
            entity.sourceFaceCount = asset.facetCount;
        }

        /** Returns the reflected Three model matrix for one XYZ Euler transform. */
        glm::mat4 makeEntityModel(
            const glm::vec3 &position,
            const glm::vec3 &rotation,
            float uniformScale)
        {
            glm::mat4 sourceModel(1.0f);
            sourceModel = glm::translate(sourceModel, position);
            sourceModel = glm::rotate(sourceModel, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
            sourceModel = glm::rotate(sourceModel, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            sourceModel = glm::rotate(sourceModel, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
            sourceModel = glm::scale(sourceModel, glm::vec3(uniformScale));
            return makeHostReflection() * sourceModel;
        }

        /** Initializes all static transforms, materials, phases, and shadow cameras. */
        void initializeEntityComponents(eastl::array<LoaderStlEntityState, 5u> &entities)
        {
            const glm::mat4 shadowViewProjection0 = makeShadowViewProjection(glm::dvec3(1.0, 1.0, 1.0));
            const glm::mat4 shadowViewProjection1 = makeShadowViewProjection(glm::dvec3(0.5, 1.0, -1.0));

            entities[0u].logicalId = "ground";
            entities[0u].storagePrefix = "WebglLoaderStlGround";
            entities[0u].objectData.model = makeEntityModel(
                glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(-Pi * 0.5, 0.0, 0.0), 1.0f);
            entities[0u].materialData = {
                .baseColor = {srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), srgbByteToLinear(0xcbu), 1.0f},
                .specularAndSurfaceParameter = {srgbByteToLinear(0x47u), srgbByteToLinear(0x47u), srgbByteToLinear(0x47u), 30.0f},
            };
            entities[0u].renderFlags.values = {0u, 0u, 1u, 0u};

            entities[1u].logicalId = "ascii-slotted-disk";
            entities[1u].storagePrefix = "WebglLoaderStlAsciiSlottedDisk";
            entities[1u].objectData.model = makeEntityModel(
                glm::vec3(0.0f, -0.25f, 0.6f), glm::vec3(0.0, -Pi * 0.5, 0.0), 0.5f);
            entities[1u].materialData = {
                .baseColor = {srgbByteToLinear(0xffu), srgbByteToLinear(0x9cu), srgbByteToLinear(0x7cu), 1.0f},
                .specularAndSurfaceParameter = {srgbByteToLinear(0x49u), srgbByteToLinear(0x49u), srgbByteToLinear(0x49u), 200.0f},
            };
            entities[1u].renderFlags.values = {0u, 1u, 1u, 1u};

            entities[2u].logicalId = "binary-head-pan";
            entities[2u].storagePrefix = "WebglLoaderStlBinaryHeadPan";
            entities[2u].objectData.model = makeEntityModel(
                glm::vec3(0.0f, -0.37f, -0.6f), glm::vec3(-Pi * 0.5, 0.0, 0.0), 2.0f);
            entities[2u].materialData = {
                .baseColor = {srgbByteToLinear(0xd5u), srgbByteToLinear(0xd5u), srgbByteToLinear(0xd5u), 1.0f},
                .specularAndSurfaceParameter = {srgbByteToLinear(0x49u), srgbByteToLinear(0x49u), srgbByteToLinear(0x49u), 200.0f},
            };
            entities[2u].renderFlags.values = {0u, 1u, 1u, 2u};

            entities[3u].logicalId = "binary-head-tilt";
            entities[3u].storagePrefix = "WebglLoaderStlBinaryHeadTilt";
            entities[3u].objectData.model = makeEntityModel(
                glm::vec3(0.136f, -0.37f, -0.6f), glm::vec3(-Pi * 0.5, 0.3, 0.0), 2.0f);
            entities[3u].materialData = entities[2u].materialData;
            entities[3u].renderFlags.values = {0u, 1u, 1u, 3u};

            entities[4u].logicalId = "binary-colored";
            entities[4u].storagePrefix = "WebglLoaderStlBinaryColored";
            entities[4u].objectData.model = makeEntityModel(
                glm::vec3(0.5f, 0.2f, 0.0f), glm::vec3(-Pi * 0.5, Pi * 0.5, 0.0), 0.3f);
            entities[4u].materialData = {
                .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
                .specularAndSurfaceParameter = {srgbByteToLinear(0x11u), srgbByteToLinear(0x11u), srgbByteToLinear(0x11u), 30.0f},
            };
            entities[4u].renderFlags.values = {1u, 1u, 1u, 4u};

            for (LoaderStlEntityState &entity : entities)
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
                throw std::overflow_error("webgl_loader_stl RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglLoaderStlRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        validateLoaderStlOptions(options);
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;

        const std::filesystem::path AssetPaths[4u] = {
            "models/stl/ascii/slotted_disk.stl",
            "models/stl/binary/pr2_head_pan.stl",
            "models/stl/binary/pr2_head_tilt.stl",
            "models/stl/binary/colored.stl",
        };
        eastl::array<StlAsset, 4u> assets;
        for (uint32_t assetIndex = 0u; assetIndex < assets.size(); ++assetIndex)
        {
            assets[assetIndex] = loadStlAsset(
                resolveStlAsset(options, AssetPaths[assetIndex]));
            if (assets[assetIndex].sha256 != AssetSha256[assetIndex] ||
                assets[assetIndex].facetCount != AssetFacetCounts[assetIndex])
                throw std::runtime_error("STL asset identity or parsed facet count differs from r185.");
            scenarioState.assetSha256[assetIndex] = assets[assetIndex].sha256;
        }

        buildGroundGeometry(entities[0u]);
        for (uint32_t assetIndex = 0u; assetIndex < assets.size(); ++assetIndex)
        {
            buildStlEntityGeometry(assets[assetIndex], entities[assetIndex + 1u]);
        }
        initializeEntityComponents(entities);
        updateCameraState(0u, options.width, options.height);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("webgl_loader_stl could not create its Scene RenderSet encoder.");
        for (LoaderStlEntityState &entity : entities)
        {
            entity.entityIndex = allocateSceneEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglLoaderStlRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderStlEntityState &entity) const
    {
        if (entity.vertices.empty() || entity.indices.empty() || entity.vertices.size() > std::numeric_limits<uint32_t>::max() || entity.indices.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::out_of_range("webgl_loader_stl entity geometry has an invalid draw range.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::vertices, entity.storagePrefix + "Vertices", entity.vertices.data(), entity.vertices.size() * sizeof(LoaderStlHostVertex), 1u);
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::indices, entity.storagePrefix + "Indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::objects, entity.storagePrefix + "Object", &entity.objectData, sizeof(entity.objectData), 1u);
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::instances, entity.storagePrefix + "Instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::materials, entity.storagePrefix + "Material", &entity.materialData, sizeof(entity.materialData), 1u);
        appendBufferPayload(allocation, WebglLoaderStlSceneRenderSetComponents::renderFlags, entity.storagePrefix + "RenderFlags", &entity.renderFlags, sizeof(entity.renderFlags), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderStlRuntimeAdapter::updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument("webgl_loader_stl capture dimensions must be positive.");
        }
        const double virtualMilliseconds = double(frameIndex) * ReferenceFrameStepMilliseconds;
        const double timer = (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.0005;
        scenarioState.cameraX = std::cos(timer) * 3.0;
        scenarioState.cameraY = 0.15;
        scenarioState.cameraZ = std::sin(timer) * 3.0;

        const glm::dvec3 sourceCamera(scenarioState.cameraX, scenarioState.cameraY, scenarioState.cameraZ);
        const glm::dvec3 sourceTarget(0.0, -0.25, 0.0);
        const glm::dvec3 hostCamera(sourceCamera.x, -sourceCamera.y, sourceCamera.z);
        const glm::dvec3 hostTarget(sourceTarget.x, -sourceTarget.y, sourceTarget.z);
        const glm::dvec3 hostForward = glm::normalize(hostTarget - hostCamera);
        const glm::mat4 view = glm::mat4(glm::lookAt(hostCamera, hostTarget, glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 projection = makePerspectiveProjection(35.0, double(width) / double(height), 1.0, 15.0);
        const glm::mat4 viewProjection = projection * view;
        for (LoaderStlEntityState &entity : entities)
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

    void WebglLoaderStlRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        updateCameraState(frameIndex, options.width, options.height);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_loader_stl could not create its camera update encoder.");
        }
        for (const LoaderStlEntityState &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex, WebglLoaderStlSceneRenderSetComponents::objects, &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderStlRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_loader_stl capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderStlRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglLoaderStlRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
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
            throw std::runtime_error("Could not open webgl_loader_stl RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete webgl_loader_stl RGBA capture.");
        }
    }

    void WebglLoaderStlRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
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
            throw std::runtime_error("Could not open webgl_loader_stl metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_stl\",\n"
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

    void WebglLoaderStlRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
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
            throw std::runtime_error("Could not open webgl_loader_stl structural snapshot path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_loader_stl\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 5,\n"
               << "  \"entityCount\": 5,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 4,\n"
               << "  \"scenePassCount\": 2,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"directional-shadow-depth\", \"entityOrdinal\": 0},\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"directional-shadow-depth\", \"entityOrdinal\": 1},\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"main-phong-fog\", \"entityOrdinal\": 0}\n"
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
               << "  \"stlFacetCounts\": [288,1000,1052,2156],\n"
               << "  \"stlAssetSha256\": [\"" << scenarioState.assetSha256[0u].c_str()
               << "\",\"" << scenarioState.assetSha256[1u].c_str()
               << "\",\"" << scenarioState.assetSha256[2u].c_str()
               << "\",\"" << scenarioState.assetSha256[3u].c_str() << "\"],\n"
               << "  \"normalGeneration\": \"stl-facet-normal\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglLoaderStlSceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 5,\n"
               << "      \"entityCount\": 5,\n"
               << "      \"entities\": [\n"
               << "        {\"entityId\": " << entities[0u].entityIndex << ", \"logicalRenderableId\": \"ground\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[1u].entityIndex << ", \"logicalRenderableId\": \"ascii-slotted-disk\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[2u].entityIndex << ", \"logicalRenderableId\": \"binary-head-pan\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[3u].entityIndex << ", \"logicalRenderableId\": \"binary-head-tilt\", \"instanceCount\": 1},\n"
               << "        {\"entityId\": " << entities[4u].entityIndex << ", \"logicalRenderableId\": \"binary-colored\", \"instanceCount\": 1}\n"
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
               << "        {\"name\": \"directional-shadow-depth\", \"renderClass\": \"WebglLoaderStlShadowDepthPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 2, \"drawCommandCount\": 2, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-phong-fog\", \"renderClass\": \"WebglLoaderStlPhongPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ]\n"
               << "}\n";
    }

    void WebglLoaderStlRuntimeAdapter::writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
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
            throw std::runtime_error("Could not open webgl_loader_stl semantic snapshot path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_stl\",\n"
               << "  \"scenarioId\": \"canonical-loader\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"ascii-288-binary-1000-1052-colored-2156-facets\",\n"
               << "  \"result\": {\n"
               << "    \"renderableObjectCount\": 4,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"canonicalSceneSha256\": \"8dc12deefcf35372ea84de676dd8791f3aaf7fb6f98c58c23575fe5338bc103b\",\n"
               << "    \"assets\": [\n"
               << "      {\"encoding\":\"ascii\",\"facetCount\":288,\"sha256\":\"" << scenarioState.assetSha256[0u].c_str() << "\"},\n"
               << "      {\"encoding\":\"binary_little_endian\",\"facetCount\":1000,\"sha256\":\"" << scenarioState.assetSha256[1u].c_str() << "\"},\n"
               << "      {\"encoding\":\"binary_little_endian\",\"facetCount\":1052,\"sha256\":\"" << scenarioState.assetSha256[2u].c_str() << "\"},\n"
               << "      {\"encoding\":\"binary_little_endian_magics_color\",\"facetCount\":2156,\"sha256\":\"" << scenarioState.assetSha256[3u].c_str() << "\"}\n"
               << "    ]\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
