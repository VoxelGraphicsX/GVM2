#include "WebglMaterialsBumpmapRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t BumpmapVertexCount = 9279u;
        constexpr uint32_t BumpmapIndexCount = 53052u;

        /** Reads one bounded locked bump-map asset. */
        eastl::vector<uint8_t> readBumpmapAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open a locked bump-map asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0) throw std::runtime_error("A locked bump-map asset is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read a locked bump-map asset.");
            return bytes;
        }

        /** Loads and expands the locked browser-generated grayscale mip chain. */
        eastl::vector<eastl::vector<uint8_t>> loadBumpmapMips(
            const std::filesystem::path &assetRoot)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(13u);
            uint32_t extent = 4096u;
            for (uint32_t level = 0u; level < 13u; ++level)
            {
                const std::filesystem::path path = assetRoot / "generated" /
                    "webgl_materials_bumpmap" /
                    ("height-mip-" + std::to_string(level) + ".r8");
                const eastl::vector<uint8_t> grayscale = readBumpmapAsset(path);
                if (grayscale.size() != size_t(extent) * extent)
                {
                    throw std::runtime_error(
                        "A locked bump-map mip has an invalid extent.");
                }
                eastl::vector<uint8_t> rgba(grayscale.size() * 4u);
                for (size_t index = 0u; index < grayscale.size(); ++index)
                {
                    rgba[index * 4u] = grayscale[index];
                    rgba[index * 4u + 1u] = grayscale[index];
                    rgba[index * 4u + 2u] = grayscale[index];
                    rgba[index * 4u + 3u] = 255u;
                }
                result.push_back(eastl::move(rgba));
                extent = eastl::max(extent / 2u, 1u);
            }
            return result;
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareBumpmapOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic UTF-8 evidence artifact. */
        void writeBumpmapText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareBumpmapOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write bump-map evidence.");
        }

        /** Creates Three's OpenGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeBumpmapPerspective(
            float fieldOfViewDegrees,
            float aspect,
            float nearDistance,
            float farDistance)
        {
            return glm::perspectiveRH_NO(
                glm::radians(fieldOfViewDegrees),
                aspect,
                nearDistance,
                farDistance);
        }
    }

    void WebglMaterialsBumpmapRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            (options.scenarioId == "initial-loader" ||
             options.scenarioId == "canonical-loader") &&
            options.targetFrame == 0u;
        const bool damped =
            options.scenarioId == "damped-orbit" &&
            options.targetFrame == 60u;
        const bool disabled =
            options.scenarioId == "bump-disabled" &&
            options.targetFrame == 61u &&
            !options.inputReplayPath.empty();
        if (options.caseId != "webgl_materials_bumpmap" ||
            (!initial && !damped && !disabled) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "Bump-map adapter requires one locked Manifest scenario.");
        }
        device = inDevice;
        const std::filesystem::path root(options.assetRoot.c_str());
        const std::filesystem::path modelPath = root / "models" / "gltf" /
            "LeePerrySmith" / "LeePerrySmith.glb";
        const ThreeCompat::DecodedGlbMesh mesh =
            ThreeCompat::decodeFirstGlbMesh(readBumpmapAsset(modelPath));
        if (mesh.positions.size() != size_t(BumpmapVertexCount) * 3u ||
            mesh.normals.size() != size_t(BumpmapVertexCount) * 3u ||
            mesh.textureCoordinates.size() != size_t(BumpmapVertexCount) * 2u ||
            mesh.indices.size() != BumpmapIndexCount)
        {
            throw std::runtime_error("Lee Perry Smith bump-map topology diverged from r185.");
        }
        vertices.resize(BumpmapVertexCount);
        for (uint32_t index = 0u; index < BumpmapVertexCount; ++index)
        {
            vertices[index].position = float3(
                mesh.positions[index * 3u],
                mesh.positions[index * 3u + 1u],
                mesh.positions[index * 3u + 2u]);
            vertices[index].normal = float3(
                mesh.normals[index * 3u],
                mesh.normals[index * 3u + 1u],
                mesh.normals[index * 3u + 2u]);
            vertices[index].textureCoordinate = float2(
                mesh.textureCoordinates[index * 2u],
                mesh.textureCoordinates[index * 2u + 1u]);
        }
        indices.assign(mesh.indices.begin(), mesh.indices.end());
        const std::filesystem::path heightPath = root / "models" / "gltf" /
            "LeePerrySmith" / "Infinite-Level_02_Disp_NoSmoothUV-4096.jpg";
        (void)readBumpmapAsset(heightPath);
        heightMips = loadBumpmapMips(root);
        heightImage.width = 4096u;
        heightImage.height = 4096u;
        heightImage.pixels = heightMips.front();

        const glm::mat4 model = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 0.0f, 12.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = makeBumpmapPerspective(
            27.0f, 800.0f / 500.0f, 0.1f, 100.0f);
        uniforms.modelView = view * model;
        uniforms.modelViewProjection = projection * uniforms.modelView;

        const glm::vec3 lightPosition(3.5f, 0.0f, 7.0f);
        const glm::mat4 lightView = glm::lookAtRH(
            lightPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 lightProjection = makeBumpmapPerspective(
            40.0f, 1.0f, 2.0f, 15.0f);
        uniforms.shadowModelViewProjection = lightProjection * lightView * model;
        const glm::vec3 viewLight = glm::vec3(view * glm::vec4(lightPosition, 1.0f));
        const glm::vec3 viewSpotDirection = glm::normalize(
            glm::mat3(view) * glm::normalize(-lightPosition));
        uniforms.lightPositionAndIntensity = float4(viewLight, 200.0f);
        uniforms.spotDirectionAndBumpScale = float4(
            viewSpotDirection,
            disabled ? 0.0f : 10.0f);
        uniforms.hemisphereSky = float4(
            0.26635560f, 0.20155625f, 0.20155625f, 0.0f);
        uniforms.hemisphereGround = float4(
            0.06662594f, 0.06662594f, 0.13286832f, 0.0f);
    }

    void WebglMaterialsBumpmapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsBumpmapRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path rgbaPath(options.captureRgbaPath.c_str());
            prepareBumpmapOutput(rgbaPath);
            std::ofstream output(rgbaPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write bump-map RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgl_materials_bumpmap\",\n  \"scenarioId\":\""
                 << options.scenarioId.c_str() << "\",\n  \"pipeline\":\""
                 << options.pipeline.c_str() << "\",\n  \"backend\":\""
                 << threeSampleBackendName(options.backend) << "\",\n  \"frame\":" << frameIndex
                 << ",\n  \"randomSeed\":" << options.randomSeed
                 << ",\n  \"width\":" << width << ",\n  \"height\":" << height
                 << ",\n  \"rowStrideBytes\":" << width * 4u
                 << ",\n  \"byteCount\":" << rgba.size()
                 << ",\n  \"format\":\"rgba8unorm\"";
        if (options.scenarioId == "bump-disabled")
        {
            metadata
                << ",\n  \"inputReplay\":{\n"
                << "    \"sha256\":\"c402314dc0ae0dffb4a710019e172948f42e538806f8c0a90ad7a395939b4d80\",\n"
                << "    \"caseId\":\"webgl_materials_bumpmap\",\n"
                << "    \"scenarioId\":\"bump-disabled\",\n"
                << "    \"captureFrame\":61,\n"
                << "    \"eventCount\":1,\n"
                << "    \"target\":\".lil-gui input[type=checkbox]\"\n"
                << "  }";
        }
        metadata << "\n}\n";
        writeBumpmapText(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_bumpmap\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"renderSetPolicy\":\"not-required\",\n"
              << "  \"sceneRenderSetCount\":0,\n"
              << "  \"renderableObjectCount\":1,\n"
              << "  \"instanceCount\":1,\n"
              << "  \"scenePassCount\":2,\n"
              << "  \"renderSetCount\":0,\n  \"entityCount\":1,\n"
              << "  \"instanceCounts\":[1],\n  \"componentSchema\":[],\n"
              << "  \"drawCommandCount\":2,\n  \"sampleCount\":1,\n"
              << "  \"scenePasses\":[\"shadow-depth\",\"main\"],\n"
              << "  \"screenPasses\":[\"output-tone-map\"]\n}\n";
        writeBumpmapText(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_materials_bumpmap\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"canonicalState\":\"canonical-loaded-scene\",\n"
                << "  \"result\":{\n"
                << "    \"renderableObjectCount\":1,\n"
                << "    \"sceneRootCount\":1,\n"
                << "    \"canonicalSceneSha256\":\"a3c9d15fbd9454874c01b14a57daaaf1d8be7363b18c5b19c0ea3943d11cf3d8\"\n"
                << "  }\n}\n";
            writeBumpmapText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglMaterialsBumpmapRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        heightImage.pixels.clear();
        heightMips.clear();
    }
}
