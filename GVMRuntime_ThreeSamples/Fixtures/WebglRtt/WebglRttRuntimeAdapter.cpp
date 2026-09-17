#include "WebglRttRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle RttSetHandle =
            ExportedRenderSet::rttSceneSet;
        constexpr GVM::Core::RenderSetHandle MainSetHandle =
            ExportedRenderSet::mainSceneSet;

        /** Converts one sRGB channel to Three's linear working space. */
        float webglRttSrgbToLinear(float value)
        {
            return value < 0.04045f
                ? value * 0.0773993808f
                : std::pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
        }

        /** Converts one hexadecimal material color to linear RGB. */
        glm::vec3 webglRttHexColor(uint32_t value)
        {
            return {
                webglRttSrgbToLinear(float((value >> 16u) & 255u) / 255.0f),
                webglRttSrgbToLinear(float((value >> 8u) & 255u) / 255.0f),
                webglRttSrgbToLinear(float(value & 255u) / 255.0f)};
        }

        /** Builds Three PlaneGeometry(800,500). */
        void buildWebglRttPlane(WebglRttHostEntity &entity)
        {
            entity.vertices = {
                {{-400.0f, 250.0f, 0.0f, 1.0f}, {}, {0.0f, 1.0f, 0.0f, 0.0f}},
                {{400.0f, 250.0f, 0.0f, 1.0f}, {}, {1.0f, 1.0f, 0.0f, 0.0f}},
                {{-400.0f, -250.0f, 0.0f, 1.0f}, {}, {0.0f, 0.0f, 0.0f, 0.0f}},
                {{400.0f, -250.0f, 0.0f, 1.0f}, {}, {1.0f, 0.0f, 0.0f, 0.0f}}};
            entity.indices = {0u, 2u, 1u, 2u, 3u, 1u};
        }

        /** Builds the exact TorusGeometry(100,25,15,30) stream. */
        void buildWebglRttTorus(WebglRttHostEntity &entity)
        {
            constexpr uint32_t RadialSegments = 15u;
            constexpr uint32_t TubularSegments = 30u;
            for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
            {
                const double v = double(radial) / RadialSegments * 2.0 * Pi;
                for (uint32_t tubular = 0u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const double u =
                        double(tubular) / TubularSegments * 2.0 * Pi;
                    const glm::vec3 center(
                        float(100.0 * std::cos(u)),
                        float(100.0 * std::sin(u)), 0.0f);
                    const glm::vec3 position(
                        float((100.0 + 25.0 * std::cos(v)) * std::cos(u)),
                        float((100.0 + 25.0 * std::cos(v)) * std::sin(u)),
                        float(25.0 * std::sin(v)));
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(glm::normalize(position - center), 0.0f),
                        {1.0f - float(tubular) / TubularSegments,
                         float(radial) / RadialSegments, 0.0f, 0.0f}});
                }
            }
            for (uint32_t radial = 1u; radial <= RadialSegments; ++radial)
            {
                for (uint32_t tubular = 1u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const uint32_t a =
                        (TubularSegments + 1u) * radial + tubular - 1u;
                    const uint32_t b =
                        (TubularSegments + 1u) * (radial - 1u) + tubular - 1u;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    entity.indices.insert(entity.indices.end(), {a, b, d, b, c, d});
                }
            }
        }

        /** Builds exact SphereGeometry(10,64,32) positions, normals, and UVs. */
        void buildWebglRttSphere(WebglRttHostEntity &entity)
        {
            constexpr uint32_t WidthSegments = 64u;
            constexpr uint32_t HeightSegments = 32u;
            constexpr float Radius = 10.0f;
            for (uint32_t row = 0u; row <= HeightSegments; ++row)
            {
                const double v = double(row) / HeightSegments;
                const double theta = v * Pi;
                const double uvOffset = row == 0u
                    ? 0.5 / WidthSegments
                    : row == HeightSegments ? -0.5 / WidthSegments : 0.0;
                for (uint32_t column = 0u; column <= WidthSegments; ++column)
                {
                    const double u = double(column) / WidthSegments;
                    const glm::vec3 position(
                        float(-Radius * std::cos(u * 2.0 * Pi) * std::sin(theta)),
                        float(Radius * std::cos(theta)),
                        float(Radius * std::sin(u * 2.0 * Pi) * std::sin(theta)));
                    entity.vertices.push_back({
                        glm::vec4(position, 1.0f),
                        glm::vec4(glm::normalize(position), 0.0f),
                        {float(u + uvOffset), float(1.0 - v), 0.0f, 0.0f}});
                }
            }
            const uint32_t stride = WidthSegments + 1u;
            for (uint32_t row = 0u; row < HeightSegments; ++row)
            {
                for (uint32_t column = 0u; column < WidthSegments; ++column)
                {
                    const uint32_t a = row * stride + column + 1u;
                    const uint32_t b = row * stride + column;
                    const uint32_t c = (row + 1u) * stride + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u) entity.indices.insert(entity.indices.end(), {a, b, d});
                    if (row + 1u != HeightSegments)
                        entity.indices.insert(entity.indices.end(), {b, c, d});
                }
            }
        }

        /** Appends one typed component payload to an entity allocation. */
        void appendWebglRttBuffer(
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
                .instanceCount = 1u});
        }

        /** Allocates one ordinary entity in a selected Scene Set. */
        void allocateWebglRttEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglRttHostEntity &entity,
            const std::string &prefix)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebglRttBuffer(allocation, WebglRttSceneRenderSetComponents::vertices,
                (prefix + "Vertices").c_str(), entity.vertices.data(),
                entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendWebglRttBuffer(allocation, WebglRttSceneRenderSetComponents::indices,
                (prefix + "Indices").c_str(), entity.indices.data(),
                entity.indices.size() * sizeof(entity.indices[0u]));
            appendWebglRttBuffer(allocation, WebglRttSceneRenderSetComponents::objects,
                (prefix + "Object").c_str(), &entity.objectData,
                sizeof(entity.objectData));
            appendWebglRttBuffer(allocation, WebglRttSceneRenderSetComponents::instances,
                (prefix + "Instance").c_str(), &entity.instanceData,
                sizeof(entity.instanceData));
            appendWebglRttBuffer(allocation, WebglRttSceneRenderSetComponents::materials,
                (prefix + "Material").c_str(), &entity.materialData,
                sizeof(entity.materialData));
            const uint8_t white[4u] = {255u, 255u, 255u, 255u};
            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle =
                WebglRttSceneRenderSetComponents::textures;
            textureInfo.textures.push_back({
                .textureName = "WebglRttIdentityTexture",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 1u,
                .height = 1u,
                .data = white,
                .dataStorageBytes = sizeof(white),
                .mipmapOffsetBytes = {0u}});
            allocation.textureInfos.push_back(eastl::move(textureInfo));
            encoder.allocEntity(allocation);
        }

        /** Creates parent folders for one evidence file. */
        void prepareWebglRttPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path value(path.c_str());
            if (!value.parent_path().empty())
                std::filesystem::create_directories(value.parent_path());
        }

        /** Writes one optional UTF-8 evidence document. */
        void writeWebglRttText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            prepareWebglRttPath(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write WebGL RTT evidence.");
        }

        /** Returns the lowercase SHA-256 digest of one file. */
        std::string calculateWebglRttFileSha256(const eastl::string &path)
        {
            if (path.empty()) return {};
            std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not read WebGL RTT replay.");
            const auto size = input.tellg();
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.seekg(0);
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(size));
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            std::ostringstream result;
            result << std::hex << std::setfill('0');
            for (uint8_t value : digest) result << std::setw(2) << unsigned(value);
            return result.str();
        }
    } // namespace

    void WebglRttRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
        const bool mouse = options.scenarioId == "mouse-camera" && options.targetFrame == 61u;
        if (options.caseId != "webgl_rtt" || (!initial && !animated && !mouse) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (mouse != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument("WebGL RTT requires its locked scenarios, extent, seed, and replay.");
        }
        device = inDevice;
        rttEntities.resize(3u);
        buildWebglRttPlane(rttEntities[0u]);
        buildWebglRttTorus(rttEntities[1u]);
        rttEntities[2u].vertices = rttEntities[1u].vertices;
        rttEntities[2u].indices = rttEntities[1u].indices;
        const glm::mat4 rttProjection = glm::ortho(
            -400.0f, 400.0f, -250.0f, 250.0f, 1.0f, 1000.0f);
        const glm::mat4 rttView = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -500.0f));
        const double virtualTimeMs = double(options.targetFrame) * (1000.0 / 60.0);
        const float rotationTime = float(std::fmod(
            (1700000000000.0 + virtualTimeMs) * 0.0015, 2.0 * Pi));
        const glm::mat4 models[3u] = {
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -100.0f)),
            glm::scale(glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 100.0f)),
                -rotationTime, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.5f)),
            glm::scale(glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 150.0f, 100.0f)),
                -rotationTime + float(Pi * 0.5), glm::vec3(0.0f, 1.0f, 0.0f)),
                glm::vec3(0.75f))};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            auto &entity = rttEntities[index];
            entity.objectData.modelView = rttView * models[index];
            entity.objectData.modelViewProjection =
                rttProjection * entity.objectData.modelView;
            entity.objectData.normalTransform = glm::transpose(
                glm::inverse(entity.objectData.modelView));
            entity.instanceData.reserved = glm::vec4(0.0f);
        }
        double shaderTime = 0.0;
        double delta = 0.01;
        for (uint32_t frame = 0u; frame <= options.targetFrame; ++frame)
        {
            if (shaderTime > 1.0 || shaderTime < 0.0) delta = -delta;
            shaderTime += delta;
        }
        rttEntities[0u].materialData.phaseTimeAndColor =
            glm::vec4(0.0f, float(shaderTime), 0.0f, 0.0f);
        const glm::vec3 gray = webglRttHexColor(0x9c9c9cu);
        const glm::vec3 darkRed = webglRttHexColor(0x9c0000u);
        rttEntities[1u].materialData.phaseTimeAndColor = glm::vec4(1.0f, gray);
        rttEntities[1u].materialData.specularAndShininess =
            glm::vec4(webglRttHexColor(0xffaa00u), 5.0f);
        rttEntities[2u].materialData.phaseTimeAndColor = glm::vec4(1.0f, darkRed);
        rttEntities[2u].materialData.specularAndShininess =
            glm::vec4(webglRttHexColor(0xff2200u), 5.0f);

        WebglRttHostEntity sphereSource;
        buildWebglRttSphere(sphereSource);
        double cameraX = 0.0;
        double cameraY = 0.0;
        if (mouse)
        {
            for (uint32_t frame = 0u; frame <= options.targetFrame; ++frame)
            {
                cameraX += (120.0 - cameraX) * 0.05;
                cameraY += (70.0 - cameraY) * 0.05;
            }
        }
        const glm::dvec3 camera(cameraX, cameraY, 100.0);
        const glm::mat4 mainView = glm::mat4(glm::lookAt(
            camera, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 mainProjection = makeThreePerspectiveProjection(
            800u, 500u, 30.0, 1.0, 10000.0);
        mainEntities.resize(25u);
        for (uint32_t row = 0u; row < 5u; ++row)
        {
            for (uint32_t column = 0u; column < 5u; ++column)
            {
                const uint32_t index = row * 5u + column;
                auto &entity = mainEntities[index];
                entity.vertices = sphereSource.vertices;
                entity.indices = sphereSource.indices;
                const glm::mat4 model = glm::rotate(
                    glm::translate(glm::mat4(1.0f), glm::vec3(
                        (float(column) - 2.0f) * 20.0f,
                        (float(row) - 2.0f) * 20.0f, 0.0f)),
                    -float(Pi * 0.5), glm::vec3(0.0f, 1.0f, 0.0f));
                entity.objectData.modelView = mainView * model;
                entity.objectData.modelViewProjection =
                    mainProjection * entity.objectData.modelView;
                entity.objectData.normalTransform = glm::transpose(
                    glm::inverse(entity.objectData.modelView));
                entity.instanceData.reserved = glm::vec4(0.0f);
                entity.materialData.phaseTimeAndColor = glm::vec4(2.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        const auto rttEncoder = renderer.createRenderSetCommandEncoder(RttSetHandle);
        const auto mainEncoder = renderer.createRenderSetCommandEncoder(MainSetHandle);
        if (!rttEncoder || !mainEncoder)
            throw std::runtime_error("Could not create both WebGL RTT Scene encoders.");
        for (uint32_t index = 0u; index < rttEntities.size(); ++index)
            allocateWebglRttEntity(*rttEncoder, rttEntities[index],
                "WebglRttOffscreen" + std::to_string(index));
        for (uint32_t index = 0u; index < mainEntities.size(); ++index)
            allocateWebglRttEntity(*mainEncoder, mainEntities[index],
                "WebglRttSphere" + std::to_string(index));
        renderer.executeRenderSetCommand(RttSetHandle, rttEncoder);
        renderer.executeRenderSetCommand(MainSetHandle, mainEncoder);
    }

    void WebglRttRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglRttRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        eastl::vector<uint8_t> rgba(size_t(width) * height * 4u);
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareWebglRttPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        const std::string replaySha =
            calculateWebglRttFileSha256(options.inputReplayPath);
        std::ostringstream metadata;
        metadata << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_rtt\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << width * 4u
            << ",\"byteCount\":" << rgba.size()
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
            << "\"samplePolicy\":{\"mode\":\"single-sample\","
            << "\"msaaEnabled\":false,\"simulateMsaa\":false},"
            << "\"inputReplay\":";
        if (replaySha.empty()) metadata << "null";
        else metadata << "{\"schemaVersion\":1,\"caseId\":\"webgl_rtt\","
            << "\"scenarioId\":\"mouse-camera\",\"captureFrame\":61,"
            << "\"sha256\":\"" << replaySha << "\","
            << "\"target\":\"body > div > canvas\",\"eventCount\":1}";
        metadata << "}\n";
        writeWebglRttText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\"schemaVersion\":1,\"caseId\":\"webgl_rtt\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":2,\"renderableObjectCount\":28,"
            << "\"entityCount\":28,\"instanceCount\":1,"
            << "\"scenePassCount\":2,\"screenPassCount\":1,"
            << "\"drawCommandCount\":2,\"sampleCount\":1,"
            << "\"msaaEnabled\":false,\"msaaSimulated\":false,"
            << "\"sceneRoots\":[{\"id\":\"sceneRTT\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"rtt-scene-set\","
            << "\"renderSetType\":\"WebglRttSceneRenderSet\","
            << "\"entityCount\":3,\"renderableObjectCount\":3,"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"generated-rtt-texture\"}],"
            << "\"scenePasses\":[{\"name\":\"rtt-scene\","
            << "\"renderClass\":\"WebglRttOffscreenScenePass\","
            << "\"renderSetId\":\"rtt-scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],"
            << "\"entities\":[";
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            if (index != 0u) snapshot << ',';
            snapshot << "{\"entityId\":" << index
                << ",\"logicalRenderableId\":\""
                << (index == 0u ? "shader-plane" : index == 1u ? "torus-gray" : "torus-red")
                << "\",\"instanceCount\":1}";
        }
        snapshot << "]},{\"id\":\"scene\","
            << "\"renderSetCount\":1,\"renderSetType\":\"WebglRttSceneRenderSet\","
            << "\"renderSetId\":\"main-scene-set\","
            << "\"entityCount\":25,\"renderableObjectCount\":25,"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"generated-rtt-texture\"}],"
            << "\"scenePasses\":[{\"name\":\"main-spheres\","
            << "\"renderClass\":\"WebglRttMainSpheresPass\","
            << "\"renderSetId\":\"main-scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],"
            << "\"entities\":[";
        for (uint32_t index = 0u; index < 25u; ++index)
        {
            if (index != 0u) snapshot << ',';
            snapshot << "{\"entityId\":" << index
                << ",\"logicalRenderableId\":\"sphere-" << index
                << "\",\"instanceCount\":1}";
        }
        snapshot << "]},{\"id\":\"sceneScreen\","
            << "\"renderSetCount\":0,\"renderableObjectCount\":0}],"
            << "\"screenPasses\":[{\"name\":\"generated-rtt-fullscreen-copy\","
            << "\"renderClass\":\"WebglRttFullscreenCopyPass\"}],"
            << "\"scenePassSequence\":["
            << "{\"sceneRoot\":\"sceneRTT\",\"scenePass\":\"rtt-scene\"},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-spheres\"}]}\n";
        writeWebglRttText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic << "{\"schemaVersion\":1,\"caseId\":\"webgl_rtt\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"result\":{\"rttEntityCount\":3,"
            << "\"mainEntityCount\":25,\"sphereVertexCount\":2145,"
            << "\"sphereIndexCount\":11904,\"torusVertexCount\":496,"
            << "\"torusIndexCount\":2700}}\n";
        writeWebglRttText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebglRttRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        rttEntities.clear();
        mainEntities.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
