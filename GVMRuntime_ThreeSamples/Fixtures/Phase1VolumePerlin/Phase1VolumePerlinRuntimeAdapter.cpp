#include "Phase1VolumePerlinRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t VolumeExtent = 128u;
        constexpr uint32_t SliceStride = 130u;
        constexpr uint32_t AtlasColumns = 16u;
        constexpr uint32_t AtlasWidth = SliceStride * AtlasColumns;
        constexpr uint32_t AtlasHeight = SliceStride * 8u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(Phase1VolumePerlinHostVertex) == 16u);
        static_assert(sizeof(Phase1VolumePerlinHostObjectData) == 16u);
        static_assert(sizeof(Phase1VolumePerlinHostInstanceData) == 16u);
        static_assert(sizeof(Phase1VolumePerlinHostMaterialData) == 16u);

        /** Returns the generated vertex component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeVertexComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::vertices;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::vertices;
#endif
        }

        /** Returns the generated index component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeIndexComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::indices;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::indices;
#endif
        }

        /** Returns the generated object component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeObjectComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::objects;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::objects;
#endif
        }

        /** Returns the generated instance component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeInstanceComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::instances;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::instances;
#endif
        }

        /** Returns the generated material component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeMaterialComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::materials;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::materials;
#endif
        }

        /** Returns the generated texture component for the selected volume shard. */
        GVM::Core::RenderComponentHandle volumeTextureComponent()
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            return WebglVolumePerlinSceneRenderSetComponents::textures;
#else
            return WebgpuVolumePerlinSceneRenderSetComponents::textures;
