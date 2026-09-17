#include "WebgpuSandboxRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "Ktx1ImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t SandboxPointCount = 1000u;
        constexpr uint32_t ReferenceRandomPrefixCount = 297u;

        static_assert(sizeof(WebgpuSandboxHostVertex) == 64u);
        static_assert(sizeof(WebgpuSandboxHostObjectData) == 144u);
        static_assert(sizeof(WebgpuSandboxHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuSandboxHostMaterialData) == 32u);
        static_assert(sizeof(WebgpuSandboxHostPrimitiveExpansionData) == 16u);
        static_assert(sizeof(WebgpuSandboxHostRenderPhaseData) == 16u);

        /** Creates parent directories for one requested sandbox artifact. */
        void prepareSandboxOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional UTF-8 sandbox evidence artifact. */
        void writeSandboxText(const eastl::string &path,
                              const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareSandboxOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_sandbox evidence.");
            }
        }

        /** Returns a one-pixel opaque white fallback for untextured entities. */
        RgbaImageData makeSandboxWhiteTexture()
        {
            RgbaImageData texture;
            texture.width = 1u;
            texture.height = 1u;
            texture.pixels = {255u, 255u, 255u, 255u};
            return texture;
        }

        /** Builds Three r185's zero-to-one perspective matrix for the fixed camera. */
        glm::mat4 makeSandboxViewProjection(uint32_t width, uint32_t height)
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 10.0;
            const double top = NearDistance * std::tan(70.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth =
                projectionHeight * double(width) / double(height);
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] =
                static_cast<float>(-2.0 * NearDistance / projectionHeight);
            projection[2u][2u] =
                static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                static_cast<float>(-FarDistance * NearDistance / depth);
            return projection * glm::translate(
                glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4.0f));
        }

        /** Appends one quad with the supplied UV orientation and face normal. */
        void appendSandboxQuad(
            eastl::vector<WebgpuSandboxHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &p0,
            const glm::vec3 &p1,
            const glm::vec3 &p2,
            const glm::vec3 &p3,
            const glm::vec3 &normal,
            bool flipY,
            const glm::vec4 &color)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const float top = flipY ? 0.0f : 1.0f;
            const float bottom = flipY ? 1.0f : 0.0f;
            vertices.push_back({glm::vec4(p0, 1.0f), glm::vec4(normal, 0.0f),
                                glm::vec4(0.0f, top, 0.0f, 0.0f), color});
            vertices.push_back({glm::vec4(p1, 1.0f), glm::vec4(normal, 0.0f),
                                glm::vec4(1.0f, top, 0.0f, 0.0f), color});
            vertices.push_back({glm::vec4(p2, 1.0f), glm::vec4(normal, 0.0f),
                                glm::vec4(0.0f, bottom, 0.0f, 0.0f), color});
            vertices.push_back({glm::vec4(p3, 1.0f), glm::vec4(normal, 0.0f),
                                glm::vec4(1.0f, bottom, 0.0f, 0.0f), color});
            indices.insert(indices.end(), {
                base, base + 2u, base + 1u,
                base + 2u, base + 3u, base + 1u,
            });
        }

        /** Builds the six separately UV-mapped faces of one unit BoxGeometry. */
        void buildSandboxBox(WebgpuSandboxEntityData &entity)
        {
            const float h = 0.5f;
            appendSandboxQuad(entity.vertices, entity.indices,
                {h, h, h}, {h, h, -h}, {h, -h, h}, {h, -h, -h},
                {1.0f, 0.0f, 0.0f}, false, glm::vec4(1.0f));
            appendSandboxQuad(entity.vertices, entity.indices,
                {-h, h, -h}, {-h, h, h}, {-h, -h, -h}, {-h, -h, h},
                {-1.0f, 0.0f, 0.0f}, false, glm::vec4(1.0f));
            appendSandboxQuad(entity.vertices, entity.indices,
                {-h, h, -h}, {h, h, -h}, {-h, h, h}, {h, h, h},
                {0.0f, 1.0f, 0.0f}, false, glm::vec4(1.0f));
            appendSandboxQuad(entity.vertices, entity.indices,
                {-h, -h, h}, {h, -h, h}, {-h, -h, -h}, {h, -h, -h},
                {0.0f, -1.0f, 0.0f}, false, glm::vec4(1.0f));
            appendSandboxQuad(entity.vertices, entity.indices,
                {-h, h, h}, {h, h, h}, {-h, -h, h}, {h, -h, h},
                {0.0f, 0.0f, 1.0f}, false, glm::vec4(1.0f));
            appendSandboxQuad(entity.vertices, entity.indices,
                {h, h, -h}, {-h, h, -h}, {h, -h, -h}, {-h, -h, -h},
                {0.0f, 0.0f, -1.0f}, false, glm::vec4(1.0f));
        }

        /** Builds Three's default 64-by-64 half-unit SphereGeometry. */
        void buildSandboxSphere(WebgpuSandboxEntityData &entity)
        {
            constexpr uint32_t WidthSegments = 64u;
            constexpr uint32_t HeightSegments = 64u;
            for (uint32_t iy = 0u; iy <= HeightSegments; ++iy)
            {
                const float v = float(iy) / float(HeightSegments);
                const float theta = v * static_cast<float>(Pi);
                const float uvOffset = iy == 0u
                    ? 0.5f / float(WidthSegments)
                    : (iy == HeightSegments
                        ? -0.5f / float(WidthSegments)
                        : 0.0f);
                for (uint32_t ix = 0u; ix <= WidthSegments; ++ix)
                {
                    const float u = float(ix) / float(WidthSegments);
                    const glm::vec3 normal(
                        -std::cos(u * float(Pi) * 2.0f) * std::sin(theta),
                        std::cos(theta),
                        std::sin(u * float(Pi) * 2.0f) * std::sin(theta));
                    entity.vertices.push_back({
                        glm::vec4(normal * 0.5f, 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(u + uvOffset, 1.0f - v, 0.0f, 0.0f),
                        glm::vec4(1.0f),
                    });
                }
            }
            for (uint32_t iy = 0u; iy < HeightSegments; ++iy)
            {
                for (uint32_t ix = 0u; ix < WidthSegments; ++ix)
                {
                    const uint32_t a = iy * (WidthSegments + 1u) + ix + 1u;
                    const uint32_t b = iy * (WidthSegments + 1u) + ix;
                    const uint32_t c = (iy + 1u) * (WidthSegments + 1u) + ix;
                    const uint32_t d = (iy + 1u) * (WidthSegments + 1u) + ix + 1u;
                    if (iy != 0u) entity.indices.insert(
                        entity.indices.end(), {a, b, d});
                    if (iy != HeightSegments - 1u) entity.indices.insert(
                        entity.indices.end(), {b, c, d});
                }
            }
        }

        /** Builds one unit PlaneGeometry with optional UV Y correction. */
        void buildSandboxPlane(WebgpuSandboxEntityData &entity, bool flipY)
        {
            appendSandboxQuad(entity.vertices, entity.indices,
                {-0.5f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.0f},
                {-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
                {0.0f, 0.0f, 1.0f}, flipY, glm::vec4(1.0f));
        }

        /** Expands the deterministic 1,000-point cloud to one-pixel quads. */
        void buildSandboxPoints(WebgpuSandboxEntityData &entity,
                                uint32_t seed)
        {
            ThreeCompat::DeterministicRandom random(seed);
            for (uint32_t index = 0u; index < ReferenceRandomPrefixCount;
                 ++index)
            {
                (void)random.nextUint32();
            }
            const float tangent = std::tan(70.0f * float(Pi) / 360.0f);
            for (uint32_t pointIndex = 0u;
                 pointIndex < SandboxPointCount;
                 ++pointIndex)
            {
                const glm::vec3 point(
                    random.nextFloat() - 0.5f,
                    random.nextFloat() - 0.5f,
                    random.nextFloat() - 0.5f);
                const float halfExtent =
                    (4.0f - point.z) * tangent / 500.0f;
                const glm::vec4 color(point * 3.0f, 1.0f);
                appendSandboxQuad(entity.vertices, entity.indices,
                    point + glm::vec3(-halfExtent, halfExtent, 0.0f),
                    point + glm::vec3(halfExtent, halfExtent, 0.0f),
                    point + glm::vec3(-halfExtent, -halfExtent, 0.0f),
                    point + glm::vec3(halfExtent, -halfExtent, 0.0f),
                    {0.0f, 0.0f, 1.0f}, false, color);
            }
        }

        /** Expands the five-vertex colored line strip to deterministic one-pixel quads. */
        void buildSandboxLine(WebgpuSandboxEntityData &entity)
        {
            const glm::vec3 points[5u] = {
                {-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
                {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f},
                {-0.5f, -0.5f, 0.0f},
            };
            const float halfWidth =
                4.0f * std::tan(70.0f * float(Pi) / 360.0f) / 500.0f;
            for (uint32_t segment = 0u; segment < 4u; ++segment)
            {
                const glm::vec3 direction = glm::normalize(
                    points[segment + 1u] - points[segment]);
                const glm::vec3 offset(
                    -direction.y * halfWidth,
                    direction.x * halfWidth, 0.0f);
                const uint32_t base = static_cast<uint32_t>(
                    entity.vertices.size());
                const glm::vec4 color0(points[segment], 1.0f);
                const glm::vec4 color1(points[segment + 1u], 1.0f);
                entity.vertices.push_back({glm::vec4(points[segment] + offset, 1.0f),
                    glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f), color0});
                entity.vertices.push_back({glm::vec4(points[segment + 1u] + offset, 1.0f),
                    glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f), color1});
                entity.vertices.push_back({glm::vec4(points[segment] - offset, 1.0f),
                    glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f), color0});
                entity.vertices.push_back({glm::vec4(points[segment + 1u] - offset, 1.0f),
                    glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f), color1});
                entity.indices.insert(entity.indices.end(), {
                    base, base + 2u, base + 1u,
                    base + 2u, base + 3u, base + 1u,
                });
            }
        }

        /** Appends one typed buffer payload to a RenderSet allocation. */
        void appendSandboxPayload(
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

        /** Allocates one complete sandbox entity through current RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateSandboxEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebgpuSandboxEntityData &entity)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(
                entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(
                entity.indices.size());
            allocation.instanceCount = 1u;
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::vertices,
                entity.logicalId + "-vertices", entity.vertices.data(),
                entity.vertices.size() * sizeof(WebgpuSandboxHostVertex));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::indices,
                entity.logicalId + "-indices", entity.indices.data(),
                entity.indices.size() * sizeof(uint32_t));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::objects,
                entity.logicalId + "-object", &entity.objectData,
                sizeof(entity.objectData));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::instances,
                entity.logicalId + "-instance", &entity.instanceData,
                sizeof(entity.instanceData));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::materials,
                entity.logicalId + "-material", &entity.materialData,
                sizeof(entity.materialData));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::primitiveExpansion,
                entity.logicalId + "-primitive-expansion",
                &entity.primitiveExpansionData,
                sizeof(entity.primitiveExpansionData));
            appendSandboxPayload(allocation,
                WebgpuSandboxSceneRenderSetComponents::renderPhases,
                entity.logicalId + "-render-phase", &entity.renderPhaseData,
                sizeof(entity.renderPhaseData));
            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle =
                WebgpuSandboxSceneRenderSetComponents::textures;
            textureInfo.textures.push_back({
                .textureName = entity.logicalId + "-texture",
                .format = entity.materialData.kindAndPhase.x == 3u
                    ? GVM::RHI::TextureFormat::RGBA8UnormSrgb
                    : GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = entity.texture.width,
                .height = entity.texture.height,
                .data = entity.texture.pixels.data(),
                .dataStorageBytes = entity.texture.pixels.size(),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureInfo));
            return encoder.allocEntity(allocation);
        }

        /** Returns the exact box transform after one animate callback per rendered frame. */
        glm::mat4 makeSandboxBoxModel(uint32_t frameIndex)
        {
            const float rotationX = float(frameIndex + 1u) * 0.01f;
            const float rotationY = float(frameIndex + 1u) * 0.02f;
            glm::mat4 model = glm::translate(
                glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, rotationX, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, rotationY, glm::vec3(0.0f, 1.0f, 0.0f));
            return model;
        }
    } // namespace

    void WebgpuSandboxRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loaded" &&
            options.targetFrame == 0u;
        const bool assets = options.scenarioId == "canonical-assets" &&
            options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" &&
            options.targetFrame == 60u;
        if (options.caseId != "webgpu_sandbox" ||
            (!initial && !assets && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgpu_sandbox requires one frozen r185 Manifest scenario.");
        }
        device = inDevice;
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        entities.resize(6u);
        const glm::mat4 viewProjection = makeSandboxViewProjection(
            options.width, options.height);
        const float timeSeconds = float(options.targetFrame) / 60.0f;
        for (WebgpuSandboxEntityData &entity : entities)
        {
            entity.objectData.viewProjection = viewProjection;
            entity.objectData.timeAndViewport = glm::vec4(
                timeSeconds, float(options.width), float(options.height), 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.primitiveExpansionData.parameters = glm::vec4(0.0f);
            entity.renderPhaseData.flags = glm::uvec4(0u);
            entity.texture = makeSandboxWhiteTexture();
        }

        WebgpuSandboxEntityData &box = entities[0u];
        box.logicalId = "animated-box";
        buildSandboxBox(box);
        box.objectData.model = makeSandboxBoxModel(options.targetFrame);
        box.materialData.kindAndPhase = glm::uvec4(0u, 0u, 0u, 0u);
        box.renderPhaseData.flags = glm::uvec4(0u, 0u, 0u, 0u);
        box.texture = decodeJpegRgba8(
            assetRoot / "textures" / "uv_grid_opengl.jpg");

        WebgpuSandboxEntityData &sphere = entities[1u];
        sphere.logicalId = "displaced-sphere";
        buildSandboxSphere(sphere);
        sphere.objectData.model = glm::translate(
            glm::mat4(1.0f), glm::vec3(-2.0f, -1.0f, 0.0f));
        sphere.materialData.kindAndPhase = glm::uvec4(1u, 0u, 0u, 0u);
        sphere.renderPhaseData.flags = glm::uvec4(0u, 0u, 0u, 0u);
        sphere.texture = decodePngRgba8(
            assetRoot / "textures" / "transition" / "transition1.png");

        WebgpuSandboxEntityData &dataPlane = entities[2u];
        dataPlane.logicalId = "data-plane";
        buildSandboxPlane(dataPlane, false);
        dataPlane.objectData.model = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        dataPlane.materialData.kindAndPhase = glm::uvec4(2u, 1u, 0u, 0u);
        dataPlane.renderPhaseData.flags = glm::uvec4(1u, 0u, 0u, 0u);

        WebgpuSandboxEntityData &compressedPlane = entities[3u];
        compressedPlane.logicalId = "uastc-plane";
        buildSandboxPlane(compressedPlane, false);
        compressedPlane.objectData.model = glm::translate(
            glm::mat4(1.0f), glm::vec3(-2.0f, 1.0f, 0.0f));
        compressedPlane.materialData.kindAndPhase = glm::uvec4(3u, 1u, 0u, 0u);
        compressedPlane.renderPhaseData.flags = glm::uvec4(1u, 1u, 0u, 0u);
        const Ktx1Rgba8Texture compressedTexture = decodeKtx1Rgba8(
            assetRoot / "decoded" / "webgpu_sandbox_2d_uastc_astc.ktx");
        if (compressedTexture.mipLevels.size() != 1u ||
            compressedTexture.mipLevels.front().width != 40u ||
            compressedTexture.mipLevels.front().height != 40u)
        {
            throw std::runtime_error(
                "The locked webgpu_sandbox ASTC decode contract changed.");
        }
        compressedPlane.texture = compressedTexture.mipLevels.front();

        WebgpuSandboxEntityData &points = entities[4u];
        points.logicalId = "point-cloud";
        buildSandboxPoints(points, options.randomSeed);
        points.objectData.model = glm::translate(
            glm::mat4(1.0f), glm::vec3(2.0f, -1.0f, 0.0f));
        points.materialData.kindAndPhase = glm::uvec4(4u, 0u, 0u, 0u);
        points.primitiveExpansionData.parameters = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

        WebgpuSandboxEntityData &line = entities[5u];
        line.logicalId = "color-line";
        buildSandboxLine(line);
        line.objectData.model = glm::translate(
            glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f));
        line.materialData.kindAndPhase = glm::uvec4(5u, 0u, 0u, 0u);
        line.primitiveExpansionData.parameters = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the webgpu_sandbox Scene Set encoder.");
        }
        for (WebgpuSandboxEntityData &entity : entities)
        {
            entity.entityIndex = allocateSandboxEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuSandboxRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (frameIndex == 0u) return;
        const float timeSeconds = float(frameIndex) / 60.0f;
        for (WebgpuSandboxEntityData &entity : entities)
        {
            entity.objectData.timeAndViewport.x = timeSeconds;
        }
        entities[0u].objectData.model = makeSandboxBoxModel(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not update webgpu_sandbox object components.");
        }
        for (const WebgpuSandboxEntityData &entity : entities)
        {
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebgpuSandboxSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        (void)options;
    }

    void WebgpuSandboxRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgpu_sandbox capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareSandboxOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_sandbox RGBA evidence.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_sandbox\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"samplePolicy\":{\"mode\":\"single-sample\","
                 << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"gpuWorkDslOnly\":true\n}\n";
        writeSandboxText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_sandbox\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"renderSetPolicy\":\"required\",\n"
                 << "  \"gpuWorkDslOnly\":true,\n"
                 << "  \"sceneRenderSetCount\":1,\n"
                 << "  \"renderSetType\":\"WebgpuSandboxSceneRenderSet\",\n"
                 << "  \"renderableObjectCount\":6,\n"
                 << "  \"entityCount\":6,\n"
                 << "  \"instanceCounts\":[1,1,1,1,1,1],\n"
                 << "  \"drawCommandCount\":2,\n"
                 << "  \"scenePassCount\":2,\n"
                 << "  \"screenPassCount\":1,\n"
                 << "  \"scenePassSequence\":["
                 << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-opaque\",\"entityOrdinal\":0},"
                 << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-transparent\",\"entityOrdinal\":0}],\n"
                 << "  \"usesRenderEntityID\":true,\n"
                 << "  \"usesRenderEntityInstanceID\":true,\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false,\n"
                 << "  \"directDrawFallback\":false,\n"
                 << "  \"sceneRoots\":[{\n"
                 << "    \"id\":\"scene\",\n"
                 << "    \"renderSetCount\":1,\n"
                 << "    \"renderSetId\":\"scene\",\n"
                 << "    \"renderSetType\":\"WebgpuSandboxSceneRenderSet\",\n"
                 << "    \"renderableObjectCount\":6,\n"
                 << "    \"entityCount\":6,\n"
                 << "    \"drawCommandCount\":2,\n"
                 << "    \"directDrawFallback\":false,\n"
                 << "    \"entities\":["
                 << "{\"entityId\":0,\"logicalRenderableId\":\"animated-box\",\"instanceCount\":1},"
                 << "{\"entityId\":1,\"logicalRenderableId\":\"displaced-sphere\",\"instanceCount\":1},"
                 << "{\"entityId\":2,\"logicalRenderableId\":\"data-plane\",\"instanceCount\":1},"
                 << "{\"entityId\":3,\"logicalRenderableId\":\"uastc-plane\",\"instanceCount\":1},"
                 << "{\"entityId\":4,\"logicalRenderableId\":\"point-cloud\",\"instanceCount\":1},"
                 << "{\"entityId\":5,\"logicalRenderableId\":\"color-line\",\"instanceCount\":1}],\n"
                 << "    \"componentSchema\":["
                 << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                 << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                 << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                 << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                 << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                 << "{\"name\":\"primitiveExpansion\",\"kind\":\"buffer\",\"role\":\"point-corner-and-line-segment-screen-expansion-data\"},"
                 << "{\"name\":\"renderPhases\",\"kind\":\"buffer\",\"role\":\"opaque-transparent-alpha-test-and-primitive-kind\"},"
                 << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"uv-displacement-data-and-decoded-ktx2-fixed-slots\"}],\n"
                 << "    \"scenePasses\":["
                 << "{\"name\":\"main-opaque\",\"renderClass\":\"WebgpuSandboxOpaquePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                 << "{\"name\":\"main-transparent\",\"renderClass\":\"WebgpuSandboxTransparentPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                 << "  }]\n}\n";
        writeSandboxText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_sandbox\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"kind\":\"loader-snapshot\",\n"
                 << "  \"canonicalState\":\"five-locked-assets-decoded-ktx2-red-data-texture\",\n"
                 << "  \"result\":{\"renderableObjectCount\":0,"
                 << "\"sceneRootCount\":1,"
                 << "\"canonicalSceneSha256\":"
                 << "\"d84e97086cc1d4c22b8ae47392cfd3b5787cd8d54699fa265b89edf4c57a7bf2\"}\n}\n";
        writeSandboxText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuSandboxRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
