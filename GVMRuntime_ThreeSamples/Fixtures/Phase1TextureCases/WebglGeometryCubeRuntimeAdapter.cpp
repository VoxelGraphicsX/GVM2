#include "WebglGeometryCubeRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

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
        constexpr uint32_t CrateExtent = 256u;
        constexpr uint32_t CrateMipCount = 9u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

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
                throw std::overflow_error("webgl_geometry_cube RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the two locked scenario identifiers and their exact target frames. */
        void validateScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_geometry_cube")
            {
                throw std::invalid_argument("WebglGeometryCube runtime adapter requires case-id webgl_geometry_cube.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool rotated = options.scenarioId == "rotated" && options.targetFrame == 120u;
            if (!initial && !rotated)
            {
                throw std::invalid_argument("webgl_geometry_cube requires initial/frame 0 or rotated/frame 120.");
            }
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

        /** Appends the locked sRGB crate mip chain to one entity TextureComponent slot. */
        void appendCrateTexturePayload(GVM::Core::RenderSetAllocInfo &allocation, const eastl::vector<uint8_t> &textureBytes, const eastl::vector<uint64_t> &mipOffsets)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglGeometryCubeSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebglGeometryCubeCrateSrgb",
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = CrateExtent,
                .height = CrateExtent,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = mipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }
    } // namespace

    void WebglGeometryCubeRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        validateScenario(options);
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument("webgl_geometry_cube requires explicit --asset-root containing textures/crate.gif.");
        }

        device = inDevice;
        buildTexturedBoxGeometry(1.0f, 1.0f, 1.0f, vertices, indices);

        const std::filesystem::path cratePath = std::filesystem::path(options.assetRoot.c_str()) / "textures" / "crate.gif";
        const RgbaImageData crate = decodeGifRgba8(cratePath);
        if (crate.width != CrateExtent || crate.height != CrateExtent)
        {
            throw std::runtime_error("textures/crate.gif must decode to the locked 256x256 r185 asset.");
        }
        const eastl::vector<RgbaImageData> mipChain = buildSrgbMipChain(crate);
        if (mipChain.size() != CrateMipCount)
        {
            throw std::runtime_error("textures/crate.gif did not produce the expected nine mip levels.");
        }

        crateTextureBytes.clear();
        crateMipOffsets.clear();
        for (const RgbaImageData &mip : mipChain)
        {
            crateMipOffsets.push_back(crateTextureBytes.size());
            crateTextureBytes.insert(crateTextureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }
        allocateSceneEntity(renderer, options, options.targetFrame);
    }

    void WebglGeometryCubeRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        const auto [rotationX, rotationY] = calculateTexturedBoxFrameRotation(frameIndex, 0.005, 0.01);
        glm::mat4 view(1.0f);
        view[3u][2u] = -2.0f;
        const glm::mat4 projection = makeThreePerspectiveProjection(options.width, options.height, 70.0, 0.1, 100.0);
        const TexturedBoxHostObjectData objectData = {
            .modelViewProjection = projection * view * makeThreeEulerXyRotation(-rotationX, rotationY),
            .materialAndFlags = {0u, 0u, 0u, 0u},
        };
        const TexturedBoxHostInstanceData instanceData = {
            .tint = {1.0f, 1.0f, 1.0f, 1.0f},
        };
        const TexturedBoxHostMaterialData materialData = {
            .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("webgl_geometry_cube could not create its Scene RenderSet command encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglGeometryCubeSceneRenderSetComponents::vertices, "WebglGeometryCubeVertices", vertices.data(), vertices.size() * sizeof(TexturedBoxHostVertex), 1u);
        appendBufferPayload(allocation, WebglGeometryCubeSceneRenderSetComponents::indices, "WebglGeometryCubeIndices", indices.data(), indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglGeometryCubeSceneRenderSetComponents::objects, "WebglGeometryCubeObject", &objectData, sizeof(objectData), 1u);
        appendBufferPayload(allocation, WebglGeometryCubeSceneRenderSetComponents::instances, "WebglGeometryCubeInstance", &instanceData, sizeof(instanceData), 1u);
        appendBufferPayload(allocation, WebglGeometryCubeSceneRenderSetComponents::materials, "WebglGeometryCubeMaterial", &materialData, sizeof(materialData), 1u);
        appendCrateTexturePayload(allocation, crateTextureBytes, crateMipOffsets);

        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryCubeRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryCubeRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }

        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_geometry_cube capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGeometryCubeRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglGeometryCubeRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
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
            throw std::runtime_error("Could not open webgl_geometry_cube RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete webgl_geometry_cube RGBA capture.");
        }
    }

    void WebglGeometryCubeRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
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
            throw std::runtime_error("Could not open webgl_geometry_cube metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_geometry_cube\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglGeometryCubeRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const auto [rotationX, rotationY] = calculateTexturedBoxFrameRotation(frameIndex, 0.005, 0.01);
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_geometry_cube snapshot output path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_geometry_cube\",\n"
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
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"indexedVertexCount\": 36,\n"
               << "  \"boxVertexCount\": 24,\n"
               << "  \"texturePath\": \"textures/crate.gif\",\n"
               << "  \"textureColorSpace\": \"srgb\",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 70,\n"
               << "  \"cameraNear\": 0.1,\n"
               << "  \"cameraFar\": 100,\n"
               << "  \"cameraPositionZ\": 2,\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglGeometryCubeSceneRenderSet\",\n"
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
               << "        {\"name\":\"main\",\"renderClass\":\"WebglGeometryCubeScenePass\","
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
