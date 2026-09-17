#include "WebglGpgpuProtoplanetRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ProtoplanetTextureWidth = 64u;
        constexpr uint32_t ProtoplanetParticleCount = ProtoplanetTextureWidth * ProtoplanetTextureWidth;
        constexpr uint32_t ProtoplanetRandomSeed = 0x12345678u;
        constexpr uint32_t PreInitialTextureRandomDrawCount = 152u;
        constexpr uint32_t PostInitialTextureRandomDrawCount = 8276u;
        constexpr uint32_t PreRestartTextureRandomDrawCount = 16u;
        constexpr uint32_t DefaultFinalRandomState = 492283682u;
        constexpr uint32_t RestartFinalRandomState = 1864344099u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr char DefaultCanonicalState[] = "seed-0x12345678-width-64-frame0-one-fixed-fragment-pair-pingpong-index-1-rng-state-492283682";
        constexpr char FixedCanonicalState[] = "seed-0x12345678-frame60-sixty-one-fixed-fragment-pairs-pingpong-index-1-rng-state-492283682";
        constexpr char RestartStateScriptSha256[] = "dde37881ef7ebe788ba211d8b084d9347ef7998276fdffd2179b40e9f3c67a45";
        constexpr char DefaultPositionSha256[] = "0e417ba691875dbb636dafb9a3688fd09b73aabb3db940b444e61b70491980bf";
        constexpr char DefaultVelocitySha256[] = "d8881655408692ae94227d0097106e8284e379eb847b4e907c42b3921817bc04";
        constexpr char RestartPositionSha256[] = "39cb00f46ee45fedcf5401e8257088e05e8e891ec7119d4bf33467e7a9b106fc";
        constexpr char RestartVelocitySha256[] = "53a9f10f4906ba95f8c03364e906453a1bc31c66db1f12b12279f979b2f546fb";

        /** Defines the exact GUI values consumed by one fillTextures invocation. */
        struct ProtoplanetInitializationParameters final
        {
            double radius = 300.0;
            double height = 8.0;
            double exponent = 0.4;
            double maximumMass = 15.0;
            double velocity = 70.0;
            double velocityExponent = 0.2;
            double randomVelocity = 0.001;
        };

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareProtoplanetOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting overflow. */
        uint64_t computeProtoplanetRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("webgl_gpgpu_protoplanet RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculateProtoplanetSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error("webgl_gpgpu_protoplanet SHA-256 input is too large.");
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

        /** Reads one immutable canonical-state script into exact bytes. */
        eastl::vector<uint8_t> readProtoplanetInputBytes(const eastl::string &inputPath)
        {
            if (inputPath.empty())
            {
                throw std::invalid_argument("webgl_gpgpu_protoplanet requires a canonical-state script.");
            }
            std::ifstream input(std::filesystem::path(inputPath.c_str()), std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open the webgl_gpgpu_protoplanet state script.");
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error("The webgl_gpgpu_protoplanet state script has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> result(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(result.data()), static_cast<std::streamsize>(result.size()));
            if (!input)
            {
                throw std::runtime_error("Could not read the complete webgl_gpgpu_protoplanet state script.");
            }
            return result;
        }

        /** Verifies the canonical restart script against its clean-reference digest. */
        void validateProtoplanetStateScript(const eastl::string &inputPath)
        {
            const eastl::vector<uint8_t> bytes = readProtoplanetInputBytes(inputPath);
            if (calculateProtoplanetSha256(bytes.data(), bytes.size()) != RestartStateScriptSha256)
            {
                throw std::runtime_error("webgl_gpgpu_protoplanet state script diverged from its lock.");
            }
        }

        /** Returns one exact upper-24-bit JavaScript Math.random value. */
        double nextProtoplanetRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Advances hidden r185 UUID and unused-attribute random draws. */
        void advanceProtoplanetRandom(ThreeCompat::DeterministicRandom &random, uint32_t drawCount)
        {
            for (uint32_t drawIndex = 0u; drawIndex < drawCount; ++drawIndex)
            {
                (void)random.nextUint32();
            }
        }

        /** Reproduces fillTextures with JavaScript doubles and Float32 stores. */
        void buildProtoplanetTextures(ThreeCompat::DeterministicRandom &random, const ProtoplanetInitializationParameters &parameters, eastl::vector<float4> &position, eastl::vector<float4> &velocity)
        {
            position.resize(ProtoplanetParticleCount);
            velocity.resize(ProtoplanetParticleCount);
            const double maximumMass = parameters.maximumMass * 1024.0 / double(ProtoplanetParticleCount);
            for (uint32_t particleIndex = 0u; particleIndex < ProtoplanetParticleCount; ++particleIndex)
            {
                double x = 0.0;
                double z = 0.0;
                double radialSquared = 0.0;
                do
                {
                    x = nextProtoplanetRandomUnit(random) * 2.0 - 1.0;
                    z = nextProtoplanetRandomUnit(random) * 2.0 - 1.0;
                    radialSquared = x * x + z * z;
                } while (radialSquared > 1.0);

                const double radial = std::sqrt(radialSquared);
                const double radialPosition = parameters.radius * std::pow(radial, parameters.exponent);
                const double tangentialVelocity = parameters.velocity * std::pow(radial, parameters.velocityExponent);
                const double velocityX = tangentialVelocity * z + (nextProtoplanetRandomUnit(random) * 2.0 - 1.0) * parameters.randomVelocity;
                const double velocityY = (nextProtoplanetRandomUnit(random) * 2.0 - 1.0) * parameters.randomVelocity * 0.05;
                const double velocityZ = -tangentialVelocity * x + (nextProtoplanetRandomUnit(random) * 2.0 - 1.0) * parameters.randomVelocity;
                x *= radialPosition;
                z *= radialPosition;
                const double y = (nextProtoplanetRandomUnit(random) * 2.0 - 1.0) * parameters.height;
                const double mass = nextProtoplanetRandomUnit(random) * maximumMass + 1.0;
                position[particleIndex] = float4(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f);
                velocity[particleIndex] = float4(static_cast<float>(velocityX), static_cast<float>(velocityY), static_cast<float>(velocityZ), static_cast<float>(mass));
            }
        }

        /** Builds Three's float projection and OrbitControls look-at view matrices. */
        void buildProtoplanetCamera(glm::mat4 &projectionMatrix, glm::mat4 &viewMatrix, float &cameraConstant)
        {
            const double nearDistance = 5.0;
            const double farDistance = 15000.0;
            const double top = nearDistance * std::tan(75.0 * Pi / 180.0 * 0.5);
            const double height = 2.0 * top;
            const double width = 1.6 * height;
            projectionMatrix = glm::mat4(0.0f);
            projectionMatrix[0u][0u] = static_cast<float>(2.0 * nearDistance / width);
            projectionMatrix[1u][1u] = static_cast<float>(2.0 * nearDistance / height);
            projectionMatrix[2u][2u] = static_cast<float>(-(farDistance + nearDistance) / (farDistance - nearDistance));
            projectionMatrix[2u][3u] = -1.0f;
            projectionMatrix[3u][2u] = static_cast<float>(-(2.0 * farDistance * nearDistance) / (farDistance - nearDistance));
            viewMatrix = glm::mat4(glm::lookAtRH(glm::dvec3(0.0, 120.0, 400.0), glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0)));
            cameraConstant = static_cast<float>(500.0 / std::tan(75.0 * Pi / 180.0 * 0.5));
        }

        /** Returns whether one identifier selects the first visible callback. */
        bool isProtoplanetInitialScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "initial-seeded-disc";
        }

        /** Returns whether one identifier selects the default frame-60 state. */
        bool isProtoplanetFixedScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "fixed-nbody-step";
        }

        /** Returns whether one identifier selects the GUI-driven restart state. */
        bool isProtoplanetRestartScenario(const ThreeSampleHostOptions &options)
        {
            return options.scenarioId == "gui-restart";
        }
    } // namespace

    void WebglGpgpuProtoplanetRuntimeAdapter::initializeResources(GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_gpgpu_protoplanet")
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet requires case-id webgl_gpgpu_protoplanet.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet comparison requires an 800x500 output.");
        }
        if (options.randomSeed != ProtoplanetRandomSeed)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet requires random seed 0x12345678.");
        }

        const bool initialScenario = isProtoplanetInitialScenario(options);
        const bool fixedScenario = isProtoplanetFixedScenario(options);
        restartScenario = isProtoplanetRestartScenario(options);
        if (!initialScenario && !fixedScenario && !restartScenario)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet received an unknown scenario-id.");
        }
        const uint32_t expectedFrame = initialScenario ? 0u : 60u;
        if (options.targetFrame != expectedFrame || options.frameCount != expectedFrame + 1u)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet frame count diverged from its lock.");
        }
        if (initialScenario && options.canonicalStatePath != DefaultCanonicalState)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet initial canonical state diverged.");
        }
        if (fixedScenario && options.canonicalStatePath != FixedCanonicalState)
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet fixed canonical state diverged.");
        }
        if (restartScenario)
        {
            validateProtoplanetStateScript(options.canonicalStatePath);
        }
        if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument("WebglGpgpuProtoplanet scenarios do not use input replay.");
        }

        ThreeCompat::DeterministicRandom random(ProtoplanetRandomSeed);
        advanceProtoplanetRandom(random, PreInitialTextureRandomDrawCount);
        ProtoplanetInitializationParameters parameters;
        buildProtoplanetTextures(random, parameters, initialPosition, initialVelocity);
        if (calculateProtoplanetSha256(initialPosition.data(), initialPosition.size() * sizeof(float4)) != DefaultPositionSha256 || calculateProtoplanetSha256(initialVelocity.data(), initialVelocity.size() * sizeof(float4)) != DefaultVelocitySha256)
        {
            throw std::runtime_error("webgl_gpgpu_protoplanet initial arrays diverged from r185.");
        }
        advanceProtoplanetRandom(random, PostInitialTextureRandomDrawCount);
        if (random.getState() != DefaultFinalRandomState)
        {
            throw std::runtime_error("webgl_gpgpu_protoplanet initial RNG state diverged from r185.");
        }

        if (restartScenario)
        {
            advanceProtoplanetRandom(random, PreRestartTextureRandomDrawCount);
            parameters.radius = 260.0;
            parameters.height = 12.0;
            buildProtoplanetTextures(random, parameters, initialPosition, initialVelocity);
            if (calculateProtoplanetSha256(initialPosition.data(), initialPosition.size() * sizeof(float4)) != RestartPositionSha256 || calculateProtoplanetSha256(initialVelocity.data(), initialVelocity.size() * sizeof(float4)) != RestartVelocitySha256 || random.getState() != RestartFinalRandomState)
            {
                throw std::runtime_error("webgl_gpgpu_protoplanet restart state diverged from r185.");
            }
            gravityConstant = 175.0f;
            density = 0.72f;
        }

        initialPositionSha256 = calculateProtoplanetSha256(initialPosition.data(), initialPosition.size() * sizeof(float4));
        initialVelocitySha256 = calculateProtoplanetSha256(initialVelocity.data(), initialVelocity.size() * sizeof(float4));
        finalRandomState = random.getState();
        buildProtoplanetCamera(projectionMatrix, viewMatrix, cameraConstant);
        device = inDevice;
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        if (!frameUploader)
        {
            throw std::logic_error("webgl_gpgpu_protoplanet frame uploader is unavailable.");
        }
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error("webgl_gpgpu_protoplanet callbacks must advance sequentially.");
        }
        WebglGpgpuProtoplanetSimulationUniforms uniforms;
        uniforms.gravityDensityAndReserved = float4(gravityConstant, density, 0.0f, 0.0f);
        frameUploader(uniforms, projectionMatrix, viewMatrix, cameraConstant, density, frameIndex);
        ++frameUpdateCount;
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeProtoplanetRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_gpgpu_protoplanet capture exceeds host storage.");
        }
        if (!positionTextureProvider || !velocityTextureProvider)
        {
            throw std::logic_error("webgl_gpgpu_protoplanet texture providers are unavailable.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        eastl::vector<float4> currentPosition(ProtoplanetParticleCount);
        eastl::vector<float4> currentVelocity(ProtoplanetParticleCount);
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error("webgl_gpgpu_protoplanet could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->readTexture(positionTextureProvider(), currentPosition.data(), currentPosition.size() * sizeof(float4))->readTexture(velocityTextureProvider(), currentVelocity.data(), currentVelocity.size() * sizeof(float4))->submit();
        currentPositionSha256 = calculateProtoplanetSha256(currentPosition.data(), currentPosition.size() * sizeof(float4));
        currentVelocitySha256 = calculateProtoplanetSha256(currentVelocity.data(), currentVelocity.size() * sizeof(float4));
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
        positionTextureProvider = nullptr;
        velocityTextureProvider = nullptr;
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareProtoplanetOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_gpgpu_protoplanet RGBA output.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete webgl_gpgpu_protoplanet RGBA output.");
        }
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareProtoplanetOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_gpgpu_protoplanet metadata output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_gpgpu_protoplanet\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << finalRandomState << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u << ",\n"
               << "  \"simulationFragmentPassCount\": " << uint64_t(frameIndex + 1u) * 2u << ",\n"
               << "  \"currentPingPongIndex\": " << ((frameIndex + 1u) & 1u) << ",\n"
               << "  \"gravityConstant\": " << gravityConstant << ",\n"
               << "  \"density\": " << density << ",\n"
               << "  \"restartApplied\": " << (restartScenario ? "true" : "false") << ",\n"
               << "  \"initialPositionSha256\": \"" << initialPositionSha256.c_str() << "\",\n"
               << "  \"initialVelocitySha256\": \"" << initialVelocitySha256.c_str() << "\",\n"
               << "  \"currentPositionSha256\": \"" << currentPositionSha256.c_str() << "\",\n"
               << "  \"currentVelocitySha256\": \"" << currentVelocitySha256.c_str() << "\",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglGpgpuProtoplanetRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareProtoplanetOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_gpgpu_protoplanet snapshot output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_gpgpu_protoplanet\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
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
               << "  \"logicalPointCount\": 4096,\n"
               << "  \"vertexCount\": 24576,\n"
               << "  \"triangleCount\": 8192,\n"
               << "  \"indexed\": false,\n"
               << "  \"positionTextureFormat\": \"rgba32float\",\n"
               << "  \"velocityTextureFormat\": \"rgba32float\",\n"
               << "  \"pingPongTextureCount\": 4,\n"
               << "  \"simulationStepCount\": " << frameIndex + 1u << ",\n"
               << "  \"simulationFragmentPassesPerStep\": 2,\n"
               << "  \"fixedNeighborIterationCountPerParticle\": 4096,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                  "\"scenePass\":\"main-expanded-circular-particles\"}],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
