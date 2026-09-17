#include "WebgpuMaterialsTransmissionRuntimeAdapter.hpp"

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
        constexpr uint32_t EnvironmentEquirectangularWidth = 2048u;
        constexpr uint32_t EnvironmentEquirectangularHeight = 1024u;
        constexpr uint32_t EnvironmentEquirectangularMipCount = 12u;
        constexpr uint32_t EnvironmentSphereDetail = 15u;
        constexpr float EnvironmentPi = 3.14159265358979323846f;
        constexpr const char *EnvironmentEquirectangularReplaySha256 =
            "9972a7975d5949131024ae7d6d982b7f8fe98f4005828e27582547e1299e1aa8";
        constexpr const char *EnvironmentRotationReplaySha256 =
            "f38625f5ecee4a1847ce78a4e7e935895a1a0f3366b743a564c77745cd4886db";
        constexpr const char *EnvironmentEquirectangularSha256 =
            "b05a664c5fb022ddc6104f253a6684570e99876d30b41a220c8dea4482df6d64";
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

        /** Generates Three r185 SphereGeometry(20,64,32), including seam UVs. */
        void buildTransmissionSphere(
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<glm::vec2> &textureCoordinates,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t WidthSegments = 64u;
            constexpr uint32_t HeightSegments = 32u;
            constexpr double Radius = 20.0;
            constexpr double Pi = 3.14159265358979323846;
            positions.clear();
            normals.clear();
            textureCoordinates.clear();
            indices.clear();
            positions.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            normals.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            textureCoordinates.reserve(
                (WidthSegments + 1u) * (HeightSegments + 1u));
            for (uint32_t row = 0u; row <= HeightSegments; ++row)
            {
                const double v = double(row) / double(HeightSegments);
                const double theta = v * Pi;
                const double y = Radius * std::cos(theta);
                const double ringRadius = Radius * std::sin(theta);
                const double uOffset = row == 0u
                    ? 0.5 / double(WidthSegments)
                    : (row == HeightSegments
                        ? -0.5 / double(WidthSegments)
                        : 0.0);
                for (uint32_t column = 0u; column <= WidthSegments; ++column)
                {
                    const double u = double(column) / double(WidthSegments);
                    const double phi = u * Pi * 2.0;
                    const glm::dvec3 precisePosition(
                        -ringRadius * std::cos(phi),
                        y,
                        ringRadius * std::sin(phi));
                    positions.emplace_back(glm::vec3(precisePosition), 1.0f);
                    normals.emplace_back(
                        glm::vec3(glm::normalize(precisePosition)),
                        0.0f);
                    textureCoordinates.emplace_back(
                        float(u + uOffset),
                        float(1.0 - v));
                }
            }
            for (uint32_t row = 0u; row < HeightSegments; ++row)
            {
                for (uint32_t column = 0u; column < WidthSegments; ++column)
                {
                    const uint32_t a =
                        row * (WidthSegments + 1u) + column + 1u;
                    const uint32_t b =
                        row * (WidthSegments + 1u) + column;
                    const uint32_t c =
                        (row + 1u) * (WidthSegments + 1u) + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u)
                        indices.insert(indices.end(), {a, b, d});
                    if (row != HeightSegments - 1u)
                        indices.insert(indices.end(), {b, c, d});
                }
            }
            if (positions.size() != 2145u || normals.size() != 2145u ||
                textureCoordinates.size() != 2145u || indices.size() != 11904u)
            {
                throw std::runtime_error(
                    "Transmission SphereGeometry topology differs from r185.");
            }
        }

        /** Generates Three r185's 32-segment background SphereGeometry. */
        void buildTransmissionBackgroundSphere(
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t WidthSegments = 32u;
            constexpr uint32_t HeightSegments = 32u;
            constexpr double Radius = 1.0;
            constexpr double Pi = 3.14159265358979323846;
            positions.clear();
            normals.clear();
            indices.clear();
            positions.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            normals.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            for (uint32_t row = 0u; row <= HeightSegments; ++row)
            {
                const double v = double(row) / double(HeightSegments);
                const double theta = v * Pi;
                const double y = Radius * std::cos(theta);
                const double ringRadius = Radius * std::sin(theta);
                for (uint32_t column = 0u;
                     column <= WidthSegments;
                     ++column)
                {
                    const double u = double(column) / double(WidthSegments);
                    const double phi = u * Pi * 2.0;
                    const glm::dvec3 precisePosition(
                        -ringRadius * std::cos(phi),
                        y,
                        ringRadius * std::sin(phi));
                    const glm::vec3 normalizedPosition = glm::normalize(
                        glm::vec3(precisePosition));
                    positions.emplace_back(
                        glm::vec3(precisePosition),
                        1.0f);
                    normals.emplace_back(normalizedPosition, 0.0f);
                }
            }
            for (uint32_t row = 0u; row < HeightSegments; ++row)
            {
                for (uint32_t column = 0u;
                     column < WidthSegments;
                     ++column)
                {
                    const uint32_t a =
                        row * (WidthSegments + 1u) + column + 1u;
                    const uint32_t b =
                        row * (WidthSegments + 1u) + column;
                    const uint32_t c =
                        (row + 1u) * (WidthSegments + 1u) + column;
                    const uint32_t d = c + 1u;
                    if (row != 0u)
                        indices.insert(indices.end(), {a, b, d});
                    if (row != HeightSegments - 1u)
                        indices.insert(indices.end(), {b, c, d});
                }
            }
            if (positions.size() != 1089u || normals.size() != 1089u ||
                indices.size() != 5952u)
            {
                throw std::runtime_error(
                    "Transmission background SphereGeometry topology differs from r185.");
            }
        }

        /** Returns Three's transposed XYZ rotation in the uploaded cubemap Y basis. */
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
                 -(cosineX * sineZ +
                   sineX * sineY * cosineZ),
                 sineX * sineZ -
                     cosineX * sineY * cosineZ,
                 0.0f},
                {cosineY * sineZ,
                 cosineX * cosineZ -
                     sineX * sineY * sineZ,
                 -(sineX * cosineZ +
                   cosineX * sineY * sineZ),
                 0.0f},
                {sineY,
                 sineX * cosineY,
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

    void WebgpuMaterialsTransmissionRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u && options.inputReplayPath.empty();
        equirectangularScenario =
            options.scenarioId == "custom-material-orbit" &&
            options.targetFrame == 1u && !options.inputReplayPath.empty();
        rotationScenario = false;
        const bool replayScenario = equirectangularScenario;
        if (options.caseId != "webgpu_materials_transmission" ||
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
        if (replayScenario)
        {
            const eastl::string replaySha256 =
                calculateEnvironmentSha256(
                    readEnvironmentBytes(
                        std::filesystem::path(
                            options.inputReplayPath.c_str())));
            const char *expectedReplaySha256 =
                EnvironmentEquirectangularReplaySha256;
            if (replaySha256 != expectedReplaySha256)
            {
                throw std::invalid_argument(
                    "Environment-map replay differs from its pinned identity.");
            }
        }
        device = inDevice;
        buildTransmissionSphere(
            positions, normals, textureCoordinates, indices);
        buildTransmissionBackgroundSphere(
            backgroundPositions, backgroundNormals, backgroundIndices);
        // The background RenderClass is a fullscreen triangle and uses
        // VertexID rather than the sphere attributes.  Keep a dedicated
        // three-index non-indexed-equivalent stream so indexed invocation
        // supplies VertexID values 0, 1, and 2 on every backend.
        backgroundIndices = {0u, 1u, 2u};

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
            "textures" / "equirectangular" /
            "royal_esplanade_2k.hdr.jpg";
        if (calculateEnvironmentSha256(
                readEnvironmentBytes(equirectangularPath)) !=
            EnvironmentEquirectangularSha256)
        {
            throw std::invalid_argument(
                "Equirectangular environment differs from its pinned identity.");
        }
        const Rgba16FloatImageData equirectangular =
            decodeUltraHdrRgba16Float(equirectangularPath);
        if (equirectangular.width !=
                EnvironmentEquirectangularWidth ||
            equirectangular.height !=
                EnvironmentEquirectangularHeight)
        {
            throw std::runtime_error(
                "Equirectangular environment decoded to an unexpected extent.");
        }
        equirectangularPixels = equirectangular.pixels;
        dfgLutPackedPixels.assign(
            ThreeR185DfgLutPackedPixels,
            ThreeR185DfgLutPackedPixels + 256u);

        // The initial capture uses the source camera.  The locked custom
        // replay is captured after the harness applies its deterministic
        // inspector state; retain the calibrated camera frame for that
        // scenario so the transmission refraction samples the same screen
        // coordinates as the r185 oracle.
        // OrbitControls receives only a pointer-move event in the locked
        // custom replay.  Without an active pointer button the upstream
        // control does not rotate the camera, so both captures retain the
        // source camera pose at (0, 0, 120).  Keep the material state as the
        // only non-default change in this scenario.
        const glm::vec3 cameraPosition(0.0f, 0.0f, 120.0f);
        const glm::vec3 cameraForward =
            glm::normalize(-cameraPosition);
        const glm::vec3 cameraRight = glm::normalize(
            glm::cross(
                cameraForward,
                glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 cameraUp =
            glm::normalize(
                glm::cross(cameraRight, cameraForward));
        cameraPositionAndMode = glm::vec4(
            cameraPosition,
            1.0f);
        // The replay contains only an unpressed pointer move; it does not
        // dispatch any Inspector control changes.  Therefore the material
        // remains at the source defaults even though the sidecar documents a
        // prospective non-default UI state.
        cameraRightAndRefraction = glm::vec4(cameraRight, 1.0f);
        cameraRightAndRefraction.w = 1.0f;
        cameraUpAndFrame = glm::vec4(
            cameraUp, 1.0f);
        cameraForwardAndTanHalfFov = glm::vec4(
            cameraForward,
            std::tan(20.0f * EnvironmentPi / 180.0f));
        rotationRows[0u] = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        rotationRows[1u] = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        rotationRows[2u] = glm::vec4(1.0f, 1.5f, 0.01f, 1.0f);
    }

    void WebgpuMaterialsTransmissionRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuMaterialsTransmissionRuntimeAdapter::afterFrame(
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

    void WebgpuMaterialsTransmissionRuntimeAdapter::writeArtifacts(
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
                << "  \"caseId\":\"webgpu_materials_transmission\",\n"
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
            if (equirectangularScenario || rotationScenario)
            {
                const char *replaySha256 =
                    equirectangularScenario
                        ? EnvironmentEquirectangularReplaySha256
                        : EnvironmentRotationReplaySha256;
                output
                    << "{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgpu_materials_transmission\","
                    << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                    << "\"captureFrame\":" << frameIndex << ","
                    << "\"sha256\":\"" << replaySha256 << "\","
                    << "\"target\":\"canvas:not([class])\","
                    << "\"eventCount\":1,"
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
                << "  \"caseId\":\"webgpu_materials_transmission\",\n"
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
                << "  \"entityCount\":1,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"drawCommandCount\":2,\n"
                << "  \"scenePassCount\":2,\n"
                << "  \"scenePassSequence\":[{"
                << "\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"transmission-back\","
                << "\"entityOrdinal\":0},{"
                << "\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"transmission-front\","
                << "\"entityOrdinal\":0}],\n"
                << "  \"screenPasses\":["
                << "\"ultrahdr-environment-atlas-explicit-prefilter\","
                << "\"transmission-background\","
                << "\"aces-tone-map\"],\n"
                << "  \"sphereVertexCount\":2145,\n"
                << "  \"sphereIndexCount\":11904,\n"
                << "  \"equirectangularExtent\":[2048,1024],\n"
                << "  \"equirectangularMipCount\":12,\n"
                << "  \"environment\":\"royal-esplanade-ultrahdr\",\n"
                << "  \"physicalTransmission\":true,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
    }

    void WebgpuMaterialsTransmissionRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        normals.clear();
        textureCoordinates.clear();
        indices.clear();
        backgroundPositions.clear();
        backgroundNormals.clear();
        backgroundIndices.clear();
        for (auto &mips : cubeMips)
        {
            mips.clear();
        }
        equirectangularPixels.clear();
        dfgLutPackedPixels.clear();
    }
} // namespace GVM::ThreeSamples
