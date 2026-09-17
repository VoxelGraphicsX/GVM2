#include "WebglPostprocessingBackgroundsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

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
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;
        constexpr const char *PisaFaces[6u] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
        constexpr const char *CubeReplaySha256 =
            "32011f190ca4060651df5ef297ecafb2c2279e4b2afe2d8df34bd01d07a57247";

        /** Reads one bounded input replay as the exact bytes used by the lock. */
        eastl::vector<uint8_t> readReplayBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open the locked background input replay: " + path.string());
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || static_cast<uint64_t>(byteCount) > std::numeric_limits<CC_LONG>::max())
                throw std::runtime_error("The locked background input replay has an invalid byte count.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read the locked background input replay.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one input replay. */
        eastl::string calculateReplaySha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Resolves the locked r185 examples directory from either accepted root layout. */
        std::filesystem::path resolveExamplesRoot(const std::filesystem::path &assetRoot)
        {
            if (std::filesystem::exists(assetRoot / "textures" / "hardwood2_diffuse.jpg")) return assetRoot;
            const std::filesystem::path nested = assetRoot / "examples";
            if (std::filesystem::exists(nested / "textures" / "hardwood2_diffuse.jpg")) return nested;
            throw std::runtime_error("The asset pack does not contain textures/hardwood2_diffuse.jpg.");
        }

        /** Creates the parent directory for one optional capture artifact. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Validates the three fixed postprocessing state scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool textureOnly = options.scenarioId == "texture-only" && options.targetFrame == 1u;
            const bool cubeOrbit = options.scenarioId == "cube-main-orbit" && options.targetFrame == 2u;
            if (options.caseId != "webgl_postprocessing_backgrounds" || (!initial && !textureOnly && !cubeOrbit) ||
                options.width != 800u || options.height != 500u || options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() || (cubeOrbit != !options.inputReplayPath.empty()))
                throw std::invalid_argument("webgl_postprocessing_backgrounds requires its locked r185 scenario contract.");
        }

        /** Appends one typed RenderSet buffer payload. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Loads an image and keeps a complete authored sRGB mip chain alive. */
        void loadMippedImage(const std::filesystem::path &path,
                             bool png,
                             eastl::vector<uint8_t> &bytes,
                             eastl::vector<uint64_t> &mipOffsets,
                             uint32_t &width,
                             uint32_t &height)
        {
            const RgbaImageData base = png ? decodePngRgba8(path) : decodeJpegRgba8(path);
            const eastl::vector<RgbaImageData> mips = buildSrgbMipChain(base);
            if (mips.empty()) throw std::runtime_error("Background image has no mip levels: " + path.string());
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

        /** Builds the indexed SphereGeometry(1,48,24) used by the r185 example. */
        void buildSphere(WebglPostprocessingBackgroundsEntityData &entity)
        {
            constexpr uint32_t WidthSegments = 48u;
            constexpr uint32_t HeightSegments = 24u;
            eastl::vector<uint32_t> grid((WidthSegments + 1u) * (HeightSegments + 1u));
            entity.vertices.clear();
            entity.indices.clear();
            for (uint32_t y = 0u; y <= HeightSegments; ++y)
            {
                const float v = float(y) / float(HeightSegments);
                const float theta = v * Pi;
                const float vertical = std::cos(theta);
                const float radius = std::sin(theta);
                for (uint32_t x = 0u; x <= WidthSegments; ++x)
                {
                    const float u = float(x) / float(WidthSegments);
                    const float phi = u * Pi * 2.0f;
                    const glm::vec3 position(-radius * std::cos(phi), vertical, radius * std::sin(phi));
                    grid[y * (WidthSegments + 1u) + x] = static_cast<uint32_t>(entity.vertices.size());
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(glm::normalize(position), 0.0f),
                        glm::vec4(u, 1.0f - v, 0.0f, 0.0f)});
                }
            }
            for (uint32_t y = 0u; y < HeightSegments; ++y)
            {
                for (uint32_t x = 0u; x < WidthSegments; ++x)
                {
                    const uint32_t a = grid[y * (WidthSegments + 1u) + x];
                    const uint32_t b = grid[y * (WidthSegments + 1u) + x + 1u];
                    const uint32_t c = grid[(y + 1u) * (WidthSegments + 1u) + x];
                    const uint32_t d = grid[(y + 1u) * (WidthSegments + 1u) + x + 1u];
                    entity.indices.insert(entity.indices.end(), {a, b, d, a, d, c});
                }
            }
        }

        /** Updates the camera, group rotation, and pass enable flags for one fixed frame. */
        void updateState(WebglPostprocessingBackgroundsEntityData &entity,
                         const eastl::string &scenario,
                         uint32_t frameIndex,
                         uint32_t width,
                         uint32_t height)
        {
            const bool textureEnabled = scenario != "cube-main-orbit";
            const bool cubeEnabled = scenario != "texture-only";
            const bool sceneEnabled = scenario != "texture-only";
            // OrbitControls applies the 22-pixel horizontal replay to the
            // camera azimuth (2*pi * dx / element.clientHeight), not to the
            // sphere's model transform.  The replay has no damping.
            const float azimuth = scenario == "cube-main-orbit"
                ? -2.0f * Pi * 22.0f / 500.0f
                : 0.0f;
            const glm::vec3 cameraPosition(
                7.0f * std::sin(azimuth), 0.0f, 7.0f * std::cos(azimuth));
            const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = glm::perspective(glm::radians(65.0f), float(width) / float(height), 1.0f, 10.0f);
            entity.objectData.model = glm::mat4(1.0f);
            entity.objectData.modelView = view * entity.objectData.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.cameraPosition = glm::vec4(cameraPosition, 0.0f);
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));
            entity.objectData.cameraRightAndTanHalfFov = glm::vec4(right, std::tan(glm::radians(32.5f)));
            entity.objectData.cameraUpAndAspect = glm::vec4(up, float(width) / float(height));
            entity.objectData.cameraForwardAndReserved = glm::vec4(forward, 0.0f);
            entity.objectData.viewportWidthHeight = glm::vec4(float(width), float(height), 0.0f, 0.0f);
            entity.objectData.clearColorAndAlpha = scenario == "texture-only"
                ? glm::vec4(0.0f, 0.0f, 1.0f, 0.5f)
                : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            entity.objectData.passFlags = glm::uvec4(textureEnabled ? 1u : 0u,
                                                      cubeEnabled ? 1u : 0u,
                                                      sceneEnabled ? 1u : 0u,
                                                      0u);
            entity.materialData.parameters.z = scenario == "texture-only" ? 0.65f : 1.0f;
            entity.materialData.parameters.w = scenario == "cube-main-orbit" ? 0.7f : 1.0f;
        }
    } // namespace

    void WebglPostprocessingBackgroundsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        scenarioState = options.scenarioId;
        const std::filesystem::path examplesRoot = resolveExamplesRoot(std::filesystem::path(options.assetRoot.c_str()));
        inputReplaySha256.clear();
        if (!options.inputReplayPath.empty())
        {
            inputReplaySha256 = calculateReplaySha256(readReplayBytes(std::filesystem::path(options.inputReplayPath.c_str())));
            if (inputReplaySha256 != CubeReplaySha256)
                throw std::invalid_argument("webgl_postprocessing_backgrounds input replay SHA-256 does not match the locked OrbitControls drag.");
        }
        buildSphere(entity);
        loadMippedImage(examplesRoot / "textures" / "hardwood2_diffuse.jpg", false,
                        entity.textureBytes[0u], entity.textureMipOffsets[0u],
                        entity.textureWidths[0u], entity.textureHeights[0u]);
        for (uint32_t face = 0u; face < 6u; ++face)
            loadMippedImage(examplesRoot / "textures" / "cube" / "pisa" / PisaFaces[face], true,
                            entity.textureBytes[face + 1u], entity.textureMipOffsets[face + 1u],
                            entity.textureWidths[face + 1u], entity.textureHeights[face + 1u]);
        // The DSL adds this value to the imported vertex position. Keep the
        // identity transform at zero; a unit vector would translate the whole
        // sphere and incorrectly shrink its projected footprint.
        entity.instanceData.offsetAndScale = glm::vec4(0.0f);
        entity.instanceData.tint = glm::vec4(1.0f);
        // r185 consumes the deterministic stream while constructing UUIDs
        // before the two material randoms.  The resulting setHSL color is
        // stored by Three in linear space as (0.3932986, 0, 0.6), with
        // roughness 0.3449783 and metalness 0.
        entity.materialData.baseColor = glm::vec4(0.3932986f, 0.0f, 0.6f, 1.0f);
        entity.materialData.parameters = glm::vec4(0.3449783f, 0.0f, 1.0f, 1.0f);
        updateState(entity, scenarioState, options.targetFrame, options.width, options.height);

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_postprocessing_backgrounds could not create its Scene Set encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendBuffer(allocation, WebglPostprocessingBackgroundsSceneRenderSetComponents::vertices,
                     "BackgroundSphereVertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
        appendBuffer(allocation, WebglPostprocessingBackgroundsSceneRenderSetComponents::indices,
                     "BackgroundSphereIndices", entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
        appendBuffer(allocation, WebglPostprocessingBackgroundsSceneRenderSetComponents::objects,
                     "BackgroundSphereObject", &entity.objectData, sizeof(entity.objectData));
        appendBuffer(allocation, WebglPostprocessingBackgroundsSceneRenderSetComponents::instances,
                     "BackgroundSphereInstance", &entity.instanceData, sizeof(entity.instanceData));
        appendBuffer(allocation, WebglPostprocessingBackgroundsSceneRenderSetComponents::materials,
                     "BackgroundSphereMaterial", &entity.materialData, sizeof(entity.materialData));
        GVM::Core::RenderSetTextureComponentAllocInfo textures;
        textures.textureComponentHandle = WebglPostprocessingBackgroundsSceneRenderSetComponents::textures;
        const char *names[7u] = {"hardwood2_diffuse.jpg", "pisa_px.png", "pisa_nx.png", "pisa_py.png", "pisa_ny.png", "pisa_pz.png", "pisa_nz.png"};
        for (uint32_t index = 0u; index < 7u; ++index)
            textures.textures.push_back({
                .textureName = names[index],
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = entity.textureWidths[index],
                .height = entity.textureHeights[index],
                .data = entity.textureBytes[index].data(),
                .dataStorageBytes = entity.textureBytes[index].size(),
                .mipmapOffsetBytes = entity.textureMipOffsets[index],
            });
        allocation.textureInfos.push_back(eastl::move(textures));
        entity.entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        updateState(entity, scenarioState, frameIndex, 800u, 500u);
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateState(entity, scenarioState, frameIndex, options.width, options.height);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_postprocessing_backgrounds could not create its update encoder.");
        encoder->setBufferComponentData(entity.entityIndex,
                                        WebglPostprocessingBackgroundsSceneRenderSetComponents::objects,
                                        &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        encoder->setBufferComponentData(entity.entityIndex,
                                        WebglPostprocessingBackgroundsSceneRenderSetComponents::materials,
                                        &entity.materialData, sizeof(entity.materialData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width,
        uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_postprocessing_backgrounds\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frameIndex << ",\"width\":" << width << ",\"height\":" << height
               << ",\"randomSeed\":" << options.randomSeed << ",\"rowStrideBytes\":" << (width * 4u)
               << ",\"byteCount\":" << byteCount << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}"
               << ",\"inputReplay\":";
        if (inputReplaySha256.empty())
        {
            output << "null";
        }
        else
        {
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_backgrounds\",\"scenarioId\":\"cube-main-orbit\",\"captureFrame\":2,\"sha256\":\""
                   << inputReplaySha256.c_str() << "\",\"target\":\"body > canvas\",\"eventCount\":3,\"lastEventFrame\":1}";
        }
        output << "}\n";
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        const uint32_t sceneInvocationCount = scenarioState == "texture-only" ? 0u : 1u;
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_postprocessing_backgrounds\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"assets\":[\"textures/hardwood2_diffuse.jpg\",\"textures/cube/pisa/*.png\"],\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglPostprocessingBackgroundsSceneRenderSet\",\n  \"renderableObjectCount\":1,\n"
               << "  \"entityCount\":1,\n  \"instanceCounts\":[1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":4,\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"screenPasses\":[\"clear\",\"texture2d-background\",\"cube-atlas-background\",\"output-color-conversion\"],\n"
               << "  \"sceneRoots\":["
               << "{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglPostprocessingBackgroundsSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"sphere\",\"instanceCount\":1}],\"drawCommandCount\":" << sceneInvocationCount
               << ",\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"background-and-cube-faces\"}],\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglPostprocessingBackgroundsMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":" << sceneInvocationCount << ",\"drawCommandCount\":" << sceneInvocationCount << ",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]},"
               << "{\"id\":\"cubeTexturePassScene\",\"renderSetCount\":0,\"renderableObjectCount\":0,\"scenePasses\":[],\"drawCommandCount\":0,\"directDrawFallback\":false}]\n}\n";
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("background capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("backgrounds has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglPostprocessingBackgroundsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entity.vertices.clear();
        entity.indices.clear();
    }
} // namespace GVM::ThreeSamples
