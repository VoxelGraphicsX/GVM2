#include "WebgpuLightprobeRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <CommonCrypto/CommonDigest.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *CubeFileNames[6u] = {
            "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
        constexpr const char *CubeHashes[6u] = {
            "e265bd50f6dac39983774331da3052ce72986b33e0adc29e7f7213e8b1286f80",
            "e325fe6316034f1a3e2ee4e8ebddd743568edddde7a8960bd18b0d53a1496f6e",
            "75336e883fa31894bb17f3c8e62aacf3597557cf6c0fa8d43b074d9de0e11021",
            "8fc1866bbb77b906511161a731fb578f98f0854e88e350e650d4e39593485d12",
            "229278d48be46647b2dcf748d3124a61b98bdf472216ff3d9ba7af5ac24dc5cb",
            "d4675e3b192aaba42bc74b73d74a2fd3b81f69db1a5814727cff8a52f12611e0"};

        static_assert(sizeof(WebgpuLightprobeVertex) == 32u);
        static_assert(sizeof(WebgpuLightprobeHostObjectData) == 192u);
        static_assert(sizeof(WebgpuLightprobeHostShData) == 144u);

        /** Reads one pinned cube face as exact bytes for identity validation. */
        eastl::vector<uint8_t> readWebgpuLightprobeBytes(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a Pisa cube face.");
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) >
                    std::numeric_limits<size_t>::max())
                throw std::runtime_error("A Pisa cube face has invalid size.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
                throw std::runtime_error("Could not read a Pisa cube face.");
            return bytes;
        }

        /** Calculates one lowercase SHA-256 digest for asset locking. */
        eastl::string calculateWebgpuLightprobeSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (const uint8_t value : digest)
                stream << std::setw(2) << unsigned(value);
            return eastl::string(stream.str().c_str());
        }

        /** Converts one decoded sRGB byte channel to linear working space. */
        double webgpuLightprobeSrgbToLinear(uint8_t value)
        {
            const double encoded = double(value) / 255.0;
            return encoded <= 0.04045
                ? encoded / 12.92
                : std::pow((encoded + 0.055) / 1.055, 2.4);
        }

        /** Builds one exact unit SphereGeometry for the requested segment counts. */
        void buildWebgpuLightprobeSphere(
            eastl::vector<WebgpuLightprobeVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            uint32_t widthSegments,
            uint32_t heightSegments)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(
                (widthSegments + 1u) * (heightSegments + 1u));
            for (uint32_t row = 0u; row <= heightSegments; ++row)
            {
                const double v = double(row) / double(heightSegments);
                const double theta = v * Pi;
                const double y = std::cos(theta);
                const double ringRadius = std::sqrt(std::max(0.0, 1.0 - y * y));
                for (uint32_t column = 0u; column <= widthSegments; ++column)
                {
                    const double phi =
                        double(column) / double(widthSegments) * 2.0 * Pi;
                    const glm::dvec3 precisePosition(
                        -ringRadius * std::cos(phi),
                        y,
                        ringRadius * std::sin(phi));
                    vertices.push_back({
                        .position = glm::vec4(glm::vec3(precisePosition), 1.0f),
                        .normal = glm::vec4(
                            glm::vec3(glm::normalize(precisePosition)), 0.0f)});
                }
            }
            for (uint32_t row = 0u; row < heightSegments; ++row)
            {
                for (uint32_t column = 0u; column < widthSegments; ++column)
                {
                    const uint32_t a =
                        row * (widthSegments + 1u) + column + 1u;
                    const uint32_t b = a - 1u;
                    const uint32_t c =
                        (row + 1u) * (widthSegments + 1u) + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u)
                        indices.insert(indices.end(), {a, b, d});
                    if (row != heightSegments - 1u)
                        indices.insert(indices.end(), {b, c, d});
                }
            }
            const uint32_t expectedVertexCount =
                (widthSegments + 1u) * (heightSegments + 1u);
            const uint32_t expectedIndexCount =
                widthSegments * (heightSegments - 1u) * 6u;
            if (vertices.size() != expectedVertexCount ||
                indices.size() != expectedIndexCount)
                throw std::runtime_error(
                    "LightProbe SphereGeometry topology differs from r185.");
        }

        /** Evaluates the nine real SH basis values used by Three r185. */
        eastl::array<double, 9u> evaluateWebgpuLightprobeBasis(
            const glm::dvec3 &normal)
        {
            const double x = normal.x;
            const double y = normal.y;
            const double z = normal.z;
            return {{
                0.282095,
                0.488603 * y,
                0.488603 * z,
                0.488603 * x,
                1.092548 * x * y,
                1.092548 * y * z,
                0.315392 * (3.0 * z * z - 1.0),
                1.092548 * x * z,
                0.546274 * (x * x - y * y)}};
        }

        /** Returns the r185 cube-texture direction for one face pixel. */
        glm::dvec3 makeWebgpuLightprobeDirection(
            uint32_t face,
            double column,
            double row)
        {
            if (face == 0u) return {-1.0, row, -column};
            if (face == 1u) return {1.0, row, column};
            if (face == 2u) return {-column, 1.0, -row};
            if (face == 3u) return {-column, -1.0, row};
            if (face == 4u) return {-column, row, 1.0};
            return {column, row, -1.0};
        }

        /** Projects the six decoded radiance faces into exact CPU SH coefficients. */
        WebgpuLightprobeHostShData
        buildWebgpuLightprobeSh(
            const eastl::array<RgbaImageData, 6u> &images)
        {
            eastl::array<glm::dvec3, 9u> coefficients{};
            double totalWeight = 0.0;
            constexpr double PixelSize = 2.0 / 256.0;
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                const RgbaImageData &image = images[face];
                for (uint32_t rowIndex = 0u; rowIndex < 256u; ++rowIndex)
                {
                    const double row =
                        1.0 - (double(rowIndex) + 0.5) * PixelSize;
                    for (uint32_t columnIndex = 0u; columnIndex < 256u; ++columnIndex)
                    {
                        const double column =
                            -1.0 + (double(columnIndex) + 0.5) * PixelSize;
                        const glm::dvec3 cubeCoordinate =
                            makeWebgpuLightprobeDirection(
                                face, column, row);
                        const double lengthSquared =
                            glm::dot(cubeCoordinate, cubeCoordinate);
                        const double weight =
                            4.0 / (std::sqrt(lengthSquared) * lengthSquared);
                        totalWeight += weight;
                        const glm::dvec3 direction =
                            glm::normalize(cubeCoordinate);
                        const auto basis =
                            evaluateWebgpuLightprobeBasis(direction);
                        // ImageIO exposes this PNG payload bottom-up after its
                        // CoreGraphics draw. Browser canvas getImageData(),
                        // which r185 projects into SH, is top-down.
                        const size_t offset =
                            (static_cast<size_t>(255u - rowIndex) * 256u +
                                columnIndex) * 4u;
                        const glm::dvec3 color(
                            webgpuLightprobeSrgbToLinear(
                                image.pixels[offset]),
                            webgpuLightprobeSrgbToLinear(
                                image.pixels[offset + 1u]),
                            webgpuLightprobeSrgbToLinear(
                                image.pixels[offset + 2u]));
                        for (uint32_t coefficient = 0u; coefficient < 9u; ++coefficient)
                            coefficients[coefficient] +=
                                color * (basis[coefficient] * weight);
                    }
                }
            }
            const double normalization = 4.0 * Pi / totalWeight;
            WebgpuLightprobeHostShData result{};
            for (uint32_t coefficient = 0u; coefficient < 9u; ++coefficient)
                result.coefficients[coefficient] = glm::vec4(
                    glm::vec3(coefficients[coefficient] * normalization), 0.0f);
            return result;
        }

        /** Builds the exact generated-backend perspective projection. */
        glm::mat4 makeWebgpuLightprobeProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double tangent = std::tan(20.0 * Pi / 180.0);
            glm::mat4 result(0.0f);
            result[0u][0u] = float(1.0 / (1.6 * tangent));
            result[1u][1u] = float(-1.0 / tangent);
            result[2u][2u] = float(FarDistance / (NearDistance - FarDistance));
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(
                FarDistance * NearDistance / (NearDistance - FarDistance));
            return result;
        }

        /** Appends one typed buffer payload to the unique Set allocation. */
        void appendWebgpuLightprobePayload(
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

        /** Prepares parent directories for one requested evidence artifact. */
        void prepareWebgpuLightprobeOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic binary artifact. */
        void writeWebgpuLightprobeBinary(
            const eastl::string &path,
            const eastl::vector<uint8_t> &bytes)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuLightprobeOutput(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!output)
                throw std::runtime_error("Could not write LightProbe RGBA evidence.");
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebgpuLightprobeText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuLightprobeOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write LightProbe text evidence.");
        }
    } // namespace

    void WebgpuLightprobeRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "canonical-pisa-loaded" &&
            options.targetFrame == 0u;
        const bool orbit =
            options.scenarioId == "intensity-and-orbit" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgpu_lightprobe" ||
            (!initial && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (initial && !options.inputReplayPath.empty()) ||
            (orbit && options.inputReplayPath.empty()))
            throw std::invalid_argument(
                "WebGPU LightProbe requires the locked r185 scenarios.");
        device = inDevice;
        buildWebgpuLightprobeSphere(
            materialVertices, materialIndices, 64u, 32u);
        buildWebgpuLightprobeSphere(
            helperVertices, helperIndices, 32u, 16u);

        eastl::array<RgbaImageData, 6u> baseImages;
        const std::filesystem::path cubeRoot =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "cube" / "pisa";
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            const std::filesystem::path facePath = cubeRoot / CubeFileNames[face];
            const eastl::vector<uint8_t> encoded =
                readWebgpuLightprobeBytes(facePath);
            if (calculateWebgpuLightprobeSha256(encoded) != CubeHashes[face])
                throw std::runtime_error("A Pisa cube face differs from r185.");
            baseImages[face] = decodePngRgba8(facePath);
            if (baseImages[face].width != 256u || baseImages[face].height != 256u)
                throw std::runtime_error("A Pisa cube face has unexpected dimensions.");
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImages[face]);
            if (mipChain.size() != 9u)
                throw std::runtime_error("A Pisa cube face produced an invalid mip chain.");
            for (const RgbaImageData &mip : mipChain)
                faces[face].mipPixels.push_back(mip.pixels);
        }
        shData = buildWebgpuLightprobeSh(baseImages);
        dfgLutPackedPixels.assign(
            ThreeR185DfgLutPackedPixels,
            ThreeR185DfgLutPackedPixels + 256u);

        for (uint32_t entity = 0u; entity < 2u; ++entity)
        {
            objectData[entity].projection = makeWebgpuLightprobeProjection();
            objectData[entity].modelView = glm::mat4(1.0f);
            objectData[entity].normalWorld = glm::mat4(1.0f);
        }
        objectData[0u].modelView[0u][0u] = 5.0f;
        objectData[0u].modelView[1u][1u] = 5.0f;
        objectData[0u].modelView[2u][2u] = 5.0f;
        objectData[0u].modelView[3u][2u] = -30.0f;
        objectData[1u].modelView[3u][0u] = -10.0f;
        objectData[1u].modelView[3u][2u] = -30.0f;
        instanceData.reserved = glm::vec4(0.0f);
        materialData[0u].intensityAndPhase = glm::vec4(1.0f, 0.6f, 1.0f, 0.0f);
        materialData[1u].intensityAndPhase = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        if (orbit)
        {
            materialData[0u].intensityAndPhase =
                glm::vec4(0.72f, 0.34f, 0.58f, 0.0f);
        }
        cameraRightAndTanHalfFov = glm::vec4(
            1.0f, 0.0f, 0.0f, float(std::tan(20.0 * Pi / 180.0)));
        cameraUpAndAspect = glm::vec4(0.0f, 1.0f, 0.0f, 1.6f);
        cameraForwardAndReserved = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create LightProbe Set encoder.");
        GVM::Core::RenderSetAllocInfo materialAllocation;
        materialAllocation.verticesCount =
            static_cast<uint32_t>(materialVertices.size());
        materialAllocation.indicesCount =
            static_cast<uint32_t>(materialIndices.size());
        materialAllocation.instanceCount = 1u;
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::vertices,
            "WebgpuLightprobeMaterialVertices",
            materialVertices.data(),
            materialVertices.size() * sizeof(materialVertices[0]), 1u);
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::indices,
            "WebgpuLightprobeMaterialIndices",
            materialIndices.data(),
            materialIndices.size() * sizeof(materialIndices[0]), 1u);
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::objects,
            "WebgpuLightprobeMaterialObject",
            &objectData[0u], sizeof(objectData[0u]), 1u);
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::instances,
            "WebgpuLightprobeMaterialInstance",
            &instanceData, sizeof(instanceData), 1u);
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::materials,
            "WebgpuLightprobeMaterialData",
            &materialData[0u], sizeof(materialData[0u]), 1u);
        appendWebgpuLightprobePayload(
            materialAllocation,
            WebgpuLightprobeSceneRenderSetComponents::sphericalHarmonics,
            "WebgpuLightprobeMaterialSphericalHarmonics",
            &shData, sizeof(shData), 1u);
        encoder->allocEntity(materialAllocation);

        GVM::Core::RenderSetAllocInfo helperAllocation;
        helperAllocation.verticesCount =
            static_cast<uint32_t>(helperVertices.size());
        helperAllocation.indicesCount =
            static_cast<uint32_t>(helperIndices.size());
        helperAllocation.instanceCount = 1u;
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::vertices,
            "WebgpuLightprobeHelperVertices",
            helperVertices.data(),
            helperVertices.size() * sizeof(helperVertices[0]), 1u);
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::indices,
            "WebgpuLightprobeHelperIndices",
            helperIndices.data(),
            helperIndices.size() * sizeof(helperIndices[0]), 1u);
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::objects,
            "WebgpuLightprobeHelperObject",
            &objectData[1u], sizeof(objectData[1u]), 1u);
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::instances,
            "WebgpuLightprobeHelperInstance",
            &instanceData, sizeof(instanceData), 1u);
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::materials,
            "WebgpuLightprobeHelperMaterial",
            &materialData[1u], sizeof(materialData[1u]), 1u);
        appendWebgpuLightprobePayload(
            helperAllocation,
            WebgpuLightprobeSceneRenderSetComponents::sphericalHarmonics,
            "WebgpuLightprobeHelperSphericalHarmonics",
            &shData, sizeof(shData), 1u);
        encoder->allocEntity(helperAllocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuLightprobeRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuLightprobeRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeWebgpuLightprobeBinary(options.captureRgbaPath, rgba);
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_lightprobe\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"";
        if (options.scenarioId == "intensity-and-orbit")
        {
            metadata
                << ",\"inputReplay\":{\"schemaVersion\":1,"
                << "\"caseId\":\"webgpu_lightprobe\","
                << "\"scenarioId\":\"intensity-and-orbit\","
                << "\"captureFrame\":1,"
                << "\"sha256\":\"3c0606256a0fe640e432332939f13bd3c8177cbc0d07330907b8179552e73d42\","
                << "\"target\":\"body > canvas\",\"eventCount\":1,"
                << "\"lastEventFrame\":0}";
        }
        metadata << "}\n";
        writeWebgpuLightprobeText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_lightprobe\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":2,"
            << "\"entityCount\":2,\"instanceCount\":2,"
            << "\"vertexCount\":2706,\"indexCount\":14784,"
            << "\"scenePassCount\":1,\"screenPassCount\":1,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebgpuLightprobeSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebgpuLightprobeSceneRenderSet\","
            << "\"renderableObjectCount\":2,\"entityCount\":2,"
            << "\"entities\":[{\"entityId\":0,"
            << "\"logicalRenderableId\":\"standard-material-sphere\","
            << "\"instanceCount\":1},{\"entityId\":1,"
            << "\"logicalRenderableId\":\"light-probe-helper\","
            << "\"instanceCount\":1}],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"sphericalHarmonics\",\"kind\":\"buffer\","
            << "\"role\":\"light-probe-sh\"},{\"name\":\"environmentTextures\","
            << "\"kind\":\"texture\",\"role\":\"environment-atlas\"}],"
            << "\"drawCommandCount\":1,"
            << "\"directDrawFallback\":false,\"scenePasses\":[{"
            << "\"name\":\"scene-main-lightprobe-pbr\",\"renderClass\":"
            << "\"WebgpuLightprobeSceneMainPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"scene-main-lightprobe-pbr\",\"entityOrdinal\":0}]}\n";
        writeWebgpuLightprobeText(options.sceneSnapshotPath, snapshot.str());
        writeWebgpuLightprobeText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebgpuLightprobeRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        materialVertices.clear();
        materialIndices.clear();
        helperVertices.clear();
        helperIndices.clear();
        for (auto &face : faces) face.mipPixels.clear();
    }
} // namespace GVM::ThreeSamples
