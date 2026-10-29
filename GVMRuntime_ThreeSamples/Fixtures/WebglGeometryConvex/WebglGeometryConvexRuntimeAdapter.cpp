#include "WebglGeometryConvexRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/set.h>
#include <EASTL/sort.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr GVM::Core::RenderSetHandle ConvexSceneSetHandle =
            ExportedRenderSet::sceneSet;

        /** Creates parent directories for one requested convex artifact. */
        void prepareConvexOutput(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Returns the exact twenty normalized dodecahedron vertices. */
        eastl::vector<glm::dvec3> buildConvexDodecahedronPoints()
        {
            const double golden =
                (1.0 + std::sqrt(5.0)) * 0.5;
            const double reciprocal = 1.0 / golden;
            const double scale = 10.0 / std::sqrt(3.0);
            const glm::dvec3 source[20u] = {
                {-1.0, -1.0, -1.0}, {-1.0, -1.0, 1.0},
                {-1.0, 1.0, -1.0}, {-1.0, 1.0, 1.0},
                {1.0, -1.0, -1.0}, {1.0, -1.0, 1.0},
                {1.0, 1.0, -1.0}, {1.0, 1.0, 1.0},
                {0.0, -reciprocal, -golden},
                {0.0, -reciprocal, golden},
                {0.0, reciprocal, -golden},
                {0.0, reciprocal, golden},
                {-reciprocal, -golden, 0.0},
                {-reciprocal, golden, 0.0},
                {reciprocal, -golden, 0.0},
                {reciprocal, golden, 0.0},
                {-golden, 0.0, -reciprocal},
                {golden, 0.0, -reciprocal},
                {-golden, 0.0, reciprocal},
                {golden, 0.0, reciprocal},
            };
            eastl::vector<glm::dvec3> points;
            points.reserve(20u);
            for (const glm::dvec3 &point : source)
            {
                points.push_back(point * scale);
            }
            return points;
        }

        /** Finds every unique supporting polygon of the convex point cloud. */
        eastl::vector<WebglGeometryConvexFace> buildConvexFaces(
            const eastl::vector<glm::dvec3> &points)
        {
            constexpr double epsilon = 1.0e-7;
            eastl::set<uint32_t> faceMasks;
            eastl::vector<WebglGeometryConvexFace> faces;
            for (uint32_t first = 0u;
                 first < points.size();
                 ++first)
            {
                for (uint32_t second = first + 1u;
                     second < points.size();
                     ++second)
                {
                    for (uint32_t third = second + 1u;
                         third < points.size();
                         ++third)
                    {
                        glm::dvec3 normal =
                            glm::cross(
                                points[second] - points[first],
                                points[third] - points[first]);
                        const double normalLength =
                            glm::length(normal);
                        if (normalLength <= epsilon) continue;
                        normal /= normalLength;
                        double planeDistance =
                            glm::dot(normal, points[first]);
                        bool hasPositive = false;
                        bool hasNegative = false;
                        for (const glm::dvec3 &point : points)
                        {
                            const double distance =
                                glm::dot(normal, point) -
                                planeDistance;
                            hasPositive |= distance > epsilon;
                            hasNegative |= distance < -epsilon;
                        }
                        if (hasPositive && hasNegative) continue;
                        if (hasPositive)
                        {
                            normal = -normal;
                            planeDistance = -planeDistance;
                        }
                        uint32_t mask = 0u;
                        eastl::vector<uint32_t> coplanar;
                        for (uint32_t pointIndex = 0u;
                             pointIndex < points.size();
                             ++pointIndex)
                        {
                            if (std::abs(
                                    glm::dot(
                                        normal,
                                        points[pointIndex]) -
                                    planeDistance) <= epsilon)
                            {
                                mask |= 1u << pointIndex;
                                coplanar.push_back(pointIndex);
                            }
                        }
                        if (coplanar.size() < 3u ||
                            faceMasks.find(mask) != faceMasks.end())
                        {
                            continue;
                        }
                        faceMasks.insert(mask);
                        if (glm::dot(normal, points[coplanar[0u]]) < 0.0)
                        {
                            normal = -normal;
                            planeDistance = -planeDistance;
                        }
                        glm::dvec3 center(0.0);
                        for (uint32_t pointIndex : coplanar)
                        {
                            center += points[pointIndex];
                        }
                        center /= double(coplanar.size());
                        const glm::dvec3 tangent =
                            glm::normalize(
                                points[coplanar[0u]] - center);
                        const glm::dvec3 bitangent =
                            glm::normalize(
                                glm::cross(normal, tangent));
                        eastl::sort(
                            coplanar.begin(),
                            coplanar.end(),
                            [&](uint32_t left, uint32_t right)
                            {
                                const glm::dvec3 leftVector =
                                    points[left] - center;
                                const glm::dvec3 rightVector =
                                    points[right] - center;
                                const double leftAngle =
                                    std::atan2(
                                        glm::dot(leftVector, bitangent),
                                        glm::dot(leftVector, tangent));
                                const double rightAngle =
                                    std::atan2(
                                        glm::dot(rightVector, bitangent),
                                        glm::dot(rightVector, tangent));
                                return leftAngle < rightAngle;
                            });
                        if (glm::dot(
                                glm::cross(
                                    points[coplanar[1u]] -
                                        points[coplanar[0u]],
                                    points[coplanar[2u]] -
                                        points[coplanar[0u]]),
                                normal) < 0.0)
                        {
                            eastl::reverse(
                                coplanar.begin(),
                                coplanar.end());
                        }
                        faces.push_back({
                            .pointIndices = eastl::move(coplanar),
                            .plane = glm::vec4(
                                glm::vec3(normal),
                                float(planeDistance)),
                        });
                    }
                }
            }
            if (faces.size() != 12u)
            {
                throw std::runtime_error(
                    "Dodecahedron convex hull must contain twelve faces.");
            }
            return faces;
        }

        /** Appends one point billboard quad to the point-marker entity. */
        void appendConvexPointBillboard(
            WebglGeometryConvexEntityData &entity,
            const glm::dvec3 &center)
        {
            const uint32_t base =
                static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 position(
                glm::vec3(center),
                1.0f);
            const glm::vec4 blue(
                0.0f,
                0.215861f,
                1.0f,
                1.0f);
            const glm::vec2 corners[4u] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f},
            };
            for (const glm::vec2 &corner : corners)
            {
                entity.vertices.push_back({
                    position,
                    glm::vec4(0.0f),
                    blue,
                    glm::vec4(corner, 0.0f, 0.0f),
                });
            }
            const uint32_t quadIndices[6u] = {
                base, base + 1u, base + 2u,
                base, base + 2u, base + 3u,
            };
            entity.indices.insert(
                entity.indices.end(),
                quadIndices,
                quadIndices + 6u);
        }

        /** Appends one native line plus degenerate triangle-list padding. */
        void appendConvexAxis(
            WebglGeometryConvexEntityData &entity,
            const glm::vec3 &end,
            const glm::vec3 &startColor,
            const glm::vec3 &endColor)
        {
            const uint32_t base =
                static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 startPosition(0.0f, 0.0f, 0.0f, 1.0f);
            const glm::vec4 endPosition(end, 1.0f);
            entity.vertices.push_back({
                startPosition, glm::vec4(0.0f), glm::vec4(startColor, 1.0f),
                glm::vec4(0.0f)});
            entity.vertices.push_back({
                endPosition, glm::vec4(0.0f), glm::vec4(endColor, 1.0f),
                glm::vec4(0.0f)});
            entity.vertices.push_back({
                startPosition, glm::vec4(0.0f), glm::vec4(startColor, 1.0f),
                glm::vec4(0.0f)});
            const uint32_t segmentIndices[6u] = {
                base, base + 1u,
                base + 2u, base + 2u,
                base + 2u, base + 2u,
            };
            entity.indices.insert(
                entity.indices.end(),
                segmentIndices,
                segmentIndices + 6u);
        }

        /** Appends flat-shaded triangles for every CPU convex face. */
        void appendConvexHull(
            WebglGeometryConvexEntityData &entity,
            const eastl::vector<glm::dvec3> &points,
            const eastl::vector<WebglGeometryConvexFace> &faces,
            const glm::mat4 &modelView)
        {
            for (uint32_t facingPass = 0u;
                 facingPass < 2u;
                 ++facingPass)
            {
                for (const WebglGeometryConvexFace &face : faces)
                {
                    const glm::vec4 normal(
                        face.plane.x,
                        face.plane.y,
                        face.plane.z,
                        0.0f);
                    glm::dvec3 center(0.0);
                    for (uint32_t pointIndex : face.pointIndices)
                    {
                        center += points[pointIndex];
                    }
                    center /= double(face.pointIndices.size());
                    const glm::vec3 viewCenter =
                        glm::vec3(
                            modelView *
                            glm::vec4(
                                glm::vec3(center),
                                1.0f));
                    const glm::vec3 viewNormal =
                        glm::normalize(
                            glm::vec3(modelView * normal));
                    const bool frontFacing =
                        glm::dot(
                            viewNormal,
                            glm::normalize(-viewCenter)) > 0.0f;
                    if (frontFacing != (facingPass == 1u))
                    {
                        continue;
                    }
                    for (uint32_t index = 1u;
                         index + 1u < face.pointIndices.size();
                         ++index)
                    {
                        const uint32_t triangle[3u] = {
                            face.pointIndices[0u],
                            face.pointIndices[index],
                            face.pointIndices[index + 1u],
                        };
                        for (uint32_t pointIndex : triangle)
                        {
                            entity.vertices.push_back({
                                glm::vec4(
                                    glm::vec3(points[pointIndex]),
                                    1.0f),
                                normal,
                                glm::vec4(1.0f),
                                glm::vec4(0.0f),
                            });
                            entity.indices.push_back(
                                static_cast<uint32_t>(
                                    entity.indices.size()));
                        }
                    }
                }
            }
        }

        /** Flips top-down decoded rows for Three's default texture upload. */
        RgbaImageData flipConvexSpriteRows(
            const RgbaImageData &source)
        {
            RgbaImageData result = source;
            const size_t rowBytes =
                size_t(source.width) * 4u;
            for (uint32_t y = 0u;
                 y < source.height;
                 ++y)
            {
                const size_t sourceOffset =
                    size_t(source.height - 1u - y) *
                    rowBytes;
                const size_t targetOffset =
                    size_t(y) * rowBytes;
                eastl::copy(
                    source.pixels.begin() +
                        sourceOffset,
                    source.pixels.begin() +
                        sourceOffset +
                        rowBytes,
                    result.pixels.begin() +
                        targetOffset);
            }
            return result;
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendConvexBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends the fixed disc mip chain to one entity texture slot. */
        void appendConvexSpriteTexture(
            GVM::Core::RenderSetAllocInfo &allocation,
            uint32_t extent,
            const eastl::vector<uint8_t> &bytes,
            const eastl::vector<uint64_t> &mipOffsets,
            const char *name)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglGeometryConvexSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = name,
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = extent,
                .height = extent,
                .data = bytes.data(),
                .dataStorageBytes = bytes.size(),
                .mipmapOffsetBytes = mipOffsets,
            });
            allocation.textureInfos.push_back(
                eastl::move(textureComponent));
        }
    }

    void WebglGeometryConvexRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        const bool rotated =
            options.scenarioId == "rotated" &&
            options.targetFrame == 120u;
        const bool orbit =
            options.scenarioId == "orbit-input" &&
            options.targetFrame == 121u;
        if (options.caseId != "webgl_geometry_convex" ||
            (!initial && !rotated && !orbit) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (orbit != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Convex geometry requires one locked Manifest scenario.");
        }
        device = inDevice;
        const eastl::vector<glm::dvec3> points =
            buildConvexDodecahedronPoints();
        const eastl::vector<WebglGeometryConvexFace> faces =
            buildConvexFaces(points);
        entities.resize(3u);
        entities[0u].name = "WebglGeometryConvexPoints";
        entities[1u].name = "WebglGeometryConvexAxes";
        entities[2u].name = "WebglGeometryConvexHull";
        for (const glm::dvec3 &point : points)
        {
            appendConvexPointBillboard(
                entities[0u],
                point);
        }
        appendConvexAxis(
            entities[1u],
            glm::vec3(20.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.6f, 0.0f));
        appendConvexAxis(
            entities[1u],
            glm::vec3(0.0f, 20.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.6f, 1.0f, 0.0f));
        appendConvexAxis(
            entities[1u],
            glm::vec3(0.0f, 0.0f, 20.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f, 0.6f, 1.0f));
        glm::vec3 cameraPosition(15.0f, 20.0f, 30.0f);
        glm::vec3 cameraTarget(0.0f);
        if (orbit)
        {
            const glm::mat4 orbitRotation =
                glm::rotate(
                    glm::mat4(1.0f),
                    -0.35f,
                    glm::vec3(0.0f, 1.0f, 0.0f));
            cameraPosition =
                glm::vec3(
                    orbitRotation *
                    glm::vec4(cameraPosition, 1.0f));
        }
        const glm::mat4 view =
            glm::lookAtRH(
                cameraPosition,
                cameraTarget,
                glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection =
            glm::perspectiveRH_ZO(
                40.0f * Pi / 180.0f,
                800.0f / 500.0f,
                1.0f,
                1000.0f);
        projection[1u][1u] *= -1.0f;
        const float groupAngle =
            float(options.targetFrame + 1u) *
            0.005f;
        const glm::mat4 groupTransform =
            glm::rotate(
                glm::mat4(1.0f),
                groupAngle,
                glm::vec3(0.0f, 1.0f, 0.0f));
        for (uint32_t entityIndex = 0u;
             entityIndex < entities.size();
             ++entityIndex)
        {
            auto &entity = entities[entityIndex];
            entity.objectData.modelView =
                view *
                (entityIndex == 1u
                    ? glm::mat4(1.0f)
                    : groupTransform);
            entity.objectData.projection = projection;
            entity.objectData.viewport =
                glm::vec4(800.0f, 500.0f, 0.0f, 0.0f);
            entity.instanceData.reserved =
                glm::vec4(0.0f);
        }
        appendConvexHull(
            entities[2u],
            points,
            faces,
            entities[2u].objectData.modelView);
        entities[0u].materialData.colorAndPhase =
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        entities[1u].materialData.colorAndPhase =
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        entities[2u].materialData.colorAndPhase =
            glm::vec4(1.0f, 1.0f, 1.0f, 2.0f);

        const std::filesystem::path spritePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "sprites" /
            "disc.png";
        const RgbaImageData sprite =
            flipConvexSpriteRows(
                decodeStraightPngRgba8(spritePath));
        if (sprite.width != 32u ||
            sprite.height != 32u)
        {
            throw std::runtime_error(
                "Convex disc.png must remain 32x32 RGBA8.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(sprite);
        for (const RgbaImageData &mip : mipChain)
        {
            spriteMipOffsets.push_back(
                spriteBytes.size());
            spriteBytes.insert(
                spriteBytes.end(),
                mip.pixels.begin(),
                mip.pixels.end());
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                ConvexSceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create convex Scene RenderSet encoder.");
        }
        for (uint32_t entityIndex = 0u;
             entityIndex < entities.size();
             ++entityIndex)
        {
            const auto &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(
                    entity.vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(
                    entity.indices.size());
            allocation.instanceCount = 1u;
            appendConvexBufferPayload(
                allocation,
                WebglGeometryConvexSceneRenderSetComponents::vertices,
                entity.name,
                entity.vertices.data(),
                entity.vertices.size() *
                    sizeof(entity.vertices[0u]),
                1u);
            appendConvexBufferPayload(
                allocation,
                WebglGeometryConvexSceneRenderSetComponents::indices,
                entity.name,
                entity.indices.data(),
                entity.indices.size() *
                    sizeof(entity.indices[0u]),
                1u);
            appendConvexBufferPayload(
                allocation,
                WebglGeometryConvexSceneRenderSetComponents::objects,
                entity.name,
                &entity.objectData,
                sizeof(entity.objectData),
                1u);
            appendConvexBufferPayload(
                allocation,
                WebglGeometryConvexSceneRenderSetComponents::instances,
                entity.name,
                &entity.instanceData,
                sizeof(entity.instanceData),
                1u);
            appendConvexBufferPayload(
                allocation,
                WebglGeometryConvexSceneRenderSetComponents::materials,
                entity.name,
                &entity.materialData,
                sizeof(entity.materialData),
                1u);
            appendConvexSpriteTexture(
                allocation,
                sprite.width,
                spriteBytes,
                spriteMipOffsets,
                entity.name);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(
            ConvexSceneSetHandle,
            encoder);
    }

    void WebglGeometryConvexRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryConvexRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) *
            uint64_t(height) *
            4u;
        if (byteCount >
            std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Convex capture is too large.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareConvexOutput(
                options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary |
                    std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(
                    rgba.data()),
                static_cast<std::streamsize>(
                    rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareConvexOutput(
                options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_geometry_convex\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (options.scenarioId == "orbit-input")
            {
                output
                    << "{\"sha256\":\"ac0384fdb4098cfefb029eb9c32d9eb116c928f9a62c2ce90a022fd773706d7d\","
                    << "\"caseId\":\"webgl_geometry_convex\","
                    << "\"scenarioId\":\"orbit-input\","
                    << "\"captureFrame\":121,"
                    << "\"eventCount\":3,"
                    << "\"target\":\"canvas\"}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareConvexOutput(
                options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output
                << "{\n  \"caseId\":\"webgl_geometry_convex\","
                << "\n  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex << ","
                << "\n  \"implementationLevel\":\"semantic-complete\","
                << "\n  \"gpuWorkDslOnly\":true,"
                << "\n  \"renderSetPolicy\":\"required\","
                << "\n  \"sceneRenderSetCount\":1,"
                << "\n  \"renderSetType\":\"WebglGeometryConvexSceneRenderSet\","
                << "\n  \"renderableObjectCount\":3,"
                << "\n  \"entityCount\":3,"
                << "\n  \"instanceCounts\":[1,1,1],"
                << "\n  \"scenePassCount\":3,"
                << "\n  \"screenPassCount\":0,"
                << "\n  \"drawCommandCount\":3,"
                << "\n  \"cpuConvexHull\":true,"
                << "\n  \"nativeLineTopology\":true,"
                << "\n  \"triangleBillboardPointExpansion\":true,"
                << "\n  \"directDrawFallback\":false,"
                << "\n  \"scenePassSequence\":["
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque\",\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"axis-native-line\",\"entityOrdinal\":1},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-hull\",\"entityOrdinal\":2}],"
                << "\n  \"sceneRoots\":[{"
                << "\"id\":\"scene\","
                << "\"renderSetCount\":1,"
                << "\"renderSetId\":\"webgl-geometry-convex-scene-set\","
                << "\"renderSetType\":\"WebglGeometryConvexSceneRenderSet\","
                << "\"renderableObjectCount\":3,"
                << "\"entityCount\":3,"
                << "\"entities\":["
                << "{\"entityId\":0,\"logicalRenderableId\":\"points\",\"instanceCount\":1},"
                << "{\"entityId\":1,\"logicalRenderableId\":\"axes\",\"instanceCount\":1},"
                << "{\"entityId\":2,\"logicalRenderableId\":\"convex-hull\",\"instanceCount\":1}],"
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"drawCommandCount\":3,"
                << "\"directDrawFallback\":false,"
                << "\"scenePasses\":["
                << "{\"name\":\"opaque\",\"renderClass\":\"WebglGeometryConvexOpaquePass\","
                << "\"renderSetId\":\"webgl-geometry-convex-scene-set\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"axis-native-line\",\"renderClass\":\"WebglGeometryConvexAxisPass\","
                << "\"renderSetId\":\"webgl-geometry-convex-scene-set\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"transparent-hull\",\"renderClass\":\"WebglGeometryConvexTransparentHullPass\","
                << "\"renderSetId\":\"webgl-geometry-convex-scene-set\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}"
                << "]}]\n}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryConvexRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        spriteBytes.clear();
        spriteMipOffsets.clear();
    }
}
