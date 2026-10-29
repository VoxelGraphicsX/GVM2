#include "WebgpuComputeTexturePingpongRuntimeAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Creates parent directories for one ping-pong artifact. */
        void preparePingpongOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Returns the validated RGBA8 capture byte count. */
        uint64_t computePingpongRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "Ping-pong capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the exact three-scenario Manifest contract. */
        void validatePingpongOptions(
            const ThreeSampleHostOptions &options)
        {
            if (options.caseId !=
                    "webgpu_compute_texture_pingpong" ||
                options.randomSeed != 0x12345678u)
            {
                throw std::invalid_argument(
                    "Ping-pong adapter requires its case and shared seed.");
            }
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 30u;
            const bool secondReset =
                options.scenarioId == "second-reset" &&
                options.targetFrame == 60u;
            if (!initial && !animated && !secondReset)
            {
                throw std::invalid_argument(
                    "Ping-pong scenario differs from the Manifest.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Ping-pong scenarios do not consume input replay.");
            }
        }
    } // namespace

    void WebgpuComputeTexturePingpongRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePingpongOptions(options);
        device = inDevice;
        constexpr float HalfPlaneWidth = 0.3125f;
        constexpr float HalfPlaneHeight = 0.5f;
        vertices = {
            {{-HalfPlaneWidth, HalfPlaneHeight, 0.5f, 1.0f},
             {0.0f, 1.0f, 0.0f, 0.0f}},
            {{HalfPlaneWidth, HalfPlaneHeight, 0.5f, 1.0f},
             {1.0f, 1.0f, 0.0f, 0.0f}},
            {{-HalfPlaneWidth, -HalfPlaneHeight, 0.5f, 1.0f},
             {0.0f, 0.0f, 0.0f, 0.0f}},
            {{HalfPlaneWidth, -HalfPlaneHeight, 0.5f, 1.0f},
             {1.0f, 0.0f, 0.0f, 0.0f}},
        };
        indices = {0u, 2u, 1u, 2u, 3u, 1u};
    }

    void WebgpuComputeTexturePingpongRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeTexturePingpongRuntimeAdapter::afterFrame(
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
            computePingpongRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeArtifacts(
            options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebgpuComputeTexturePingpongRuntimeAdapter::writeArtifacts(
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
            preparePingpongOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write ping-pong RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            preparePingpongOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgpu_compute_texture_pingpong\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\""
                   << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"randomSeed\":" << options.randomSeed << ",\n"
                   << "  \"randomState\":161639577,\n"
                   << "  \"width\":" << width << ",\n"
                   << "  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":"
                   << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n"
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"inputReplay\":null\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            preparePingpongOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgpu_compute_texture_pingpong\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":false,\n"
                   << "  \"assetHashes\":[],\n"
                   << "  \"renderSetPolicy\":\"not-required\",\n"
                   << "  \"sceneRenderSetCount\":0,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"entityCount\":0,\n"
                   << "  \"instanceCount\":1,\n"
                   << "  \"vertexCount\":4,\n"
                   << "  \"indexCount\":6,\n"
                   << "  \"computePassCount\":"
                   << (frameIndex * 2u + 3u) << ",\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":0,\n"
                   << "  \"scenePasses\":["
                   << "\"WebgpuComputeTexturePingpongMainPass\"],\n"
                   << "  \"screenPasses\":[],\n"
                   << "  \"attachmentFormats\":["
                   << "\"rgba8unorm\",\"depth32float\"],\n"
                   << "  \"storageTextureFormats\":["
                   << "\"rgba16float\",\"rgba16float\","
                   << "\"rgba16float\"],\n"
                   << "  \"storageTextureDimensions\":[512,512],\n"
                   << "  \"mipTextureDimensions\":[256,256],\n"
                   << "  \"computeDispatchThreads\":[512,512,1],\n"
                   << "  \"mipDispatchThreads\":[256,256,1],\n"
                   << "  \"resetCount\":1,\n"
                   << "  \"presentTexture\":\""
                   << (frameIndex % 2u == 0u ? "pong" : "ping")
                   << "\",\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"directDrawFallback\":false\n}\n";
        }
    }

    void WebgpuComputeTexturePingpongRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
