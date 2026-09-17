#include "WebglGpgpuBirdsRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t BirdsTextureWidth = 32u;
        constexpr uint32_t BirdsCount = BirdsTextureWidth * BirdsTextureWidth;
        constexpr uint32_t BirdsVertexCount = BirdsCount * 9u;
        constexpr uint32_t BirdsRandomSeed = 0x12345678u;
        constexpr uint32_t BirdsPreTextureRandomDrawCount = 152u;
        constexpr uint32_t BirdsTextureRandomDrawCount = BirdsCount * 3u;
        constexpr uint32_t BirdsPostTextureRandomDrawCount = 84u;
        constexpr uint32_t BirdsTotalReferenceRandomDrawCount =
            BirdsPreTextureRandomDrawCount +
            BirdsTextureRandomDrawCount * 2u +
            BirdsPostTextureRandomDrawCount;
        constexpr uint32_t BirdsBeforePositionRandomState = 2785242051u;
        constexpr uint32_t BirdsAfterPositionRandomState = 2963850739u;
        constexpr uint32_t BirdsAfterVelocityRandomState = 1379151263u;
        constexpr uint32_t BirdsFinalReferenceRandomState = 28636613u;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr char InitialPositionSha256[] =
            "9b6fe996872a6bd9baf1984f6923cf1d008ed5965909efbe5e0923ee6dd0367d";
        constexpr char InitialVelocitySha256[] =
            "9ca9d0dba7e162b01c9f9508f1bd43e97a355616b718f050e84efa85115d435b";
        constexpr char GeometryPositionSha256[] =
            "23ec20c8347d55f2215ed71d44ad491275c09bce51526c31c7f6b59a0db21f40";
        constexpr char GeometryColorSha256[] =
            "4e6260c1abc98494a78f7523891f450a0150348ab4c0505e14a0da5c4f5a84b9";
        constexpr char GeometryReferenceSha256[] =
            "8f13ebf93175ec793c9694e01cfc41e7d381c0e25fcbd2db3fa60a329398a43c";
        constexpr char GeometryBirdVertexSha256[] =
            "14c47fdffdb796febb8aefe7eede54f05ddeb34ad9c294539581ccc8d874c8f4";
        constexpr char PointerReplaySha256[] =
            "ddf07fda08630675f161e4a2137d9c60b1af0f221441d11debc799da94822c52";
        constexpr char PointerStateScriptSha256[] =
            "12f0b5a1e175fbd78fc3f74ff14370b7006a2c60b6d87bd43a52e4d68ff059d6";
        constexpr double BirdLocalPositions[] = {
            0.0, 0.0, -20.0,
            0.0, 4.0, -20.0,
            0.0, 0.0, 30.0,
            0.0, 0.0, -15.0,
            -20.0, 0.0, 0.0,
            0.0, 0.0, 15.0,
            0.0, 0.0, 15.0,
            20.0, 0.0, 0.0,
            0.0, 0.0, -15.0,
        };

        static_assert(sizeof(WebglGpgpuBirdsVertex) == 36u);
        static_assert(offsetof(WebglGpgpuBirdsVertex, position) == 0u);
        static_assert(offsetof(WebglGpgpuBirdsVertex, birdColor) == 12u);
        static_assert(offsetof(WebglGpgpuBirdsVertex, reference) == 24u);
        static_assert(offsetof(WebglGpgpuBirdsVertex, birdVertex) == 32u);
        static_assert(sizeof(WebglGpgpuBirdsSimulationUniforms) == 48u);
        static_assert(BirdsTotalReferenceRandomDrawCount == 6380u);

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareBirdsOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeBirdsRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_gpgpu_birds RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculateBirdsSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_gpgpu_birds SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
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

        /** Reads one required immutable replay artifact into exact bytes. */
        eastl::vector<uint8_t> readBirdsInputBytes(
            const eastl::string &inputPath)
        {
            if (inputPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_gpgpu_birds requires an explicit replay artifact path.");
            }
            std::ifstream input(
                std::filesystem::path(inputPath.c_str()),
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a webgl_gpgpu_birds replay artifact.");
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds replay artifact has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> result(static_cast<size_t>(end));
            input.read(
                reinterpret_cast<char *>(result.data()),
                static_cast<std::streamsize>(result.size()));
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read a complete webgl_gpgpu_birds replay artifact.");
            }
            return result;
        }

        /** Verifies one exact replay artifact against its clean-reference digest. */
        void validateBirdsInputArtifact(
            const eastl::string &inputPath,
            const char *expectedSha256)
        {
            const eastl::vector<uint8_t> bytes =
                readBirdsInputBytes(inputPath);
            if (calculateBirdsSha256(bytes.data(), bytes.size()) !=
                expectedSha256)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds replay artifact diverged from its clean-reference lock.");
            }
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript unit double. */
        double nextBirdsRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one r185 hexadecimal sRGB channel to its linear working value. */
        float convertBirdsSrgbChannelToLinear(uint32_t channel)
        {
            const double srgb = double(channel) / 255.0;
            const double linear = srgb < 0.04045
                                      ? srgb * 0.0773993808
                                      : std::pow(
                                            srgb * 0.9478672986 +
                                                0.0521327014,
                                            2.4);
            return static_cast<float>(linear);
        }

        /** Builds exact DataTexture Float32 payloads from the shared reference stream. */
        void buildBirdsInitialTextures(
            eastl::vector<float4> &position,
            eastl::vector<float4> &velocity)
        {
            ThreeCompat::DeterministicRandom random(BirdsRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < BirdsPreTextureRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            if (random.getState() != BirdsBeforePositionRandomState)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds pre-texture RNG state diverged from r185.");
            }

            position.clear();
            position.reserve(BirdsCount);
            for (uint32_t birdIndex = 0u;
                 birdIndex < BirdsCount;
                 ++birdIndex)
            {
                const double x = nextBirdsRandomUnit(random) * 800.0 - 400.0;
                const double y = nextBirdsRandomUnit(random) * 800.0 - 400.0;
                const double z = nextBirdsRandomUnit(random) * 800.0 - 400.0;
                position.push_back(float4(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    1.0f));
            }
            if (random.getState() != BirdsAfterPositionRandomState)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds position RNG state diverged from r185.");
            }

            velocity.clear();
            velocity.reserve(BirdsCount);
            for (uint32_t birdIndex = 0u;
                 birdIndex < BirdsCount;
                 ++birdIndex)
            {
                const double x = (nextBirdsRandomUnit(random) - 0.5) * 10.0;
                const double y = (nextBirdsRandomUnit(random) - 0.5) * 10.0;
                const double z = (nextBirdsRandomUnit(random) - 0.5) * 10.0;
                velocity.push_back(float4(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    1.0f));
            }
            if (random.getState() != BirdsAfterVelocityRandomState)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds velocity RNG state diverged from r185.");
            }

            if (calculateBirdsSha256(
                    position.data(),
                    position.size() * sizeof(float4)) !=
                    InitialPositionSha256 ||
                calculateBirdsSha256(
                    velocity.data(),
                    velocity.size() * sizeof(float4)) !=
                    InitialVelocitySha256)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds initial Float32 textures diverged from the clean r185 probe.");
            }

            for (uint32_t drawIndex = 0u;
                 drawIndex < BirdsPostTextureRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            if (random.getState() != BirdsFinalReferenceRandomState)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds final reference RNG state diverged from r185.");
            }
        }

        /** Builds all physically repeated BirdGeometry attributes and their interleaving. */
        void buildBirdsGeometry(
            eastl::vector<WebglGpgpuBirdsVertex> &vertices,
            eastl::string &positionSha256,
            eastl::string &colorSha256,
            eastl::string &referenceSha256,
            eastl::string &ordinalSha256)
        {
            eastl::vector<float> positions;
            eastl::vector<float> colors;
            eastl::vector<float> references;
            eastl::vector<float> birdVertices;
            positions.reserve(BirdsVertexCount * 3u);
            colors.reserve(BirdsVertexCount * 3u);
            references.reserve(BirdsVertexCount * 2u);
            birdVertices.reserve(BirdsVertexCount);
            vertices.clear();
            vertices.reserve(BirdsVertexCount);

            for (uint32_t vertexIndex = 0u;
                 vertexIndex < BirdsVertexCount;
                 ++vertexIndex)
            {
                const uint32_t localVertex = vertexIndex % 9u;
                const uint32_t birdIndex = vertexIndex / 9u;
                const size_t positionOffset =
                    static_cast<size_t>(localVertex) * 3u;
                const float positionX = static_cast<float>(
                    BirdLocalPositions[positionOffset] * 0.2);
                const float positionY = static_cast<float>(
                    BirdLocalPositions[positionOffset + 1u] * 0.2);
                const float positionZ = static_cast<float>(
                    BirdLocalPositions[positionOffset + 2u] * 0.2);
                positions.push_back(positionX);
                positions.push_back(positionY);
                positions.push_back(positionZ);

                const double numericColor =
                    double(0x666666u) +
                    double(birdIndex) / double(BirdsCount) *
                        double(0x666666u);
                const uint32_t hexadecimalColor =
                    static_cast<uint32_t>(std::floor(numericColor));
                const float red = convertBirdsSrgbChannelToLinear(
                    hexadecimalColor >> 16u & 0xffu);
                const float green = convertBirdsSrgbChannelToLinear(
                    hexadecimalColor >> 8u & 0xffu);
                const float blue = convertBirdsSrgbChannelToLinear(
                    hexadecimalColor & 0xffu);
                colors.push_back(red);
                colors.push_back(green);
                colors.push_back(blue);

                const float referenceX = static_cast<float>(
                    double(birdIndex % BirdsTextureWidth) /
                    double(BirdsTextureWidth));
                const float referenceY = static_cast<float>(
                    double(birdIndex / BirdsTextureWidth) /
                    double(BirdsTextureWidth));
                references.push_back(referenceX);
                references.push_back(referenceY);
                birdVertices.push_back(static_cast<float>(localVertex));

                WebglGpgpuBirdsVertex vertex;
                vertex.position = float3(positionX, positionY, positionZ);
                vertex.birdColor = float3(red, green, blue);
                vertex.reference = float2(referenceX, referenceY);
                vertex.birdVertex = static_cast<float>(localVertex);
                vertices.push_back(vertex);
            }

            positionSha256 = calculateBirdsSha256(
                positions.data(),
                positions.size() * sizeof(float));
            colorSha256 = calculateBirdsSha256(
                colors.data(),
                colors.size() * sizeof(float));
            referenceSha256 = calculateBirdsSha256(
                references.data(),
                references.size() * sizeof(float));
            ordinalSha256 = calculateBirdsSha256(
                birdVertices.data(),
                birdVertices.size() * sizeof(float));
            if (positionSha256 != GeometryPositionSha256 ||
                colorSha256 != GeometryColorSha256 ||
                referenceSha256 != GeometryReferenceSha256 ||
                ordinalSha256 != GeometryBirdVertexSha256)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_birds geometry attributes diverged from the clean r185 probe.");
            }
        }

        /** Builds the separate Float32 projection and view matrices uploaded by Three r185. */
        void buildBirdsCameraMatrices(
            glm::mat4 &projectionMatrix,
            glm::mat4 &viewMatrix)
        {
            const double nearDistance = 1.0;
            const double farDistance = 3000.0;
            const double top = nearDistance *
                               std::tan(75.0 * Pi / 180.0 * 0.5);
            const double height = 2.0 * top;
            const double width = 1.6 * height;
            projectionMatrix = glm::mat4(0.0f);
            projectionMatrix[0u][0u] = static_cast<float>(
                2.0 * nearDistance / width);
            projectionMatrix[1u][1u] = static_cast<float>(
                2.0 * nearDistance / height);
            projectionMatrix[2u][2u] = static_cast<float>(
                -(farDistance + nearDistance) /
                (farDistance - nearDistance));
            projectionMatrix[2u][3u] = -1.0f;
            projectionMatrix[3u][2u] = static_cast<float>(
                -(2.0 * farDistance * nearDistance) /
                (farDistance - nearDistance));
            viewMatrix = glm::mat4(1.0f);
            viewMatrix[3u][2u] = -350.0f;
        }

        /** Reproduces the reference bootstrap's repeated fixed-step clock. */
        double makeBirdsVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double result = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                result += FrameStepMilliseconds;
            }
            return result;
        }

        /** Returns whether one identifier selects the clean initial callback. */
        bool isBirdsInitialScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "initial-seeded-flock";
        }

        /** Returns whether one identifier selects the clean fixed-frame callback. */
        bool isBirdsFixedScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "fixed-flock-step";
        }

        /** Returns whether one identifier selects the locked GUI and pointer replay. */
        bool isBirdsInteractiveScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "pointer-and-gui";
        }
    } // namespace

    void WebglGpgpuBirdsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_gpgpu_birds")
        {
            throw std::invalid_argument(
                "WebglGpgpuBirds requires case-id webgl_gpgpu_birds.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirds comparison requires an 800x500 output.");
        }
        if (options.randomSeed != BirdsRandomSeed)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirds requires random seed 0x12345678.");
        }

        const bool initialScenario = isBirdsInitialScenario(options);
        const bool fixedScenario = isBirdsFixedScenario(options);
        interactiveScenario = isBirdsInteractiveScenario(options);
        if (!initialScenario && !fixedScenario && !interactiveScenario)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirds received an unknown scenario-id.");
        }
        const uint32_t expectedFrame = initialScenario ? 0u : 1u;
        if (options.targetFrame != expectedFrame ||
            options.frameCount != expectedFrame + 1u)
        {
            throw std::invalid_argument(
                "WebglGpgpuBirds scenario frame count diverged from its manifest lock.");
        }

        if (interactiveScenario)
        {
            validateBirdsInputArtifact(
                options.inputReplayPath,
                PointerReplaySha256);
            validateBirdsInputArtifact(
                options.canonicalStatePath,
                PointerStateScriptSha256);
            configuredSeparation = 28u;
            configuredAlignment = 17u;
            configuredCohesion = 31u;
        }
        else if (!options.inputReplayPath.empty() ||
                 !options.canonicalStatePath.empty())
        {
            throw std::invalid_argument(
                "Default WebglGpgpuBirds scenarios must not receive replay artifacts.");
        }

        device = inDevice;
        buildBirdsInitialTextures(initialPosition, initialVelocity);
        buildBirdsGeometry(
            vertices,
            vertexPositionSha256,
            vertexColorSha256,
            vertexReferenceSha256,
            vertexOrdinalSha256);
        buildBirdsCameraMatrices(projectionMatrix, viewMatrix);
    }

    void WebglGpgpuBirdsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        if (!frameUploader)
        {
            throw std::logic_error(
                "webgl_gpgpu_birds generated frame uploader is unavailable.");
        }
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error(
                "webgl_gpgpu_birds callbacks must advance sequentially.");
        }

        WebglGpgpuBirdsSimulationUniforms uniforms;
        const float delta = frameIndex == 0u
                                ? 0.0f
                                : static_cast<float>(
                                      FrameStepMilliseconds / 1000.0);
        uniforms.deltaAndTime = float4(
            delta,
            static_cast<float>(makeBirdsVirtualTimeMilliseconds(frameIndex)),
            0.0f,
            0.0f);
        uniforms.distancesAndFreedom = float4(
            static_cast<float>(configuredSeparation),
            static_cast<float>(configuredAlignment),
            static_cast<float>(configuredCohesion),
            0.75f);
        if (frameIndex == 0u)
        {
            uniforms.predator = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        else if (interactiveScenario && frameIndex == 1u)
        {
            uniforms.predator = float4(-0.25f, 0.0f, 0.0f, 0.0f);
        }
        else
        {
            uniforms.predator = float4(12.5f, -20.0f, 0.0f, 0.0f);
        }

        frameUploader(
            uniforms,
            projectionMatrix,
            viewMatrix,
            frameIndex);
        ++frameUpdateCount;
    }

    void WebglGpgpuBirdsRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeBirdsRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_gpgpu_birds capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_gpgpu_birds could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!positionTextureProvider || !velocityTextureProvider)
        {
            throw std::logic_error(
                "webgl_gpgpu_birds simulation texture providers are unavailable.");
        }
        eastl::vector<float4> currentPosition(BirdsCount);
        eastl::vector<float4> currentVelocity(BirdsCount);
        graphicsQueue
            ->readTexture(
                positionTextureProvider(),
                currentPosition.data(),
                currentPosition.size() * sizeof(float4))
            ->readTexture(
                velocityTextureProvider(),
                currentVelocity.data(),
                currentVelocity.size() * sizeof(float4))
            ->submit();
        currentPositionSha256 = calculateBirdsSha256(
            currentPosition.data(),
            currentPosition.size() * sizeof(float4));
        currentVelocitySha256 = calculateBirdsSha256(
            currentVelocity.data(),
            currentVelocity.size() * sizeof(float4));
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGpgpuBirdsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
        positionTextureProvider = nullptr;
        velocityTextureProvider = nullptr;
    }

    void WebglGpgpuBirdsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        prepareBirdsOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_birds RGBA output.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_gpgpu_birds RGBA output.");
        }
    }

    void WebglGpgpuBirdsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        prepareBirdsOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_birds metadata output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_gpgpu_birds\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str()
               << "\",\n"
               << "  \"backend\": \""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << BirdsFinalReferenceRandomState
               << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u
               << ",\n"
               << "  \"simulationRenderPassCount\": "
               << uint64_t(frameIndex + 1u) * 2u << ",\n"
               << "  \"currentPingPongIndex\": "
               << ((frameIndex + 1u) & 1u) << ",\n"
               << "  \"separation\": " << configuredSeparation << ",\n"
               << "  \"alignment\": " << configuredAlignment << ",\n"
               << "  \"cohesion\": " << configuredCohesion << ",\n"
               << "  \"freedom\": 0.75,\n"
               << "  \"pointerReplayFrame\": "
               << (interactiveScenario ? "1" : "null") << ",\n"
               << "  \"pointerPredator\": "
               << (interactiveScenario ? "[-0.25,0.0,0.0]" : "null")
               << ",\n"
               << "  \"inputReplay\": ";
        if (!interactiveScenario)
        {
            output << "null,\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << PointerReplaySha256
                   << "\",\n"
                   << "    \"caseId\": \"webgl_gpgpu_birds\",\n"
                   << "    \"scenarioId\": \"pointer-and-gui\",\n"
                   << "    \"captureFrame\": 1,\n"
                   << "    \"eventCount\": 1,\n"
                   << "    \"lastEventFrame\": 1,\n"
                   << "    \"target\": \"body > div:nth-of-type(2) > canvas\"\n"
                   << "  },\n";
        }
        output << "  \"currentPositionSha256\": \""
               << currentPositionSha256.c_str() << "\",\n"
               << "  \"currentVelocitySha256\": \""
               << currentVelocitySha256.c_str() << "\",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u
               << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglGpgpuBirdsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareBirdsOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_birds snapshot output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_gpgpu_birds\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"upstreamCommit\": \"2431a09f46f34c560bc8e44b33be0e567723d5b9\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"vertexCount\": 9216,\n"
               << "  \"triangleCount\": 3072,\n"
               << "  \"indexed\": false,\n"
               << "  \"positionTextureFormat\": \"rgba32float\",\n"
               << "  \"velocityTextureFormat\": \"rgba32float\",\n"
               << "  \"pingPongTextureCount\": 4,\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u
               << ",\n"
               << "  \"simulationRenderPassesPerStep\": 2,\n"
               << "  \"fixedNeighborIterationCountPerBird\": 1024,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                  "\"scenePass\":\"main-double-sided-flock\"}],\n"
               << "  \"initialPositionSha256\": \""
               << InitialPositionSha256 << "\",\n"
               << "  \"initialVelocitySha256\": \""
               << InitialVelocitySha256 << "\",\n"
               << "  \"geometryPositionSha256\": \""
               << vertexPositionSha256.c_str() << "\",\n"
               << "  \"geometryColorSha256\": \""
               << vertexColorSha256.c_str() << "\",\n"
               << "  \"geometryReferenceSha256\": \""
               << vertexReferenceSha256.c_str() << "\",\n"
               << "  \"geometryBirdVertexSha256\": \""
               << vertexOrdinalSha256.c_str() << "\",\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
