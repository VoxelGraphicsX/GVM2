#include "WebgpuParallaxUvRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "SampleAssetDecoders.hpp"
#include "ThreeR185DfgLutData.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *ParallaxTextureFiles[5u] = {
            "Ice002_1K-JPG_Color.jpg",
            "Ice003_1K-JPG_Color.jpg",
            "Ice002_1K-JPG_Roughness.jpg",
            "Ice002_1K-JPG_NormalGL.jpg",
            "Ice002_1K-JPG_Displacement.jpg",
        };
        constexpr const char *ParallaxTextureHashes[5u] = {
            "9aa010f0fa4e01a982e593eb5b06f82671acc9023a67d9451bbc94cacd7ef44c",
            "faee9ddb462d05bc96ebd3a4d412d229381e1d188abf777102e08aa4e696740a",
            "52a806348c7ddb23f201e4c1fc677e33914f0921ba7253163533daee3c4de121",
            "f2c190e341fede5bec99c86757529e4df387dba8a1ab8127bcc85b7ed528a018",
            "68be58c53d6e4238dccb0efd3a8c89a947477ff9c51b736e905cb9075e7a3a03",
        };
        constexpr const char *ParallaxEnvironmentHash =
            "fe2d6641d4798d49e86548a81bc7e313caa16d99aac38b49808bf975b164d8c5";
        constexpr const char *ParallaxSettingsReplayHash =
            "43d65915610a78b6fdc40a6d83272d5df3ab541aa4b62697c5bc0479c033fba9";
        constexpr const char *ParallaxSettingsReplayTarget =
            "canvas:not([class])";
        constexpr const char *ParallaxCanonicalSceneHash =
            "5fba73cd084e5d493a55c4c2ef336c6c7d928a00d2e6167a3da8a7f228cb9e30";
        constexpr uint32_t ParallaxMaterialBaseSize = 1024u;
        constexpr uint32_t ParallaxMaterialMipCount = 11u;
        constexpr uint32_t ParallaxMaterialAtlasWidth = 1026u;
        constexpr uint32_t ParallaxMaterialAtlasHeight = 2070u;

        /** Packs one complete material mip chain into a single-mip atlas with wrapped one-texel gutters. */
        eastl::vector<uint8_t> packParallaxMaterialMipAtlas(
            const eastl::vector<RgbaImageData> &mips)
        {
            if (mips.size() != ParallaxMaterialMipCount ||
                mips[0u].width != ParallaxMaterialBaseSize ||
                mips[0u].height != ParallaxMaterialBaseSize)
            {
                throw std::invalid_argument(
                    "Parallax material mip atlas requires one complete 1024-square chain.");
            }
            eastl::vector<uint8_t> atlas(
                size_t(ParallaxMaterialAtlasWidth) *
                    ParallaxMaterialAtlasHeight * 4u,
                0u);
            uint32_t destinationY = 0u;
            for (uint32_t level = 0u;
                 level < ParallaxMaterialMipCount;
                 ++level)
            {
                const RgbaImageData &mip = mips[level];
                const uint32_t expectedSize =
                    eastl::max(1u, ParallaxMaterialBaseSize >> level);
                if (mip.width != expectedSize || mip.height != expectedSize ||
                    mip.pixels.size() != size_t(expectedSize) * expectedSize * 4u)
                {
                    throw std::invalid_argument(
                        "Parallax material mip atlas received an invalid level.");
                }
                for (int32_t row = -1;
                     row <= int32_t(expectedSize);
                     ++row)
                {
                    const uint32_t sourceY = uint32_t(
                        (row + int32_t(expectedSize)) % int32_t(expectedSize));
                    for (int32_t column = -1;
                         column <= int32_t(expectedSize);
                         ++column)
                    {
                        const uint32_t sourceX = uint32_t(
                            (column + int32_t(expectedSize)) % int32_t(expectedSize));
                        const size_t sourceOffset =
                            (size_t(sourceY) * expectedSize + sourceX) * 4u;
                        const size_t destinationOffset =
                            (size_t(destinationY + uint32_t(row + 1)) *
                                 ParallaxMaterialAtlasWidth +
                             uint32_t(column + 1)) * 4u;
                        std::memcpy(
                            atlas.data() + destinationOffset,
                            mip.pixels.data() + sourceOffset,
                            4u);
                    }
                }
                destinationY += expectedSize + 2u;
            }
            if (destinationY != ParallaxMaterialAtlasHeight - 1u)
            {
                throw std::runtime_error(
                    "Parallax material mip atlas layout is inconsistent.");
            }
            return atlas;
        }

        /** Reads one bounded immutable asset into deterministic host storage. */
        eastl::vector<uint8_t> readParallaxAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open one locked parallax asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("One parallax asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read one complete parallax asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded payload. */
        eastl::string calculateParallaxSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Converts one finite float channel to IEEE 754 binary16 storage. */
        uint16_t convertParallaxFloatToHalf(float value)
        {
            // Match Three.js DataUtils.toHalfFloat: clamp to the finite
            // half-float range and truncate the mantissa instead of rounding
            // it.  HDRLoader uploads these exact half bits to WebGPU.
            if (value > 65504.0f) value = 65504.0f;
            if (value < -65504.0f) value = -65504.0f;
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            const uint32_t tableIndex = (bits >> 23u) & 0x1ffu;
            const int32_t exponent =
                int32_t(tableIndex & 0xffu) - 127;
            uint32_t base = 0u;
            uint32_t shift = 0u;
            if (exponent < -27)
            {
                base = 0u;
                shift = 24u;
            }
            else if (exponent < -14)
            {
                base = 0x0400u >> uint32_t(-exponent - 14);
                shift = uint32_t(-exponent - 1);
            }
            else if (exponent <= 15)
            {
                base = uint32_t(exponent + 15) << 10u;
                shift = 13u;
            }
            else if (exponent < 128)
            {
                base = 0x7c00u;
                shift = 24u;
            }
            else
            {
                base = 0x7c00u;
                shift = 13u;
            }
            if ((tableIndex & 0x100u) != 0u)
            {
                base |= 0x8000u;
            }
            return static_cast<uint16_t>(
                base | ((bits & 0x007fffffu) >> shift));
        }

        /** Creates the exact indexed CircleGeometry(25,64) payload. */
        void buildParallaxCircle(
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals,
            eastl::vector<glm::vec2> &textureCoordinates,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t Segments = 64u;
            positions.clear();
            normals.clear();
            textureCoordinates.clear();
            indices.clear();
            positions.reserve(Segments + 2u);
            normals.reserve(Segments + 2u);
            textureCoordinates.reserve(Segments + 2u);
            positions.push_back(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
            textureCoordinates.push_back(glm::vec2(0.5f));
            constexpr float Pi = 3.14159265358979323846f;
            for (uint32_t segment = 0u; segment <= Segments; ++segment)
            {
                const float angle = float(segment) / float(Segments) *
                    Pi * 2.0f;
                const float x = std::cos(angle);
                const float y = std::sin(angle);
                positions.push_back(glm::vec4(x * 25.0f, y * 25.0f, 0.0f, 1.0f));
                normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
                textureCoordinates.push_back(glm::vec2(
                    (x + 1.0f) * 0.5f,
                    (y + 1.0f) * 0.5f));
            }
            for (uint32_t segment = 1u; segment <= Segments; ++segment)
            {
                indices.insert(indices.end(), {segment, segment + 1u, 0u});
            }
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareParallaxOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one deterministic UTF-8 artifact. */
        void writeParallaxText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareParallaxOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error("Could not write a parallax text artifact.");
            }
        }
    } // namespace

    void WebgpuParallaxUvRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-loaded" && options.targetFrame == 0u;
        const bool canonical =
            options.scenarioId == "canonical-assets" && options.targetFrame == 0u;
        const bool autoOrbit =
            options.scenarioId == "auto-orbit" && options.targetFrame == 60u;
        const bool settings =
            options.scenarioId == "settings-orbit" && options.targetFrame == 61u;
        if (options.caseId != "webgpu_parallax_uv" ||
            (!initial && !canonical && !autoOrbit && !settings) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (settings != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Parallax UV adapter requires one locked Manifest scenario.");
        }
        if (settings)
        {
            const eastl::vector<uint8_t> replayBytes =
                readParallaxAsset(std::filesystem::path(options.inputReplayPath.c_str()));
            if (calculateParallaxSha256(replayBytes) != ParallaxSettingsReplayHash)
            {
                throw std::runtime_error(
                    "The parallax settings replay differs from its r185 lock.");
            }
        }
        device = inDevice;
        buildParallaxCircle(positions, normals, textureCoordinates, indices);
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const std::filesystem::path textureRoot =
            assetRoot / "textures" / "ambientcg";
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            const std::filesystem::path path =
                textureRoot / ParallaxTextureFiles[index];
            const eastl::vector<uint8_t> bytes = readParallaxAsset(path);
            if (calculateParallaxSha256(bytes) != ParallaxTextureHashes[index])
            {
                throw std::runtime_error("One Ice texture differs from its r185 lock.");
            }
            const RgbaImageData image = decodeJpegRgba8(path);
            if (index == 0u)
            {
                textureWidth = image.width;
                textureHeight = image.height;
            }
            if (image.width != textureWidth || image.height != textureHeight)
            {
                throw std::runtime_error("The five Ice textures must share one extent.");
            }
            const eastl::vector<RgbaImageData> mipChain =
                index < 2u
                    ? buildSrgbMipChain(image)
                    : buildUnormMipChain(image);
            textureAtlases[index] = packParallaxMaterialMipAtlas(mipChain);
        }
        textureAtlasWidth = ParallaxMaterialAtlasWidth;
        textureAtlasHeight = ParallaxMaterialAtlasHeight;
        const std::filesystem::path environmentPath =
            assetRoot / "textures" / "equirectangular" /
            "752-hdri-skies-com_1k.hdr";
        const eastl::vector<uint8_t> environmentBytes =
            readParallaxAsset(environmentPath);
        if (calculateParallaxSha256(environmentBytes) != ParallaxEnvironmentHash)
        {
            throw std::runtime_error("The parallax HDR differs from its r185 lock.");
        }
        const ThreeCompat::DecodedRadianceImage environmentImage =
            ThreeCompat::decodeRadianceRgbe(environmentBytes);
        environmentWidth = environmentImage.width;
        environmentHeight = environmentImage.height;
        environmentPixels.reserve(environmentImage.rgba.size());
        for (float value : environmentImage.rgba)
        {
            environmentPixels.push_back(convertParallaxFloatToHalf(value));
        }
        dfgLutPackedPixels.assign(
            ThreeR185DfgLutPackedPixels,
            ThreeR185DfgLutPackedPixels + 256u);

        constexpr float Pi = 3.14159265358979323846f;
        // OrbitControls performs one explicit update after enabling autoRotate;
        // the initial capture therefore uses exactly one fixed 60 Hz step.
        const float orbitAngle = autoOrbit
            ? 62.0f * Pi / 1800.0f
            : 2.0f * Pi / 1800.0f;
        const float baseCosine = std::cos(orbitAngle);
        const float baseSine = std::sin(orbitAngle);
        glm::vec3 cameraPosition(
            15.0f * baseCosine + 15.0f * baseSine,
            7.0f,
            -15.0f * baseSine + 15.0f * baseCosine);
        if (settings)
        {
            /* OrbitControls starts from (15,7,15), applies the recorded
               56/-28 pixel drag on a 500px element, and leaves the camera
               below the ground plane. The fixed-step host advances the
               auto-rotation while the replay is active. */
            const float radius = std::sqrt(499.0f);
            const float theta = Pi * 0.25f + 63.0f * Pi / 1800.0f -
                2.0f * Pi * 56.0f / 500.0f;
            const float phi = std::acos(7.0f / radius) +
                2.0f * Pi * 28.0f / 500.0f;
            cameraPosition = glm::vec3(
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::cos(theta));
        }
        else if (autoOrbit)
        {
            cameraPosition = glm::vec3(
                15.0f * baseCosine + 15.0f * baseSine,
                7.0f,
                -15.0f * baseSine + 15.0f * baseCosine);
        }
        const glm::vec3 cameraForward = glm::normalize(-cameraPosition);
        const glm::vec3 cameraRight = glm::normalize(glm::cross(
            cameraForward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 cameraUp = glm::normalize(glm::cross(
            cameraRight, cameraForward));
        cameraPositionAndScale = glm::vec4(cameraPosition, settings ? 4.5f : 3.0f);
        cameraRightAndBackgroundBlur = glm::vec4(
            cameraRight, settings ? 0.8f : 0.4f);
        cameraUpAndParallaxScale = glm::vec4(
            cameraUp, settings ? 0.25f : 0.5f);
        cameraForwardAndTanHalfFov = glm::vec4(
            cameraForward, std::tan(22.5f * Pi / 180.0f));
        viewportAndExposure = glm::vec4(800.0f, 500.0f, 6.0f, 0.0f);
        const float outputOffset = autoOrbit ? 0.0f : 0.30f;
        outputFlagsAndOffset = glm::vec4(
            (canonical || autoOrbit || settings) ? 1.0f : 0.0f,
            outputOffset,
            0.0f,
            0.0f);
    }

    void WebgpuParallaxUvRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuParallaxUvRuntimeAdapter::afterFrame(
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
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareParallaxOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write the parallax RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgpu_parallax_uv\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"samplePolicy\":{\"mode\":\"single-sample\","
                 << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"inputReplay\":";
        if (options.scenarioId == "settings-orbit")
        {
            metadata << "{\"schemaVersion\":1,\"caseId\":\"webgpu_parallax_uv\","
                     << "\"scenarioId\":\"settings-orbit\",\"captureFrame\":61,"
                     << "\"sha256\":\"" << ParallaxSettingsReplayHash
                     << "\",\"target\":\"" << ParallaxSettingsReplayTarget
                     << "\",\"eventCount\":3,\"lastEventFrame\":0}";
        }
        else
        {
            metadata << "null";
        }
        metadata << "\n}\n";
        writeParallaxText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_parallax_uv\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"implementationLevel\":\"semantic-complete\",\n"
                 << "  \"gpuWorkDslOnly\":true,\n"
                 << "  \"renderSetPolicy\":\"not-required\",\n"
                 << "  \"sceneRenderSetCount\":0,\n"
                 << "  \"renderableObjectCount\":1,\n"
                 << "  \"entityCount\":1,\n  \"instanceCount\":1,\n"
                 << "  \"vertexCount\":66,\n  \"indexCount\":192,\n"
                 << "  \"scenePassCount\":1,\n  \"screenPassCount\":2,\n"
                 << "  \"drawCommandCount\":3,\n"
                 << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                 << "\"scenePass\":\"main-parallax\",\"entityOrdinal\":0}],\n"
                 << "  \"directDrawFallback\":false\n}\n";
        writeParallaxText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        if (options.scenarioId == "canonical-assets")
        {
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgpu_parallax_uv\",\n"
                     << "  \"scenarioId\":\"canonical-assets\",\n"
                     << "  \"frame\":0,\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"six-assets-one-ordinary-circle-entity\",\n"
                     << "  \"result\":{\n"
                     << "    \"assetSha256\":[\"" << ParallaxTextureHashes[0]
                     << "\",\"" << ParallaxTextureHashes[1]
                     << "\",\"" << ParallaxTextureHashes[2]
                     << "\",\"" << ParallaxTextureHashes[3]
                     << "\",\"" << ParallaxTextureHashes[4]
                     << "\",\"" << ParallaxEnvironmentHash << "\"],\n"
                     << "    \"geometry\":{\"type\":\"CircleGeometry\","
                     << "\"segments\":64,\"vertexCount\":66,\"indexCount\":192},\n"
                     << "    \"renderableObjectCount\":0,\n"
                     << "    \"sceneRenderableObjectCount\":1,\n"
                     << "    \"sceneRootCount\":1,\n"
                     << "    \"canonicalSceneSha256\":\""
                     << ParallaxCanonicalSceneHash << "\"\n"
                     << "  }\n}\n";
        }
        else
        {
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgpu_parallax_uv\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"assetCount\":6,\n"
                     << "  \"circleSegments\":64,\n"
                     << "  \"uvScale\":" << cameraPositionAndScale.w << ",\n"
                     << "  \"parallaxScale\":" << cameraUpAndParallaxScale.w << ",\n"
                     << "  \"backgroundBlurriness\":" << cameraRightAndBackgroundBlur.w << "\n}\n";
        }
        writeParallaxText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuParallaxUvRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        normals.clear();
        textureCoordinates.clear();
        indices.clear();
        for (auto &atlas : textureAtlases)
            atlas.clear();
        environmentPixels.clear();
        dfgLutPackedPixels.clear();
    }
} // namespace GVM::ThreeSamples
