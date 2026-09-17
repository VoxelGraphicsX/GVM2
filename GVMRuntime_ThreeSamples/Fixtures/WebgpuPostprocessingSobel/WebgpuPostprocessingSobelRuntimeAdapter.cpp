#include "WebgpuPostprocessingSobelRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr GVM::Core::RenderSetHandle RoomSetHandle =
            ExportedRenderSet::roomSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t RenderableCount = 9u;
        constexpr uint32_t DragonMeshIndex = 1u;
        constexpr const char *EffectDisabledReplaySha256 =
            "9205724c87ffe713335c6c4f09c7002391ae6ddeaabb4194aeda75a85641e0b6";
        constexpr const char *OrbitReplaySha256 =
            "5d9ff9776fa7aa4c809855e64936cdf2b66b755cb9bfc467967b9b0fdaa5c883";

        static_assert(sizeof(WebgpuPostprocessingSobelVertex) == 32u);
        static_assert(sizeof(WebgpuPostprocessingSobelHostObjectData) == 192u);
        static_assert(sizeof(WebgpuPostprocessingSobelHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuPostprocessingSobelHostMaterialData) == 32u);

        /** Creates parent directories for one requested evidence path. */
        void prepareWebgpuPostprocessingSobelOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebgpuPostprocessingSobelText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuPostprocessingSobelOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU postprocessing evidence.");
        }

        /** Reads one complete offline GLB or input-replay payload. */
        eastl::vector<uint8_t> readWebgpuPostprocessingSobelAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(
                    "Could not open the locked WebGPU Sobel asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<size_t>::max()))
            {
                throw std::runtime_error(
                    "The locked WebGPU Sobel asset has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error(
                    "Could not read the complete WebGPU Sobel asset.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest for one bounded replay file. */
        eastl::string calculateWebgpuPostprocessingSobelReplaySha256(
            const std::filesystem::path &path)
        {
            const eastl::vector<uint8_t> bytes =
                readWebgpuPostprocessingSobelAsset(path);
            if (bytes.size() > static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error(
                    "The WebGPU Sobel input replay is too large for SHA-256.");
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest{};
            CC_SHA256(
                bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
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

        /** Advances the exact upper-24-bit xorshift32 reference stream. */
        double nextWebgpuPostprocessingSobelRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebgpuPostprocessingSobelRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double cz = std::cos(rotationZ * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double sz = std::sin(rotationZ * 0.5);
            const double qx = sx * cy * cz + cx * sy * sz;
            const double qy = cx * sy * cz - sx * cy * sz;
            const double qz = cx * cy * sz + sx * sy * cz;
            const double qw = cx * cy * cz - sx * sy * sz;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Builds Three's 70-degree perspective projection in binary64. */
        glm::dmat4 makeWebgpuPostprocessingSobelProjection()
        {
            constexpr double Near = 0.1;
            constexpr double Far = 100.0;
            const double inverseTangent =
                1.0 / std::tan(70.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Appends one expanded triangle with its exact flat face normal. */
        void appendWebgpuPostprocessingSobelTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<WebgpuPostprocessingSobelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::dvec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }

        /** Builds exact expanded SphereGeometry(1,4,4) topology. */
        void buildWebgpuPostprocessingSobelSphere(
            eastl::vector<WebgpuPostprocessingSobelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            glm::dvec3 grid[5u][5u];
            for (uint32_t y = 0u; y <= 4u; ++y)
            {
                const double v = double(y) / 4.0;
                const double theta = v * Pi;
                const double verticalPosition = std::cos(theta);
                const double ringRadius = std::sqrt(
                    1.0 - verticalPosition * verticalPosition);
                for (uint32_t x = 0u; x <= 4u; ++x)
                {
                    const double u = double(x) / 4.0;
                    const double phi = u * Pi * 2.0;
                    grid[y][x] = glm::dvec3(
                        -ringRadius * std::cos(phi),
                        verticalPosition,
                        ringRadius * std::sin(phi));
                }
            }
            vertices.clear();
            indices.clear();
            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const glm::dvec3 &a = grid[y][x + 1u];
                    const glm::dvec3 &b = grid[y][x];
                    const glm::dvec3 &c = grid[y + 1u][x];
                    const glm::dvec3 &d = grid[y + 1u][x + 1u];
                    if (y != 0u)
                        appendWebgpuPostprocessingSobelTriangle(
                            a, b, d, vertices, indices);
                    if (y != 3u)
                        appendWebgpuPostprocessingSobelTriangle(
                            b, c, d, vertices, indices);
                }
            }
            if (vertices.size() != 72u || indices.size() != 72u)
                throw std::runtime_error(
                    "WebGPU postprocessing SphereGeometry topology differs from r185.");
        }

        /** Converts the selected Dragon GLB primitive to the RenderSet vertex ABI. */
        void buildWebgpuPostprocessingSobelDragon(
            const ThreeCompat::DecodedGlbMesh &mesh,
            eastl::vector<WebgpuPostprocessingSobelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const size_t vertexCount = mesh.positions.size() / 3u;
            if (vertexCount == 0u || mesh.normals.size() != vertexCount * 3u ||
                mesh.indices.empty() || mesh.indices.size() % 3u != 0u)
            {
                throw std::runtime_error(
                    "DragonAttenuation.glb has incomplete triangle attributes.");
            }
            vertices.resize(vertexCount);
            for (size_t vertex = 0u; vertex < vertexCount; ++vertex)
            {
                vertices[vertex].position = glm::vec4(
                    mesh.positions[vertex * 3u],
                    mesh.positions[vertex * 3u + 1u],
                    mesh.positions[vertex * 3u + 2u], 1.0f);
                vertices[vertex].normal = glm::vec4(
                    mesh.normals[vertex * 3u],
                    mesh.normals[vertex * 3u + 1u],
                    mesh.normals[vertex * 3u + 2u], 0.0f);
            }
            indices = mesh.indices;
        }

        /** Appends one typed component payload to a Scene entity allocation. */
        void appendWebgpuPostprocessingSobelPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount = 1u)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }
    }

    void WebgpuPostprocessingSobelRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool scenario0 = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool scenario1 = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool scenario2 = options.scenarioId == "effect-disabled" && options.targetFrame == 1u;
        const bool scenario3 = options.scenarioId == "orbit-input" && options.targetFrame == 1u;
        if (options.caseId != "webgpu_postprocessing_sobel" ||
            !(scenario0 || scenario1 || scenario2 || scenario3) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            ((scenario2 || scenario3) != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument("webgpu_postprocessing_sobel requires its locked r185 scenario matrix.");
        }
        device = inDevice;
        inputReplaySha256.clear();
        inputReplayEventCount = 0u;
        if (scenario2 || scenario3)
        {
            inputReplaySha256 =
                calculateWebgpuPostprocessingSobelReplaySha256(
                    std::filesystem::path(options.inputReplayPath.c_str()));
            const char *expectedSha256 = scenario2 ?
                EffectDisabledReplaySha256 : OrbitReplaySha256;
            if (inputReplaySha256 != expectedSha256)
                throw std::runtime_error(
                    "WebGPU Sobel input replay diverged from its locked digest.");
            inputReplayEventCount = scenario2 ? 1u : 3u;
        }
        if (options.assetRoot.empty())
            throw std::invalid_argument(
                "webgpu_postprocessing_sobel requires a locked asset root.");
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        std::filesystem::path dragonPath =
            assetRoot / "models" / "gltf" / "DragonAttenuation.glb";
        if (!std::filesystem::exists(dragonPath))
            dragonPath = assetRoot / "examples" / "models" / "gltf" /
                "DragonAttenuation.glb";
        if (!std::filesystem::exists(dragonPath))
            throw std::runtime_error(
                "The locked DragonAttenuation.glb is missing from the asset pack.");
        const eastl::vector<uint8_t> dragonBytes =
            readWebgpuPostprocessingSobelAsset(dragonPath);
        const ThreeCompat::DecodedGlbMesh dragonMesh =
            ThreeCompat::decodeGlbMesh(dragonBytes, DragonMeshIndex);
        if (dragonMesh.positions.size() != 76809u * 3u ||
            dragonMesh.normals.size() != 76809u * 3u ||
            dragonMesh.indices.size() != 273648u)
        {
            throw std::runtime_error(
                "DragonAttenuation.glb does not match the locked r185 child[1] mesh.");
        }
        buildWebgpuPostprocessingSobelDragon(dragonMesh, vertices, indices);
        objects.resize(RenderableCount);
        uint32_t randomState = options.randomSeed;
        /* Renderer, scene, geometry, material, light, and pipeline setup
           consume 233 deterministic Math.random calls before the first Mesh
           transform is assigned.  Each Mesh UUID then consumes four more
           calls before the next Mesh receives its transform. */
        for (uint32_t draw = 0u; draw < 233u; ++draw)
            (void)nextWebgpuPostprocessingSobelRandom(randomState);
        const glm::dmat4 view = glm::lookAtRH(
            glm::dvec3(0.0, 1.0, 3.0),
            glm::dvec3(0.0, 0.5, 0.0),
            glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 projection = makeWebgpuPostprocessingSobelProjection();
        const glm::dmat4 dragonRotation = makeWebgpuPostprocessingSobelRotation(
            Pi * 0.5, 0.0, 0.0);
        glm::dmat4 dragonModelDouble = glm::translate(
            glm::dmat4(1.0), glm::dvec3(0.0, -0.7306479811668396, 0.0));
        dragonModelDouble = dragonModelDouble * dragonRotation;
        dragonModelDouble[0u] *= 0.25;
        dragonModelDouble[1u] *= 0.25;
        dragonModelDouble[2u] *= 0.25;
        dragonModel = glm::mat4(dragonModelDouble);
        const glm::dmat4 dragonModelView = view * dragonModelDouble;
        objects[0u].modelView = glm::mat4(dragonModelView);
        objects[0u].modelViewProjection = glm::mat4(projection);
        objects[0u].normalModelView = glm::mat4(
            glm::transpose(glm::inverse(dragonModelView)));
        for (uint32_t entity = 1u; entity < RenderableCount; ++entity)
        {
            const glm::dmat4 roomModel = glm::translate(
                glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 1000.0));
            const glm::dmat4 roomModelView = view * roomModel;
            objects[entity].modelView = glm::mat4(roomModelView);
            objects[entity].modelViewProjection = glm::mat4(projection);
            objects[entity].normalModelView = glm::mat4(
                glm::transpose(glm::inverse(roomModelView)));
        }
        instanceData.resize(2u);
        for (auto &instance : instanceData)
            instance.reserved = glm::vec4(0.0f);
        materialData.diffuseAndShininess =
            glm::vec4(1.0f, 1.0f, 1.0f, 30.0f);
        materialData.specular =
            glm::vec4(0.0056053917f, 0.0056053917f, 0.0056053917f, 0.0f);
        renderFlags.values[0] = 1u;
        renderFlags.values[1] = 0u;
        renderFlags.values[2] = 0u;
        renderFlags.values[3] = 0u;
        roomRenderFlags.values[0] = 0u;
        roomRenderFlags.values[1] = 0u;
        roomRenderFlags.values[2] = 0u;
        roomRenderFlags.values[3] = 0u;
        const auto sceneEncoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        const auto roomEncoder = renderer.createRenderSetCommandEncoder(
            RoomSetHandle);
        if (!sceneEncoder || !roomEncoder)
            throw std::runtime_error(
                "Could not create the WebGPU postprocessing Scene Set encoders.");
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            const std::string entitySuffix = std::to_string(entity);
            const std::string vertexName =
                "WebgpuPostprocessingSobelSphereVertices-" + entitySuffix;
            const std::string indexName =
                "WebgpuPostprocessingSobelSphereIndices-" + entitySuffix;
            const std::string objectName =
                "WebgpuPostprocessingSobelObject-" + entitySuffix;
            const std::string instanceName =
                "WebgpuPostprocessingSobelInstance-" + entitySuffix;
            const std::string materialName =
                "WebgpuPostprocessingSobelMaterial-" + entitySuffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            const uint32_t entityInstanceCount = entity == 1u ? 2u : 1u;
            allocation.instanceCount = entityInstanceCount;
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::vertices,
                vertexName.c_str(),
                vertices.data(), vertices.size() * sizeof(vertices[0u]));
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::indices,
                indexName.c_str(),
                indices.data(), indices.size() * sizeof(indices[0u]));
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::objects,
                objectName.c_str(),
                &objects[entity], sizeof(objects[entity]));
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::instances,
                instanceName.c_str(),
                instanceData.data(),
                instanceData.size() * sizeof(instanceData[0u]),
                entityInstanceCount);
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::materials,
                materialName.c_str(),
                &materialData, sizeof(materialData));
            appendWebgpuPostprocessingSobelPayload(
                allocation,
                WebgpuPostprocessingSobelSceneRenderSetComponents::renderFlags,
                "WebgpuPostprocessingSobelRenderFlags",
                entity == 0u ? &renderFlags : &roomRenderFlags,
                sizeof(renderFlags));
            if (entity == 0u)
                sceneEncoder->allocEntity(allocation);
            else
                roomEncoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, sceneEncoder);
        renderer.executeRenderSetCommand(RoomSetHandle, roomEncoder);
    }

    void WebgpuPostprocessingSobelRuntimeAdapter::updateOrbitCamera(
        GVM::Core::AbstractRendererImpl &renderer,
        uint32_t frameIndex)
    {
        if (frameIndex != 1u || objects.empty()) return;

        // OrbitControls receives one pointer-move update and one animation-loop
        // update before the locked frame-1 capture.  Reproduce its 0.05 damping
        // factor without introducing a renderer-side input API.
        constexpr double TwoPi = 6.28318530717958647692;
        constexpr double Damping = 0.05;
        constexpr double DragX = 60.0;
        constexpr double DragY = -30.0;
        constexpr double ElementHeight = 500.0;
        const double radius = std::sqrt(0.5 * 0.5 + 3.0 * 3.0);
        const double initialPhi = std::acos(0.5 / radius);
        const double dampingAccumulation = 1.0 - (1.0 - Damping) * (1.0 - Damping);
        const double theta = -TwoPi * DragX / ElementHeight * dampingAccumulation;
        const double phi = initialPhi -
            TwoPi * DragY / ElementHeight * dampingAccumulation;
        const glm::dvec3 target(0.0, 0.5, 0.0);
        const glm::dvec3 cameraPosition = target + glm::dvec3(
            radius * std::sin(phi) * std::sin(theta),
            radius * std::cos(phi),
            radius * std::sin(phi) * std::cos(theta));
        const glm::dmat4 view = glm::lookAtRH(
            cameraPosition,
            target,
            glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 modelView = view * glm::dmat4(dragonModel);
        objects[0u].modelView = glm::mat4(modelView);
        objects[0u].modelViewProjection = glm::mat4(
            makeWebgpuPostprocessingSobelProjection());
        objects[0u].normalModelView = glm::mat4(
            glm::transpose(glm::inverse(modelView)));

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not update the WebGPU postprocessing Sobel orbit camera.");
        encoder->setBufferComponentData(
            0u,
            WebgpuPostprocessingSobelSceneRenderSetComponents::objects,
            &objects[0u],
            sizeof(objects[0u]),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuPostprocessingSobelRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (options.scenarioId == "orbit-input")
            updateOrbitCamera(renderer, frameIndex);
    }

    void WebgpuPostprocessingSobelRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebgpuPostprocessingSobelOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU postprocessing RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_postprocessing_sobel\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"samplePolicy\":{"
            << "\"mode\":\"single-sample\",\"msaaEnabled\":false,"
            << "\"simulateMsaa\":false},\"inputReplay\":";
        if (inputReplaySha256.empty())
        {
            metadata << "null}\n";
        }
        else
        {
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_sobel\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":1,\"sha256\":\""
                << inputReplaySha256.c_str()
                << "\",\"target\":\"canvas:not([class])\",\"eventCount\":"
                << inputReplayEventCount << "}}\n";
        }
        writeWebgpuPostprocessingSobelText(
            options.captureMetadataPath, metadata.str());
        const auto componentSchema =
            "[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"scene-cull-material-phase\"}]";
        std::ostringstream roomEntities;
        for (uint32_t entity = 1u; entity < RenderableCount; ++entity)
        {
            if (entity != 1u) roomEntities << ',';
            roomEntities << "{\"entityId\":" << (entity - 1u)
                         << ",\"logicalRenderableId\":\"room-sphere-"
                         << entity << "\",\"instanceCount\":"
                         << (entity == 1u ? 2u : 1u) << "}";
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_sobel\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":2,\"renderableObjectCount\":9,"
            << "\"entityCount\":9,\"instanceCount\":10,"
            << "\"vertexCount\":" << vertices.size() * RenderableCount
            << ",\"indexCount\":" << indices.size() * RenderableCount
            << ",\"scenePassCount\":3,\"screenPassCount\":4,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebgpuPostprocessingSobelSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebgpuPostprocessingSobelSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"dragon\",\"instanceCount\":1}],"
            << "\"componentSchema\":" << componentSchema << ","
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":"
            << "\"WebgpuPostprocessingSobelMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]},{"
            << "\"id\":\"roomEnvironment\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"room-set\",\"renderSetType\":"
            << "\"WebgpuPostprocessingSobelSceneRenderSet\","
            << "\"renderableObjectCount\":8,\"entityCount\":8,"
            << "\"entities\":[" << roomEntities.str() << "],"
            << "\"componentSchema\":" << componentSchema << ","
            << "\"drawCommandCount\":0,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"room-capture-back-sided\",\"renderClass\":"
            << "\"WebgpuPostprocessingSobelRoomBackPass\",\"renderSetId\":\"room-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":0,\"drawCommandCount\":0,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false},{\"name\":\"room-capture-front-sided\","
            << "\"renderClass\":\"WebgpuPostprocessingSobelRoomFrontPass\",\"renderSetId\":\"room-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":0,\"drawCommandCount\":0,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main\","
            << "\"entityOrdinal\":0}]}\n";
        writeWebgpuPostprocessingSobelText(
            options.sceneSnapshotPath, snapshot.str());
        const bool canonicalLoaderScenario =
            options.scenarioId == "canonical-loader" && frameIndex == 0u;
        if (canonicalLoaderScenario)
        {
            const std::string semantic =
                "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_sobel\","
                "\"scenarioId\":\"canonical-loader\",\"frame\":0,"
                "\"kind\":\"loader-snapshot\","
                "\"canonicalState\":\"two-decoded-meshes-only-dragon-attached-seventy-six-thousand-eight-hundred-nine-vertices\","
                "\"result\":{\"renderableObjectCount\":2,\"sceneRootCount\":2,"
                "\"canonicalSceneSha256\":\"e4c89c4c9d366b26d5875cd75550e229231e3db7512f32905d0b71082a927652\"}}\n";
            writeWebgpuPostprocessingSobelText(
                options.semanticSnapshotPath, semantic);
        }
        else
        {
            writeWebgpuPostprocessingSobelText(
                options.semanticSnapshotPath, snapshot.str());
        }
        captureWritten = true;
    }

    void WebgpuPostprocessingSobelRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        objects.clear();
        instanceData.clear();
        device = {};
    }
}
