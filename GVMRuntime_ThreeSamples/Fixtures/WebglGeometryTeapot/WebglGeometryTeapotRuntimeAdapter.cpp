#include "WebglGeometryTeapotRuntimeAdapter.hpp"

#include "WebglGeometryTeapotData.hpp"
#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

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
        constexpr uint32_t TeapotSegments = 15u;
        constexpr GVM::Core::RenderSetHandle TeapotSceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr const char *PisaFaceNames[6u] = {
            "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

        /** Stores one indexed patch mesh before camera-facing triangle expansion. */
        struct TeapotIndexedMesh final
        {
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec3> normals;
            eastl::vector<glm::vec2> uvs;
            eastl::vector<uint32_t> indices;
        };

        /** Converts one authored sRGB channel to Three's linear working space. */
        float teapotSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns cubic Bernstein weights and their first derivatives. */
        void evaluateTeapotBasis(
            double parameter,
            double (&basis)[4u],
            double (&derivative)[4u])
        {
            const double inverse = 1.0 - parameter;
            const double inverseSquared = inverse * inverse;
            const double parameterSquared = parameter * parameter;
            basis[0u] = inverseSquared * inverse;
            basis[1u] = 3.0 * parameter * inverseSquared;
            basis[2u] = 3.0 * parameterSquared * inverse;
            basis[3u] = parameterSquared * parameter;
            derivative[0u] = -3.0 * inverseSquared;
            derivative[1u] = 3.0 * inverseSquared - 6.0 * parameter * inverse;
            derivative[2u] = 6.0 * parameter * inverse - 3.0 * parameterSquared;
            derivative[3u] = 3.0 * parameterSquared;
        }

        /** Returns whether one triangle contains two equal Float32 positions. */
        bool teapotTriangleIsDegenerate(
            const TeapotIndexedMesh &mesh,
            uint32_t first,
            uint32_t second,
            uint32_t third)
        {
            return mesh.positions[first] == mesh.positions[second] ||
                mesh.positions[first] == mesh.positions[third] ||
                mesh.positions[second] == mesh.positions[third];
        }

        /** Tessellates all 32 r185 patches at the canonical level 15. */
        TeapotIndexedMesh buildTeapotIndexedMesh()
        {
            TeapotIndexedMesh mesh;
            constexpr uint32_t VerticesPerPatch =
                (TeapotSegments + 1u) * (TeapotSegments + 1u);
            mesh.positions.reserve(32u * VerticesPerPatch);
            mesh.normals.reserve(32u * VerticesPerPatch);
            mesh.uvs.reserve(32u * VerticesPerPatch);
            mesh.indices.reserve(32u * TeapotSegments * TeapotSegments * 6u);
            constexpr double MaximumHeight = 3.15;
            constexpr double HalfHeight = MaximumHeight * 0.5;
            constexpr double TrueSize = 300.0 / HalfHeight;
            for (uint32_t patch = 0u; patch < 32u; ++patch)
            {
                const uint32_t patchVertexBase =
                    static_cast<uint32_t>(mesh.positions.size());
                for (uint32_t sStep = 0u;
                     sStep <= TeapotSegments;
                     ++sStep)
                {
                    double sBasis[4u];
                    double sDerivative[4u];
                    evaluateTeapotBasis(
                        double(sStep) / double(TeapotSegments),
                        sBasis,
                        sDerivative);
                    for (uint32_t tStep = 0u;
                         tStep <= TeapotSegments;
                         ++tStep)
                    {
                        double tBasis[4u];
                        double tDerivative[4u];
                        evaluateTeapotBasis(
                            double(tStep) / double(TeapotSegments),
                            tBasis,
                            tDerivative);
                        glm::dvec3 position(0.0);
                        glm::dvec3 sDirection(0.0);
                        glm::dvec3 tDirection(0.0);
                        for (uint32_t row = 0u; row < 4u; ++row)
                        {
                            for (uint32_t column = 0u;
                                 column < 4u;
                                 ++column)
                            {
                                const uint32_t pointIndex =
                                    TeapotPatchIndices[
                                        patch * 16u + row * 4u + column];
                                const glm::dvec3 controlPoint(
                                    TeapotControlPoints[pointIndex * 3u],
                                    TeapotControlPoints[pointIndex * 3u + 1u],
                                    TeapotControlPoints[pointIndex * 3u + 2u]);
                                position += controlPoint *
                                    sBasis[row] * tBasis[column];
                                sDirection += controlPoint *
                                    sDerivative[row] * tBasis[column];
                                tDirection += controlPoint *
                                    sBasis[row] * tDerivative[column];
                            }
                        }
                        glm::dvec3 patchNormal =
                            glm::cross(tDirection, sDirection);
                        const double normalLength = glm::length(patchNormal);
                        if (normalLength > 0.0) patchNormal /= normalLength;
                        glm::dvec3 outputNormal;
                        if (std::abs(position.x) <= 1.0e-12 &&
                            std::abs(position.y) <= 1.0e-12)
                        {
                            outputNormal = glm::dvec3(
                                0.0,
                                position.z > HalfHeight ? 1.0 : -1.0,
                                0.0);
                        }
                        else
                        {
                            outputNormal = glm::dvec3(
                                patchNormal.x,
                                patchNormal.z,
                                -patchNormal.y);
                        }
                        mesh.positions.push_back(glm::vec3(
                            float(TrueSize * position.x),
                            float(TrueSize * (position.z - HalfHeight)),
                            float(-TrueSize * position.y)));
                        mesh.normals.push_back(glm::vec3(outputNormal));
                        mesh.uvs.push_back(glm::vec2(
                            1.0f - float(tStep) / float(TeapotSegments),
                            1.0f - float(sStep) / float(TeapotSegments)));
                    }
                }
                constexpr uint32_t RowStride = TeapotSegments + 1u;
                for (uint32_t sStep = 0u;
                     sStep < TeapotSegments;
                     ++sStep)
                {
                    for (uint32_t tStep = 0u;
                         tStep < TeapotSegments;
                         ++tStep)
                    {
                        const uint32_t first = patchVertexBase +
                            sStep * RowStride + tStep;
                        const uint32_t second = first + 1u;
                        const uint32_t third = second + RowStride;
                        const uint32_t fourth = first + RowStride;
                        if (!teapotTriangleIsDegenerate(
                                mesh, first, second, third))
                        {
                            mesh.indices.insert(
                                mesh.indices.end(),
                                {first, second, third});
                        }
                        if (!teapotTriangleIsDegenerate(
                                mesh, first, third, fourth))
                        {
                            mesh.indices.insert(
                                mesh.indices.end(),
                                {first, third, fourth});
                        }
                    }
                }
            }
            if (mesh.positions.size() != 8192u ||
                mesh.indices.size() != 42840u)
            {
                throw std::runtime_error(
                    "The r185 level-15 teapot topology changed.");
            }
            return mesh;
        }

        /** Expands indexed triangles and flips smooth normals for fixed-camera back faces. */
        void buildTeapotRenderVertices(
            const TeapotIndexedMesh &mesh,
            const glm::vec3 &cameraPosition,
            eastl::vector<WebglGeometryTeapotHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(mesh.indices.size());
            indices.reserve(mesh.indices.size());
            for (uint32_t triangleOffset = 0u;
                 triangleOffset < mesh.indices.size();
                 triangleOffset += 3u)
            {
                const uint32_t first = mesh.indices[triangleOffset];
                const uint32_t second = mesh.indices[triangleOffset + 1u];
                const uint32_t third = mesh.indices[triangleOffset + 2u];
                const glm::vec3 center =
                    (mesh.positions[first] + mesh.positions[second] +
                     mesh.positions[third]) / 3.0f;
                const glm::vec3 faceNormal = glm::cross(
                    mesh.positions[second] - mesh.positions[first],
                    mesh.positions[third] - mesh.positions[first]);
                const bool backFacing =
                    glm::dot(faceNormal, cameraPosition - center) < 0.0f;
                const uint32_t sourceIndices[3u] = {first, second, third};
                for (const uint32_t sourceIndex : sourceIndices)
                {
                    const glm::vec3 normal = backFacing
                        ? -mesh.normals[sourceIndex]
                        : mesh.normals[sourceIndex];
                    vertices.push_back({
                        glm::vec4(mesh.positions[sourceIndex], 1.0f),
                        glm::vec4(normal, 0.0f),
                        glm::vec4(mesh.uvs[sourceIndex], 0.0f, 0.0f)});
                    indices.push_back(
                        static_cast<uint32_t>(indices.size()));
                }
            }
        }

        /** Appends one typed CPU payload to a RenderSet allocation. */
        void appendTeapotBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *label,
            const void *value,
            size_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = label,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareTeapotOutput(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }
    }

    void WebglGeometryTeapotRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        const bool reflective = options.scenarioId == "reflective" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgl_geometry_teapot" ||
            (!initial && !reflective) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "Teapot requires one locked 800x500 Manifest scenario.");
        }
        device = inDevice;
        const glm::vec3 cameraPosition(-600.0f, 550.0f, 1300.0f);
        const TeapotIndexedMesh mesh = buildTeapotIndexedMesh();
        buildTeapotRenderVertices(
            mesh,
            cameraPosition,
            vertices,
            indices);
        const glm::mat4 view = glm::lookAtRH(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspectiveRH_ZO(
            45.0f * Pi / 180.0f,
            800.0f / 500.0f,
            1.0f,
            80000.0f);
        projection[1u][1u] *= -1.0f;
        WebglGeometryTeapotHostObjectData objectData{
            .modelViewProjection = projection * view,
            .cameraPosition = glm::vec4(cameraPosition, 1.0f),
            .directionalLight = glm::vec4(
                glm::normalize(glm::vec3(0.32f, 0.39f, 0.7f)),
                0.0f),
            .cameraRightAndTanHalfFov = glm::vec4(
                glm::normalize(glm::cross(
                    glm::normalize(-cameraPosition),
                    glm::vec3(0.0f, 1.0f, 0.0f))),
                std::tan(22.5f * Pi / 180.0f)),
            .cameraUpAndAspect = glm::vec4(
                glm::normalize(glm::cross(
                    glm::normalize(glm::cross(
                        glm::normalize(-cameraPosition),
                        glm::vec3(0.0f, 1.0f, 0.0f))),
                    glm::normalize(-cameraPosition))),
                800.0f / 500.0f),
            .cameraForwardAndReserved = glm::vec4(
                glm::normalize(-cameraPosition),
                0.0f),
        };
        WebglGeometryTeapotHostInstanceData instanceData{
            .reserved = glm::vec4(0.0f),
        };
        const float diffuse = reflective
            ? 1.0f
            : teapotSrgbToLinear(192.0f / 255.0f);
        const float specular = reflective
            ? teapotSrgbToLinear(17.0f / 255.0f)
            : teapotSrgbToLinear(64.0f / 255.0f);
        const float ambient = teapotSrgbToLinear(124.0f / 255.0f) * 2.0f;
        WebglGeometryTeapotHostMaterialData materialData{
            .diffuseAndShininess = glm::vec4(
                diffuse,
                diffuse,
                diffuse,
                reflective ? 30.0f : 300.0f),
            .specularAndMode = glm::vec4(
                specular, specular, specular, reflective ? 1.0f : 0.0f),
            .ambientAndDirectionalIntensity =
                glm::vec4(ambient, 2.0f, 0.0f, 0.0f),
        };
        const auto encoder = renderer.createRenderSetCommandEncoder(
            TeapotSceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the teapot Scene RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendTeapotBufferPayload(
            allocation,
            WebglGeometryTeapotSceneRenderSetComponents::vertices,
            "WebglGeometryTeapotVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendTeapotBufferPayload(
            allocation,
            WebglGeometryTeapotSceneRenderSetComponents::indices,
            "WebglGeometryTeapotIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendTeapotBufferPayload(
            allocation,
            WebglGeometryTeapotSceneRenderSetComponents::objects,
            "WebglGeometryTeapotObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendTeapotBufferPayload(
            allocation,
            WebglGeometryTeapotSceneRenderSetComponents::instances,
            "WebglGeometryTeapotInstance",
            &instanceData,
            sizeof(instanceData),
            1u);
        appendTeapotBufferPayload(
            allocation,
            WebglGeometryTeapotSceneRenderSetComponents::materials,
            "WebglGeometryTeapotMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
        textureInfo.textureComponentHandle =
            WebglGeometryTeapotSceneRenderSetComponents::textures;
        const std::filesystem::path cubeRoot =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "cube" / "pisa";
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            const RgbaImageData baseImage = decodePngRgba8(
                cubeRoot / PisaFaceNames[face]);
            if (baseImage.width != 256u || baseImage.height != 256u)
            {
                throw std::runtime_error(
                    "One Pisa face decoded to an unexpected extent.");
            }
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImage);
            if (mipChain.size() != 9u)
            {
                throw std::runtime_error(
                    "One Pisa face produced an unexpected mip count.");
            }
            environmentPixels[face].clear();
            environmentMipOffsets[face].clear();
            for (const RgbaImageData &mip : mipChain)
            {
                environmentMipOffsets[face].push_back(
                    environmentPixels[face].size());
                environmentPixels[face].insert(
                    environmentPixels[face].end(),
                    mip.pixels.begin(),
                    mip.pixels.end());
            }
            textureInfo.textures.push_back({
                .textureName = PisaFaceNames[face],
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = 256u,
                .height = 256u,
                .data = environmentPixels[face].data(),
                .dataStorageBytes = environmentPixels[face].size(),
                .mipmapOffsetBytes = environmentMipOffsets[face],
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureInfo));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(TeapotSceneSetHandle, encoder);
    }

    void WebglGeometryTeapotRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryTeapotRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Teapot capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareTeapotOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareTeapotOutput(options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,"
                << "\n  \"source\":\"gvm-three-r185\","
                << "\n  \"caseId\":\"webgl_geometry_teapot\","
                << "\n  \"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                << "\n  \"pipeline\":\"" << options.pipeline.c_str() << "\","
                << "\n  \"backend\":\"" << threeSampleBackendName(options.backend) << "\","
                << "\n  \"frame\":" << frameIndex << ","
                << "\n  \"randomSeed\":" << options.randomSeed << ","
                << "\n  \"width\":" << width << ","
                << "\n  \"height\":" << height << ","
                << "\n  \"rowStrideBytes\":" << uint64_t(width) * 4u << ","
                << "\n  \"byteCount\":" << rgba.size() << ","
                << "\n  \"format\":\"rgba8unorm\","
                << "\n  \"samplePolicy\":{\"mode\":\"single-sample\","
                << "\"msaaEnabled\":false,\"simulateMsaa\":false}"
                << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareTeapotOutput(options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output
                << "{\n  \"caseId\":\"webgl_geometry_teapot\","
                << "\n  \"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                << "\n  \"frame\":" << frameIndex << ","
                << "\n  \"implementationLevel\":\"semantic-complete\","
                << "\n  \"gpuWorkDslOnly\":true,"
                << "\n  \"renderSetPolicy\":\"required\","
                << "\n  \"sceneRenderSetCount\":1,"
                << "\n  \"renderSetType\":\"WebglGeometryTeapotSceneRenderSet\","
                << "\n  \"renderableObjectCount\":1,"
                << "\n  \"entityCount\":1,"
                << "\n  \"instanceCounts\":[1],"
                << "\n  \"scenePassCount\":1,"
                << "\n  \"screenPassCount\":1,"
                << "\n  \"drawCommandCount\":1,"
                << "\n  \"usesRenderEntityID\":true,"
                << "\n  \"usesRenderEntityInstanceID\":true,"
                << "\n  \"sampleCount\":1,"
                << "\n  \"cpuTeapotTessellation\":true,"
                << "\n  \"tessellationLevel\":15,"
                << "\n  \"vertexCount\":" << vertices.size() << ","
                << "\n  \"indexCount\":" << indices.size() << ","
                << "\n  \"msaaEnabled\":false,"
                << "\n  \"simulateMsaa\":false,"
                << "\n  \"directDrawFallback\":false,"
                << "\n  \"sceneRoots\":[{\"id\":\"scene\","
                << "\"renderSetCount\":1,"
                << "\"renderSetId\":\"webgl-geometry-teapot-scene-set\","
                << "\"renderSetType\":\"WebglGeometryTeapotSceneRenderSet\","
                << "\"renderableObjectCount\":1,"
                << "\"entityCount\":1,"
                << "\"entities\":[{\"entityId\":0,"
                << "\"logicalRenderableId\":\"teapot\",\"instanceCount\":1}],"
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"drawCommandCount\":1,"
                << "\"directDrawFallback\":false,"
                << "\"scenePasses\":[{\"name\":\"teapot\","
                << "\"renderClass\":\"WebglGeometryTeapotTeapotPass\","
                << "\"renderSetId\":\"webgl-geometry-teapot-scene-set\","
                << "\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,"
                << "\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]}]\n}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryTeapotRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        for (eastl::vector<uint8_t> &pixels : environmentPixels)
        {
            pixels.clear();
        }
        for (eastl::vector<uint64_t> &offsets : environmentMipOffsets)
        {
            offsets.clear();
        }
    }
} // namespace GVM::ThreeSamples
