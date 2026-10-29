#include "WebgpuMaterialsBasicRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

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
        constexpr uint32_t EntityCount = 500u;
        constexpr uint32_t SphereSegments = 32u;
        constexpr uint32_t SphereRings = 16u;
        constexpr uint32_t CubeFaceCount = 6u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        // Keep the pre-sphere phase separate from the authored position/scale
        // stream so the deterministic capture remains reproducible.
        // The frozen sample contract consumes this deterministic prefix before
        // the first Mesh's authored position/scale values are requested.
        // The pinned r185 browser bootstrap consumes a deterministic prefix
        // before the first Mesh position/scale is sampled.  The four UUID
        // words below account for the remaining bootstrap draws in the
        // sample-local stream so the first authored position starts at draw
        // 205, matching the reference capture.
        constexpr uint32_t PreSphereRandomCalls = 200u;
        constexpr const char *CanonicalSceneSha256 =
            "b8276ca88369af967a2329792c9c3df202ce8c02612790ff3842eba3d0dd44b5";
        constexpr const char *RefractionReplaySha256 =
            "d17abe812b97532af604ffce0bf4dc0146cc6796263a1db075ff0db8c4788721";
        constexpr const char *TransparentReplaySha256 =
            "9ee61e882c080467447db0768bdbd3c17e35d567e46f9a3a2448419361ff5219";
        constexpr const char *CubeFaceNames[CubeFaceCount] = {
            "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

        /** Advances the shared Three.js xorshift32 stream and returns a [0,1) value. */
        float nextThreeRandom(uint32_t &state)
        {
            uint32_t value = state == 0u ? 0x6d2b79f5u : state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Creates parent directories for one deterministic capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Resolves the locked r185 examples directory from either accepted asset-root layout. */
        std::filesystem::path resolveExamplesRoot(const std::filesystem::path &assetRoot)
        {
            if (std::filesystem::exists(assetRoot / "textures" / "cube" / "pisa" / CubeFaceNames[0]))
                return assetRoot;
            const std::filesystem::path nested = assetRoot / "examples";
            if (std::filesystem::exists(nested / "textures" / "cube" / "pisa" / CubeFaceNames[0]))
                return nested;
            throw std::runtime_error("The asset pack does not contain the six Pisa cubemap PNG faces.");
        }

        /** Validates the fixed r185 cubemap scenarios and single-sample output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            // Use the frozen r185 Manifest scenario identifiers.  The legacy
            // aliases remain accepted so older local captures stay readable.
            const bool initial = options.scenarioId == "initial-reflection" &&
                options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool refraction = options.scenarioId == "refraction" && options.targetFrame == 61u;
            const bool transparent = options.scenarioId == "transparent-sorted" && options.targetFrame == 61u;
            if (options.caseId != "webgpu_materials_basic" || (!initial && !animated && !refraction && !transparent) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgpu_materials_basic requires its locked r185 scenario contract.");
        }

        /** Appends one typed RenderSet buffer payload. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
                          const void *value,
                          uint64_t bytes)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = bytes,
                .instanceCount = 1u,
            });
        }

        /** Builds one explicit sRGB mip chain and keeps its packed bytes alive. */
        void loadCubeFace(const std::filesystem::path &path,
                          eastl::vector<uint8_t> &bytes,
                          eastl::vector<uint64_t> &mipOffsets,
                          uint32_t &width,
                          uint32_t &height)
        {
            const RgbaImageData base = decodePngRgba8(path);
            const eastl::vector<RgbaImageData> mips = buildSrgbMipChain(base);
            if (mips.empty()) throw std::runtime_error("Cubemap face has no mip levels: " + path.string());
            width = base.width;
            height = base.height;
            bytes.clear();
            mipOffsets.clear();
            for (const RgbaImageData &mip : mips)
            {
                mipOffsets.push_back(bytes.size());
                bytes.insert(bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }

        /** Generates the r185 SphereGeometry(1,32,16) as a deterministic triangle list. */
        void buildSphereGeometry(eastl::vector<WebgpuMaterialsBasicHostVertex> &vertices,
                                 eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve((SphereSegments + 1u) * (SphereRings + 1u));
            for (uint32_t ring = 0u; ring <= SphereRings; ++ring)
            {
                const double v = double(ring) / double(SphereRings);
                const double theta = Pi * v;
                const double yDouble = 0.1 * std::cos(theta);
                const double ringRadius = std::sqrt(0.1 * 0.1 - yDouble * yDouble);
                const float y = float(yDouble);
                const float ringRadiusFloat = float(ringRadius);
                for (uint32_t segment = 0u; segment <= SphereSegments; ++segment)
                {
                    const float u = float(segment) / float(SphereSegments);
                    const double phi = 2.0 * double(Pi) * double(segment) / double(SphereSegments);
                    const glm::vec3 position(
                        -ringRadiusFloat * float(std::cos(phi)),
                        y,
                        ringRadiusFloat * float(std::sin(phi)));
                    const glm::vec3 normal = glm::normalize(position);
                    vertices.push_back({glm::vec4(position, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(u, 1.0f - float(v), 0.0f, 0.0f)});
                }
            }
            for (uint32_t ring = 0u; ring < SphereRings; ++ring)
            {
                for (uint32_t segment = 0u; segment < SphereSegments; ++segment)
                {
                    // Match Three's SphereGeometry grid winding exactly.  The
                    // pole rows intentionally omit their degenerate triangle;
                    // retaining it creates back-face gaps in a reflective mesh.
                    const uint32_t row = SphereSegments + 1u;
                    const uint32_t a = ring * row + segment + 1u;
                    const uint32_t b = ring * row + segment;
                    const uint32_t c = (ring + 1u) * row + segment;
                    const uint32_t d = (ring + 1u) * row + segment + 1u;
                    if (ring != 0u)
                    {
                        indices.push_back(a);
                        indices.push_back(b);
                        indices.push_back(d);
                    }
                    if (ring != SphereRings - 1u)
                    {
                        indices.push_back(b);
                        indices.push_back(c);
                        indices.push_back(d);
                    }
                }
            }
        }

        /** Writes the camera basis consumed by the DSL background ray reconstruction. */
        void updateCameraBasis(WebgpuMaterialsBasicHostObjectData &objectData,
                               const glm::vec3 &cameraPosition,
                               uint32_t width,
                               uint32_t height)
        {
            const glm::vec3 forward(0.0f, 0.0f, -1.0f);
            const glm::vec3 right(1.0f, 0.0f, 0.0f);
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            objectData.cameraRightAndTanHalfFov = glm::vec4(right, std::tan(glm::radians(30.0f)));
            objectData.cameraUpAndAspect = glm::vec4(up, float(width) / float(height));
            objectData.cameraForwardAndReserved = glm::vec4(forward, 0.0f);
        }

        /** Builds the right-handed Three.js perspective matrix independently of GLM handedness macros. */
        glm::mat4 makeThreePerspective(float verticalFieldOfViewRadians,
                                        float aspect,
                                        float nearPlane,
                                        float farPlane)
        {
            const float focalLength = 1.0f / std::tan(verticalFieldOfViewRadians * 0.5f);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = focalLength / aspect;
            projection[1u][1u] = focalLength;
            projection[2u][2u] = (farPlane + nearPlane) / (nearPlane - farPlane);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            return projection;
        }

        /** Converts an sRGB authored material channel to Three's linear working space. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Recomputes the deterministic perspective camera used by the r185 example. */
        void updateEntityMatrices(WebgpuMaterialsBasicEntityData &entity,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t frameIndex)
        {
            const glm::vec3 cameraPosition(0.0f, 0.0f, 3.0f);
            glm::mat4 view(1.0f);
            view[3u][2u] = -cameraPosition.z;
            const glm::mat4 projection = makeThreePerspective(
                glm::radians(60.0f), float(width) / float(height), 0.01f, 100.0f);
            entity.objectData = entity.baseObjectData;
            entity.objectData.modelView = view * entity.objectData.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPositionAndFlags.x = cameraPosition.x;
            entity.objectData.cameraPositionAndFlags.y = cameraPosition.y;
            entity.objectData.cameraPositionAndFlags.z = cameraPosition.z;
            updateCameraBasis(entity.objectData, cameraPosition, width, height);
        }
    } // namespace

    void WebgpuMaterialsBasicRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        const std::filesystem::path examplesRoot =
            resolveExamplesRoot(std::filesystem::path(options.assetRoot.c_str()));
        eastl::vector<WebgpuMaterialsBasicHostVertex> sphereVertices;
        eastl::vector<uint32_t> sphereIndices;
        buildSphereGeometry(sphereVertices, sphereIndices);
        eastl::array<eastl::vector<uint8_t>, CubeFaceCount> environmentBytes;
        eastl::array<eastl::vector<uint64_t>, CubeFaceCount> environmentMipOffsets;
        eastl::array<uint32_t, CubeFaceCount> environmentWidths = {};
        eastl::array<uint32_t, CubeFaceCount> environmentHeights = {};
        for (uint32_t face = 0u; face < CubeFaceCount; ++face)
            loadCubeFace(examplesRoot / "textures" / "cube" / "pisa" / CubeFaceNames[face],
                         environmentBytes[face], environmentMipOffsets[face],
                         environmentWidths[face], environmentHeights[face]);
        const bool isTransparent = options.scenarioId == "transparent-sorted";
        const bool isRefraction = options.scenarioId == "refraction";
        entities.clear();
        entities.resize(EntityCount);

        uint32_t randomState = options.randomSeed;
        for (uint32_t randomCall = 0u; randomCall < PreSphereRandomCalls; ++randomCall)
            (void)nextThreeRandom(randomState);
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            WebgpuMaterialsBasicEntityData &entity = entities[entityIndex];
            entity.vertices = sphereVertices;
            entity.indices = sphereIndices;
            // Preserve the four bootstrap draws between Mesh construction and
            // the authored position/scale values; this is the observed r185
            // random stream contract even though GVM allocates entities
            // directly.
            for (uint32_t uuidWord = 0u; uuidWord < 4u; ++uuidWord)
                (void)nextThreeRandom(randomState);
            entity.randomPosition = glm::vec3(
                nextThreeRandom(randomState) * 10.0f - 5.0f,
                nextThreeRandom(randomState) * 10.0f - 5.0f,
                nextThreeRandom(randomState) * 10.0f - 5.0f);
            entity.randomScale = nextThreeRandom(randomState) * 3.0f + 1.0f;
            entity.materialData.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, isTransparent ? 0.5f : 1.0f);
            entity.materialData.parameters = glm::vec4(
                1.0f,
                isRefraction ? 0.72f : 0.98f,
                isTransparent ? 1.0f : 0.0f,
                0.0f);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), entity.randomPosition) *
                glm::scale(glm::mat4(1.0f), glm::vec3(entity.randomScale));
            entity.baseObjectData.modelView = model;
            entity.baseObjectData.modelViewProjection = model;
            entity.baseObjectData.model = model;
            entity.baseObjectData.cameraPositionAndFlags = glm::vec4(0.0f, 0.0f, 3.0f, isRefraction ? 1.0f : 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            updateEntityMatrices(entity, options.width, options.height, options.targetFrame);
            if (entityIndex == 0u)
            {
                for (uint32_t face = 0u; face < CubeFaceCount; ++face)
                {
                    entity.textureBytes[face] = environmentBytes[face];
                    entity.textureMipOffsets[face] = environmentMipOffsets[face];
                    entity.textureWidths[face] = environmentWidths[face];
                    entity.textureHeights[face] = environmentHeights[face];
                }
            }
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_materials_basic could not create its Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            WebgpuMaterialsBasicEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("WebgpuMaterialsBasicSphere-") + eastl::to_string(entityIndex);
            appendBuffer(allocation, WebgpuMaterialsBasicSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendBuffer(allocation, WebgpuMaterialsBasicSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebgpuMaterialsBasicSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebgpuMaterialsBasicSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebgpuMaterialsBasicSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            // The current RenderSet texture ABI exposes eight total slots per
            // component.  The six immutable cube faces are uploaded once on
            // entity zero and sampled through the Scene-owned texture pool.
            if (entityIndex == 0u)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
                textureComponent.textureComponentHandle = WebgpuMaterialsBasicSceneRenderSetComponents::textures;
                for (uint32_t face = 0u; face < CubeFaceCount; ++face)
                {
                    textureComponent.textures.push_back({
                        .textureName = prefix + "-" + CubeFaceNames[face],
                        .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                        .width = entity.textureWidths[face],
                        .height = entity.textureHeights[face],
                        .data = entity.textureBytes[face].data(),
                        .dataStorageBytes = entity.textureBytes[face].size(),
                        .mipmapOffsetBytes = entity.textureMipOffsets[face],
                    });
                }
                allocation.textureInfos.push_back(eastl::move(textureComponent));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuMaterialsBasicRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        // Preserve Date.now() semantics using the repository-wide 60 Hz
        // virtual frame step.
        const double timer = 0.0001 * (ReferenceEpochMilliseconds +
            double(frameIndex) * (1000.0 / 60.0));
        const glm::mat4 projection = makeThreePerspective(glm::radians(60.0f), 800.0f / 500.0f, 0.01f, 100.0f);
        const glm::vec3 cameraPosition(0.0f, 0.0f, 3.0f);
        glm::mat4 view(1.0f);
        view[3u][2u] = -cameraPosition.z;
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            WebgpuMaterialsBasicEntityData &entity = entities[entityIndex];
            entity.objectData = entity.baseObjectData;
            const glm::vec3 animatedPosition(
                5.0f * float(std::cos(timer + double(entityIndex))),
                5.0f * float(std::sin(timer + double(entityIndex) * 1.1)),
                entity.randomPosition.z);
            entity.objectData.model = glm::translate(glm::mat4(1.0f), animatedPosition) *
                glm::scale(glm::mat4(1.0f), glm::vec3(entity.randomScale));
            entity.objectData.modelView = view * entity.objectData.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPositionAndFlags.x = cameraPosition.x;
            entity.objectData.cameraPositionAndFlags.y = cameraPosition.y;
            entity.objectData.cameraPositionAndFlags.z = cameraPosition.z;
            updateCameraBasis(entity.objectData, cameraPosition, 800u, 500u);
        }
    }

    void WebgpuMaterialsBasicRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_materials_basic could not create its update encoder.");
        for (const WebgpuMaterialsBasicEntityData &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebgpuMaterialsBasicSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuMaterialsBasicRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebgpuMaterialsBasicRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frame, uint32_t width,
        uint32_t height, uint64_t bytes) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgpu_materials_basic\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
            << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << bytes << ",\"format\":\"rgba8unorm\",\"randomSeed\":"
            << options.randomSeed << ",\"sampleCount\":1,\"msaaEnabled\":false";
        if (options.scenarioId == "refraction" || options.scenarioId == "transparent-sorted")
        {
            const char *replaySha256 = options.scenarioId == "refraction"
                ? RefractionReplaySha256
                : TransparentReplaySha256;
            out << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgpu_materials_basic\",\"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\"captureFrame\":61,\"sha256\":\""
                << replaySha256
                << "\",\"target\":\"canvas:not([class])\",\"eventCount\":2}";
        }
        else
        {
            out << ",\"inputReplay\":null";
        }
        out << "}\n";
    }

    void WebgpuMaterialsBasicRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgpu_materials_basic\",\n  \"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
            << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
            << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
            << "  \"renderSetType\":\"WebgpuMaterialsBasicSceneRenderSet\",\n  \"renderableObjectCount\":" << EntityCount << ",\n"
            << "  \"entityCount\":" << EntityCount << ",\n  \"instanceCounts\":[";
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
            out << (entityIndex == 0u ? "" : ",") << "1";
        out << "],\n  \"textureSlotCount\":6,\n"
            << "  \"scenePassCount\":2,\n  \"screenPassCount\":1,\n  \"drawCommandCount\":1,\n"
            << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
            << "  \"scenePasses\":[{\"name\":\"opaque-spheres\",\"renderClass\":\"WebgpuMaterialsBasicOpaquePass\",\"renderSetId\":\"webgpu-materials-basic-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << (options.scenarioId == "transparent-sorted" ? 0u : 1u)
            << ",\"drawCommandCount\":" << (options.scenarioId == "transparent-sorted" ? 0u : 1u)
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-spheres\",\"renderClass\":\"WebgpuMaterialsBasicTransparentPass\",\"renderSetId\":\"webgpu-materials-basic-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << (options.scenarioId == "transparent-sorted" ? 1u : 0u)
            << ",\"drawCommandCount\":" << (options.scenarioId == "transparent-sorted" ? 1u : 0u)
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
            << "  \"screenPasses\":[{\"name\":\"environment-background\",\"renderClass\":\"WebgpuMaterialsBasicBackgroundPass\",\"drawMode\":\"fullscreen-triangle\"}],\n"
            << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"webgpu-materials-basic-scene-set\",\"renderSetType\":\"WebgpuMaterialsBasicSceneRenderSet\",\"renderableObjectCount\":" << EntityCount << ",\"entityCount\":" << EntityCount << ",\"entities\":[";
        for (uint32_t entityIndex = 0u; entityIndex < EntityCount; ++entityIndex)
        {
            if (entityIndex != 0u) out << ",";
            out << "{\"entityId\":" << entityIndex << ",\"logicalRenderableId\":\"sphere-" << entityIndex
                << "\",\"instanceCount\":1}";
        }
        out << "],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"opaque-spheres\",\"renderClass\":\"WebgpuMaterialsBasicOpaquePass\",\"renderSetId\":\"webgpu-materials-basic-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << (options.scenarioId == "transparent-sorted" ? 0u : 1u)
            << ",\"drawCommandCount\":" << (options.scenarioId == "transparent-sorted" ? 0u : 1u)
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-spheres\",\"renderClass\":\"WebgpuMaterialsBasicTransparentPass\",\"renderSetId\":\"webgpu-materials-basic-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":"
            << (options.scenarioId == "transparent-sorted" ? 1u : 0u)
            << ",\"drawCommandCount\":" << (options.scenarioId == "transparent-sorted" ? 1u : 0u)
            << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
    }

    /** Writes the locked canonical scene sidecar for the 500-sphere environment scene. */
    void WebgpuMaterialsBasicRuntimeAdapter::writeLoaderSemanticSnapshot(
        const ThreeSampleHostOptions &options) const
    {
        if (options.semanticSnapshotPath.empty() || options.scenarioId != "initial-reflection") return;
        const std::filesystem::path path(options.semanticSnapshotPath.c_str());
        preparePath(path);
        std::ofstream out(path, std::ios::trunc);
        out << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgpu_materials_basic\",\n"
            << "  \"scenarioId\":\"initial-reflection\",\n  \"frame\":0,\n"
            << "  \"kind\":\"canonical-scene-snapshot\",\n  \"canonicalState\":\"seed-42-500-sphere-reflection\",\n"
            << "  \"result\":{\"renderableObjectCount\":" << EntityCount << ",\"sceneRootCount\":1,\"canonicalSceneSha256\":\""
            << CanonicalSceneSha256 << "\",\"assetPath\":\"textures/cube/pisa/{px,nx,py,ny,pz,nz}.png\"}\n}\n";
    }

    void WebgpuMaterialsBasicRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frame, GVM::RHI::Texture texture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frame != options.targetFrame) return;
        const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4u;
        if (bytes > std::numeric_limits<size_t>::max()) throw std::overflow_error("Cubemap capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(bytes));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgpu_materials_basic has no graphics queue.");
        queue->readTexture(texture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frame, width, height, bytes);
        writeStructuralSnapshot(options, frame);
        writeLoaderSemanticSnapshot(options);
        captureWritten = true;
    }

    void WebgpuMaterialsBasicRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
} // namespace GVM::ThreeSamples
