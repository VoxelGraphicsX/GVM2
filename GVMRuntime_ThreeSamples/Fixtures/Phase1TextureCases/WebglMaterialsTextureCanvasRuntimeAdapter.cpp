#include "WebglMaterialsTextureCanvasRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <EASTL/array.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr eastl::array<uint8_t, 4u> WhiteTexture = {255u, 255u, 255u, 255u};

        /** Mirrors the 96-byte CanvasTexture material component emitted by both UGLC pipelines. */
        struct alignas(16) WebglMaterialsTextureCanvasHostMaterialData
        {
            TexturedBoxHostFloat4 baseColor;
            TexturedBoxHostFloat4 segment0;
            TexturedBoxHostFloat4 segment1;
            TexturedBoxHostFloat4 segment2;
            TexturedBoxHostFloat4 segment3;
            TexturedBoxHostUint4 state;
        };

        static_assert(sizeof(WebglMaterialsTextureCanvasHostMaterialData) == 96u);

        /** Resolves the canonical replay against either its explicit path or the immutable asset pack. */
        std::filesystem::path resolveInputReplayPath(const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(options.inputReplayPath.c_str());
            if (requested.is_absolute() && std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() && std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) / requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument("painted CanvasTexture scenario could not resolve its explicit --input-replay.");
        }

        /** Validates the locked CanvasTexture scenario and exact target frame pair. */
        void validateScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_materials_texture_canvas")
            {
                throw std::invalid_argument("CanvasTexture runtime adapter requires case-id webgl_materials_texture_canvas.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool painted = options.scenarioId == "painted" && options.targetFrame == 30u;
            if (!initial && !painted)
            {
                throw std::invalid_argument("webgl_materials_texture_canvas requires initial/frame 0 or painted/frame 30.");
            }
            if (initial && !options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Initial CanvasTexture scenario must not consume input replay.");
            }
            if (painted && options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Painted CanvasTexture scenario requires --input-replay.");
            }
        }

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("CanvasTexture RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Converts one GLM replay segment into the explicit RenderSet component ABI. */
        TexturedBoxHostFloat4 convertReplaySegment(const glm::vec4 &segment)
        {
            return {segment.x, segment.y, segment.z, segment.w};
        }

        /** Packs replay segments and material color into the CanvasTexture material component. */
        WebglMaterialsTextureCanvasHostMaterialData makeCanvasMaterialData(const CanvasTextureReplayResult &replay)
        {
            return {
                .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
                .segment0 = convertReplaySegment(replay.segments[0u]),
                .segment1 = convertReplaySegment(replay.segments[1u]),
                .segment2 = convertReplaySegment(replay.segments[2u]),
                .segment3 = convertReplaySegment(replay.segments[3u]),
                .state = {replay.activeSegmentCount, 0u, 0u, 0u},
            };
        }

        /** Appends one typed payload to a RenderSet buffer component allocation. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const char *name, const void *value, uint64_t byteCount, uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends the entity-owned white base texture used by the analytic Canvas shader. */
        void appendBaseTexturePayload(GVM::Core::RenderSetAllocInfo &allocation)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglMaterialsTextureCanvasSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebglMaterialsTextureCanvasBase",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 1u,
                .height = 1u,
                .data = WhiteTexture.data(),
                .dataStorageBytes = WhiteTexture.size(),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }
    } // namespace

    void WebglMaterialsTextureCanvasRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        validateScenario(options);
        device = inDevice;
        replayResult = options.scenarioId == "painted" ? parseCanonicalCanvasTextureReplay(resolveInputReplayPath(options)) : createInitialCanvasTextureReplay();
        buildTexturedBoxGeometry(200.0f, 200.0f, 200.0f, vertices, indices);
        allocateSceneEntity(renderer, options, options.targetFrame);
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        const auto [rotationX, rotationY] = calculateTexturedBoxFrameRotation(frameIndex, 0.01, 0.01);
        glm::mat4 view(1.0f);
        view[3u][2u] = -500.0f;
        const glm::mat4 projection = makeThreePerspectiveProjection(options.width, options.height, 50.0, 1.0, 2000.0);
        const TexturedBoxHostObjectData objectData = {
            .modelViewProjection = projection * view * makeThreeEulerXyRotation(-rotationX, rotationY),
            .materialAndFlags = {0u, 0u, 0u, 0u},
        };
        const TexturedBoxHostInstanceData instanceData = {
            .tint = {1.0f, 1.0f, 1.0f, 1.0f},
        };
        const WebglMaterialsTextureCanvasHostMaterialData materialData = makeCanvasMaterialData(replayResult);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("CanvasTexture case could not create its Scene RenderSet command encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglMaterialsTextureCanvasSceneRenderSetComponents::vertices, "WebglMaterialsTextureCanvasVertices", vertices.data(), vertices.size() * sizeof(TexturedBoxHostVertex), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureCanvasSceneRenderSetComponents::indices, "WebglMaterialsTextureCanvasIndices", indices.data(), indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureCanvasSceneRenderSetComponents::objects, "WebglMaterialsTextureCanvasObject", &objectData, sizeof(objectData), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureCanvasSceneRenderSetComponents::instances, "WebglMaterialsTextureCanvasInstance", &instanceData, sizeof(instanceData), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureCanvasSceneRenderSetComponents::materials, "WebglMaterialsTextureCanvasMaterial", &materialData, sizeof(materialData), 1u);
        appendBaseTexturePayload(allocation);

        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("CanvasTexture capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
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
            throw std::runtime_error("Could not open CanvasTexture RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete CanvasTexture RGBA capture.");
        }
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
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
            throw std::runtime_error("Could not open CanvasTexture metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_materials_texture_canvas\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!replayResult.painted)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"sha256\": \"" << replayResult.sha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_materials_texture_canvas\",\n"
                   << "    \"scenarioId\": \"painted\",\n"
                   << "    \"captureFrame\": " << replayResult.captureFrame << ",\n"
                   << "    \"eventCount\": " << replayResult.eventCount << ",\n"
                   << "    \"target\": \"" << replayResult.target.c_str() << "\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglMaterialsTextureCanvasRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const auto [rotationX, rotationY] = calculateTexturedBoxFrameRotation(frameIndex, 0.01, 0.01);
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open CanvasTexture snapshot output path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_materials_texture_canvas\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"entityCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"canvasGenerationPipeline\": \"dsl-fragment-analytic\",\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"indexedVertexCount\": 36,\n"
               << "  \"boxVertexCount\": 24,\n"
               << "  \"canvasWidth\": 128,\n"
               << "  \"canvasHeight\": 128,\n"
               << "  \"textureColorSpace\": \"none-linear\",\n"
               << "  \"canvasGenerationPassCount\": 0,\n"
               << "  \"inputReplayEventCount\": " << replayResult.eventCount << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 50,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 2000,\n"
               << "  \"cameraPositionZ\": 500,\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglMaterialsTextureCanvasSceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 1,\n"
               << "      \"entityCount\": 1,\n"
               << "      \"entities\": [{\"entityId\": " << entityIndex << ", \"logicalRenderableId\": \"box\", \"instanceCount\": 1}],\n"
               << "      \"componentSchema\": [\n"
               << "        {\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},\n"
               << "        {\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},\n"
               << "        {\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},\n"
               << "        {\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},\n"
               << "        {\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},\n"
               << "        {\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}\n"
               << "      ],\n"
               << "      \"drawCommandCount\": 1,\n"
               << "      \"directDrawFallback\": false,\n"
               << "      \"scenePasses\": [\n"
               << "        {\"name\":\"main\",\"renderClass\":\"WebglMaterialsTextureCanvasScenePass\","
                  "\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,"
                  "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                  "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
                  "\"usesExplicitDrawCount\":false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
