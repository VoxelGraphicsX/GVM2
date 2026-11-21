#include "Test10ProceduralCityWorldApp.hpp"

#include <GVMCore/Public/TaskRuntime.hpp>
#include <GVMCore/Public/ThreadExecutor.hpp>

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(GVM_SAMPLE_HAS_UPSTREAM_MARL)
#include <GVMCore/Public/MarlTaskExecutor.hpp>
namespace
{
    constexpr bool kHasUpstreamMarl = true;

    struct OptionalMarlAdapter
    {
        eastl::unique_ptr<marl::Scheduler> scheduler;
        eastl::unique_ptr<GVM::Core::MarlTaskExecutor> executor;

        GVM::Core::ITaskExecutor *init(uint32_t workers)
        {
            marl::Scheduler::Config cfg;
            cfg.setWorkerThreadCount(static_cast<int>(workers));
            scheduler = eastl::make_unique<marl::Scheduler>(cfg);
            executor = eastl::make_unique<GVM::Core::MarlTaskExecutor>(*scheduler);
            return executor.get();
        }

        void reset()
        {
            executor = nullptr;
            scheduler = nullptr;
        }
    };
} // namespace
#else
namespace
{
    constexpr bool kHasUpstreamMarl = false;

    struct OptionalMarlAdapter
    {
        GVM::Core::ITaskExecutor *init(uint32_t) { return nullptr; }
        void reset() {}
    };
} // namespace
#endif

#include "UGLBin/generate_result.hpp"

namespace
{
    uint32_t resolveParallelWorkerCount(uint32_t requested)
    {
        if (requested > 0u)
        {
            return requested;
        }
        const unsigned hw = std::thread::hardware_concurrency();
        return hw >= 2u ? std::min<uint32_t>(hw, 12u) : 4u;
    }

    /// Populates exact LH [0, 1] View-Projection rows for an aerial orbit+pan metropolis camera.
    void populateCityCamera(
        ProceduralCitySample::CityParams &params,
        const glm::vec3 &target,
        float orbitYaw,
        float orbitPitch,
        float orbitDistance,
        float aspect)
    {
        const float safeAspect = aspect > 0.05f ? aspect : (16.0f / 9.0f);
        const float cosPitch = std::cos(orbitPitch);
        const glm::vec3 eye = target + glm::vec3(
            std::sin(orbitYaw) * cosPitch * orbitDistance,
            std::sin(orbitPitch) * orbitDistance,
            -std::cos(orbitYaw) * cosPitch * orbitDistance);

        const glm::vec3 forward = glm::normalize(target - eye);
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
        const glm::vec3 up = glm::cross(forward, right);

        constexpr float kFovY = 50.0f * (3.14159265359f / 180.0f);
        constexpr float kZNear = 1.0f;
        constexpr float kZFar = 950.0f;
        const float yScale = 1.0f / std::tan(kFovY * 0.5f);
        const float xScale = yScale / safeAspect;
        const float zA = kZFar / (kZFar - kZNear);
        const float zB = -(kZNear * kZFar) / (kZFar - kZNear);

        const float dotRightEye = glm::dot(right, eye);
        const float dotUpEye = glm::dot(up, eye);
        const float dotFwdEye = glm::dot(forward, eye);

        params.viewProjRow0 = glm::vec4(right * xScale, -dotRightEye * xScale);
        params.viewProjRow1 = glm::vec4(up * yScale, -dotUpEye * yScale);
        params.viewProjRow2 = glm::vec4(forward * zA, -dotFwdEye * zA + zB);
        params.viewProjRow3 = glm::vec4(forward, -dotFwdEye);
        params.eyePosition = glm::vec4(eye, 1.0f);

        const auto atmo = ProceduralCitySample::evaluateCityAtmosphere(params.timeOfDay);
        params.sunDirection = atmo.sunDirection;
    }

    /// Owns one concrete executor + TaskRuntime bundle with safe reverse-order destruction.
    struct RuntimeBundle
    {
        GVM::Samples::CityExecutorMode mode = GVM::Samples::CityExecutorMode::ThreadPool;
        uint32_t workerCount = 1u;
        bool timestampFeedback = true;

        eastl::unique_ptr<GVM::Core::ThreadExecutor> threadExecutor;
        OptionalMarlAdapter marlAdapter;
        eastl::unique_ptr<GVM::Core::TaskRuntime> runtime;

        RuntimeBundle(
            GVM::RHI::DeviceImpl &device,
            GVM::Samples::CityExecutorMode execMode,
            uint32_t parallelWorkers,
            bool enableTimestamps,
            uint32_t workerOverride = 0u)
            : mode(execMode)
            , timestampFeedback(enableTimestamps)
        {
            GVM::Core::TaskRuntimeDescriptor desc;
            desc.enablePassTimestampFeedback = enableTimestamps;
            desc.maximumActiveExecutions = 64u;
            desc.maximumTasksPerExecution = 16384u;
            desc.maximumPassesPerExecution = 1024u;

            if (execMode == GVM::Samples::CityExecutorMode::Serial)
            {
                workerCount = 1u;
                threadExecutor = eastl::make_unique<GVM::Core::ThreadExecutor>(1u);
                runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *threadExecutor, desc);
                return;
            }

            const uint32_t effectiveWorkers = workerOverride > 0u ? workerOverride : parallelWorkers;
            if (execMode == GVM::Samples::CityExecutorMode::MarlFibers)
            {
                if (auto *marlExec = marlAdapter.init(effectiveWorkers))
                {
                    workerCount = effectiveWorkers;
                    runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *marlExec, desc);
                    return;
                }
            }

