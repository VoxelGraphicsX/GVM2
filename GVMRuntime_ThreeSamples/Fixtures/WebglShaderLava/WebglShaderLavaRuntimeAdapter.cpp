#include "WebglShaderLavaRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

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
        /** Owns libjpeg's bounded non-local error state for one lava decode. */
        struct ShaderLavaJpegErrorState
        {
            jpeg_error_mgr base;
            std::jmp_buf jumpTarget;
            uint8_t *pixels = nullptr;
        };

        constexpr const char *CloudSha256 =
            "aab284a9765e95d92e52165f8d18cfbe206651c726a4843b9820b0cb952bda98";
        constexpr const char *LavaSha256 =
            "8dd33ecfacd108323888479a85cdf10a2159fe82daa247c2f46978952730e76f";
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one complete bounded lava asset for hashing or exact JPEG decode. */
        eastl::vector<uint8_t> readShaderLavaAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open one pinned lava asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error("Pinned lava asset has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read one pinned lava asset.");
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity for a pinned lava asset. */
        eastl::string calculateShaderLavaSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char Digits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(Digits[value >> 4u]);
                result.push_back(Digits[value & 15u]);
            }
            return result;
        }

        /** Converts one libjpeg fatal error into a bounded decoder jump. */
        void handleShaderLavaJpegError(j_common_ptr decoder)
        {
            auto *state = reinterpret_cast<ShaderLavaJpegErrorState *>(decoder->err);
            std::free(state->pixels);
            state->pixels = nullptr;
            std::longjmp(state->jumpTarget, 1);
        }

        /** Decodes the pinned lava JPEG through Chrome-compatible libjpeg-turbo RGBA. */
        RgbaImageData decodeShaderLavaJpeg(const eastl::vector<uint8_t> &encoded)
        {
            jpeg_decompress_struct decoder = {};
            ShaderLavaJpegErrorState errorState = {};
            decoder.err = jpeg_std_error(&errorState.base);
            errorState.base.error_exit = handleShaderLavaJpegError;
            if (setjmp(errorState.jumpTarget) != 0)
            {
                jpeg_destroy_decompress(&decoder);
                throw std::runtime_error("libjpeg-turbo could not decode lavatile.jpg.");
            }
            jpeg_create_decompress(&decoder);
            jpeg_mem_src(&decoder, encoded.data(), static_cast<unsigned long>(encoded.size()));
            if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
                throw std::runtime_error("lavatile.jpg has an invalid header.");
#if defined(JCS_EXTENSIONS)
            decoder.out_color_space = JCS_EXT_RGBA;
#else
#error "webgl_shader_lava requires libjpeg-turbo RGBA output."
#endif
            decoder.dct_method = JDCT_ISLOW;
            decoder.do_fancy_upsampling = TRUE;
            jpeg_start_decompress(&decoder);
            const size_t rowBytes = size_t(decoder.output_width) * 4u;
            const size_t byteCount = rowBytes * size_t(decoder.output_height);
            errorState.pixels = static_cast<uint8_t *>(std::malloc(byteCount));
            if (errorState.pixels == nullptr) throw std::bad_alloc();
            while (decoder.output_scanline < decoder.output_height)
            {
                JSAMPROW row = errorState.pixels + size_t(decoder.output_scanline) * rowBytes;
                jpeg_read_scanlines(&decoder, &row, 1u);
            }
            RgbaImageData result;
            result.width = decoder.output_width;
            result.height = decoder.output_height;
            jpeg_finish_decompress(&decoder);
            jpeg_destroy_decompress(&decoder);
            result.pixels.assign(errorState.pixels, errorState.pixels + byteCount);
            std::free(errorState.pixels);
            errorState.pixels = nullptr;
            return result;
        }

        /** Generates the exact indexed TorusGeometry(0.65,0.3,30,30). */
        void buildShaderLavaTorus(eastl::vector<WebglShaderLavaVertex> &vertices,
                                  eastl::vector<uint> &indices)
        {
            constexpr uint32_t Segments = 30u;
            vertices.reserve((Segments + 1u) * (Segments + 1u));
            for (uint32_t radial = 0u; radial <= Segments; ++radial)
            {
                const double v = double(radial) / Segments * Pi * 2.0;
                for (uint32_t tubular = 0u; tubular <= Segments; ++tubular)
                {
                    const double u = double(tubular) / Segments * Pi * 2.0;
                    vertices.push_back({
                        float3(float((0.65 + 0.3 * std::cos(v)) * std::cos(u)),
                               float((0.65 + 0.3 * std::cos(v)) * std::sin(u)),
                               float(0.3 * std::sin(v))),
                        float2(float(tubular) / Segments,
                               float(radial) / Segments)});
                }
            }
            indices.reserve(Segments * Segments * 6u);
            for (uint32_t radial = 1u; radial <= Segments; ++radial)
            {
                for (uint32_t tubular = 1u; tubular <= Segments; ++tubular)
                {
                    const uint32_t a = (Segments + 1u) * radial + tubular - 1u;
                    const uint32_t b = (Segments + 1u) * (radial - 1u) + tubular - 1u;
                    const uint32_t c = (Segments + 1u) * (radial - 1u) + tubular;
                    const uint32_t d = (Segments + 1u) * radial + tubular;
                    indices.push_back(a); indices.push_back(b); indices.push_back(d);
                    indices.push_back(b); indices.push_back(c); indices.push_back(d);
                }
            }
        }

        /** Creates Three's perspective matrix before DSL depth conversion. */
        glm::mat4 makeShaderLavaProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 3000.0;
            constexpr double Aspect = 800.0 / 500.0;
            const double top = NearDistance * std::tan(35.0 * Pi / 360.0);
            const double h = top * 2.0;
            const double w = h * Aspect;
            glm::mat4 result(0.0f);
            result[0][0] = float(2.0 * NearDistance / w);
            result[1][1] = float(2.0 * NearDistance / h);
            result[2][2] = float(-(FarDistance + NearDistance) /
                                  (FarDistance - NearDistance));
            result[2][3] = -1.0f;
            result[3][2] = float(-2.0 * FarDistance * NearDistance /
                                  (FarDistance - NearDistance));
            return result;
        }

        /** Creates parent directories for one optional lava artifact. */
        void prepareShaderLavaOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one deterministic UTF-8 lava evidence document. */
        void writeShaderLavaText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareShaderLavaOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write lava evidence.");
        }

        /** Converts image-data mip objects into generated texture upload byte arrays. */
        eastl::vector<eastl::vector<uint8_t>> collectShaderLavaMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images) result.push_back(image.pixels);
            return result;
        }

        /** Flips decoded rows to reproduce Three's default WebGL texture upload. */
        RgbaImageData flipShaderLavaRows(const RgbaImageData &source)
        {
            RgbaImageData result = source;
            const size_t rowBytes = static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                const size_t sourceOffset =
                    static_cast<size_t>(source.height - 1u - row) * rowBytes;
                const size_t destinationOffset = static_cast<size_t>(row) * rowBytes;
                for (size_t column = 0u; column < rowBytes; ++column)
                    result.pixels[destinationOffset + column] =
                        source.pixels[sourceOffset + column];
            }
            return result;
        }
    }

    void WebglShaderLavaRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        const bool initial =
            (options.scenarioId == "initial-assets" || options.scenarioId == "initial") &&
            options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
        if (options.caseId != "webgl_shader_lava" || (!initial && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty() ||
            !options.inputReplayPath.empty())
            throw std::invalid_argument("Lava adapter requires one locked Manifest scenario.");
        device = inDevice;
        buildShaderLavaTorus(vertices, indices);
        if (vertices.size() != 961u || indices.size() != 5400u)
            throw std::runtime_error("TorusGeometry topology diverged from r185.");
        const std::filesystem::path lavaRoot =
            std::filesystem::path(options.assetRoot.c_str()) / "textures" / "lava";
        const eastl::vector<uint8_t> cloudBytes = readShaderLavaAsset(lavaRoot / "cloud.png");
        const eastl::vector<uint8_t> lavaBytes = readShaderLavaAsset(lavaRoot / "lavatile.jpg");
        if (calculateShaderLavaSha256(cloudBytes) != CloudSha256 ||
            calculateShaderLavaSha256(lavaBytes) != LavaSha256)
            throw std::runtime_error("Lava assets differ from the r185 lock.");
        const RgbaImageData decodedCloud =
            decodeStraightPngRgba8(lavaRoot / "cloud.png");
        const RgbaImageData cloud = flipShaderLavaRows(decodedCloud);
        const RgbaImageData lava =
            flipShaderLavaRows(decodeShaderLavaJpeg(lavaBytes));
        cloudWidth = cloud.width; cloudHeight = cloud.height;
        lavaWidth = lava.width; lavaHeight = lava.height;
        cloudMips = collectShaderLavaMips(buildUnormMipChain(cloud));
        lavaMips = collectShaderLavaMips(buildSrgbMipChain(lava));
        const float time = animated ? 2.0f : 1.0f;
        const float rotationX = animated ? 0.55f : 0.3f;
        const float rotationY = animated ? 0.0625f : 0.0f;
        glm::mat4 model = glm::rotate(
            glm::mat4(1.0f), rotationX, glm::vec3(1, 0, 0));
        model = glm::rotate(model, rotationY, glm::vec3(0, 1, 0));
        const glm::mat4 view = glm::lookAtRH(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0));
        uniforms.modelViewProjection = makeShaderLavaProjection() * view * model;
        uniforms.timeFogAndStrength = float4(time, 0.45f, 1.25f, 0.0f);
    }

    void WebglShaderLavaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options, uint32_t frameIndex)
    { (void)renderer; (void)options; (void)frameIndex; }

    void WebglShaderLavaRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options, uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareShaderLavaOutput(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write lava RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgl_shader_lava\",\n  \"scenarioId\":\""
                 << options.scenarioId.c_str() << "\",\n  \"pipeline\":\""
                 << options.pipeline.c_str() << "\",\n  \"backend\":\""
                 << threeSampleBackendName(options.backend) << "\",\n  \"frame\":"
                 << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed
                 << ",\n  \"width\":" << width << ",\n  \"height\":" << height
                 << ",\n  \"rowStrideBytes\":" << uint64_t(width) * 4u
                 << ",\n  \"byteCount\":" << byteCount
                 << ",\n  \"format\":\"rgba8unorm\",\n  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false\n}\n";
        writeShaderLavaText(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_shader_lava\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str()
              << "\",\n  \"frame\":" << frameIndex
              << ",\n  \"gpuWorkDslOnly\":true,\n  \"renderSetPolicy\":\"not-required\",\n"
              << "  \"sceneRenderSetCount\":0,\n  \"renderableObjectCount\":1,\n"
              << "  \"instanceCount\":1,\n  \"vertexCount\":961,\n  \"indexCount\":5400,\n"
              << "  \"drawCommandCount\":1,\n  \"scenePassCount\":1,\n  \"screenPassCount\":4,\n"
              << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"lava-torus\",\"entityOrdinal\":0}],\n"
              << "  \"sampleCount\":1,\n  \"msaaEnabled\":false\n}\n";
        writeShaderLavaText(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            const std::string semantic =
                "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_shader_lava\",\n"
                "  \"scenarioId\":\"initial-assets\",\n  \"frame\":0,\n"
                "  \"kind\":\"loader-snapshot\",\n"
                "  \"canonicalState\":\"cloud-and-lava-sha-locked-one-torus-three-screen-passes\",\n"
                "  \"result\":{\"renderableObjectCount\":0,\"sceneRootCount\":1,"
                "\"canonicalSceneSha256\":\"9f27ad130265ae5b60232851ff5b543409add0db2e94f7fe0bfe72db7f479bfd\","
                "\"assetCount\":2,\"vertexCount\":961,\"indexCount\":5400,"
                "\"cloudSha256\":\"aab284a9765e95d92e52165f8d18cfbe206651c726a4843b9820b0cb952bda98\","
                "\"lavaSha256\":\"8dd33ecfacd108323888479a85cdf10a2159fe82daa247c2f46978952730e76f\"}\n}\n";
            writeShaderLavaText(options.semanticSnapshotPath, semantic);
        }
        captureWritten = true;
    }

    void WebglShaderLavaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer; (void)options;
        vertices.clear(); indices.clear(); cloudMips.clear(); lavaMips.clear();
    }
}