#endif
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareVolumeOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Appends one typed payload to the volume RenderSet allocation. */
        void appendVolumeBufferPayload(
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

        /** Returns one fixed duplicated Three r185 ImprovedNoise permutation entry. */
        uint32_t improvedNoisePermutation(uint32_t index)
        {
            static constexpr uint8_t Permutation[256u] = {
                151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
                140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
                247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
                57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,
                175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,
                229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
                102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,
                89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,
                173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,
                255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
                223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,
                155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,
                232,178,185,112,104,218,246,97,228,251,34,242,193,238,210,
                144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,
                192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,
                127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
                128,195,78,66,215,61,156,180
            };
            return Permutation[index & 255u];
        }

        /** Evaluates Three r185 ImprovedNoise's quintic interpolation weight. */
        double improvedNoiseFade(double value)
        {
            return value * value * value *
                   (value * (value * 6.0 - 15.0) + 10.0);
        }

        /** Evaluates Three r185 ImprovedNoise's selected lattice gradient. */
        double improvedNoiseGradient(
            uint32_t hash,
            double x,
            double y,
            double z)
        {
            const uint32_t h = hash & 15u;
            const double u = h < 8u ? x : y;
            const double v =
                h < 4u ? y : (h == 12u || h == 14u ? x : z);
            return ((h & 1u) == 0u ? u : -u) +
                   ((h & 2u) == 0u ? v : -v);
        }

        /** Linearly interpolates two JavaScript-double noise values. */
        double improvedNoiseLerp(double first, double second, double weight)
        {
            return (1.0 - weight) * first + weight * second;
        }

        /** Evaluates one exact JavaScript-double Three r185 ImprovedNoise sample. */
        double improvedNoise(double x, double y, double z)
        {
            const double floorX = std::floor(x);
            const double floorY = std::floor(y);
            const double floorZ = std::floor(z);
            const uint32_t latticeX =
                static_cast<uint32_t>(static_cast<int64_t>(floorX)) & 255u;
            const uint32_t latticeY =
                static_cast<uint32_t>(static_cast<int64_t>(floorY)) & 255u;
            const uint32_t latticeZ =
                static_cast<uint32_t>(static_cast<int64_t>(floorZ)) & 255u;
            x -= floorX;
            y -= floorY;
            z -= floorZ;
            const double u = improvedNoiseFade(x);
            const double v = improvedNoiseFade(y);
            const double w = improvedNoiseFade(z);
            const uint32_t a =
                improvedNoisePermutation(latticeX) + latticeY;
            const uint32_t aa =
                improvedNoisePermutation(a) + latticeZ;
            const uint32_t ab =
                improvedNoisePermutation(a + 1u) + latticeZ;
            const uint32_t b =
                improvedNoisePermutation(latticeX + 1u) + latticeY;
            const uint32_t ba =
                improvedNoisePermutation(b) + latticeZ;
            const uint32_t bb =
                improvedNoisePermutation(b + 1u) + latticeZ;
            return improvedNoiseLerp(
                improvedNoiseLerp(
                    improvedNoiseLerp(
                        improvedNoiseGradient(
                            improvedNoisePermutation(aa), x, y, z),
                        improvedNoiseGradient(
                            improvedNoisePermutation(ba), x - 1.0, y, z),
                        u),
                    improvedNoiseLerp(
                        improvedNoiseGradient(
                            improvedNoisePermutation(ab), x, y - 1.0, z),
                        improvedNoiseGradient(
                            improvedNoisePermutation(bb), x - 1.0, y - 1.0, z),
                        u),
                    v),
                improvedNoiseLerp(
                    improvedNoiseLerp(
                        improvedNoiseGradient(
                            improvedNoisePermutation(aa + 1u), x, y, z - 1.0),
                        improvedNoiseGradient(
                            improvedNoisePermutation(ba + 1u), x - 1.0, y, z - 1.0),
                        u),
                    improvedNoiseLerp(
                        improvedNoiseGradient(
                            improvedNoisePermutation(ab + 1u), x, y - 1.0, z - 1.0),
                        improvedNoiseGradient(
                            improvedNoisePermutation(bb + 1u), x - 1.0, y - 1.0, z - 1.0),
                        u),
                    v),
                w);
        }

        /** Builds the exact 128-cubed R8 noise field in a guttered 16-by-8 atlas. */
        eastl::vector<uint8_t> buildVolumeAtlas()
        {
            eastl::vector<uint8_t> volume(
                size_t(VolumeExtent) * VolumeExtent * VolumeExtent);
            for (uint32_t z = 0u; z < VolumeExtent; ++z)
            {
                for (uint32_t y = 0u; y < VolumeExtent; ++y)
                {
                    for (uint32_t x = 0u; x < VolumeExtent; ++x)
                    {
                        const double noise = improvedNoise(
                            double(x) / VolumeExtent * 6.5,
                            double(y) / VolumeExtent * 6.5,
                            double(z) / VolumeExtent * 6.5);
                        const double encoded =
                            std::clamp(noise * 128.0 + 128.0, 0.0, 255.0);
                        const size_t index =
                            size_t(x) +
                            size_t(y) * VolumeExtent +
                            size_t(z) * VolumeExtent * VolumeExtent;
                        volume[index] = static_cast<uint8_t>(encoded);
                    }
                }
            }
            eastl::vector<uint8_t> atlas(
                size_t(AtlasWidth) * AtlasHeight * 4u,
                0u);
            for (uint32_t z = 0u; z < VolumeExtent; ++z)
            {
                const uint32_t originX =
                    (z % AtlasColumns) * SliceStride;
                const uint32_t originY =
                    (z / AtlasColumns) * SliceStride;
                for (uint32_t atlasY = 0u; atlasY < SliceStride; ++atlasY)
                {
                    const uint32_t y =
                        std::clamp<int32_t>(
                            static_cast<int32_t>(atlasY) - 1,
                            0,
                            static_cast<int32_t>(VolumeExtent - 1u));
                    for (uint32_t atlasX = 0u; atlasX < SliceStride; ++atlasX)
                    {
                        const uint32_t x =
                            std::clamp<int32_t>(
                                static_cast<int32_t>(atlasX) - 1,
                                0,
                                static_cast<int32_t>(VolumeExtent - 1u));
                        const uint8_t value =
                            volume[
                                size_t(x) +
                                size_t(y) * VolumeExtent +
                                size_t(z) * VolumeExtent * VolumeExtent];
                        const size_t destination =
                            (size_t(originX + atlasX) +
                             size_t(originY + atlasY) * AtlasWidth) *
                            4u;
                        atlas[destination + 0u] = value;
                        atlas[destination + 1u] = value;
                        atlas[destination + 2u] = value;
                        atlas[destination + 3u] = 255u;
                    }
                }
            }
            return atlas;
        }

        /** Builds the canonical indexed BoxGeometry with outward winding. */
        void buildVolumeBox(
            eastl::vector<Phase1VolumePerlinHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices = {
                {{-0.5f, -0.5f, -0.5f, 1.0f}},
                {{0.5f, -0.5f, -0.5f, 1.0f}},
                {{0.5f, 0.5f, -0.5f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f, 1.0f}},
                {{0.5f, -0.5f, 0.5f, 1.0f}},
                {{0.5f, 0.5f, 0.5f, 1.0f}},
                {{-0.5f, 0.5f, 0.5f, 1.0f}},
            };
            indices = {
                4u,5u,6u,4u,6u,7u,
                1u,0u,3u,1u,3u,2u,
                5u,1u,2u,5u,2u,6u,
                0u,4u,7u,0u,7u,3u,
                7u,6u,2u,7u,2u,3u,
                0u,1u,5u,0u,5u,4u,
            };
        }

        /** Replays the canonical OrbitControls drag into an orthonormal camera basis. */
        Phase1VolumePerlinHostFrame makeVolumeFrame(
            const ThreeSampleHostOptions &options)
        {
            constexpr double Pi = 3.14159265358979323846;
            const bool settings = !options.inputReplayPath.empty();
            double theta = 0.0;
            double phi = Pi * 0.5;
            if (settings)
            {
                theta -= 2.0 * Pi * 70.0 / 500.0;
                phi -= 2.0 * Pi * -35.0 / 500.0;
            }
            const glm::vec3 cameraPosition(
                float(2.0 * std::sin(phi) * std::sin(theta)),
                float(2.0 * std::cos(phi)),
                float(2.0 * std::sin(phi) * std::cos(theta)));
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right =
                glm::normalize(
                    glm::cross(
                        forward,
                        glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up =
                glm::normalize(glm::cross(right, forward));
            Phase1VolumePerlinHostFrame value;
            value.cameraPosition = glm::vec4(cameraPosition, 1.0f);
            value.cameraRight = glm::vec4(right, 0.0f);
            value.cameraUp = glm::vec4(up, 0.0f);
            value.cameraForward = glm::vec4(forward, 0.0f);
            value.raymarch =
                glm::vec4(
                    0.6f,
                    200.0f,
                    0.0f,
                    0.0f);
            value.viewportAndOverlay =
                glm::vec4(
                    float(options.width),
                    float(options.height),
                    0.0f,
                    0.0f);
            return value;
        }

        /** Writes one tightly packed RGBA8 volume capture file. */
        void writeVolumeRgba(
            const eastl::string &pathValue,
            const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty())
            {
                return;
            }
            const std::filesystem::path path(pathValue.c_str());
            prepareVolumeOutputPath(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the Phase 1 volume RGBA capture.");
            }
        }
    } // namespace

    void Phase1VolumePerlinRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        device = inDevice;
        caseId = options.caseId;
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
        if (caseId != "webgl_volume_perlin")
        {
            throw std::invalid_argument(
                "The WebGL volume shard received an unexpected case.");
        }
#else
        if (caseId != "webgpu_volume_perlin")
        {
            throw std::invalid_argument(
                "The WebGPU volume shard received an unexpected case.");
        }
#endif
        frame = makeVolumeFrame(options);
        buildVolumeBox(vertices, indices);
        atlasBytes = buildVolumeAtlas();
        const Phase1VolumePerlinHostObjectData objectData = {};
        const Phase1VolumePerlinHostInstanceData instanceData = {};
        const Phase1VolumePerlinHostMaterialData materialData = {};

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the dedicated volume RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendVolumeBufferPayload(
            allocation,
            volumeVertexComponent(),
            "Phase1VolumePerlinVertices",
            vertices.data(),
            vertices.size() * sizeof(Phase1VolumePerlinHostVertex));
        appendVolumeBufferPayload(
            allocation,
            volumeIndexComponent(),
            "Phase1VolumePerlinIndices",
            indices.data(),
            indices.size() * sizeof(uint32_t));
        appendVolumeBufferPayload(
            allocation,
            volumeObjectComponent(),
            "Phase1VolumePerlinObject",
            &objectData,
            sizeof(objectData));
        appendVolumeBufferPayload(
            allocation,
            volumeInstanceComponent(),
            "Phase1VolumePerlinInstance",
            &instanceData,
            sizeof(instanceData));
        appendVolumeBufferPayload(
            allocation,
            volumeMaterialComponent(),
            "Phase1VolumePerlinMaterial",
            &materialData,
            sizeof(materialData));
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            volumeTextureComponent();
        textureComponent.textures.push_back({
            .textureName = "Phase1VolumePerlinGutteredAtlas",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .width = AtlasWidth,
            .height = AtlasHeight,
            .data = atlasBytes.data(),
            .dataStorageBytes = atlasBytes.size(),
            .mipmapOffsetBytes = {0u},
        });
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void Phase1VolumePerlinRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void Phase1VolumePerlinRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "The volume capture is too large.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void Phase1VolumePerlinRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        writeVolumeRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(
                options.captureMetadataPath.c_str());
            prepareVolumeOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"";
            if (!options.inputReplayPath.empty())
            {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
                constexpr const char *ReplaySha256 =
                    "57a03f042333fbb74266cda1aeaf0364338c55a19c953c4a83bce063a885fdb1";
                constexpr const char *ReplayTarget = "canvas";
#else
                constexpr const char *ReplaySha256 =
                    "b0612276c4434807daf311e23d846cf6c97bf1328e296e97d7f4caaba8353d70";
                constexpr const char *ReplayTarget =
                    "canvas:not([class])";
#endif
                output
                    << ",\n  \"inputReplay\":{\"schemaVersion\":1,"
                    << "\"caseId\":\"" << caseId.c_str()
                    << "\",\"scenarioId\":\""
                    << options.scenarioId.c_str()
                    << "\",\"captureFrame\":" << frameIndex
                    << ",\"sha256\":\"" << ReplaySha256
                    << "\",\"target\":\"" << ReplayTarget
                    << "\",\"eventCount\":3}\n";
            }
            else
            {
                output << '\n';
            }
            output << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
