#include "WebglUboRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "TexturedBoxSampleData.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 200u;
        constexpr uint32_t PreLoopRandomDrawCount = 108u;
        constexpr uint32_t PerEntityUuidDrawCount = 8u;
        constexpr uint32_t PostLoopRandomDrawCount = 36u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint8_t WhiteTexture[4u] = {255u, 255u, 255u, 255u};

        /** Returns the reference JavaScript unit value from one random draw. */
        double nextWebglUboRandom(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one hexadecimal sRGB material byte to linear light. */
        float convertWebglUboColorChannel(uint32_t byte)
        {
            const double srgb = double(byte) / 255.0;
            return static_cast<float>(srgb <= 0.04045
                ? srgb / 12.92
                : std::pow((srgb + 0.055) / 1.055, 2.4));
        }

        /** Builds Three's exact XYZ Euler matrix for one entity. */
        glm::mat4 makeWebglUboRotation(
            double sourceX,
            double sourceY,
            double sourceZ)
        {
            const double x = sourceX;
            const double y = sourceY;
            const double z = sourceZ;
            const double a = std::cos(x);
            const double b = std::sin(x);
            const double c = std::cos(y);
            const double d = std::sin(y);
            const double e = std::cos(z);
            const double f = std::sin(z);
            const double ae = a * e;
            const double af = a * f;
            const double be = b * e;
            const double bf = b * f;
            glm::mat4 result(1.0f);
            result[0u][0u] = static_cast<float>(c * e);
            result[1u][0u] = static_cast<float>(-c * f);
            result[2u][0u] = static_cast<float>(d);
            result[0u][1u] = static_cast<float>(af + be * d);
            result[1u][1u] = static_cast<float>(ae - bf * d);
            result[2u][1u] = static_cast<float>(-b * c);
            result[0u][2u] = static_cast<float>(bf - ae * d);
            result[1u][2u] = static_cast<float>(be + af * d);
            result[2u][2u] = static_cast<float>(a * c);
            return result;
        }

        /** Builds Three r185's non-indexed radius-one TetrahedronGeometry. */
        void buildWebglUboTetrahedron(
            eastl::vector<WebglUboHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 source[4u] = {
                glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)),
                glm::normalize(glm::vec3(-1.0f, -1.0f, 1.0f)),
                glm::normalize(glm::vec3(-1.0f, 1.0f, -1.0f)),
                glm::normalize(glm::vec3(1.0f, -1.0f, -1.0f))};
            constexpr uint32_t faceIndices[12u] = {
                2u, 1u, 0u, 0u, 3u, 2u,
                1u, 3u, 0u, 2u, 3u, 1u};
            vertices.clear();
            indices.clear();
            vertices.reserve(12u);
            indices.reserve(12u);
            for (uint32_t face = 0u; face < 4u; ++face)
            {
                const glm::vec3 a = source[faceIndices[face * 3u]];
                const glm::vec3 b = source[faceIndices[face * 3u + 1u]];
                const glm::vec3 c = source[faceIndices[face * 3u + 2u]];
                const glm::vec3 normal = glm::normalize(glm::cross(c - b, a - b));
                for (const glm::vec3 &position : {a, b, c})
                {
                    vertices.push_back({
                        .position = glm::vec4(position, 1.0f),
                        .normal = glm::vec4(normal, 0.0f),
                        .textureCoordinate = glm::vec4(0.0f),
                    });
                    indices.push_back(static_cast<uint32_t>(indices.size()));
                }
            }
        }

        /** Converts the shared BoxGeometry payload to the UBO Scene layout. */
        void buildWebglUboBox(
            eastl::vector<WebglUboHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(
                1.0f, 1.0f, 1.0f, sourceVertices, indices);
            glm::vec3 faceNormals[6u] = {};
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const TexturedBoxHostFloat4 &sourceA =
                    sourceVertices[indices[face * 6u]].position;
                const TexturedBoxHostFloat4 &sourceB =
                    sourceVertices[indices[face * 6u + 1u]].position;
                const TexturedBoxHostFloat4 &sourceC =
                    sourceVertices[indices[face * 6u + 2u]].position;
                const glm::vec3 a(sourceA.x, sourceA.y, sourceA.z);
                const glm::vec3 b(sourceB.x, sourceB.y, sourceB.z);
                const glm::vec3 c(sourceC.x, sourceC.y, sourceC.z);
                faceNormals[face] = glm::normalize(glm::cross(c - b, a - b));
            }
            vertices.clear();
            vertices.reserve(sourceVertices.size());
            for (uint32_t index = 0u; index < sourceVertices.size(); ++index)
            {
                vertices.push_back({
                    .position = glm::vec4(
                        sourceVertices[index].position.x,
                        sourceVertices[index].position.y,
                        sourceVertices[index].position.z,
                        1.0f),
                    .normal = glm::vec4(faceNormals[index / 4u], 0.0f),
                    .textureCoordinate = glm::vec4(
                        sourceVertices[index].texCoord.x,
                        sourceVertices[index].texCoord.y,
                        0.0f,
                        0.0f),
                });
            }
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendWebglUboBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
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

        /** Creates parent folders for an explicitly requested artifact. */
        void prepareWebglUboOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglUboRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        if (options.caseId != "webgl_ubo" || (!initial && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            !options.inputReplayPath.empty() || options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "WebGL UBO adapter requires its locked scenarios, extent, seed, and assets.");
        }
        device = inDevice;
        buildWebglUboTetrahedron(tetrahedronVertices, tetrahedronIndices);
        buildWebglUboBox(boxVertices, boxIndices);
        const RgbaImageData crate = decodeGifRgba8(
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "crate.gif");
        const eastl::vector<RgbaImageData> crateMips = buildSrgbMipChain(crate);
        for (const RgbaImageData &mip : crateMips)
        {
            crateMipOffsets.push_back(crateBytes.size());
            crateBytes.insert(
                crateBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }
        projection = makeThreePerspectiveProjection(
            options.width, options.height, 45.0, 0.1, 100.0);

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PreLoopRandomDrawCount; ++draw)
            (void)random.nextUint32();
        const double elapsedSeconds = double(options.targetFrame) / 60.0;
        glm::mat4 view(1.0f);
        view[3u][2u] = -25.0f;
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error("WebGL UBO could not create its Scene Set encoder.");

        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            for (uint32_t draw = 0u; draw < PerEntityUuidDrawCount; ++draw)
                (void)random.nextUint32();
            const bool textured = (entity & 1u) != 0u;
            glm::vec4 baseColor(1.0f);
            if (!textured)
            {
                const uint32_t color = static_cast<uint32_t>(
                    nextWebglUboRandom(random) * 16777215.0);
                baseColor = glm::vec4(
                    convertWebglUboColorChannel((color >> 16u) & 255u),
                    convertWebglUboColorChannel((color >> 8u) & 255u),
                    convertWebglUboColorChannel(color & 255u),
                    1.0f);
            }
            const double scale = 1.0 + nextWebglUboRandom(random) * 0.5;
            const double rotationX = nextWebglUboRandom(random) * Pi + elapsedSeconds * 0.5;
            const double rotationY = nextWebglUboRandom(random) * Pi + elapsedSeconds * 0.3;
            const double rotationZ = nextWebglUboRandom(random) * Pi;
            const double positionX = nextWebglUboRandom(random) * 40.0 - 20.0;
            const double positionY = nextWebglUboRandom(random) * 40.0 - 20.0;
            const double positionZ = nextWebglUboRandom(random) * 20.0 - 10.0;
            glm::mat4 model = makeWebglUboRotation(
                rotationX, rotationY, rotationZ);
            for (uint32_t column = 0u; column < 3u; ++column)
                model[column] *= static_cast<float>(scale);
            model[3u] = glm::vec4(
                static_cast<float>(positionX),
                static_cast<float>(positionY),
                static_cast<float>(positionZ),
                1.0f);
            const glm::mat4 modelView = view * model;
            objects[entity] = {
                .modelView = modelView,
                .normalTransform = glm::transpose(glm::inverse(modelView)),
                .materialPhase = glm::uvec4(textured ? 1u : 0u, 0u, 0u, 0u),
            };
            instances[entity].reserved = glm::vec4(0.0f);
            materials[entity].baseColor = baseColor;

            const auto &vertices = textured ? boxVertices : tetrahedronVertices;
            const auto &indices = textured ? boxIndices : tetrahedronIndices;
            const eastl::string prefix =
                "WebglUboEntity" + eastl::to_string(entity);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendWebglUboBuffer(allocation, WebglUboSceneRenderSetComponents::vertices,
                prefix + "Vertices", vertices.data(), vertices.size() * sizeof(vertices[0u]));
            appendWebglUboBuffer(allocation, WebglUboSceneRenderSetComponents::indices,
                prefix + "Indices", indices.data(), indices.size() * sizeof(indices[0u]));
            appendWebglUboBuffer(allocation, WebglUboSceneRenderSetComponents::objects,
                prefix + "Object", &objects[entity], sizeof(objects[entity]));
            appendWebglUboBuffer(allocation, WebglUboSceneRenderSetComponents::instances,
                prefix + "Instance", &instances[entity], sizeof(instances[entity]));
            appendWebglUboBuffer(allocation, WebglUboSceneRenderSetComponents::materials,
                prefix + "Material", &materials[entity], sizeof(materials[entity]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglUboSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = prefix + "Texture",
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = textured ? crate.width : 1u,
                .height = textured ? crate.height : 1u,
                .data = textured ? crateBytes.data() : WhiteTexture,
                .dataStorageBytes = textured ? crateBytes.size() : sizeof(WhiteTexture),
                .mipmapOffsetBytes = textured
                    ? crateMipOffsets
                    : eastl::vector<uint64_t>{0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        for (uint32_t draw = 0u; draw < PostLoopRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();
        if (finalRandomState != 1754807086u)
            throw std::runtime_error("WebGL UBO random stream diverged from the oracle.");
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglUboRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglUboRuntimeAdapter::afterFrame(
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
            prepareWebglUboOutputPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write WebGL UBO RGBA.");
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareWebglUboOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_ubo\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"randomSeed\": " << options.randomSeed << ",\n"
                << "  \"randomState\": " << finalRandomState << ",\n"
                << "  \"width\": " << width << ", \"height\": " << height << ",\n"
                << "  \"rowStrideBytes\": " << width * 4u << ",\n"
                << "  \"byteCount\": " << rgba.size() << ",\n"
                << "  \"format\": \"rgba8unorm\", \"sampleCount\": 1\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareWebglUboOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_ubo\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"implementationLevel\": \"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\": true,\n"
                << "  \"sceneRenderSetCount\": 1, \"entityCount\": 200,\n"
                << "  \"instanceCount\": 200, \"scenePassCount\": 1,\n"
                << "  \"drawCommandCount\": 200, \"sampleCount\": 1,\n"
                << "  \"renderSetType\": \"WebglUboSceneRenderSet\"\n}\n";
        }
        captureWritten = true;
    }

    void WebglUboRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        tetrahedronVertices.clear();
        tetrahedronIndices.clear();
        boxVertices.clear();
        boxIndices.clear();
        crateBytes.clear();
        crateMipOffsets.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