            workerCount = effectiveWorkers;
            threadExecutor = eastl::make_unique<GVM::Core::ThreadExecutor>(effectiveWorkers);
            runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *threadExecutor, desc);
        }

        ~RuntimeBundle()
        {
            runtime = nullptr;
            marlAdapter.reset();
            threadExecutor = nullptr;
        }
    };

    const char *executorModeLabel(GVM::Samples::CityExecutorMode mode)
    {
        switch (mode)
        {
        case GVM::Samples::CityExecutorMode::Serial: return "Serial (ThreadExecutor x1)";
        case GVM::Samples::CityExecutorMode::ThreadPool: return "ThreadExecutor (Parallel Pool)";
        case GVM::Samples::CityExecutorMode::MarlFibers:
            return kHasUpstreamMarl
                ? "MarlTaskExecutor (google/marl Fibers)"
                : "MarlTaskExecutor (Fallback -> ThreadExecutor)";
        }
        return "Unknown";
    }

    template <typename T>
    T awaitExecutionOrThrow(
        GVM::Core::Execution<T> &exec,
        uint64_t timeoutNs,
        const char *timeoutContext)
    {
        if (!exec.wait(timeoutNs))
        {
            exec.cancel();
            (void)exec.wait();
            throw std::runtime_error(timeoutContext);
        }
        if (exec.status() != GVM::Core::ExecutionStatus::Succeeded)
        {
            throw std::runtime_error(exec.error().empty()
                ? timeoutContext
                : exec.error().c_str());
        }
        return exec.get();
    }

    // =========================================================================
    // Pairwise Binary-Tree Geometry Reduction (Identical Topology Across Modes)
    // =========================================================================

    GVM::Core::Task<ProceduralCitySample::CityGeometryPack> reduceGeometryPackTree(
        GVM::Core::TaskGraph &graph,
        std::vector<GVM::Core::Task<ProceduralCitySample::CityGeometryPack>> packs)
    {
        if (packs.empty())
        {
            return graph.add([]() { return ProceduralCitySample::CityGeometryPack{}; });
        }

        while (packs.size() > 1u)
        {
            std::vector<GVM::Core::Task<ProceduralCitySample::CityGeometryPack>> merged;
            merged.reserve((packs.size() + 1u) / 2u);
            for (size_t i = 0u; i + 1u < packs.size(); i += 2u)
            {
                merged.push_back(graph.add(ProceduralCitySample::mergeCityGeometry, packs[i], packs[i + 1u]));
            }
            if ((packs.size() & 1u) != 0u)
            {
                merged.push_back(packs.back());
            }
            packs = std::move(merged);
        }
        return packs.front();
    }

    // =========================================================================
    // Portable Non-Blocking Nested TaskGraph Execution (All ITaskExecutors)
    // =========================================================================

    struct EvaluateCityQuadrantSubgraph
    {
        std::shared_ptr<std::atomic<uint32_t>> nestedSubgraphCounter;

        GVM::Core::Task<ProceduralCitySample::CityGeometryPack> operator()(
            GVM::Core::TaskGraph &subGraph,
            uint32_t quadrantIndex,
            ProceduralCitySample::CityParams params) const
        {
            const ProceduralCitySample::QuadrantSubdivisionPlan plan =
                ProceduralCitySample::subdivideUrbanQuadrantLots(params, quadrantIndex);

            uint32_t spawnedSubgraphs = 1u;
            std::vector<GVM::Core::Task<ProceduralCitySample::CityGeometryPack>> packs;
            packs.reserve(
                plan.skyscraperBlocks.size() +
                plan.midriseAndResBlocks.size() +
                plan.corridors.size());

            for (const auto &spec : plan.skyscraperBlocks)
            {
                ++spawnedSubgraphs;
                packs.push_back(subGraph.add(
                    ProceduralCitySample::Entries::buildSkyscraperDistrictSubgraph,
                    spec));
            }

            for (const auto &spec : plan.midriseAndResBlocks)
            {
                ++spawnedSubgraphs;
                packs.push_back(subGraph.add(
                    ProceduralCitySample::Entries::buildMidriseAndResidentialSubgraph,
                    spec));
            }

            for (const auto &corr : plan.corridors)
            {
                ++spawnedSubgraphs;
                packs.push_back(subGraph.add(
                    ProceduralCitySample::Entries::buildInfrastructureAndBridgeSubgraph,
                    corr));
            }

            if (nestedSubgraphCounter != nullptr)
            {
                nestedSubgraphCounter->fetch_add(spawnedSubgraphs, std::memory_order_relaxed);
            }

            return reduceGeometryPackTree(subGraph, std::move(packs));
        }
    };

    // =========================================================================
    // Unified Outer Graph Submission (Windowed Swapchain & Headless Benchmark)
    // =========================================================================

    ProceduralCitySample::CityFrameSummary executeSwapchainFrame(
        RuntimeBundle &bundle,
        const GVM::RHI::SwapchainQueryResult &swapchainTarget,
        const ProceduralCitySample::CityParams &params,
        uint32_t &outRuntimeSubmits)
    {
        auto submitCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        ProceduralCitySample::CitySwapchainRequest req;
        req.swapchainTarget = swapchainTarget;
        req.params = params;

        auto execution = bundle.runtime->submit(
            [submitCounter](const ProceduralCitySample::CitySwapchainRequest &r)
            {
                GVM::Core::TaskGraph graph;
                auto envLut = graph.add(ProceduralCitySample::BakeCityEnvironmentLutNode{}, r.params);

                EvaluateCityQuadrantSubgraph evalQuad{submitCounter};
                GVM::Core::Task<ProceduralCitySample::CityGeometryPack> cpuQuads[4];
                GVM::Core::Task<ProceduralCitySample::CityQuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(evalQuad, q, r.params);
                    gpuQuads[q] = graph.add(
                        ProceduralCitySample::PrepareCityQuadrantGpuNode{},
                        r.params,
                        envLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    ProceduralCitySample::summarizeCityQuadrants,
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);
                auto sceneColor = graph.add(
                    ProceduralCitySample::RenderProceduralCityNode{},
                    r.params,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto presented = graph.add(
                    ProceduralCitySample::PresentCitySwapchainNode{},
                    r.swapchainTarget,
                    sceneColor);
                return graph.add(ProceduralCitySample::finalizeCitySwapchainSummary, summary, presented);
            },
            req);

        const auto result = awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test10 Procedural City TaskGraph frame failed or timed out.");
        outRuntimeSubmits = submitCounter->load(std::memory_order_acquire);
        return result;
    }

    ProceduralCitySample::CityBenchmarkResult executeBenchmarkFrame(
        RuntimeBundle &bundle,
        const ProceduralCitySample::CityParams &params,
        uint32_t &outRuntimeSubmits)
    {
        auto submitCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        auto execution = bundle.runtime->submit(
            [submitCounter](const ProceduralCitySample::CityParams &p)
            {
                GVM::Core::TaskGraph graph;
                auto envLut = graph.add(ProceduralCitySample::BakeCityEnvironmentLutNode{}, p);

                EvaluateCityQuadrantSubgraph evalQuad{submitCounter};
                GVM::Core::Task<ProceduralCitySample::CityGeometryPack> cpuQuads[4];
                GVM::Core::Task<ProceduralCitySample::CityQuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(evalQuad, q, p);
                    gpuQuads[q] = graph.add(
                        ProceduralCitySample::PrepareCityQuadrantGpuNode{},
                        p,
                        envLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    ProceduralCitySample::summarizeCityQuadrants,
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);
                auto sceneColor = graph.add(
                    ProceduralCitySample::RenderProceduralCityNode{},
                    p,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(ProceduralCitySample::evaluateCityBenchmarkPixels, summary, p, pixels);
            },
            params);

        const auto result = awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test10 benchmark execution failed or timed out.");
        outRuntimeSubmits = submitCounter->load(std::memory_order_acquire);
        return result;
    }

    // =========================================================================
    // Self-Contained Lossless RGBA8 PNG Writer & Showcase Frame Capture
    // =========================================================================

    void appendBigEndian32(std::vector<uint8_t> &dst, uint32_t value)
    {
        dst.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>(value & 0xFFu));
    }

    uint32_t computePngCrc32(const uint8_t *data, size_t len)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0u; i < len; ++i)
        {
            crc ^= static_cast<uint32_t>(data[i]);
            for (uint32_t b = 0u; b < 8u; ++b)
            {
                const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
                crc = (crc >> 1u) ^ (0xEDB88320u & mask);
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    void appendPngChunk(
        std::vector<uint8_t> &png,
        const char (&chunkType)[5],
        const uint8_t *data,
        size_t len)
    {
        appendBigEndian32(png, static_cast<uint32_t>(len));
        const size_t crcStart = png.size();
        png.push_back(static_cast<uint8_t>(chunkType[0]));
        png.push_back(static_cast<uint8_t>(chunkType[1]));
        png.push_back(static_cast<uint8_t>(chunkType[2]));
        png.push_back(static_cast<uint8_t>(chunkType[3]));
        if (len > 0u && data != nullptr)
        {
            png.insert(png.end(), data, data + len);
        }
        const uint32_t crc = computePngCrc32(png.data() + crcStart, 4u + len);
        appendBigEndian32(png, crc);
    }

    bool writeRgba8Png(const char *path, uint32_t width, uint32_t height, const uint8_t *rgba)
    {
        if (path == nullptr || path[0] == '\0' || rgba == nullptr || width == 0u || height == 0u)
        {
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4u;
        const size_t scanlineStride = 1u + rowBytes;
        const size_t rawSize = static_cast<size_t>(height) * scanlineStride;

        std::vector<uint8_t> rawStream(rawSize, 0u);
        for (uint32_t y = 0u; y < height; ++y)
        {
            uint8_t *dstRow = rawStream.data() + static_cast<size_t>(y) * scanlineStride;
            const uint8_t *srcRow = rgba + static_cast<size_t>(height - 1u - y) * rowBytes;
            dstRow[0] = 0x00u; // Filter method 0 (None)
            std::memcpy(dstRow + 1u, srcRow, rowBytes);
        }

        // Compute Adler-32 over unfiltered + filter-byte scanline stream
        constexpr uint32_t kAdlerMod = 65521u;
        uint32_t s1 = 1u;
        uint32_t s2 = 0u;
        size_t adlerIdx = 0u;
        while (adlerIdx < rawSize)
        {
            const size_t batch = std::min<size_t>(rawSize - adlerIdx, 5552u);
            for (size_t i = 0u; i < batch; ++i)
            {
                s1 += static_cast<uint32_t>(rawStream[adlerIdx + i]);
                s2 += s1;
            }
            s1 %= kAdlerMod;
            s2 %= kAdlerMod;
            adlerIdx += batch;
        }
        const uint32_t adler32 = (s2 << 16u) | s1;

        // Zlib wrapper (CMF=0x78, FLG=0x01) + Deflate stored blocks (BTYPE=00, <= 65535 bytes) + Adler-32
        const size_t blockCount = (rawSize + 65534u) / 65535u;
        std::vector<uint8_t> zlibStream;
        zlibStream.reserve(2u + blockCount * 5u + rawSize + 4u);
        zlibStream.push_back(0x78u);
        zlibStream.push_back(0x01u);

        size_t offset = 0u;
        while (offset < rawSize)
        {
            const uint16_t blockLen = static_cast<uint16_t>(std::min<size_t>(rawSize - offset, 65535u));
            const bool isFinal = (offset + blockLen) == rawSize;
            const uint16_t nlen = static_cast<uint16_t>(~blockLen);

            zlibStream.push_back(isFinal ? 0x01u : 0x00u);
            zlibStream.push_back(static_cast<uint8_t>(blockLen & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>((blockLen >> 8u) & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>(nlen & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>((nlen >> 8u) & 0xFFu));
            zlibStream.insert(
                zlibStream.end(),
                rawStream.data() + offset,
                rawStream.data() + offset + blockLen);
            offset += blockLen;
        }
        appendBigEndian32(zlibStream, adler32);

        std::vector<uint8_t> png;
        png.reserve(8u + 25u + 12u + zlibStream.size() + 12u);
        constexpr uint8_t kPngSignature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
        png.insert(png.end(), kPngSignature, kPngSignature + 8u);

        std::vector<uint8_t> ihdr;
        ihdr.reserve(13u);
        appendBigEndian32(ihdr, width);
        appendBigEndian32(ihdr, height);
        ihdr.push_back(8u); // Bit depth: 8
        ihdr.push_back(6u); // Color type: 6 (RGBA)
        ihdr.push_back(0u); // Compression method: 0 (Deflate)
        ihdr.push_back(0u); // Filter method: 0
        ihdr.push_back(0u); // Interlace method: 0 (None)
        appendPngChunk(png, "IHDR", ihdr.data(), ihdr.size());

        appendPngChunk(png, "IDAT", zlibStream.data(), zlibStream.size());
        appendPngChunk(png, "IEND", nullptr, 0u);

        std::FILE *fp = std::fopen(path, "wb");
        if (fp == nullptr)
        {
            return false;
        }
        const size_t written = std::fwrite(png.data(), 1u, png.size(), fp);
        const int closeRes = std::fclose(fp);
        return written == png.size() && closeRes == 0;
    }

    struct CityCaptureOutput
    {
        ProceduralCitySample::CityFrameSummary summary;
        eastl::vector<uint8_t> pixels;
    };

    CityCaptureOutput executeCaptureFrame(
        RuntimeBundle &bundle,
        const ProceduralCitySample::CityParams &params,
        uint32_t &outRuntimeSubmits)
    {
        auto submitCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        auto execution = bundle.runtime->submit(
            [submitCounter](const ProceduralCitySample::CityParams &p)
            {
                GVM::Core::TaskGraph graph;
                auto envLut = graph.add(ProceduralCitySample::BakeCityEnvironmentLutNode{}, p);

                EvaluateCityQuadrantSubgraph evalQuad{submitCounter};
                GVM::Core::Task<ProceduralCitySample::CityGeometryPack> cpuQuads[4];
                GVM::Core::Task<ProceduralCitySample::CityQuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(evalQuad, q, p);
                    gpuQuads[q] = graph.add(
                        ProceduralCitySample::PrepareCityQuadrantGpuNode{},
                        p,
                        envLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    ProceduralCitySample::summarizeCityQuadrants,
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);
                auto sceneColor = graph.add(
                    ProceduralCitySample::RenderProceduralCityNode{},
                    p,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(
                    [](ProceduralCitySample::CityFrameSummary s, eastl::vector<uint8_t> px)
                    {
                        return CityCaptureOutput{s, std::move(px)};
                    },
                    summary,
                    pixels);
            },
            params);

        auto result = awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test10 showcase capture execution failed or timed out.");
        outRuntimeSubmits = submitCounter->load(std::memory_order_acquire);
        return result;
    }
} // namespace

namespace GVM::Samples
{
    struct Test10ProceduralCityWorldApp::Impl
    {
        uint32_t parallelWorkers = resolveParallelWorkerCount(0u);
        CityExecutorMode activeMode = kHasUpstreamMarl
            ? CityExecutorMode::MarlFibers
            : CityExecutorMode::ThreadPool;
        bool enableTimestampFeedback = true;
        eastl::unique_ptr<RuntimeBundle> bundle;

        ProceduralCitySample::CityParams cityParams;
        ProceduralCitySample::CityFrameSummary lastSummary;
        uint32_t lastRuntimeSubmits = 0u;

        glm::vec3 cameraTarget{15.0f, 12.0f, 0.0f};
        float orbitYaw = -0.68f;
        float orbitPitch = 0.42f;
        float orbitDistance = 215.0f;
        bool autoOrbit = true;
        bool animateTraffic = true;
        int activeDragButton = 0; // 1 = left orbit, 2/3 = pan
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

        double smoothedExecMs = 0.0;
        double baselineSerialMs = 0.0;
    };

    Test10ProceduralCityWorldApp::Test10ProceduralCityWorldApp(
        SampleWindowManager &window,
        GVM::RHI::GraphicsBackend preferredBackend)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test10 Open-World Procedural City (Nested TaskGraph)",
                                        .preferredGraphicsBackend = preferredBackend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->bundle = eastl::make_unique<RuntimeBundle>(
            *device(),
            mImpl->activeMode,
            mImpl->parallelWorkers,
            mImpl->enableTimestampFeedback);
    }

    Test10ProceduralCityWorldApp::~Test10ProceduralCityWorldApp()
    {
        mImpl = nullptr;
    }

    void Test10ProceduralCityWorldApp::onWindowEvent(const SampleWindowEvent &event)
    {
        if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
        {
            mImpl->activeDragButton = 0;
            return;
        }

        if (event.type == SampleWindowEvent::Type::PointerDown)
        {
            mImpl->activeDragButton = event.pointerId > 0 ? event.pointerId : 1;
            mImpl->lastMouseX = event.x;
            mImpl->lastMouseY = event.y;
        }
        else if (event.type == SampleWindowEvent::Type::PointerUp)
        {
            mImpl->activeDragButton = 0;
        }
        else if (event.type == SampleWindowEvent::Type::PointerMove && mImpl->activeDragButton != 0)
        {
            const float dx = event.x - mImpl->lastMouseX;
            const float dy = event.y - mImpl->lastMouseY;
            mImpl->lastMouseX = event.x;
            mImpl->lastMouseY = event.y;

            if (mImpl->activeDragButton == 1)
            {
                mImpl->orbitYaw += dx * 0.0055f;
                mImpl->orbitPitch = std::clamp(mImpl->orbitPitch + dy * 0.005f, 0.10f, 1.35f);
            }
            else
            {
                // Pan across the city street plane relative to current camera yaw
                const float sinY = std::sin(mImpl->orbitYaw);
                const float cosY = std::cos(mImpl->orbitYaw);
                const glm::vec3 fwdXZ(-sinY, 0.0f, cosY);
                const glm::vec3 rightXZ(cosY, 0.0f, sinY);
                const float panScale = mImpl->orbitDistance * 0.0022f;
                mImpl->cameraTarget -= rightXZ * (dx * panScale);
                mImpl->cameraTarget += fwdXZ * (dy * panScale);
                mImpl->cameraTarget.x = std::clamp(mImpl->cameraTarget.x, -220.0f, 220.0f);
                mImpl->cameraTarget.z = std::clamp(mImpl->cameraTarget.z, -220.0f, 220.0f);
            }
        }
        else if (event.type == SampleWindowEvent::Type::KeyDown)
        {
            const float sinY = std::sin(mImpl->orbitYaw);
            const float cosY = std::cos(mImpl->orbitYaw);
            const glm::vec3 fwdXZ(-sinY, 0.0f, cosY);
            const glm::vec3 rightXZ(cosY, 0.0f, sinY);
            constexpr float kStep = 10.0f;

            if (event.key == 'w' || event.key == 'W')
            {
                mImpl->cameraTarget += fwdXZ * kStep;
            }
            else if (event.key == 's' || event.key == 'S')
            {
                mImpl->cameraTarget -= fwdXZ * kStep;
            }
            else if (event.key == 'a' || event.key == 'A')
            {
                mImpl->cameraTarget -= rightXZ * kStep;
            }
            else if (event.key == 'd' || event.key == 'D')
            {
                mImpl->cameraTarget += rightXZ * kStep;
            }
            else if (event.key == 'q' || event.key == 'Q' || event.key == '=')
            {
                mImpl->orbitDistance = std::clamp(mImpl->orbitDistance - 16.0f, 40.0f, 420.0f);
            }
            else if (event.key == 'e' || event.key == 'E' || event.key == '-')
            {
                mImpl->orbitDistance = std::clamp(mImpl->orbitDistance + 16.0f, 40.0f, 420.0f);
            }
            mImpl->cameraTarget.x = std::clamp(mImpl->cameraTarget.x, -220.0f, 220.0f);
            mImpl->cameraTarget.z = std::clamp(mImpl->cameraTarget.z, -220.0f, 220.0f);
        }
    }

    void Test10ProceduralCityWorldApp::updateFrame(const SampleFrameContext &context)
    {
        if (mImpl->animateTraffic)
        {
            mImpl->cityParams.timeSeconds += context.deltaSeconds;
        }
        if (mImpl->autoOrbit && mImpl->activeDragButton == 0)
        {
            mImpl->orbitYaw += context.deltaSeconds * 0.08f;
        }

        const uint32_t w = context.drawableWidth > 0u ? context.drawableWidth : 1u;
        const uint32_t h = context.drawableHeight > 0u ? context.drawableHeight : 1u;
        mImpl->cityParams.extent = glm::uvec2(w, h);
        populateCityCamera(
            mImpl->cityParams,
            mImpl->cameraTarget,
            mImpl->orbitYaw,
            mImpl->orbitPitch,
            mImpl->orbitDistance,
            float(w) / float(h));

        if (mImpl->bundle == nullptr ||
            mImpl->bundle->mode != mImpl->activeMode ||
            mImpl->bundle->timestampFeedback != mImpl->enableTimestampFeedback)
        {
            mImpl->bundle = eastl::make_unique<RuntimeBundle>(
                *device(),
                mImpl->activeMode,
                mImpl->parallelWorkers,
                mImpl->enableTimestampFeedback);
        }
    }

    void Test10ProceduralCityWorldApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 126.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.82f);
        if (ImGui::Begin("Procedural Metropolis (480m x 480m Nested TaskGraph)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int modeIdx = static_cast<int>(mImpl->activeMode);
            ImGui::TextUnformatted("Executor Backend:");
            if (ImGui::RadioButton("Serial (1 Worker, Nested UGL Subgraphs)", modeIdx == 0))
            {
                mImpl->activeMode = CityExecutorMode::Serial;
            }
            if (ImGui::RadioButton("ThreadExecutor Pool (Nested UGL Subgraphs)", modeIdx == 1))
            {
                mImpl->activeMode = CityExecutorMode::ThreadPool;
            }
            if (kHasUpstreamMarl)
            {
                if (ImGui::RadioButton("MarlTaskExecutor (Nested UGL Subgraphs)", modeIdx == 2))
                {
                    mImpl->activeMode = CityExecutorMode::MarlFibers;
                }
            }
            else
            {
                ImGui::BeginDisabled();
                ImGui::RadioButton("MarlTaskExecutor (build with -DGVM_ENABLE_UPSTREAM_MARL=ON)", false);
                ImGui::EndDisabled();
            }

            ImGui::Checkbox("GPU Pass Timestamp Feedback", &mImpl->enableTimestampFeedback);
            ImGui::Checkbox("Auto-Orbit (LMB Orbit / RMB Pan / WASD+QE)", &mImpl->autoOrbit);
            ImGui::Checkbox("Animate Live Road Traffic", &mImpl->animateTraffic);
            ImGui::SliderFloat("Orbit Distance", &mImpl->orbitDistance, 40.0f, 420.0f, "%.1f m");
            if (ImGui::Button("Reset Aerial Metropolis View"))
            {
                mImpl->cameraTarget = glm::vec3(15.0f, 12.0f, 0.0f);
                mImpl->orbitYaw = -0.68f;
                mImpl->orbitPitch = 0.42f;
                mImpl->orbitDistance = 215.0f;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Urban Grammar & Atmosphere:");
            int seedVal = static_cast<int>(mImpl->cityParams.seed);
            if (ImGui::SliderInt("City Seed", &seedVal, 1, 9999))
            {
                mImpl->cityParams.seed = static_cast<uint32_t>(seedVal);
            }
            ImGui::SliderFloat("Time of Day", &mImpl->cityParams.timeOfDay, 0.0f, 24.0f, "%.2f h");
            ImGui::SliderFloat("Traffic Speed", &mImpl->cityParams.trafficSpeed, 0.0f, 35.0f, "%.1f m/s");
            ImGui::SliderFloat("Canal Meander", &mImpl->cityParams.canalAmplitude, 4.0f, 28.0f, "%.1f m");
            ImGui::SliderFloat("Downtown CBD Radius", &mImpl->cityParams.cbdRadius, 70.0f, 150.0f, "%.0f m");
            ImGui::SliderFloat("Commercial Ring Radius", &mImpl->cityParams.commercialRadius, 140.0f, 220.0f, "%.0f m");

            ImGui::Separator();
            ImGui::Text("Active Engine: %s (%u workers)",
                executorModeLabel(mImpl->bundle->mode),
                mImpl->bundle->workerCount);
            ImGui::Text("Graph Execution Time: %.2f ms", mImpl->smoothedExecMs);
            if (mImpl->baselineSerialMs > 0.01 && mImpl->smoothedExecMs > 0.01)
            {
                ImGui::Text("Speedup vs Serial: %.2fx (Serial: %.2f ms)",
                    mImpl->baselineSerialMs / mImpl->smoothedExecMs,
                    mImpl->baselineSerialMs);
            }

            ImGui::Separator();
            ImGui::Text("Buildings: %u (CBD Towers: %u | Commercial: %u | Residential: %u)",
                mImpl->lastSummary.buildingCount,
                mImpl->lastSummary.cbdTowerCount,
                mImpl->lastSummary.commercialBuildingCount,
                mImpl->lastSummary.residentialBuildingCount);
            ImGui::Text("Parks: %u | Canal Bridges: %u | Nested Subgraphs: %u (Live Submits: %u)",
                mImpl->lastSummary.parkCount,
                mImpl->lastSummary.bridgeCount,
                mImpl->lastSummary.nestedSubgraphCount,
                mImpl->lastRuntimeSubmits);
            ImGui::Text("Vehicles: %u | Trees: %u | Streetlamps: %u | Props/Signs: %u",
                mImpl->lastSummary.vehicleCount,
                mImpl->lastSummary.treeCount,
                mImpl->lastSummary.lampCount,
                mImpl->lastSummary.propCount);
            ImGui::Text("Total Triangles: %u (Static Arch: %u)",
                mImpl->lastSummary.triangleCount,
                mImpl->lastSummary.archTriangleCount);

            const auto snapshot = mImpl->bundle->runtime->passProfileSnapshot();
            if (!snapshot.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Learned GPU Pass Weights (EMA):");
                const size_t limit = std::min<size_t>(snapshot.size(), 8u);
                for (size_t i = 0; i < limit; ++i)
                {
                    const auto &entry = snapshot[i];
                    ImGui::Text("  %s: %.3f ms (w=%u)",
                        entry.label.c_str(),
                        entry.smoothedDurationNs * 1e-6,
                        static_cast<unsigned>(entry.learnedWeight));
                }
            }
        }
        ImGui::End();
    }

    void Test10ProceduralCityWorldApp::renderSampleFrame()
    {
        auto nextTexture = swapchain()->queryNextTexture();
        const auto t0 = std::chrono::steady_clock::now();
        uint32_t runtimeSubmits = 0u;
        const auto summary = executeSwapchainFrame(
            *mImpl->bundle,
            nextTexture,
            mImpl->cityParams,
            runtimeSubmits);
        const auto t1 = std::chrono::steady_clock::now();
        const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        mImpl->smoothedExecMs = mImpl->smoothedExecMs <= 0.0
            ? execMs
            : (0.85 * mImpl->smoothedExecMs + 0.15 * execMs);
        if (mImpl->bundle->mode == CityExecutorMode::Serial)
        {
            mImpl->baselineSerialMs = mImpl->smoothedExecMs;
        }

        mImpl->lastSummary = summary;
        mImpl->lastRuntimeSubmits = runtimeSubmits;
        swapchain()->present();
    }

    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test10ProceduralCityWorldApp>(window);
    }

    int runProceduralCityHeadlessBenchmark(
        GVM::RHI::GraphicsBackend preferredBackend,
        uint32_t warmupIterations,
        uint32_t measuredIterations,
        uint32_t workerThreads)
    {
        const uint32_t parallelWorkers = resolveParallelWorkerCount(workerThreads);
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::RHI::createInstance({
                .preferredBackend = preferredBackend,
                .diagnosticsOverlay = {.enabled = GVM::RHI::False},
            }),
            GVM::RHI::destroyInstance);
        if (instance == nullptr)
        {
            std::fprintf(stderr, "[Test10 Benchmark] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test10 Benchmark] Failed to create RHI device.\n");
            return 1;
        }

        const char *backendStr = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ? "Metal" : "Vulkan";
        std::printf("====================================================================================================\n");
        std::printf(" GVM Test_10 Open-World Procedural City (480m x 480m) + Nested TaskGraph Benchmark (%s)\n", backendStr);
        std::printf(" Lot Subdivision -> Heterogeneous UGL [[Export]] Subgraphs -> 3-Way Compute Fission -> Forward Pass\n");
        std::printf(" Warmup: %u iters | Measured: %u iters | Parallel Workers: %u\n",
            warmupIterations, measuredIterations, parallelWorkers);
        std::printf("====================================================================================================\n");

        ProceduralCitySample::CityParams params;
        params.extent = glm::uvec2(640u, 480u);
        params.seed = 2026u;
        params.worldHalfSize = 240.0f;
        params.timeOfDay = 18.35f;
        params.timeSeconds = 1.25f;
        params.trafficSpeed = 14.0f;
        params.canalAmplitude = 24.0f;
        params.canalFrequency = 0.015f;
        params.cbdRadius = 114.0f;
        params.commercialRadius = 178.0f;
        populateCityCamera(
            params,
            glm::vec3(15.0f, 12.0f, 0.0f),
            -0.68f,
            0.42f,
            215.0f,
            640.0f / 480.0f);

        struct BenchRunSpec
        {
            const char *label;
            CityExecutorMode mode;
            uint32_t workerOverride;
        };

        std::vector<BenchRunSpec> runs = {
            {"Serial (1 Worker, Nested UGL Subgraphs)", CityExecutorMode::Serial, 1u},
            {"ThreadExecutor (Pool, Nested Subgraphs)", CityExecutorMode::ThreadPool, parallelWorkers},
        };
        if (kHasUpstreamMarl)
        {
            runs.push_back({"Marl (1 Worker, Nested UGL Subgraphs)", CityExecutorMode::MarlFibers, 1u});
            runs.push_back({"Marl (N Workers, Nested UGL Subgraphs)", CityExecutorMode::MarlFibers, parallelWorkers});
        }

        struct ModeBenchRecord
        {
            const char *label = "";
            CityExecutorMode mode = CityExecutorMode::Serial;
            uint32_t workers = 1u;
            uint32_t runtimeSubmits = 0u;
            double avgMs = 0.0;
            double minMs = 0.0;
            ProceduralCitySample::CityBenchmarkResult result;
            eastl::vector<GVM::Core::PassProfileFeedbackEntry> passSnapshot;
        };

        std::vector<ModeBenchRecord> records;
        for (const auto &spec : runs)
        {
            RuntimeBundle bundle(*device, spec.mode, parallelWorkers, /*enableTimestamps=*/true, spec.workerOverride);
            try
            {
                uint32_t dummySubmits = 0u;
                for (uint32_t w = 0u; w < warmupIterations; ++w)
                {
                    (void)executeBenchmarkFrame(bundle, params, dummySubmits);
                }

                double totalMs = 0.0;
                double bestMs = 1e9;
                ProceduralCitySample::CityBenchmarkResult lastRes{};
                uint32_t lastSubmits = 0u;
                for (uint32_t iter = 0u; iter < measuredIterations; ++iter)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    lastRes = executeBenchmarkFrame(bundle, params, lastSubmits);
                    const auto t1 = std::chrono::steady_clock::now();
                    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    totalMs += ms;
                    bestMs = std::min(bestMs, ms);
                }

                ModeBenchRecord rec;
                rec.label = spec.label;
                rec.mode = spec.mode;
                rec.workers = bundle.workerCount;
                rec.runtimeSubmits = lastSubmits;
                rec.avgMs = totalMs / double(std::max(measuredIterations, 1u));
                rec.minMs = bestMs;
                rec.result = lastRes;
                rec.passSnapshot = bundle.runtime->passProfileSnapshot();
                records.push_back(std::move(rec));
            }
            catch (const std::exception &err)
            {
                std::fprintf(stderr, "[Test10 Benchmark] Failed (%s): %s\n", spec.label, err.what());
                return 1;
            }
        }

        const auto &ref = records.front().result;
        std::printf("Districts   : %u Buildings (CBD=%u | Commercial=%u | Residential=%u) | Parks=%u | Bridges=%u\n",
            ref.summary.buildingCount,
            ref.summary.cbdTowerCount,
            ref.summary.commercialBuildingCount,
            ref.summary.residentialBuildingCount,
            ref.summary.parkCount,
            ref.summary.bridgeCount);
        std::printf("World Fleet : %u Vehicles | %u Trees | %u Lamps | %u Props | %u Tris (%u Arch Tris)\n",
            ref.summary.vehicleCount,
            ref.summary.treeCount,
            ref.summary.lampCount,
            ref.summary.propCount,
            ref.summary.triangleCount,
            ref.summary.archTriangleCount);
        std::printf("Pixel Check : Non-Sky Pixels=%u | Deterministic Checksum=0x%08x\n",
            ref.nonSkyPixelCount,
            ref.pixelChecksum);
        std::printf("----------------------------------------------------------------------------------------------------\n");
        std::printf("%-40s | %7s | %10s | %9s | %9s | %8s\n",
            "Executor Mode", "Workers", "NestedSubm", "Avg (ms)", "Best (ms)", "Speedup");
        std::printf("----------------------------------------------------------------------------------------------------\n");

        const double serialAvg = records.front().avgMs;
        for (const auto &rec : records)
        {
            const double speedup = rec.avgMs > 0.0 ? (serialAvg / rec.avgMs) : 1.0;
            std::printf("%-40s | %7u | %10u | %9.2f | %9.2f | %7.2fx\n",
                rec.label,
                rec.workers,
                rec.runtimeSubmits,
                rec.avgMs,
                rec.minMs,
                speedup);

            // Determinism and scene coverage checks across all execution modes.
            if (rec.result.summary.cbdTowerCount != ref.summary.cbdTowerCount ||
                rec.result.summary.commercialBuildingCount != ref.summary.commercialBuildingCount ||
                rec.result.summary.residentialBuildingCount != ref.summary.residentialBuildingCount ||
                rec.result.summary.buildingCount != ref.summary.buildingCount ||
                rec.result.summary.parkCount != ref.summary.parkCount ||
                rec.result.summary.bridgeCount != ref.summary.bridgeCount ||
                rec.result.summary.vehicleCount != ref.summary.vehicleCount ||
                rec.result.summary.treeCount != ref.summary.treeCount ||
                rec.result.summary.lampCount != ref.summary.lampCount ||
                rec.result.summary.propCount != ref.summary.propCount ||
                rec.result.summary.triangleCount != ref.summary.triangleCount ||
                rec.result.summary.nestedSubgraphCount != ref.summary.nestedSubgraphCount ||
                rec.result.nonSkyPixelCount != ref.nonSkyPixelCount ||
                rec.result.pixelChecksum != ref.pixelChecksum)
            {
                std::fprintf(stderr,
                    "[Test10 Benchmark] Determinism mismatch in %s! (checksum=0x%08x vs 0x%08x, nonSky=%u vs %u)\n",
                    rec.label,
                    rec.result.pixelChecksum,
                    ref.pixelChecksum,
                    rec.result.nonSkyPixelCount,
                    ref.nonSkyPixelCount);
                return 1;
            }
            if (rec.runtimeSubmits == 0u || rec.result.summary.nestedSubgraphCount == 0u)
            {
                std::fprintf(stderr, "[Test10 Benchmark] Expected nested subgraph count > 0 for %s!\n", rec.label);
                return 1;
            }
            if (rec.result.summary.buildingCount <= 80u ||
                rec.result.summary.vehicleCount <= 500u ||
                rec.result.summary.triangleCount <= 60000u ||
                rec.result.nonSkyPixelCount <= 120000u ||
                rec.result.summary.bridgeCount == 0u ||
                rec.result.summary.parkCount == 0u)
            {
                std::fprintf(stderr,
                    "[Test10 Benchmark] Scene richness threshold failed (buildings=%u, vehicles=%u, tris=%u, nonSky=%u, bridges=%u, parks=%u)!\n",
                    rec.result.summary.buildingCount,
                    rec.result.summary.vehicleCount,
                    rec.result.summary.triangleCount,
                    rec.result.nonSkyPixelCount,
                    rec.result.summary.bridgeCount,
                    rec.result.summary.parkCount);
                return 1;
            }
        }
        std::printf("----------------------------------------------------------------------------------------------------\n");

        const auto &lastSnapshot = records.back().passSnapshot;
        if (!lastSnapshot.empty())
        {
            std::printf("Learned GPU Pass Timestamp Feedback (%zu passes):\n", lastSnapshot.size());
            const size_t maxPrint = std::min<size_t>(lastSnapshot.size(), 10u);
            for (size_t i = 0; i < maxPrint; ++i)
            {
                const auto &e = lastSnapshot[i];
                std::printf("  - %-32s : %8.3f ms  (samples=%u, weight=%u)\n",
                    e.label.c_str(),
                    e.smoothedDurationNs * 1e-6,
                    e.sampleCount,
                    static_cast<unsigned>(e.learnedWeight));
            }
        }
        else
        {
            std::printf("Learned GPU Pass Timestamp Feedback: (queue timestamp queries unsupported or zero-tick collapsed)\n");
        }
        std::printf("====================================================================================================\n");
        return 0;
    }

    int runProceduralCityShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir)
    {
        if (captureDir == nullptr || captureDir[0] == '\0')
        {
            std::fprintf(stderr, "[Test10 Capture] --capture-dir requires a non-empty directory path.\n");
            return 1;
        }

        std::error_code fsErr;
        std::filesystem::create_directories(captureDir, fsErr);
        if (fsErr)
        {
            std::fprintf(stderr, "[Test10 Capture] Failed to create directory '%s': %s\n",
                captureDir, fsErr.message().c_str());
            return 1;
        }

        const uint32_t parallelWorkers = resolveParallelWorkerCount(0u);
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::RHI::createInstance({
                .preferredBackend = preferredBackend,
                .diagnosticsOverlay = {.enabled = GVM::RHI::False},
            }),
            GVM::RHI::destroyInstance);
        if (instance == nullptr)
        {
            std::fprintf(stderr, "[Test10 Capture] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test10 Capture] Failed to create RHI device.\n");
            return 1;
        }

        constexpr uint32_t kCaptureWidth = 1280u;
        constexpr uint32_t kCaptureHeight = 800u;
        constexpr float kCaptureAspect = float(kCaptureWidth) / float(kCaptureHeight);

        RuntimeBundle bundle(
            *device,
            CityExecutorMode::MarlFibers,
            parallelWorkers,
            /*enableTimestamps=*/true);

        struct ShowcaseShotSpec
        {
            const char *filename;
            float timeOfDay;
            float timeSeconds;
            glm::vec3 target;
            float orbitYaw;
            float orbitPitch;
            float orbitDistance;
        };

        const ShowcaseShotSpec shots[] = {
            {
                "test10_01_dusk_metropolis_aerial.png",
                18.35f,
                1.25f,
                glm::vec3(10.0f, 24.0f, -6.0f),
                0.70f,
                0.34f,
                248.0f,
            },
            {
                "test10_02_canal_bridges_daylight.png",
                14.2f,
                2.40f,
                glm::vec3(32.0f, 12.0f, -8.0f),
                0.20f,
                0.31f,
                146.0f,
            },
            {
                "test10_03_night_boulevard_traffic.png",
                21.5f,
                3.10f,
                glm::vec3(-118.0f, 12.0f, -55.0f),
                0.05f,
                0.12f,
                100.0f,
            },
        };

        const std::filesystem::path outDir(captureDir);
        const char *backendStr = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ? "Metal" : "Vulkan";
        std::printf("[Test10 Capture] Rendering 3 showcase shots (%ux%u, %s, %s, %u workers) -> %s\n",
            kCaptureWidth,
            kCaptureHeight,
            backendStr,
            executorModeLabel(bundle.mode),
            bundle.workerCount,
            captureDir);

        for (const auto &shot : shots)
        {
            ProceduralCitySample::CityParams params;
            params.extent = glm::uvec2(kCaptureWidth, kCaptureHeight);
            params.seed = 2026u;
            params.worldHalfSize = 240.0f;
            params.timeOfDay = shot.timeOfDay;
            params.timeSeconds = shot.timeSeconds;
            params.trafficSpeed = 14.0f;
            params.canalAmplitude = 24.0f;
            params.canalFrequency = 0.015f;
            params.cbdRadius = 114.0f;
            params.commercialRadius = 178.0f;
            populateCityCamera(
                params,
                shot.target,
                shot.orbitYaw,
                shot.orbitPitch,
                shot.orbitDistance,
                kCaptureAspect);

            uint32_t nestedSubmits = 0u;
            const auto captured = executeCaptureFrame(bundle, params, nestedSubmits);
            const size_t expectedBytes = static_cast<size_t>(kCaptureWidth) * static_cast<size_t>(kCaptureHeight) * 4u;
            if (captured.pixels.size() < expectedBytes)
            {
                std::fprintf(stderr, "[Test10 Capture] Readback buffer too small (%zu < %zu) for %s.\n",
                    captured.pixels.size(), expectedBytes, shot.filename);
                return 1;
            }

            const std::filesystem::path outPath = outDir / shot.filename;
            const std::string outPathStr = outPath.string();
            if (!writeRgba8Png(outPathStr.c_str(), kCaptureWidth, kCaptureHeight, captured.pixels.data()))
            {
                std::fprintf(stderr, "[Test10 Capture] Failed to write PNG: %s\n", outPathStr.c_str());
                return 1;
            }

            std::printf("[Test10 Capture] Saved %s (time=%.2fh, buildings=%u, vehicles=%u, tris=%u, nestedSubmits=%u)\n",
                outPathStr.c_str(),
                shot.timeOfDay,
                captured.summary.buildingCount,
                captured.summary.vehicleCount,
                captured.summary.triangleCount,
                nestedSubmits);
        }

        return 0;
    }
} // namespace GVM::Samples
