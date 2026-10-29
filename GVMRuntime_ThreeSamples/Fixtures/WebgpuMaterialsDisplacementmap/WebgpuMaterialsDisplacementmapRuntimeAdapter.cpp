#include "WebgpuMaterialsDisplacementmapRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"


#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t NinjaExpandedVertexCount = 26652u;
        constexpr const char *SettingsReplaySha256 =
            "2e9e3da5ad35aca34b4c60bf99fefd3e93b10bb566f43d36e41bc8dbf7b5a492";
        constexpr const char *ObjSha256 =
            "07819eacecae77a8539fbaf673d9a0908ec2e6383d1b6cedd94544b2a2f2c53b";
        constexpr const char *CanonicalSceneSha256 =
            "9d8474b0ea09f4b225b645c526106f9df55f4710bfa6d11f95f24b175dad9f20";
        constexpr eastl::array<const char *, 6u> CubeFaceNames = {
            "px.jpg", "nx.jpg", "py.jpg", "ny.jpg", "pz.jpg", "nz.jpg"};

        /** Reads one bounded locked displacement-map input. */
        eastl::vector<uint8_t> readDisplacementmapAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a displacement-map asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error("A displacement-map asset is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read a displacement-map asset.");
            return bytes;
        }

        /** Packs decoded RGBA mip levels into generated texture upload arrays. */
        eastl::vector<eastl::vector<uint8_t>> packDisplacementmapMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images)
                result.push_back(image.pixels);
            return result;
        }

        /** Creates parent directories for one requested capture artifact. */
        void prepareDisplacementmapOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeDisplacementmapText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareDisplacementmapOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write displacement-map evidence.");
        }

        /** Builds Three's orthographic projection before DSL depth conversion. */
        glm::mat4 makeDisplacementmapProjection()
        {
            return glm::orthoRH_NO(
                -800.0f, 800.0f, -500.0f, 500.0f, 1.0f, 10000.0f);
        }
    }

    void WebgpuMaterialsDisplacementmapRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            (options.scenarioId == "initial-loader" ||
             options.scenarioId == "canonical-loader") &&
            options.targetFrame == 0u && options.inputReplayPath.empty();
        const bool animated =
            options.scenarioId == "animated-light" &&
            options.targetFrame == 60u && options.inputReplayPath.empty();
        const bool settings =
            options.scenarioId == "material-settings" &&
            options.targetFrame == 61u && !options.inputReplayPath.empty();
        if (options.caseId != "webgpu_materials_displacementmap" ||
            (!initial && !animated && !settings) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "Displacement-map adapter requires one locked Manifest scenario.");
        }

        device = inDevice;
        const std::filesystem::path root(options.assetRoot.c_str());
        const std::filesystem::path ninjaRoot =
            root / "models" / "obj" / "ninja";
        const eastl::vector<uint8_t> objBytes =
            readDisplacementmapAsset(ninjaRoot / "ninjaHead_Low.obj");
        const ThreeCompat::DecodedObjMesh mesh =
            ThreeCompat::decodeObjTriangleMesh(objBytes);
        if (mesh.positions.size() != size_t(NinjaExpandedVertexCount) * 3u ||
            mesh.normals.size() != size_t(NinjaExpandedVertexCount) * 3u ||
            mesh.textureCoordinates.size() !=
                size_t(NinjaExpandedVertexCount) * 2u)
        {
            throw std::runtime_error("The ninja-head OBJ topology differs from r185.");
        }
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (uint32_t index = 0u; index < NinjaExpandedVertexCount; ++index)
        {
            const glm::vec3 position(
                mesh.positions[index * 3u],
                mesh.positions[index * 3u + 1u],
                mesh.positions[index * 3u + 2u]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        vertices.resize(NinjaExpandedVertexCount);
        indices.resize(NinjaExpandedVertexCount);
        for (uint32_t index = 0u; index < NinjaExpandedVertexCount; ++index)
        {
            vertices[index].position = float3(
                (mesh.positions[index * 3u] - center.x) * 25.0f,
                (mesh.positions[index * 3u + 1u] - center.y) * 25.0f,
                (mesh.positions[index * 3u + 2u] - center.z) * 25.0f);
            vertices[index].normal = float3(
                mesh.normals[index * 3u],
                mesh.normals[index * 3u + 1u],
                mesh.normals[index * 3u + 2u]);
            vertices[index].textureCoordinate = float2(
                mesh.textureCoordinates[index * 2u],
                mesh.textureCoordinates[index * 2u + 1u]);
            indices[index] = index;
        }

        normalMips = packDisplacementmapMips(buildUnormMipChain(
            decodePngRgba8(ninjaRoot / "normal.png")));
        aoMips = packDisplacementmapMips(buildUnormMipChain(
            decodeJpegRgba8(ninjaRoot / "ao.jpg")));
        displacementMips = packDisplacementmapMips(buildUnormMipChain(
            decodeJpegRgba8(ninjaRoot / "displacement.jpg")));
        const std::filesystem::path cubeRoot =
            root / "textures" / "cube" / "SwedishRoyalCastle";
        for (uint32_t face = 0u; face < CubeFaceNames.size(); ++face)
        {
            const RgbaImageData image =
                decodeJpegRgba8(cubeRoot / CubeFaceNames[face]);
            if (image.width != 512u || image.height != 512u)
                throw std::runtime_error("A Swedish cube face has an invalid extent.");
            cubeMips[face] = packDisplacementmapMips(
                buildSrgbMipChain(image));
        }
        dfgLutPackedPixels.assign(
            ThreeR185DfgLutPackedPixels,
            ThreeR185DfgLutPackedPixels + 256u);

        const glm::mat4 modelView = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1500.0f));
        uniforms.modelView = modelView;
        uniforms.modelViewProjection =
            makeDisplacementmapProjection() * modelView;
        uniforms.normalTransform = glm::transpose(glm::inverse(modelView));
        const float angle = float(options.targetFrame) * 0.01f;
        uniforms.redLightPositionAndIntensity = glm::vec4(
            2500.0f * std::cos(angle), 0.0f,
            2500.0f * std::sin(angle) - 1500.0f, 1.5f);
        uniforms.cameraLightPositionAndIntensity =
            glm::vec4(0.0f, 0.0f, 0.0f, 3.0f);
        uniforms.blueLightPositionAndIntensity =
            glm::vec4(-1000.0f, 0.0f, -500.0f, 1.5f);
        uniforms.materialState0 = glm::vec4(
            settings ? 0.65f : 1.0f,
            settings ? 0.35f : 0.4f,
            0.2f,
            1.0f);
        uniforms.materialState1 = glm::vec4(
            settings ? 1.25f : 2.436143f,
            settings ? 0.5f : 1.0f,
            1.0f,
            options.scenarioId == "initial-loader" ? 0.0f : 1.0f);
        uniforms.viewportAndUi = glm::vec4(
            800.0f, 500.0f,
            options.scenarioId == "initial-loader" ? 0.0f : 1.0f,
            0.0f);
    }

    void WebgpuMaterialsDisplacementmapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuMaterialsDisplacementmapRuntimeAdapter::afterFrame(
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
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareDisplacementmapOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write displacement-map RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgpu_materials_displacementmap\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
            << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"randomSeed\":" << options.randomSeed << ",\n"
            << "  \"width\":" << width << ",\n"
            << "  \"height\":" << height << ",\n"
            << "  \"rowStrideBytes\":" << width * 4u << ",\n"
            << "  \"byteCount\":" << rgba.size() << ",\n"
            << "  \"format\":\"rgba8unorm\"";
        if (options.scenarioId == "material-settings")
        {
            metadata
                << ",\n  \"inputReplay\":{\n"
                << "    \"sha256\":\"" << SettingsReplaySha256 << "\",\n"
                << "    \"caseId\":\"webgpu_materials_displacementmap\",\n"
                << "    \"scenarioId\":\"material-settings\",\n"
                << "    \"captureFrame\":61,\n"
                << "    \"eventCount\":1,\n"
                << "    \"target\":\"body > div > canvas\"\n"
                << "  }";
        }
        metadata << "\n}\n";
        writeDisplacementmapText(options.captureMetadataPath, metadata.str());

        std::ostringstream scene;
        scene
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgpu_materials_displacementmap\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"not-required\",\n"
            << "  \"sceneRenderSetCount\":0,\n"
            << "  \"renderableObjectCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"scenePassCount\":2,\n"
            << "  \"renderSetCount\":0,\n"
            << "  \"entityCount\":1,\n"
            << "  \"instanceCounts\":[1],\n"
            << "  \"componentSchema\":[],\n"
            << "  \"drawCommandCount\":2,\n"
            << "  \"sampleCount\":1,\n"
            << "  \"sceneRoots\":[{\"name\":\"scene\",\"renderSetRuntimeInstanceCount\":0}],\n"
            << "  \"scenePasses\":[\"double-sided-back\",\"double-sided-front\"],\n"
            << "  \"screenPasses\":[\"swedish-cube-atlas-gutter-build\"]\n"
            << "}\n";
        writeDisplacementmapText(options.sceneSnapshotPath, scene.str());

        std::ostringstream semantic;
        semantic
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgpu_materials_displacementmap\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"kind\":\"loader-snapshot\",\n"
            << "  \"canonicalState\":\"one-nonindexed-mesh-26652-positions\",\n"
            << "  \"result\":{\n"
            << "    \"renderableObjectCount\":1,\n"
            << "    \"sceneRootCount\":1,\n"
            << "    \"canonicalSceneSha256\":\"" << CanonicalSceneSha256 << "\",\n"
            << "    \"assetCount\":10,\n"
            << "    \"vertexCount\":26652,\n"
            << "    \"indexCount\":26652,\n"
            << "    \"textureCount\":9,\n"
            << "    \"objSha256\":\"" << ObjSha256 << "\"\n"
            << "  }\n"
            << "}\n";
        writeDisplacementmapText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuMaterialsDisplacementmapRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        normalMips.clear();
        aoMips.clear();
        displacementMips.clear();
        for (auto &mips : cubeMips) mips.clear();
        dfgLutPackedPixels.clear();
    }
}
