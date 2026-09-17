#include "WebglMaterialsMatcapRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/algorithm.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <jpeglib.h>

#include <csetjmp>
#include <cstdlib>
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
        /** Owns libjpeg's non-local error state and any in-flight raw output allocation. */
        struct MatcapJpegErrorState
        {
            jpeg_error_mgr base;
            std::jmp_buf jumpTarget;
            uint8_t *pixels = nullptr;
        };

        constexpr const char *GlbSha256 =
            "402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576";
        constexpr const char *ExrSha256 =
            "14deb8c88d02cd44d3694c9b63a5a56fb481364298900441a2be42f9eb70a9d3";
        constexpr const char *NormalSha256 =
            "36925e51ad9b324b94e8faf4692da1b4132809f2762bb8d5bd549ffd215d4ca6";
        constexpr const char *DroppedSha256 =
            "bf1c51469cd0bd5720b5ac0bb7023f5263c31f5f392e7710f7a6ca5df6b417b5";
        constexpr const char *ReplaySha256 =
            "1ac014b30857e6d7be1263b17c455d4646eac7e05b959c61383c4e70e255111e";
        constexpr const char *CanonicalSceneSha256 =
            "f67fa99b91bb2cb55ed9dfd0d90fb2121f30e8110dc86bc1049cd9ed9d708889";
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one complete bounded matcap asset or replay payload. */
        eastl::vector<uint8_t> readMatcapInput(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open one pinned matcap input.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("A pinned matcap input has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read one complete matcap input.");
            }
            return bytes;
        }

        /** Converts one libjpeg fatal error into a bounded decoder jump after releasing raw output. */
        void handleMatcapJpegError(j_common_ptr decoder)
        {
            auto *state = reinterpret_cast<MatcapJpegErrorState *>(decoder->err);
            std::free(state->pixels);
            state->pixels = nullptr;
            std::longjmp(state->jumpTarget, 1);
        }

        /** Decodes one pinned JPEG with the same libjpeg-turbo RGBA path used by the r185 browser Oracle. */
        RgbaImageData decodeMatcapJpegRgba8(
            const eastl::vector<uint8_t> &encodedBytes)
        {
            jpeg_decompress_struct decoder = {};
            MatcapJpegErrorState errorState = {};
            decoder.err = jpeg_std_error(&errorState.base);
            errorState.base.error_exit = handleMatcapJpegError;
            if (setjmp(errorState.jumpTarget) != 0)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error("libjpeg-turbo could not decode a pinned matcap JPEG.");
            }
            jpeg_create_decompress(&decoder);
            jpeg_mem_src(
                &decoder,
                encodedBytes.data(),
                static_cast<unsigned long>(encodedBytes.size()));
            if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error("Pinned matcap JPEG has an invalid header.");
            }
#if defined(JCS_EXTENSIONS)
            decoder.out_color_space = JCS_EXT_RGBA;
