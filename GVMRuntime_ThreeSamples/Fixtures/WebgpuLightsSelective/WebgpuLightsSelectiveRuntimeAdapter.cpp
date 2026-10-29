#include "WebgpuLightsSelectiveRuntimeAdapter.hpp"
#include "WebgpuLightsSelectiveDfgLut.hpp"

#include "../WebglGeometryTeapot/WebglGeometryTeapotData.hpp"
#include "Phase1TextureCases/GifImageDecoder.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Converts one sRGB channel to Three.js r185's linear-sRGB working space. */
        float webgpuLightsSelectiveSrgbToLinear(float channel)
        {
            const float normalized = std::clamp(channel, 0.0f, 1.0f);
            return normalized < 0.04045f
                ? normalized * 0.0773993808f
                : std::pow(normalized * 0.9478672986f + 0.0521327014f, 2.4f);
        }

        /** Converts a packed Three.js hexadecimal color to linear-sRGB components. */
        glm::vec3 webgpuLightsSelectiveHexColor(uint32_t hexColor)
        {
            return glm::vec3(
                webgpuLightsSelectiveSrgbToLinear(float((hexColor >> 16u) & 0xffu) / 255.0f),
                webgpuLightsSelectiveSrgbToLinear(float((hexColor >> 8u) & 0xffu) / 255.0f),
                webgpuLightsSelectiveSrgbToLinear(float(hexColor & 0xffu) / 255.0f));
        }

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebgpuLightsSelectivePath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Decodes one pinned JPEG texture through the sample asset decoder. */
        RgbaImageData decodeWebgpuLightsSelectiveTexture(
            const std::filesystem::path &assetPath)
        {
            return decodeJpegRgba8(assetPath);
        }


        /** Packs raw UNorm mip levels into the existing RenderSet texture ABI. */
        void packWebgpuLightsSelectiveMipChain(
            const eastl::vector<RgbaImageData> &levels,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &offsets)
        {
            bytes.clear();
            offsets.clear();
            for (const RgbaImageData &level : levels)
            {
                offsets.push_back(static_cast<uint64_t>(bytes.size()));
                bytes.insert(bytes.end(), level.pixels.begin(), level.pixels.end());
            }
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebgpuLightsSelectiveBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                                     GVM::Core::RenderComponentHandle component,
                                     const char *name,
                                     const void *data,
                                     uint64_t byteCount,
                                     uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Stores one indexed teapot surface before RenderSet upload. */
        struct WebgpuLightsSelectiveTeapotMesh final
        {
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec3> normals;
            eastl::vector<glm::vec2> uvs;
            eastl::vector<uint32_t> indices;
        };

        /** Computes cubic Bernstein weights and derivatives for a Bezier patch. */
        void evaluateWebgpuLightsSelectiveBasis(
            double parameter,
            double (&basis)[4u],
            double (&derivative)[4u])
        {
            const double inverse = 1.0 - parameter;
            basis[0u] = inverse * inverse * inverse;
            basis[1u] = 3.0 * parameter * inverse * inverse;
            basis[2u] = 3.0 * parameter * parameter * inverse;
            basis[3u] = parameter * parameter * parameter;
            derivative[0u] = -3.0 * inverse * inverse;
            derivative[1u] = 3.0 * inverse * inverse - 6.0 * parameter * inverse;
            derivative[2u] = 6.0 * parameter * inverse - 3.0 * parameter * parameter;
            derivative[3u] = 3.0 * parameter * parameter;
        }

        /** Builds the exact r185 TeapotGeometry(.8, 18) patch tessellation. */
        WebgpuLightsSelectiveTeapotMesh buildWebgpuLightsSelectiveTeapot()
        {
            constexpr uint32_t Segments = 18u;
            constexpr double MaxHeight = 3.15;
            constexpr double HalfHeight = MaxHeight * 0.5;
            constexpr double TrueSize = 0.8 / HalfHeight;
            constexpr uint32_t RowStride = Segments + 1u;
            WebgpuLightsSelectiveTeapotMesh mesh;
            mesh.positions.reserve(28u * RowStride * RowStride);
            mesh.normals.reserve(28u * RowStride * RowStride);
            mesh.uvs.reserve(28u * RowStride * RowStride);
            mesh.indices.reserve((8u * Segments * Segments + 16u * Segments * Segments -
                8u * Segments + 16u * Segments * Segments) * 3u);
            for (uint32_t surface = 0u; surface < 32u; ++surface)
            {
                const bool isBody = surface < 20u || surface >= 28u;
                const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
                for (uint32_t sStep = 0u; sStep <= Segments; ++sStep)
                {
                    double sBasis[4u];
                    double sDerivative[4u];
                    evaluateWebgpuLightsSelectiveBasis(double(sStep) / double(Segments), sBasis, sDerivative);
                    for (uint32_t tStep = 0u; tStep <= Segments; ++tStep)
                    {
                        double tBasis[4u];
                        double tDerivative[4u];
                        evaluateWebgpuLightsSelectiveBasis(double(tStep) / double(Segments), tBasis, tDerivative);
                        glm::dvec3 position(0.0);
                        glm::dvec3 sDirection(0.0);
                        glm::dvec3 tDirection(0.0);
                        for (uint32_t row = 0u; row < 4u; ++row)
                        {
                            for (uint32_t column = 0u; column < 4u; ++column)
                            {
                                const uint32_t pointIndex = TeapotPatchIndices[surface * 16u + row * 4u + column];
                                glm::dvec3 controlPoint(
                                    TeapotControlPoints[pointIndex * 3u],
                                    TeapotControlPoints[pointIndex * 3u + 1u],
                                    TeapotControlPoints[pointIndex * 3u + 2u]);
                                // TeapotGeometry(.8, 18) uses the r185 default
                                // fitLid=true and blinn=true path.  The default
                                // Blinn data is already normalized by maxHeight;
                                // only the lid XY fit is applied here.
                                if (surface >= 20u && surface < 28u)
                                {
                                    controlPoint.x *= 1.077;
                                    controlPoint.y *= 1.077;
                                }
                                const double weight = sBasis[row] * tBasis[column];
                                position += controlPoint * weight;
                                sDirection += controlPoint * sDerivative[row] * tBasis[column];
                                tDirection += controlPoint * sBasis[row] * tDerivative[column];
                            }
                        }
                        glm::dvec3 patchNormal = glm::cross(tDirection, sDirection);
                        const double normalLength = glm::length(patchNormal);
                        if (normalLength > 0.0) patchNormal /= normalLength;
                        glm::dvec3 outputNormal;
                        if (std::abs(position.x) <= 1.0e-12 && std::abs(position.y) <= 1.0e-12)
                        {
                            outputNormal = glm::dvec3(0.0, position.z > HalfHeight ? 1.0 : -1.0, 0.0);
                        }
                        else
                        {
                            outputNormal = glm::dvec3(patchNormal.x, patchNormal.z, -patchNormal.y);
                        }
                        mesh.positions.emplace_back(
                            float(TrueSize * position.x),
                            float(TrueSize * (position.z - HalfHeight)),
                            float(-TrueSize * position.y));
                        mesh.normals.emplace_back(glm::vec3(outputNormal));
                        mesh.uvs.emplace_back(
                            1.0f - float(tStep) / float(Segments),
                            1.0f - float(sStep) / float(Segments));
                    }
                }
                if (!isBody && surface >= 20u && surface < 28u)
                {
                    // The lid remains part of the same exact patch stream; this
                    // branch is intentionally explicit to make the source contract visible.
                }
                for (uint32_t sStep = 0u; sStep < Segments; ++sStep)
                {
                    for (uint32_t tStep = 0u; tStep < Segments; ++tStep)
                    {
                        const uint32_t first = base + sStep * RowStride + tStep;
                        const uint32_t second = first + 1u;
                        const uint32_t third = second + RowStride;
                        const uint32_t fourth = first + RowStride;
                        const glm::vec3 &a = mesh.positions[first];
                        const glm::vec3 &b = mesh.positions[second];
                        const glm::vec3 &c = mesh.positions[third];
                        const glm::vec3 &d = mesh.positions[fourth];
                        if (a != b && a != c && b != c) mesh.indices.insert(mesh.indices.end(), {first, second, third});
                        if (a != c && a != d && c != d) mesh.indices.insert(mesh.indices.end(), {first, third, fourth});
                    }
                }
            }
            if (mesh.positions.size() != 11552u || mesh.indices.size() != 61776u)
            {
                throw std::runtime_error("The r185 selective-light teapot topology changed.");
            }
            return mesh;
        }

        /** Converts one indexed teapot mesh to the RenderSet vertex layout. */
        void appendWebgpuLightsSelectiveTeapot(
            const WebgpuLightsSelectiveTeapotMesh &mesh,
            WebgpuLightsSelectiveEntity &entity)
        {
            entity.vertices.reserve(mesh.positions.size());
            entity.indices = mesh.indices;
            for (size_t index = 0u; index < mesh.positions.size(); ++index)
            {
                entity.vertices.push_back({
                    glm::vec4(mesh.positions[index], 1.0f),
                    glm::vec4(mesh.normals[index], 0.0f),
                    glm::vec4(mesh.uvs[index], 0.0f, 0.0f)});
            }
        }

        /** Generates the exact 16x8, radius-.1 light-marker SphereGeometry. */
        void appendWebgpuLightsSelectiveLightSphere(WebgpuLightsSelectiveEntity &entity)
        {
            constexpr uint32_t WidthSegments = 16u;
            constexpr uint32_t HeightSegments = 8u;
            constexpr float Radius = 0.1f;
            for (uint32_t iy = 0u; iy < HeightSegments; ++iy)
            {
                const double theta0 = Pi * double(iy) / double(HeightSegments);
                const double theta1 = Pi * double(iy + 1u) / double(HeightSegments);
                for (uint32_t ix = 0u; ix < WidthSegments; ++ix)
                {
                    const double phi0 = 2.0 * Pi * double(ix) / double(WidthSegments);
                    const double phi1 = 2.0 * Pi * double(ix + 1u) / double(WidthSegments);
                    const glm::vec3 a(-Radius * float(std::sin(theta0) * std::cos(phi1)), Radius * float(std::cos(theta0)), Radius * float(std::sin(theta0) * std::sin(phi1)));
                    const glm::vec3 b(-Radius * float(std::sin(theta0) * std::cos(phi0)), Radius * float(std::cos(theta0)), Radius * float(std::sin(theta0) * std::sin(phi0)));
                    const glm::vec3 c(-Radius * float(std::sin(theta1) * std::cos(phi0)), Radius * float(std::cos(theta1)), Radius * float(std::sin(theta1) * std::sin(phi0)));
                    const glm::vec3 d(-Radius * float(std::sin(theta1) * std::cos(phi1)), Radius * float(std::cos(theta1)), Radius * float(std::sin(theta1) * std::sin(phi1)));
                    const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                    const glm::vec3 positions[4u] = {a, b, c, d};
                    const glm::vec2 uvs[4u] = {
                        {float(ix + 1u) / float(WidthSegments), 1.0f - float(iy) / float(HeightSegments)},
                        {float(ix) / float(WidthSegments), 1.0f - float(iy) / float(HeightSegments)},
                        {float(ix) / float(WidthSegments), 1.0f - float(iy + 1u) / float(HeightSegments)},
                        {float(ix + 1u) / float(WidthSegments), 1.0f - float(iy + 1u) / float(HeightSegments)}};
                    for (uint32_t corner = 0u; corner < 4u; ++corner)
                    {
                        entity.vertices.push_back({glm::vec4(positions[corner], 1.0f), glm::vec4(glm::normalize(positions[corner]), 0.0f), glm::vec4(uvs[corner], 0.0f, 0.0f)});
                    }
                    if (iy != 0u) entity.indices.insert(entity.indices.end(), {base, base + 1u, base + 3u});
                    if (iy + 1u != HeightSegments) entity.indices.insert(entity.indices.end(), {base + 1u, base + 2u, base + 3u});
                }
            }
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webgpuLightsSelectiveProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
        }

        /** Validates the four deterministic target/webgpuLightsSelective scenarios. */
        /** Validates the frozen WebgpuLightsSelective scenario matrix and output contract. */
        void validateWebgpuLightsSelectiveOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-assets" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "animated-lights" && options.targetFrame == 60u;
            const bool scenario2 = options.scenarioId == "material-gui" && options.targetFrame == 61u;
            const bool scenario3 = options.scenarioId == "orbit" && options.targetFrame == 61u;
            if (options.caseId != "webgpu_lights_selective"
                || (!scenario0 && !scenario1 && !scenario2 && !scenario3)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgpu_lights_selective scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebgpuLightsSelectiveRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebgpuLightsSelectiveOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(7u);
        const WebgpuLightsSelectiveTeapotMesh teapot = buildWebgpuLightsSelectiveTeapot();
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            appendWebgpuLightsSelectiveTeapot(teapot, entities[index]);
        }
        for (uint32_t index = 3u; index < 7u; ++index)
        {
            appendWebgpuLightsSelectiveLightSphere(entities[index]);
        }

        glm::vec3 camera(0.0f, 0.0f, 7.0f);
        if (options.scenarioId == "orbit")
        {
            // OrbitControls receives the locked 800x500 drag (400,250)->(460,220).
            // With damping disabled the spherical delta is applied in the first update.
            const double radius = std::sqrt(
                double(camera.x) * double(camera.x) +
                double(camera.y) * double(camera.y) +
                double(camera.z) * double(camera.z));
            const double initialPhi = std::acos(double(camera.y) / radius);
            const double theta = -2.0 * Pi * 60.0 / 500.0;
            const double phi = initialPhi + 2.0 * Pi * 30.0 / 500.0;
            camera = glm::vec3(
                float(radius * std::sin(phi) * std::sin(theta)),
                float(radius * std::cos(phi)),
                float(radius * std::sin(phi) * std::cos(theta)));
        }
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f),
            float(options.width) / float(options.height), 0.01f, 100.0f);
        // The upstream animate() callback advances all four lights from the
        // deterministic performance.now() clock before every render.
        const double time = double(options.targetFrame) / 60.0;
        const double lightTime = time * 0.5;
        const glm::vec3 lightPositions[4u] = {
            {float(std::sin(lightTime * 0.7) * 3.0),
             float(std::cos(lightTime * 0.5) * 4.0),
             float(std::cos(lightTime * 0.3) * 3.0)},
            {float(std::cos(lightTime * 0.3) * 3.0),
             float(std::sin(lightTime * 0.5) * 4.0),
             float(std::sin(lightTime * 0.7) * 3.0)},
            {float(std::sin(lightTime * 0.7) * 3.0),
             float(std::cos(lightTime * 0.3) * 4.0),
             float(std::sin(lightTime * 0.5) * 3.0)},
            {float(std::sin(lightTime * 0.3) * 3.0),
             float(std::cos(lightTime * 0.7) * 4.0),
             float(std::sin(lightTime * 0.5) * 3.0)}};
        const glm::vec3 lightColors[4u] = {
            webgpuLightsSelectiveHexColor(0xff0040u),
            webgpuLightsSelectiveHexColor(0x0040ffu),
            webgpuLightsSelectiveHexColor(0x80ff80u),
            webgpuLightsSelectiveHexColor(0xffaa00u)};
        // PointLight.power=1700 is converted by Three.js to candela intensity.
        const float lightIntensities[4u] = {
            1700.0f / float(4.0 * Pi), 1700.0f / float(4.0 * Pi),
            1700.0f / float(4.0 * Pi), 1700.0f / float(4.0 * Pi)};
        const glm::vec3 teapotPositions[3u] = {{-3.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {3.0f, -1.0f, 0.0f}};
        const float baseColor = webgpuLightsSelectiveSrgbToLinear(85.0f / 255.0f);
        normalPixels = decodeWebgpuLightsSelectiveTexture(std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "water" / "Water_1_M_Normal.jpg").pixels;
        roughnessPixels = decodeWebgpuLightsSelectiveTexture(std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "roughness_map.jpg").pixels;
        const glm::vec3 markerColors[4u] = {
            lightColors[0u], lightColors[1u], lightColors[2u], lightColors[3u]};
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            const glm::mat4 model = index < 3u
                ? glm::translate(glm::mat4(1.0f), teapotPositions[index]) *
                    glm::rotate(glm::mat4(1.0f), -glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))
                : glm::translate(glm::mat4(1.0f), lightPositions[index - 3u]);
            entity.objectData.model = model;
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(model));
            entity.objectData.viewNormalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            entity.objectData.cameraPosition = glm::vec4(camera, 1.0f);
            const glm::vec3 viewLightPosition0 = glm::vec3(view * glm::vec4(lightPositions[0u], 1.0f));
            const glm::vec3 viewLightPosition1 = glm::vec3(view * glm::vec4(lightPositions[1u], 1.0f));
            const glm::vec3 viewLightPosition2 = glm::vec3(view * glm::vec4(lightPositions[2u], 1.0f));
            const glm::vec3 viewLightPosition3 = glm::vec3(view * glm::vec4(lightPositions[3u], 1.0f));
            entity.objectData.lightPositionPower0 = glm::vec4(viewLightPosition0, lightIntensities[0u]);
            entity.objectData.lightPositionPower1 = glm::vec4(viewLightPosition1, lightIntensities[1u]);
            entity.objectData.lightPositionPower2 = glm::vec4(viewLightPosition2, lightIntensities[2u]);
            entity.objectData.lightPositionPower3 = glm::vec4(viewLightPosition3, lightIntensities[3u]);
            entity.objectData.lightColor0 = glm::vec4(lightColors[0u], 1.0f);
            entity.objectData.lightColor1 = glm::vec4(lightColors[1u], 1.0f);
            entity.objectData.lightColor2 = glm::vec4(lightColors[2u], 1.0f);
            entity.objectData.lightColor3 = glm::vec4(lightColors[3u], 1.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            if (index < 3u)
            {
                // The locked normal-map capture uses Three.js' texture-space
                // convention: the sampled image is not Y-flipped and both
                // tangent-space X/Y channels are inverted by the KTX-free
                // sample asset orientation.
                const float normalVariant = 7.0f;
                entity.materialData.baseColorAndFlags = glm::vec4(
                    baseColor, baseColor, baseColor,
                    index == 1u ? normalVariant : 1.0f);
                const uint32_t mask = index == 0u ? 1u : index == 1u ? 15u : 2u;
                const bool guiState = options.scenarioId == "material-gui" && index == 1u;
                entity.materialData.lightMaskRoughnessMetalnessAndPhase = glm::vec4(
                    float(mask), guiState ? 0.18f : (index == 1u ? 0.5f : 1.0f),
                    guiState ? 0.82f : (index == 1u ? 0.5f : 0.0f),
                    index == 1u ? 1.0f : 0.0f);
            }
            else
            {
                const glm::vec3 marker = markerColors[index - 3u];
                entity.materialData.baseColorAndFlags = glm::vec4(
                    marker.x, marker.y, marker.z, 1.0f);
                entity.materialData.lightMaskRoughnessMetalnessAndPhase = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            }
            entity.lightData.maskAndIntensity = entity.materialData.lightMaskRoughnessMetalnessAndPhase;
            entity.renderFlags.values[0] = index < 3u ? 1u : 0u;
            entity.renderFlags.values[1] = index;
            entity.renderFlags.values[2] = 0u;
            entity.renderFlags.values[3] = 0u;
        }

        auto normalImage = decodeWebgpuLightsSelectiveTexture(std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "water" / "Water_1_M_Normal.jpg");
        auto roughnessImage = decodeWebgpuLightsSelectiveTexture(std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "roughness_map.jpg");
        const auto normalMips = buildUnormMipChain(normalImage);
        const auto roughnessMips = buildUnormMipChain(roughnessImage);
        eastl::vector<uint8_t> normalTextureBytes;
        eastl::vector<uint8_t> roughnessTextureBytes;
        eastl::vector<uint64_t> normalMipOffsets;
        eastl::vector<uint64_t> roughnessMipOffsets;
        packWebgpuLightsSelectiveMipChain(normalMips, normalTextureBytes, normalMipOffsets);
        packWebgpuLightsSelectiveMipChain(roughnessMips, roughnessTextureBytes, roughnessMipOffsets);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_lights_selective could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebgpuLightsSelectiveVertices" + suffix;
            const std::string indexName = "WebgpuLightsSelectiveIndices" + suffix;
            const std::string objectName = "WebgpuLightsSelectiveObject" + suffix;
            const std::string instanceName = "WebgpuLightsSelectiveInstance" + suffix;
            const std::string materialName = "WebgpuLightsSelectiveMaterial" + suffix;
            const std::string lightName = "WebgpuLightsSelectiveLightData" + suffix;
            const std::string flagsName = "WebgpuLightsSelectiveRenderFlags" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::lightData, lightName.c_str(), &entity.lightData, sizeof(entity.lightData), 1u);
            appendWebgpuLightsSelectiveBuffer(allocation, WebgpuLightsSelectiveSceneRenderSetComponents::renderFlags, flagsName.c_str(), &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebgpuLightsSelectiveSceneRenderSetComponents::textures;
            // TextureComponent descriptors are pooled across the Scene Set; all
            // entities deliberately reuse the two locked map identities.
            const char *normalTextureName = "WebgpuLightsSelectiveNormalTexture";
            const char *roughnessTextureName = "WebgpuLightsSelectiveRoughnessTexture";
            textureComponent.textures.push_back({
                .textureName = normalTextureName,
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = normalImage.width,
                .height = normalImage.height,
                .data = normalTextureBytes.data(),
                .dataStorageBytes = normalTextureBytes.size(),
                .mipmapOffsetBytes = normalMipOffsets,
            });
            textureComponent.textures.push_back({
                .textureName = roughnessTextureName,
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = roughnessImage.width,
                .height = roughnessImage.height,
                .data = roughnessTextureBytes.data(),
                .dataStorageBytes = roughnessTextureBytes.size(),
                .mipmapOffsetBytes = roughnessMipOffsets,
            });
            textureComponent.textures.push_back({
                .textureName = "WebgpuLightsSelectiveDfgLut",
                .format = GVM::RHI::TextureFormat::RG16Float,
                .width = 16u,
                .height = 16u,
                .data = WebgpuLightsSelectiveDfgLutPackedPixels,
                .dataStorageBytes = sizeof(WebgpuLightsSelectiveDfgLutPackedPixels),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuLightsSelectiveRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuLightsSelectiveRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        prepareWebgpuLightsSelectivePath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebgpuLightsSelectivePath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgpu_lights_selective\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << (uint64_t(width) * 4u) << ",\n"
                   << "  \"byteCount\": " << byteCount << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderSetType\": \"WebgpuLightsSelectiveSceneRenderSet\",\n"
                   << "  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 1,\n  \"screenPassCount\": 2,\n"
                   << "  \"drawCommandCount\": 1,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n"
                   << "  \"msaaEnabled\": false,\n"
                   << "  \"simulateMsaa\": false,\n"
                   << "  \"samplePolicy\": {\"mode\": \"single-sample\", \"msaaEnabled\": false, \"simulateMsaa\": false}";
            if (!options.inputReplayPath.empty())
            {
                const bool materialGui = options.scenarioId == "material-gui";
                output << ",\n  \"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgpu_lights_selective\",\"scenarioId\":\""
                       << options.scenarioId.c_str() << "\",\"captureFrame\":61,\"sha256\":\""
                       << (materialGui
                           ? "2f5c958b6c755930c5f758f9c743951d0c0cd876d0d89a89684d0cc10175df7d"
                           : "465c0c2c52dd355382cf95bf5d9d95813a52284b8e5525ed6d3b0a33accec4ad")
                       << "\",\"target\":\"canvas:not([class])\",\"eventCount\":"
                       << (materialGui ? 2 : 3) << "}";
            }
            output << "\n}\n";
        }
        const std::string snapshot = [&]() {
            std::ostringstream value;
            value << "{\"schemaVersion\":1,\"caseId\":\"webgpu_lights_selective\",\"scenarioId\":\""
                  << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
                  << ",\"renderableObjectCount\":7,\"gpuWorkDslOnly\":true"
                  << ",\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1"
                  << ",\"drawCommandCount\":1,\"scenePassCount\":1,\"screenPassCount\":2"
                  << ",\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebgpuLightsSelectiveSceneRenderSet\",\"renderableObjectCount\":7,\"entityCount\":7,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":["
                  << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                  << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                  << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                  << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                  << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                  << "{\"name\":\"lightData\",\"kind\":\"buffer\",\"role\":\"four-point-lights-and-selective-mask\"},"
                  << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"marker-and-standard-material-phase\"},"
                  << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"normal-roughness-or-metalness-fixed-slot\"}],\"scenePasses\":[{\"name\":\"main-selective-standard\",\"renderClass\":\"WebgpuLightsSelectiveMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) value << ',';
                value << "{\"entityId\":" << index << ",\"logicalRenderableId\":\""
                      << (index < 3u ? "teapot-" : "light-marker-") << (index < 3u ? index : index - 3u)
                      << "\",\"instanceCount\":1}";
            }
            value << "]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-selective-standard\"}]}\n";
            return value.str();
        }();
        prepareWebgpuLightsSelectivePath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << snapshot;
        }
        captureWritten = true;
    }

    void WebgpuLightsSelectiveRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
