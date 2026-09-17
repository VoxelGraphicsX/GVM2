#include "WebgpuComputeReduceRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *SubgroupReplaySha256 =
            "4ffe25c26c49b2508f292ff52e93de9bb9a46355aa78ac8e20b297a656f1230d";

        /** Reads one bounded deterministic input-replay file. */
        eastl::vector<uint8_t> readComputeReduceReplay(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open the compute-reduce replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error("The compute-reduce replay has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read the complete compute-reduce replay.");
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity for a replay payload. */
        eastl::string calculateComputeReduceSha256(
            const eastl::vector<uint8_t> &bytes)
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

        /** Creates parent directories for one optional evidence path. */
        void prepareComputeReduceOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeComputeReduceText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareComputeReduceOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write compute-reduce evidence.");
        }
    } // namespace

    void WebgpuComputeReduceRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool validated =
            options.scenarioId == "validated-default" && options.targetFrame == 60u;
        const bool subgroup =
            options.scenarioId == "subgroup-comparison" && options.targetFrame == 61u;
        if (options.caseId != "webgpu_compute_reduce" ||
            (!initial && !validated && !subgroup) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (subgroup != !options.inputReplayPath.empty()))
            throw std::invalid_argument(
                "Compute-reduce adapter requires one locked Manifest scenario.");
        device = inDevice;
        executeReduction = !initial;
        leftAlgorithm = subgroup ? 2u : 0u;
        rightAlgorithm = subgroup ? 3u : 4u;
        if (subgroup)
        {
            replaySha256 = calculateComputeReduceSha256(
                readComputeReduceReplay(
                    std::filesystem::path(options.inputReplayPath.c_str())));
            if (replaySha256 != SubgroupReplaySha256)
                throw std::runtime_error(
                    "The compute-reduce replay differs from the locked payload.");
        }
    }

    void WebgpuComputeReduceRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeReduceRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareComputeReduceOutput(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write compute-reduce RGBA output.");
        }

        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgpu_compute_reduce\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
            << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"randomSeed\":" << options.randomSeed << ",\n"
            << "  \"width\":" << width << ",\n"
            << "  \"height\":" << height << ",\n"
            << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
            << "  \"byteCount\":" << byteCount << ",\n"
            << "  \"format\":\"rgba8unorm\",\n"
            << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
            << "  \"inputReplay\":";
        if (options.scenarioId == "subgroup-comparison")
        {
            metadata
                << "{\"sha256\":\"" << SubgroupReplaySha256
                << "\",\"caseId\":\"webgpu_compute_reduce\","
                << "\"scenarioId\":\"subgroup-comparison\","
                << "\"captureFrame\":61,\"eventCount\":1,"
                << "\"target\":\"body > canvas:first-of-type\"},\n";
        }
        else
        {
            metadata << "null,\n";
        }
        metadata
            << "  \"renderSetCount\":0,\n"
            << "  \"sceneRootCount\":2,\n"
            << "  \"entityCount\":2,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"scenePassCount\":2,\n"
            << "  \"screenPassCount\":0,\n"
            << "  \"computeElementCount\":262144\n}\n";
        writeComputeReduceText(options.captureMetadataPath, metadata.str());

        std::ostringstream scene;
        scene
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgpu_compute_reduce\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"not-required\",\n"
            << "  \"sceneRenderSetCount\":0,\n"
            << "  \"sceneRoots\":[\"leftScene\",\"rightScene\"],\n"
            << "  \"renderableObjectCount\":2,\n"
            << "  \"entityCount\":2,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"drawCommandCount\":2,\n"
            << "  \"scenePassCount\":2,\n"
            << "  \"screenPassCount\":0,\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false\n}\n";
        writeComputeReduceText(options.sceneSnapshotPath, scene.str());

        std::ostringstream semantic;
        semantic
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"algorithm\":\"three-r185-parallel-reduction\",\n"
            << "  \"inputElementCount\":262144,\n"
            << "  \"initialValue\":1,\n"
            << "  \"reductionExecuted\":" << (executeReduction ? "true" : "false") << ",\n"
            << "  \"leftAlgorithm\":" << leftAlgorithm << ",\n"
            << "  \"rightAlgorithm\":" << rightAlgorithm << ",\n"
            << "  \"expectedResult\":262144,\n"
            << "  \"leftKernel\":\""
            << (leftAlgorithm == 0u ? "n-over-two" : "groupshared-workgroup") << "\",\n"
            << "  \"rightKernel\":\"subgroup-lane-read-synthesis\",\n"
            << "  \"captureTiming\":\"before-one-second-highlight-transition\",\n"
            << "  \"sourceCanvasExtents\":[[0,0,400,500],[400,0,400,500]]\n}\n";
        writeComputeReduceText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuComputeReduceRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        replaySha256.clear();
    }
} // namespace GVM::ThreeSamples
