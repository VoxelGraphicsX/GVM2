#include "WebglMaterialsEnvmapsRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <glm/geometric.hpp>
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
        constexpr uint32_t EnvironmentFaceExtent = 1024u;
        constexpr uint32_t EnvironmentFaceMipCount = 11u;
        constexpr uint32_t EnvironmentEquirectangularWidth = 4096u;
        constexpr uint32_t EnvironmentEquirectangularHeight = 2048u;
        constexpr uint32_t EnvironmentEquirectangularMipCount = 13u;
        constexpr uint32_t EnvironmentSphereDetail = 15u;
        constexpr float EnvironmentPi = 3.14159265358979323846f;
        constexpr const char *EnvironmentReplaySha256 =
            "bd02c01af1c02bdda2ff04f765e589cc1fdde19dde390bd09bcb6d5bddf22a4f";
        constexpr const char *EnvironmentEquirectangularSha256 =
            "3efa22071f3f84ec26248ee58aa05aba635a0a9d810bca93da5a94f33e907524";
        constexpr eastl::array<const char *, 6u> EnvironmentCubeFileNames = {
            "posx.jpg",
            "negx.jpg",
            "posy.jpg",
            "negy.jpg",
            "posz.jpg",
            "negz.jpg"};
        constexpr eastl::array<const char *, 6u> EnvironmentCubeSha256 = {
            "bfea380ae3b2f2b44d3040c9fe7a8f3e325450a12b7a0f29561b5ca175f1a250",
            "047052e77f698a819903f1d2fe27b40ee56930150fc38913da7e1c58e8dcae70",
            "cc467777340037ee8f95460b19772a8ae6a71c125af1714bfd408db2c1c33d98",
            "52b4b95125c777d48ac9ea568d79d55fd3fb62e24a7da37600720cdddb7ab333",
            "d56085e5da2e8f2cee13694519ed153995045f5a11b1d41d25be70380cf15548",
            "1bc79f442f31931576d734565821b41b331a017dd86f2ac6a60ac6d3bbbbe6ba"};

        /** Creates parent directories for one explicitly requested environment artifact. */
        void prepareEnvironmentOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable asset or replay into bounded storage. */
        eastl::vector<uint8_t> readEnvironmentBytes(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a pinned environment-map input.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "A pinned environment-map input is empty.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read a pinned environment-map input.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded byte range. */
        eastl::string calculateEnvironmentSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(
                    std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Environment-map SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH>
                digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] =
                "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(
                    HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Appends one normalized non-index-shared sphere triangle. */
        void appendEnvironmentSphereTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<uint32_t> &indices)
        {
            const glm::dvec3 vertices[3u] = {
                glm::normalize(a),
                glm::normalize(b),
                glm::normalize(c)};
            for (const glm::dvec3 &vertex : vertices)
            {
                const glm::vec4 value(
                    float(vertex.x),
                    float(vertex.y),
                    float(vertex.z),
                    1.0f);
                positions.push_back(value);
                normals.push_back(value);
                indices.push_back(
                    static_cast<uint32_t>(indices.size()));
            }
        }

        /** Subdivides one base icosahedron face with r185's linear detail grid. */
        void appendEnvironmentSubdividedFace(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<uint32_t> &indices)
        {
            const uint32_t columns = EnvironmentSphereDetail + 1u;
            eastl::vector<eastl::vector<glm::dvec3>> grid;
            grid.resize(columns + 1u);
            for (uint32_t column = 0u;
                 column <= columns;
                 ++column)
            {
                const double columnFactor =
                    double(column) / double(columns);
                const glm::dvec3 start =
                    a + (c - a) * columnFactor;
                const glm::dvec3 end =
                    b + (c - b) * columnFactor;
                const uint32_t rows = columns - column;
                grid[column].resize(rows + 1u);
                for (uint32_t row = 0u; row <= rows; ++row)
                {
                    grid[column][row] =
                        row == 0u && column == columns
                            ? start
                            : start +
                                  (end - start) *
                                      (double(row) / double(rows));
                }
            }
            for (uint32_t column = 0u;
                 column < columns;
                 ++column)
            {
                for (uint32_t triangle = 0u;
                     triangle < 2u * (columns - column) - 1u;
                     ++triangle)
                {
                    const uint32_t row = triangle / 2u;
                    if ((triangle & 1u) == 0u)
                    {
                        appendEnvironmentSphereTriangle(
                            grid[column][row + 1u],
                            grid[column + 1u][row],
                            grid[column][row],
                            positions,
                            normals,
                            indices);
                    }
                    else
                    {
                        appendEnvironmentSphereTriangle(
                            grid[column][row + 1u],
                            grid[column + 1u][row + 1u],
                            grid[column + 1u][row],
                            positions,
                            normals,
                            indices);
                    }
                }
            }
        }

        /** Generates the exact detail-15 IcosahedronGeometry position and normal streams. */
        void buildEnvironmentSphere(
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<uint32_t> &indices)
        {
            const double goldenRatio =
                (1.0 + std::sqrt(5.0)) / 2.0;
            const glm::dvec3 baseVertices[12u] = {
                {-1.0, goldenRatio, 0.0},
                {1.0, goldenRatio, 0.0},
                {-1.0, -goldenRatio, 0.0},
                {1.0, -goldenRatio, 0.0},
                {0.0, -1.0, goldenRatio},
                {0.0, 1.0, goldenRatio},
                {0.0, -1.0, -goldenRatio},
                {0.0, 1.0, -goldenRatio},
                {goldenRatio, 0.0, -1.0},
                {goldenRatio, 0.0, 1.0},
                {-goldenRatio, 0.0, -1.0},
                {-goldenRatio, 0.0, 1.0}};
            constexpr uint32_t baseIndices[60u] = {
                0u, 11u, 5u, 0u, 5u, 1u, 0u, 1u, 7u,
                0u, 7u, 10u, 0u, 10u, 11u, 1u, 5u, 9u,
                5u, 11u, 4u, 11u, 10u, 2u, 10u, 7u, 6u,
                7u, 1u, 8u, 3u, 9u, 4u, 3u, 4u, 2u,
                3u, 2u, 6u, 3u, 6u, 8u, 3u, 8u, 9u,
                4u, 9u, 5u, 2u, 4u, 11u, 6u, 2u, 10u,
                8u, 6u, 7u, 9u, 8u, 1u};
            positions.clear();
            normals.clear();
            indices.clear();
            positions.reserve(15360u);
            normals.reserve(15360u);
            indices.reserve(15360u);
            for (uint32_t face = 0u; face < 20u; ++face)
            {
                appendEnvironmentSubdividedFace(
                    baseVertices[baseIndices[face * 3u]],
                    baseVertices[baseIndices[face * 3u + 1u]],
                    baseVertices[baseIndices[face * 3u + 2u]],
                    positions,
                    normals,
                    indices);
            }
            if (positions.size() != 15360u ||
                normals.size() != 15360u ||
                indices.size() != 15360u)
            {
                throw std::runtime_error(
                    "Detail-15 environment sphere produced an unexpected topology.");
            }
        }

        /** Returns Three's transposed intrinsic XYZ environment rotation rows. */
        eastl::array<glm::vec4, 3u> buildEnvironmentRotationRows(
            float angle)
        {
            const float cosineX = std::cos(angle);
            const float sineX = std::sin(angle);
            const float cosineY = cosineX;
            const float sineY = sineX;
            const float cosineZ = cosineX;
            const float sineZ = sineX;
            return {{
                {cosineY * cosineZ,
                 cosineX * sineZ +
                     sineX * sineY * cosineZ,
                 sineX * sineZ -
                     cosineX * sineY * cosineZ,
                 0.0f},
                {-cosineY * sineZ,
                 cosineX * cosineZ -
                     sineX * sineY * sineZ,
                 sineX * cosineZ +
                     cosineX * sineY * sineZ,
                 0.0f},
                {sineY,
                 -sineX * cosineY,
                 cosineX * cosineY,
                 0.0f}}};
        }

        /** Converts one decoded mip chain into renderer-owned byte levels. */
        eastl::vector<eastl::vector<uint8_t>> collectEnvironmentMipPixels(
            const eastl::vector<RgbaImageData> &mips)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(mips.size());
            for (const RgbaImageData &mip : mips)
            {
                result.push_back(mip.pixels);
            }
            return result;
        }

        /** Computes a tightly packed RGBA8 capture size without overflow. */
        uint64_t environmentCaptureByteCount(
            uint32_t width,
            uint32_t height)
        {
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error(
                    "Environment-map capture byte count overflowed.");
            }
            return pixelCount * 4u;
        }
    } // namespace

    void WebglMaterialsEnvmapsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        replayScenario =
            options.scenarioId == "equirect-refraction-rotation" &&
            options.targetFrame == 60u;
        if (options.caseId != "webgl_materials_envmaps" ||
            (!initial && !replayScenario) ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.width != 800u ||
            options.height != 500u ||
            options.assetRoot.empty() ||
            (replayScenario != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Environment-map adapter requires one locked Manifest scenario.");
        }
        if (replayScenario &&
            calculateEnvironmentSha256(
                readEnvironmentBytes(
                    std::filesystem::path(
                        options.inputReplayPath.c_str()))) !=
                EnvironmentReplaySha256)
        {
            throw std::invalid_argument(
                "Environment-map replay differs from its pinned identity.");
        }
        device = inDevice;
        buildEnvironmentSphere(positions, normals, indices);

        const std::filesystem::path assetRoot(
            options.assetRoot.c_str());
        const std::filesystem::path cubeRoot =
            assetRoot / "textures" / "cube" / "Bridge2";
        for (uint32_t face = 0u; face < 6u; ++face)
        {
            const std::filesystem::path facePath =
                cubeRoot / EnvironmentCubeFileNames[face];
            if (calculateEnvironmentSha256(
                    readEnvironmentBytes(facePath)) !=
                EnvironmentCubeSha256[face])
            {
                throw std::invalid_argument(
                    "One Bridge2 face differs from its pinned r185 identity.");
            }
            const RgbaImageData image =
                decodeJpegRgba8(facePath);
            if (image.width != EnvironmentFaceExtent ||
                image.height != EnvironmentFaceExtent)
            {
                throw std::runtime_error(
                    "A Bridge2 face decoded to an unexpected extent.");
            }
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(image);
            if (mipChain.size() != EnvironmentFaceMipCount)
            {
                throw std::runtime_error(
                    "A Bridge2 face produced an unexpected mip count.");
            }
            cubeMips[face] =
                collectEnvironmentMipPixels(mipChain);
        }
        const std::filesystem::path equirectangularPath =
            assetRoot /
            "textures" /
            "2294472375_24a3b8ef46_o.jpg";
        if (calculateEnvironmentSha256(
                readEnvironmentBytes(equirectangularPath)) !=
            EnvironmentEquirectangularSha256)
        {
            throw std::invalid_argument(
                "Equirectangular environment differs from its pinned identity.");
        }
        const RgbaImageData equirectangular =
            decodeJpegRgba8(equirectangularPath);
        if (equirectangular.width !=
                EnvironmentEquirectangularWidth ||
            equirectangular.height !=
                EnvironmentEquirectangularHeight)
        {
            throw std::runtime_error(
                "Equirectangular environment decoded to an unexpected extent.");
        }
        const eastl::vector<RgbaImageData> equirectangularMipChain =
            buildSrgbMipChain(equirectangular);
        if (equirectangularMipChain.size() !=
            EnvironmentEquirectangularMipCount)
        {
            throw std::runtime_error(
                "Equirectangular environment produced an unexpected mip count.");
        }
        equirectangularMips =
            collectEnvironmentMipPixels(
                equirectangularMipChain);

        const float yaw = replayScenario
            ? -2.0f * EnvironmentPi * 48.0f / 500.0f
            : 0.0f;
        const float phi = replayScenario
            ? EnvironmentPi * 0.5f +
                  2.0f * EnvironmentPi * 24.0f / 500.0f
            : EnvironmentPi * 0.5f;
        const glm::vec3 cameraPosition(
            std::sin(phi) * std::sin(yaw) * 2.5f,
            std::cos(phi) * 2.5f,
            std::sin(phi) * std::cos(yaw) * 2.5f);
        const glm::vec3 cameraForward =
            glm::normalize(-cameraPosition);
        const glm::vec3 cameraRight = glm::normalize(
            glm::cross(
                cameraForward,
                glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 cameraUp =
            glm::normalize(
                glm::cross(cameraRight, cameraForward));
        cameraPositionAndMode =
            glm::vec4(
                cameraPosition,
                replayScenario ? 1.0f : 0.0f);
        cameraRightAndRefraction =
            glm::vec4(
                cameraRight,
                replayScenario ? 1.0f : 0.0f);
        cameraUpAndFrame =
            glm::vec4(
                cameraUp,
                float(options.targetFrame));
        cameraForwardAndTanHalfFov =
            glm::vec4(
                cameraForward,
                std::tan(35.0f * EnvironmentPi / 180.0f));
        rotationRows =
            buildEnvironmentRotationRows(
                replayScenario ? 0.061f : 0.0f);
    }

    void WebglMaterialsEnvmapsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsEnvmapsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            environmentCaptureByteCount(width, height);
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(
            options,
            frameIndex,
            width,
            height,
            rgba);
        captureWritten = true;
    }

    void WebglMaterialsEnvmapsRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareEnvironmentOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write environment-map RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareEnvironmentOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_materials_envmaps\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (replayScenario)
            {
                output
                    << "{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_materials_envmaps\","
                    << "\"scenarioId\":\"equirect-refraction-rotation\","
                    << "\"captureFrame\":60,"
                    << "\"sha256\":\"" << EnvironmentReplaySha256 << "\","
                    << "\"target\":\"canvas\","
                    << "\"eventCount\":3,"
                    << "\"lastEventFrame\":0}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareEnvironmentOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_materials_envmaps\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"singleSample\":true,\n"
                << "  \"msaaEnabled\":false,\n"
                << "  \"renderSetPolicy\":\"not-required\",\n"
                << "  \"sceneRenderSetCount\":0,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":0,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"scenePassSequence\":[{"
                << "\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"main\"}],\n"
                << "  \"screenPasses\":["
                << "\"cube-atlas-gutter-build\","
                << "\"environment-background\","
                << "\"output-tone-map\"],\n"
                << "  \"sphereVertexCount\":15360,\n"
                << "  \"sphereIndexCount\":15360,\n"
                << "  \"cubeFaceExtent\":[1024,1024],\n"
                << "  \"cubeMipCount\":11,\n"
                << "  \"cubeAtlasExtent\":[3078,4138],\n"
                << "  \"equirectangularExtent\":[4096,2048],\n"
                << "  \"equirectangularMipCount\":13,\n"
                << "  \"environment\":\""
                << (replayScenario
                        ? "equirectangular"
                        : "bridge2-cube")
                << "\",\n"
                << "  \"refraction\":"
                << (replayScenario ? "true" : "false") << ",\n"
                << "  \"backgroundMaterialRotationSynchronized\":"
                << (replayScenario ? "true" : "false") << ",\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
    }

    void WebglMaterialsEnvmapsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        normals.clear();
        indices.clear();
        for (auto &mips : cubeMips)
        {
            mips.clear();
        }
        equirectangularMips.clear();
    }
} // namespace GVM::ThreeSamples