#if defined(GVM_WEBGL_VOLUME_PERLIN_ADAPTER)
            constexpr const char *RenderSetTypeName =
                "WebglVolumePerlinSceneRenderSet";
            constexpr const char *RenderClassName =
                "WebglVolumePerlinScenePass";
            constexpr const char *ResolveClassName =
                "WebglVolumePerlinResolvePass";
            constexpr uint32_t ScreenPassCount = 1u;
#else
            constexpr const char *RenderSetTypeName =
                "WebgpuVolumePerlinSceneRenderSet";
            constexpr const char *RenderClassName =
                "WebgpuVolumePerlinScenePass";
            constexpr const char *ResolveClassName =
                "WebgpuVolumePerlinResolvePass";
            constexpr uint32_t ScreenPassCount = 2u;
#endif
            const std::filesystem::path path(
                options.sceneSnapshotPath.c_str());
            prepareVolumeOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output
                << "{\n  \"caseId\":\"" << caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"strict-pass\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"assetBacked\":false,\n"
                << "  \"assetHashes\":[],\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":1,\n"
                << "  \"instanceCounts\":[1],\n"
                << "  \"componentSchema\":[\"vertices\",\"indices\","
                   "\"objects\",\"instances\",\"materials\",\"textures\"],\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"screenPassCount\":" << ScreenPassCount << ",\n"
                << "  \"scenePasses\":[\"" << RenderClassName << "\"],\n"
                << "  \"screenPasses\":[\"" << ResolveClassName << "\""