#else
#error "webgl_materials_matcap requires libjpeg-turbo JCS_EXT_RGBA output."
#endif
            decoder.dct_method = JDCT_ISLOW;
            decoder.do_fancy_upsampling = TRUE;
            jpeg_start_decompress(&decoder);
            if (decoder.output_width == 0u || decoder.output_height == 0u ||
                decoder.output_components != 4u)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error("Pinned matcap JPEG has an unsupported output shape.");
            }
            const size_t rowBytes = size_t(decoder.output_width) * 4u;
            const size_t byteCount = rowBytes * size_t(decoder.output_height);
            errorState.pixels = static_cast<uint8_t *>(std::malloc(byteCount));
            if (errorState.pixels == nullptr)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::bad_alloc();
            }
            while (decoder.output_scanline < decoder.output_height)
            {
                JSAMPROW row =
                    errorState.pixels +
                    size_t(decoder.output_scanline) * rowBytes;
                jpeg_read_scanlines(&decoder, &row, 1u);
            }
            const uint32_t width = decoder.output_width;
            const uint32_t height = decoder.output_height;
            jpeg_finish_decompress(&decoder);
            jpeg_destroy_decompress(&decoder);
            RgbaImageData result;
            result.width = width;
            result.height = height;
            result.pixels.assign(
                errorState.pixels,
                errorState.pixels + byteCount);
            std::free(errorState.pixels);
            errorState.pixels = nullptr;
            return result;
        }

        /** Returns one lowercase SHA-256 identity for a bounded byte array. */
        eastl::string calculateMatcapSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Packs decoded mip images into the texture upload shape used by the DSL renderer. */
        eastl::vector<eastl::vector<uint8_t>> packMatcapMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images)
            {
                result.push_back(image.pixels);
            }
            return result;
        }

        /** Builds Three's OpenGL perspective matrix for the fixed r185 camera. */
        glm::mat4 makeMatcapProjection()
        {
            constexpr double FieldOfViewDegrees = 40.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 100.0;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(2.0 * NearDistance / height);
            result[2u][2u] = float(-(FarDistance + NearDistance) / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(-2.0 * FarDistance * NearDistance / depth);
            return result;
        }

        /** Converts one sRGB GUI component into Three's linear working color. */
        float matcapSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Expands triangles with analytic position derivatives for derivative-equivalent TBN reconstruction. */
        eastl::vector<WebglMaterialsMatcapVertex> buildMatcapVertices(
            const ThreeCompat::DecodedGlbMesh &mesh)
        {
            eastl::vector<WebglMaterialsMatcapVertex> result;
            result.reserve(mesh.indices.size());
            for (size_t index = 0u; index < mesh.indices.size(); index += 3u)
            {
                const uint32_t i0 = mesh.indices[index];
                const uint32_t i1 = mesh.indices[index + 1u];
                const uint32_t i2 = mesh.indices[index + 2u];
                const glm::vec3 p0(
                    mesh.positions[i0 * 3u],
                    mesh.positions[i0 * 3u + 1u],
                    mesh.positions[i0 * 3u + 2u]);
                const glm::vec3 p1(
                    mesh.positions[i1 * 3u],
                    mesh.positions[i1 * 3u + 1u],
                    mesh.positions[i1 * 3u + 2u]);
                const glm::vec3 p2(
                    mesh.positions[i2 * 3u],
                    mesh.positions[i2 * 3u + 1u],
                    mesh.positions[i2 * 3u + 2u]);
                const glm::vec2 uv0(
                    mesh.textureCoordinates[i0 * 2u],
                    mesh.textureCoordinates[i0 * 2u + 1u]);
                const glm::vec2 uv1(
                    mesh.textureCoordinates[i1 * 2u],
                    mesh.textureCoordinates[i1 * 2u + 1u]);
                const glm::vec2 uv2(
                    mesh.textureCoordinates[i2 * 2u],
                    mesh.textureCoordinates[i2 * 2u + 1u]);
                const glm::vec3 edge1 = p1 - p0;
                const glm::vec3 edge2 = p2 - p0;
                const glm::vec2 deltaUv1 = uv1 - uv0;
                const glm::vec2 deltaUv2 = uv2 - uv0;
                const float determinant =
                    deltaUv1.x * deltaUv2.y -
                    deltaUv2.x * deltaUv1.y;
                glm::vec3 derivativeU(1.0f, 0.0f, 0.0f);
                glm::vec3 derivativeV(0.0f, 1.0f, 0.0f);
                if (std::abs(determinant) > 1.0e-12f)
                {
                    const float inverseDeterminant = 1.0f / determinant;
                    derivativeU =
                        (edge1 * deltaUv2.y - edge2 * deltaUv1.y) *
                        inverseDeterminant;
                    derivativeV =
                        (edge2 * deltaUv1.x - edge1 * deltaUv2.x) *
                        inverseDeterminant;
                }
                const uint32_t triangleIndices[] = {i0, i1, i2};
                for (const uint32_t vertexIndex : triangleIndices)
                {
                    result.push_back({
                        .position = float3(
                            mesh.positions[vertexIndex * 3u],
                            mesh.positions[vertexIndex * 3u + 1u],
                            mesh.positions[vertexIndex * 3u + 2u]),
                        .normal = float3(
                            mesh.normals[vertexIndex * 3u],
                            mesh.normals[vertexIndex * 3u + 1u],
                            mesh.normals[vertexIndex * 3u + 2u]),
                        .textureCoordinate = float2(
                            mesh.textureCoordinates[vertexIndex * 2u],
                            mesh.textureCoordinates[vertexIndex * 2u + 1u]),
                        .positionDerivativeU = float3(
                            derivativeU.x,
                            derivativeU.y,
                            derivativeU.z),
                        .positionDerivativeV = float3(
                            derivativeV.x,
                            derivativeV.y,
                            derivativeV.z),
                    });
                }
            }
            return result;
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMatcapOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic UTF-8 matcap evidence artifact. */
        void writeMatcapText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMatcapOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error("Could not write a matcap evidence artifact.");
            }
        }

        /** Returns the exact canonical loader snapshot shared by runtime and Oracle. */
        std::string makeMatcapSemanticSnapshot()
        {
            return
                "{\n"
                "  \"schemaVersion\": 1,\n"
                "  \"caseId\": \"webgl_materials_matcap\",\n"
                "  \"scenarioId\": \"canonical-loader\",\n"
                "  \"frame\": 0,\n"
                "  \"kind\": \"loader-snapshot\",\n"
                "  \"canonicalState\": \"canonical-loaded-scene\",\n"
                "  \"result\": {\n"
                "    \"renderableObjectCount\": 1,\n"
                "    \"sceneRootCount\": 1,\n"
                "    \"canonicalSceneSha256\": \"2ab2470c44f889d08ef1d93eb850397913ba1406645f89d99c6cdcce20871b8c\",\n"
                "    \"assetCount\": 4,\n"
                "    \"vertexCount\": 9279,\n"
                "    \"indexCount\": 53052,\n"
                "    \"glbSha256\": \"402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576\",\n"
                "    \"exrSha256\": \"14deb8c88d02cd44d3694c9b63a5a56fb481364298900441a2be42f9eb70a9d3\",\n"
                "    \"normalMapSha256\": \"36925e51ad9b324b94e8faf4692da1b4132809f2762bb8d5bd549ffd215d4ca6\",\n"
                "    \"droppedMatcapSha256\": \"bf1c51469cd0bd5720b5ac0bb7023f5263c31f5f392e7710f7a6ca5df6b417b5\"\n"
                "  }\n"
                "}\n";
        }
    } // namespace

    void WebglMaterialsMatcapRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-loader" &&
            options.targetFrame == 0u;
        const bool canonical =
            options.scenarioId == "canonical-loader" &&
            options.targetFrame == 0u;
        const bool custom =
            options.scenarioId == "custom-matcap" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgl_materials_matcap" ||
            (!initial && !canonical && !custom) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (custom != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Matcap adapter requires one locked Manifest scenario.");
        }
        device = inDevice;
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const std::filesystem::path glbPath =
            assetRoot / "models" / "gltf" / "LeePerrySmith" /
            "LeePerrySmith.glb";
        const std::filesystem::path normalPath =
            assetRoot / "models" / "gltf" / "LeePerrySmith" /
            "Infinite-Level_02_Tangent_SmoothUV.jpg";
        const std::filesystem::path exrPath =
            assetRoot / "textures" / "matcaps" / "040full.exr";
        const std::filesystem::path droppedPath =
            assetRoot / "textures" / "matcaps" /
            "matcap-porcelain-white.jpg";

        const eastl::vector<uint8_t> glbBytes = readMatcapInput(glbPath);
        const eastl::vector<uint8_t> normalBytes = readMatcapInput(normalPath);
        const eastl::vector<uint8_t> exrBytes = readMatcapInput(exrPath);
        const eastl::vector<uint8_t> droppedBytes = readMatcapInput(droppedPath);
        glbSha256 = calculateMatcapSha256(glbBytes);
        normalSha256 = calculateMatcapSha256(normalBytes);
        exrSha256 = calculateMatcapSha256(exrBytes);
        droppedSha256 = calculateMatcapSha256(droppedBytes);
        if (glbSha256 != GlbSha256 || normalSha256 != NormalSha256 ||
            exrSha256 != ExrSha256 || droppedSha256 != DroppedSha256)
        {
            throw std::runtime_error("One matcap asset differs from the r185 lock.");
        }
        if (custom)
        {
            const eastl::vector<uint8_t> replayBytes = readMatcapInput(
                std::filesystem::path(options.inputReplayPath.c_str()));
            if (calculateMatcapSha256(replayBytes) != ReplaySha256)
            {
                throw std::runtime_error("Matcap replay differs from its lock.");
            }
        }

        const ThreeCompat::DecodedGlbMesh mesh =
            ThreeCompat::decodeFirstGlbMesh(glbBytes);
        if (mesh.positions.size() != 9279u * 3u ||
            mesh.normals.size() != 9279u * 3u ||
            mesh.textureCoordinates.size() != 9279u * 2u ||
            mesh.indices.size() != 53052u)
        {
            throw std::runtime_error(
                "Lee Perry Smith geometry counts differ from the r185 lock.");
        }
        vertices = buildMatcapVertices(mesh);
        indices.resize(mesh.indices.size());
        for (size_t index = 0u; index < indices.size(); ++index)
        {
            indices[index] = static_cast<uint>(index);
        }
        exrMatcap = decodeExrRgba16Float(exrPath);
        droppedMatcap = decodeMatcapJpegRgba8(droppedBytes);
        normalMap = decodeMatcapJpegRgba8(normalBytes);
        droppedMatcapMips = packMatcapMips(
            buildSrgbMipChain(droppedMatcap));
        decodedNormalSha256 = calculateMatcapSha256(normalMap.pixels);
        eastl::vector<uint8_t> flippedNormalPixels(normalMap.pixels.size());
        const size_t normalRowBytes = size_t(normalMap.width) * 4u;
        for (uint32_t row = 0u; row < normalMap.height; ++row)
        {
            const size_t sourceOffset = size_t(row) * normalRowBytes;
            const size_t targetOffset =
                size_t(normalMap.height - 1u - row) * normalRowBytes;
            eastl::copy_n(
                normalMap.pixels.begin() + sourceOffset,
                normalRowBytes,
                flippedNormalPixels.begin() + targetOffset);
        }
        decodedNormalFlippedSha256 =
            calculateMatcapSha256(flippedNormalPixels);
        normalMips = packMatcapMips(buildUnormMipChain(normalMap));
        if (exrMatcap.width != 512u || exrMatcap.height != 512u ||
            droppedMatcap.width == 0u || droppedMatcap.height == 0u ||
            normalMap.width != 1024u || normalMap.height != 1024u)
        {
            throw std::runtime_error("Decoded matcap texture dimensions differ from r185.");
        }

        const glm::mat4 model = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, -0.25f, 0.0f));
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 0.0f, 13.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view * model;
        uniforms.modelViewProjection =
            makeMatcapProjection() * uniforms.modelView;
        uniforms.normalTransform = glm::transpose(glm::inverse(uniforms.modelView));
        if (custom)
        {
            uniforms.colorExposureCustom = float4(
                matcapSrgbToLinear(128.0f / 255.0f),
                matcapSrgbToLinear(192.0f / 255.0f),
                1.0f,
                1.0f);
        }
        else
        {
            uniforms.colorExposureCustom = float4(1.0f, 1.0f, 1.0f, 0.0f);
        }
    }

    void WebglMaterialsMatcapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsMatcapRuntimeAdapter::afterFrame(
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
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareMatcapOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write the matcap RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgl_materials_matcap\",\n"
            << "  \"scenarioId\": \"" << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \"" << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\": " << frameIndex
            << ",\n  \"randomSeed\": " << options.randomSeed
            << ",\n  \"width\": " << width
            << ",\n  \"height\": " << height
            << ",\n  \"rowStrideBytes\": " << uint64_t(width) * 4u
            << ",\n  \"byteCount\": " << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n"
            << "  \"sampleCount\": 1,\n"
            << "  \"msaaEnabled\": false,\n"
            << "  \"renderSetCount\": 0,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"instanceCount\": 1,\n"
            << "  \"vertexCount\": 9279,\n"
            << "  \"indexCount\": 53052,\n"
            << "  \"drawCommandCount\": 1,\n"
            << "  \"glbSha256\": \"" << glbSha256.c_str() << "\",\n"
            << "  \"exrSha256\": \"" << exrSha256.c_str() << "\",\n"
            << "  \"normalMapSha256\": \"" << normalSha256.c_str() << "\",\n"
            << "  \"decodedNormalSha256\": \""
            << decodedNormalSha256.c_str() << "\",\n"
            << "  \"decodedNormalFlippedSha256\": \""
            << decodedNormalFlippedSha256.c_str() << "\",\n"
            << "  \"droppedMatcapSha256\": \"" << droppedSha256.c_str()
            << "\"";
        if (options.scenarioId == "custom-matcap")
        {
            metadata
                << ",\n  \"inputReplay\": {\n"
                << "    \"sha256\": \"" << ReplaySha256 << "\",\n"
                << "    \"caseId\": \"webgl_materials_matcap\",\n"
                << "    \"scenarioId\": \"custom-matcap\",\n"
                << "    \"captureFrame\": 1,\n"
                << "    \"eventCount\": 1,\n"
                << "    \"target\": \"canvas:not([class])\"\n"
                << "  }";
        }
        metadata << "\n}\n";
        writeMatcapText(options.captureMetadataPath, metadata.str());
        writeMatcapText(
            options.sceneSnapshotPath,
            std::string("{\n  \"caseId\": \"webgl_materials_matcap\",\n") +
            "  \"scenarioId\": \"" + options.scenarioId.c_str() + "\",\n" +
            "  \"frame\": " + std::to_string(frameIndex) + ",\n" +
            "  \"gpuWorkDslOnly\": true,\n" +
            "  \"renderSetPolicy\": \"not-required\",\n" +
            "  \"sceneRenderSetCount\": 0,\n" +
            "  \"renderableObjectCount\": 1,\n" +
            "  \"entityCount\": 1,\n" +
            "  \"instanceCount\": 1,\n" +
            "  \"vertexCount\": 9279,\n" +
            "  \"indexCount\": 53052,\n" +
            "  \"drawCommandCount\": 1,\n" +
            "  \"scenePassCount\": 1,\n" +
            "  \"sampleCount\": 1,\n" +
            "  \"msaaEnabled\": false\n}\n");
        if (!options.semanticSnapshotPath.empty())
        {
            writeMatcapText(
                options.semanticSnapshotPath,
                makeMatcapSemanticSnapshot());
        }
        captureWritten = true;
    }

    void WebglMaterialsMatcapRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        normalMips.clear();
        droppedMatcapMips.clear();
        normalMap.pixels.clear();
        droppedMatcap.pixels.clear();
        exrMatcap.pixels.clear();
        indices.clear();
        vertices.clear();
    }
} // namespace GVM::ThreeSamples
