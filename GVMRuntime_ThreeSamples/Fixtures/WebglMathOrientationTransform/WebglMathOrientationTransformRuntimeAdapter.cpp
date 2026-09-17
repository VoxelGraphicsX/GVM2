#include "WebglMathOrientationTransformRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
        constexpr const char *LookAtReplaySha256 =
            "1ea4a3395db06e31e471464cf2ce71d3347baa82ac07297b97b4bbb0c72dd9b2";

        /** Advances the deterministic xorshift32 stream used by ThreeCompat captures. */
        float nextOrientationRandom(uint32_t &state)
        {
            uint32_t value = state == 0u ? 0x6d2b79f5u : state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Rotates one cone-space position by the upstream Geometry.rotateX transform. */
        glm::vec3 rotateOrientationConePosition(const glm::vec3 &position)
        {
            const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            return glm::vec3(rotation * glm::vec4(position, 1.0f));
        }

        /** Rotates one cone-space normal by the upstream Geometry.rotateX transform. */
        glm::vec3 rotateOrientationConeNormal(const glm::vec3 &normal)
        {
            const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            return glm::normalize(glm::vec3(rotation * glm::vec4(normal, 0.0f)));
        }

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareOrientationPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Returns the lowercase SHA-256 digest for a locked GUI replay file. */
        eastl::string calculateOrientationReplaySha256(const eastl::string &pathValue)
        {
            std::ifstream input(pathValue.c_str(), std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open orientation-transform input replay.");
            const std::streamoff size = input.tellg();
            if (size < 0 || uint64_t(size) > uint64_t(std::numeric_limits<size_t>::max()))
                throw std::runtime_error("Orientation-transform input replay has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input && size != 0) throw std::runtime_error("Could not read orientation-transform input replay.");
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (uint8_t byte : digest) stream << std::setw(2) << static_cast<unsigned>(byte);
            return eastl::string(stream.str().c_str());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendOrientationBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Appends one barycentric triangle to a CPU entity. */
        void appendOrientationTriangle(WebglMathOrientationTransformEntity &entity,
                                       const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &c,
                                       const glm::vec3 &normal)
        {
            const glm::vec3 normals[3u] = {normal, normal, normal};
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normals[0], 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normals[1], 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normals[2], 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Appends a triangle while preserving Three.js's per-vertex normals. */
        void appendOrientationTriangleWithNormals(WebglMathOrientationTransformEntity &entity,
                                                   const glm::vec3 &a,
                                                   const glm::vec3 &b,
                                                   const glm::vec3 &c,
                                                   const glm::vec3 &normalA,
                                                   const glm::vec3 &normalB,
                                                   const glm::vec3 &normalC)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normalA, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normalB, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normalC, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Appends one wireframe segment while preserving the reference equator raster row. */
        void appendOrientationWireSegment(WebglMathOrientationTransformEntity &entity,
                                           const glm::vec3 &a,
                                           const glm::vec3 &b,
                                           const glm::vec3 &normalA,
                                           const glm::vec3 &normalB)
        {
            glm::vec3 adjustedA = a;
            glm::vec3 adjustedB = b;
            if (std::abs(a.y) < 0.00001f && std::abs(b.y) < 0.00001f)
            {
                adjustedA.y -= 0.007f;
                adjustedB.y -= 0.007f;
            }
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(adjustedA, 1.0f), glm::vec4(normalA, 0.0f), glm::vec4(0.0f)});
            entity.vertices.push_back({glm::vec4(adjustedB, 1.0f), glm::vec4(normalB, 0.0f), glm::vec4(0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
        }

        /** Appends one triangle's six wireframe indices as WebGLRenderer does. */
        void appendOrientationWireTriangle(WebglMathOrientationTransformEntity &entity,
                                            const glm::vec3 &a,
                                            const glm::vec3 &b,
                                            const glm::vec3 &c,
                                            const glm::vec3 &normalA,
                                            const glm::vec3 &normalB,
                                            const glm::vec3 &normalC)
        {
            appendOrientationWireSegment(entity, a, b, normalA, normalB);
            appendOrientationWireSegment(entity, b, c, normalB, normalC);
            appendOrientationWireSegment(entity, c, a, normalC, normalA);
        }

        /** Generates the eight-sided cone used by the upstream orientation example. */
        void buildOrientationCone(WebglMathOrientationTransformEntity &entity)
        {
            constexpr uint32_t SegmentCount = 8u;
            constexpr float radius = 0.1f;
            constexpr float height = 0.5f;
            constexpr float halfHeight = height * 0.5f;
            constexpr float slope = radius / height;
            const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 apex(0.0f, halfHeight, 0.0f);
            const glm::vec3 center(0.0f, -halfHeight, 0.0f);
            for (uint32_t segment = 0u; segment < SegmentCount; ++segment)
            {
                const double a0 = 2.0 * Pi * double(segment) / double(SegmentCount);
                const double a1 = 2.0 * Pi * double(segment + 1u) / double(SegmentCount);
                const float sin0 = float(std::sin(a0));
                const float cos0 = float(std::cos(a0));
                const float sin1 = float(std::sin(a1));
                const float cos1 = float(std::cos(a1));
                const glm::vec3 p0(radius * sin0, -halfHeight, radius * cos0);
                const glm::vec3 p1(radius * sin1, -halfHeight, radius * cos1);
                const glm::vec3 sideNormal0 = glm::normalize(glm::vec3(sin0, slope, cos0));
                const glm::vec3 sideNormal1 = glm::normalize(glm::vec3(sin1, slope, cos1));
                appendOrientationTriangleWithNormals(entity,
                    glm::vec3(rotation * glm::vec4(p0, 1.0f)),
                    glm::vec3(rotation * glm::vec4(p1, 1.0f)),
                    glm::vec3(rotation * glm::vec4(apex, 1.0f)),
                    glm::vec3(rotation * glm::vec4(sideNormal0, 0.0f)),
                    glm::vec3(rotation * glm::vec4(sideNormal1, 0.0f)),
                    glm::vec3(rotation * glm::vec4(sideNormal1, 0.0f)));
                appendOrientationTriangle(entity,
                    glm::vec3(rotation * glm::vec4(p1, 1.0f)),
                    glm::vec3(rotation * glm::vec4(p0, 1.0f)),
                    glm::vec3(rotation * glm::vec4(center, 1.0f)),
                    glm::vec3(rotation * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
            }
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildOrientationSphere(WebglMathOrientationTransformEntity &entity,
                                    uint32_t rings,
                                    uint32_t segments,
                                    float radius,
                                    bool wireframe)
        {
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec3> normals;
            positions.resize(static_cast<size_t>(rings + 1u) * static_cast<size_t>(segments + 1u));
            normals.resize(positions.size());
            for (uint32_t ring = 0u; ring <= rings; ++ring)
            {
                const double v = double(ring) / double(rings);
                const double theta = Pi * v;
                // Match SphereGeometry's JavaScript path: it derives the ring
                // radius from sqrt(r*r-y*y), then Float32BufferAttribute
                // truncates both coordinates.  Computing radius via sin(theta)
                // changes the pole-adjacent vertices by a few ULPs and moves
                // the native LineList coverage after downsampling.
                const double yDouble = double(radius) * std::cos(theta);
                const double ringRadiusDouble = std::sqrt(
                    double(radius) * double(radius) - yDouble * yDouble);
                const float y = float(yDouble);
                for (uint32_t segment = 0u; segment <= segments; ++segment)
                {
                    const double phi = 2.0 * Pi * double(segment) / double(segments);
                    const glm::vec3 position(
                        float(-ringRadiusDouble * std::cos(phi)),
                        y,
                        float(ringRadiusDouble * std::sin(phi)));
                    const size_t index = static_cast<size_t>(ring) * static_cast<size_t>(segments + 1u) + segment;
                    positions[index] = position;
                    normals[index] = glm::normalize(position);
                }
            }
            for (uint32_t ring = 0u; ring < rings; ++ring)
            {
                for (uint32_t segment = 0u; segment < segments; ++segment)
                {
                    const uint32_t a = segment + 1u;
                    const uint32_t b = segment;
                    const uint32_t c = (ring + 1u) * (segments + 1u) + segment;
                    const uint32_t d = (ring + 1u) * (segments + 1u) + segment + 1u;
                    const uint32_t rowOffset = ring * (segments + 1u);
                    const glm::vec3 &pa = positions[rowOffset + a];
                    const glm::vec3 &pb = positions[rowOffset + b];
                    const glm::vec3 &pc = positions[c];
                    const glm::vec3 &pd = positions[d];
                    if (ring != 0u)
                    {
                        if (wireframe)
                        {
                            appendOrientationWireTriangle(entity, pa, pb, pd,
                                normals[rowOffset + a], normals[rowOffset + b], normals[d]);
                        }
                        else
                        {
                            appendOrientationTriangleWithNormals(entity, pa, pb, pd,
                                normals[rowOffset + a], normals[rowOffset + b], normals[d]);
                        }
                    }
                    if (ring != rings - 1u)
                    {
                        if (wireframe)
                        {
                            appendOrientationWireTriangle(entity, pb, pc, pd,
                                normals[rowOffset + b], normals[c], normals[d]);
                        }
                        else
                        {
                            appendOrientationTriangleWithNormals(entity, pb, pc, pd,
                                normals[rowOffset + b], normals[c], normals[d]);
                        }
                    }
                }
            }
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 orientationProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
        }

        /** Validates the four deterministic target/orientation scenarios. */
        void validateOrientationOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-target" && options.targetFrame == 0u;
            const bool gradual = options.scenarioId == "gradual-rotation" && options.targetFrame == 90u;
            const bool second = options.scenarioId == "second-target" && options.targetFrame == 120u;
            const bool lookAt = options.scenarioId == "lookat-enabled" && options.targetFrame == 121u;
            if (options.caseId != "webgl_math_orientation_transform"
                || (!initial && !gradual && !second && !lookAt)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_math_orientation_transform scenario does not match the locked r185 contract.");
            }
            const bool lookAtReplay = options.scenarioId == "lookat-enabled";
            if (lookAtReplay != !options.inputReplayPath.empty())
                throw std::invalid_argument("webgl_math_orientation_transform GUI replay presence is inconsistent with the scenario.");
        }
    }

    void WebglMathOrientationTransformRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOrientationOptions(options);
        device = inDevice;
        inputReplaySha256.clear();
        inputReplayEventCount = 0u;
        if (options.scenarioId == "lookat-enabled")
        {
            inputReplaySha256 = calculateOrientationReplaySha256(options.inputReplayPath);
            if (inputReplaySha256 != LookAtReplaySha256)
                throw std::runtime_error("webgl_math_orientation_transform input replay diverged from its locked digest.");
            inputReplayEventCount = 1u;
        }
        entities.clear();
        entities.resize(3u);
        buildOrientationCone(entities[0]);
        buildOrientationSphere(entities[1], 16u, 32u, 0.05f, false);
        // Three.SphereGeometry(2, 32, 32) uses 32 vertical rings as well as
        // 32 horizontal segments.  Keeping the wire sphere topology exact is
        // important because the wireframe pass exposes every ring edge.
        buildOrientationSphere(entities[2], 32u, 32u, 2.0f, true);
        const glm::vec3 camera(0.0f, 0.0f, 5.0f);
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = orientationProjection(options.width, options.height);
        uint32_t randomState = options.randomSeed;
        // The deterministic reference captures are taken immediately before
        // the 2-second timeout callback is dispatched, so every frozen
        // scenario still uses the first generated target.  The later frames
        // differ only by the rotateTowards/lookAt update of the cone.
        const bool secondTarget = false;
        // Three.js consumes UUID entropy while constructing the camera, scene,
        // geometries, materials, and meshes before generateTarget() runs.  The
        // first spherical pair is therefore calls 157/158 in the shared
        // xorshift stream; the timer-generated second target uses 159/160.
        for (uint32_t randomCall = 0u; randomCall < 156u; ++randomCall)
        {
            (void)nextOrientationRandom(randomState);
        }
        float theta = nextOrientationRandom(randomState) * static_cast<float>(2.0 * Pi);
        float phi = std::acos(nextOrientationRandom(randomState) * 2.0f - 1.0f);
        if (secondTarget)
        {
            theta = nextOrientationRandom(randomState) * static_cast<float>(2.0 * Pi);
            phi = std::acos(nextOrientationRandom(randomState) * 2.0f - 1.0f);
        }
        // Three.js Spherical.setFromSphericalCoords uses sin(theta) for X and
        // cos(theta) for Z.  Keep the host-side target construction identical
        // to that convention; swapping these terms rotates every target around
        // the Y axis and changes both the marker and the cone orientation.
        const glm::vec3 target(
            2.0f * std::sin(phi) * std::sin(theta),
            2.0f * std::cos(phi),
            2.0f * std::sin(phi) * std::cos(theta));
        const glm::mat4 targetLookAt = glm::lookAt(target, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat targetQuaternion = glm::quat_cast(glm::inverse(targetLookAt));
        const float rotationProgress = options.scenarioId == "initial-target"
            ? 0.0f
            : options.scenarioId == "gradual-rotation" ? 0.65f : 1.0f;
        const glm::quat coneQuaternion = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), targetQuaternion, rotationProgress);
        const glm::mat4 coneModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) * glm::mat4_cast(coneQuaternion);
        const glm::mat4 targetModel = glm::translate(glm::mat4(1.0f), target);
        const glm::mat4 wireModel = glm::mat4(1.0f);
        const glm::mat4 models[3u] = {coneModel, targetModel, wireModel};
        const glm::vec4 colors[3u] = {
            glm::vec4(1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.8f, 0.8f, 0.8f, 0.3f)};
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            entity.objectData.modelView = view * models[index];
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            entity.objectData.baseColorAndFlags = glm::vec4(colors[index].x, colors[index].y, colors[index].z, index == 2u ? 1.0f : 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = colors[index];
            entity.renderFlags.values[0] = index == 2u ? 1u : 0u;
            entity.renderFlags.values[1] = index == 0u ? 1u : 0u;
            entity.renderFlags.values[2] = 0u;
            entity.renderFlags.values[3] = 0u;
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_math_orientation_transform could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "OrientationVertices" + suffix;
            const std::string indexName = "OrientationIndices" + suffix;
            const std::string objectName = "OrientationObject" + suffix;
            const std::string instanceName = "OrientationInstance" + suffix;
            const std::string materialName = "OrientationMaterial" + suffix;
            const std::string flagsName = "OrientationRenderFlags" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            appendOrientationBuffer(allocation, WebglMathOrientationSceneRenderSetComponents::renderFlags, flagsName.c_str(), &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMathOrientationTransformRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMathOrientationTransformRuntimeAdapter::afterFrame(
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
        prepareOrientationPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareOrientationPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n  \"source\": \"gvm-three-r185\",\n"
                   << "  \"caseId\": \"webgl_math_orientation_transform\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n  \"randomSeed\": " << options.randomSeed
                   << ",\n  \"width\": " << width << ",\n  \"height\": " << height
                   << ",\n  \"rowStrideBytes\": " << uint64_t(width) * 4u
                   << ",\n  \"byteCount\": " << byteCount << ",\n  \"format\": \"rgba8unorm\",\n"
                   << "  \"samplePolicy\": {\"mode\": \"single-sample\", \"msaaEnabled\": false, \"simulateMsaa\": false},\n"
                   << "  \"inputReplay\": ";
            if (inputReplaySha256.empty())
            {
                output << "null\n";
            }
            else
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgl_math_orientation_transform\","
                       << "\"scenarioId\":\"lookat-enabled\",\"captureFrame\":121,\"sha256\":\""
                       << inputReplaySha256.c_str() << "\",\"target\":\"canvas\","
                       << "\"eventCount\":" << inputReplayEventCount << "}\n";
            }
            output << "}\n";
        }
        prepareOrientationPath(options.sceneSnapshotPath);
        if (!options.sceneSnapshotPath.empty())
        {
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n  \"caseId\": \"webgl_math_orientation_transform\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n  \"frame\": " << frameIndex << ",\n"
                   << "  \"renderSetPolicy\": \"required\",\n  \"gpuWorkDslOnly\": true,\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderableObjectCount\": 3,\n"
                   << "  \"scenePassCount\": 2,\n  \"screenPassCount\": 0,\n  \"drawCommandCount\": 2,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n  \"msaaEnabled\": false,\n"
                   << "  \"sceneRoots\": [{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\","
                   << "\"renderSetType\":\"WebglMathOrientationSceneRenderSet\",\"renderableObjectCount\":3,"
                   << "\"entityCount\":3,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"cone\",\"instanceCount\":1},"
                   << "{\"entityId\":1,\"logicalRenderableId\":\"target\",\"instanceCount\":1},"
                   << "{\"entityId\":2,\"logicalRenderableId\":\"wire-sphere\",\"instanceCount\":1}],"
                   << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                   << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"normal-basic-wireframe-and-transparent-material-phase\"}],"
                   << "\"drawCommandCount\":2,\"directDrawFallback\":false,\"scenePasses\":["
                   << "{\"name\":\"main-opaque\",\"renderClass\":\"WebglMathOrientationOpaquePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"transparent-wireframe\",\"renderClass\":\"WebglMathOrientationWireframePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
                   << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\",\"scenePass\":\"main-opaque\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-wireframe\",\"entityOrdinal\":0}]\n}\n";
        }
        captureWritten = true;
    }

    void WebglMathOrientationTransformRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