#if defined(GVM_WEBGPU_VOLUME_PERLIN_ADAPTER)
                << ",\"WebgpuVolumePerlinInspectorPass\""
#endif
                << "],\n"
                << "  \"attachmentFormats\":[\"rgba8unorm\"],\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"renderSetIndexedIndirect\":true,\n"
                << "  \"directDrawFallback\":false,\n"
                << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                   "\"scenePass\":\"perlin-volume\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene\",\n"
                << "    \"renderSetType\":\"" << RenderSetTypeName << "\",\n"
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
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "    \"scenePasses\":[{\"name\":\"perlin-volume\","
                << "\"renderClass\":\"" << RenderClassName << "\","
                   "\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,"
                   "\"drawMode\":\"render-set-indexed-indirect\","
                   "\"invocationCount\":1,\"drawCommandCount\":1,"
                   "\"usesStandaloneGeometry\":false,"
                   "\"usesExplicitDrawCount\":false}],\n"
                << "    \"entities\":[{\"entityId\":" << entityIndex
                << ",\"logicalRenderableId\":\"volume-box\","
                   "\"instanceCount\":1}]\n"
                << "  }]\n}\n";
        }
    }

    void Phase1VolumePerlinRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        atlasBytes.clear();
    }
} // namespace GVM::ThreeSamples
