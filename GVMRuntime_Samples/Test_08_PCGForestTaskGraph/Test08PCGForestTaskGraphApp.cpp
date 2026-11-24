#include "Test08PCGForestTaskGraphApp.hpp"

#include <GVMCore/Public/TaskRuntime.hpp>
#include <GVMCore/Public/ThreadExecutor.hpp>

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

    uint32_t pngCrc32(const uint8_t *data, size_t len)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0u; i < len; ++i)
        {
            crc ^= uint32_t(data[i]);
            for (int bit = 0; bit < 8; ++bit)
            {
                const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
                crc = (crc >> 1u) ^ mask;
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    void appendBe32(std::vector<uint8_t> &out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        out.push_back(static_cast<uint8_t>(value & 0xFFu));
    }

    bool writePngChunk(std::FILE *fp, const char type[4], const uint8_t *data, uint32_t len)
    {
        uint8_t lenBe[4] = {
            static_cast<uint8_t>((len >> 24u) & 0xFFu),
            static_cast<uint8_t>((len >> 16u) & 0xFFu),
            static_cast<uint8_t>((len >> 8u) & 0xFFu),
            static_cast<uint8_t>(len & 0xFFu),
        };
        if (std::fwrite(lenBe, 1u, 4u, fp) != 4u ||
            std::fwrite(type, 1u, 4u, fp) != 4u)
        {
            return false;
        }
        if (len > 0u && std::fwrite(data, 1u, len, fp) != len)
        {
            return false;
        }

        std::vector<uint8_t> crcInput;
        crcInput.reserve(4u + len);
        crcInput.insert(crcInput.end(), type, type + 4);
        if (len > 0u)
        {
            crcInput.insert(crcInput.end(), data, data + len);
        }
        const uint32_t crc = pngCrc32(crcInput.data(), crcInput.size());
        uint8_t crcBe[4] = {
            static_cast<uint8_t>((crc >> 24u) & 0xFFu),
            static_cast<uint8_t>((crc >> 16u) & 0xFFu),
            static_cast<uint8_t>((crc >> 8u) & 0xFFu),
            static_cast<uint8_t>(crc & 0xFFu),
        };
        return std::fwrite(crcBe, 1u, 4u, fp) == 4u;
    }

    /// Self-contained lossless RGBA8 PNG writer (PNG signature + IHDR + stored DEFLATE IDAT + IEND).
    bool writeRgba8Png(
        const char *path,
        uint32_t width,
        uint32_t height,
        const uint8_t *rgba)
    {
        if (path == nullptr || rgba == nullptr || width == 0u || height == 0u)
        {
            return false;
        }

        const size_t rowBytes = size_t(width) * 4u;
        const size_t rawSize = size_t(height) * (1u + rowBytes);
        std::vector<uint8_t> rawScanlines(rawSize);
        for (uint32_t y = 0u; y < height; ++y)
        {
            uint8_t *dst = rawScanlines.data() + size_t(y) * (1u + rowBytes);
            dst[0] = 0x00u; // Filter type 0 (None)
            const uint8_t *src = rgba + size_t(y) * rowBytes;
            std::copy(src, src + rowBytes, dst + 1u);
        }

        // Adler-32 checksum over unfiltered + filter-byte scanlines.
        uint32_t s1 = 1u;
        uint32_t s2 = 0u;
        for (uint8_t b : rawScanlines)
        {
            s1 = (s1 + uint32_t(b)) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        const uint32_t adler32 = (s2 << 16u) | s1;

        // Zlib stream: 2-byte header + stored DEFLATE blocks (BTYPE=00, <= 65535 bytes each) + Adler-32.
        constexpr size_t kMaxStoredBlock = 65535u;
        const size_t blockCount = (rawSize + kMaxStoredBlock - 1u) / kMaxStoredBlock;
        std::vector<uint8_t> zlibStream;
        zlibStream.reserve(2u + rawSize + blockCount * 5u + 4u);
        zlibStream.push_back(0x78u);
        zlibStream.push_back(0x01u);

        size_t offset = 0u;
        while (offset < rawSize)
        {
            const size_t chunkLen = std::min(kMaxStoredBlock, rawSize - offset);
            const bool isFinal = (offset + chunkLen) >= rawSize;
            const uint16_t len16 = static_cast<uint16_t>(chunkLen);
            const uint16_t nlen16 = static_cast<uint16_t>(~len16);

            zlibStream.push_back(isFinal ? 0x01u : 0x00u); // BFINAL | BTYPE=00
            zlibStream.push_back(static_cast<uint8_t>(len16 & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>((len16 >> 8u) & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>(nlen16 & 0xFFu));
            zlibStream.push_back(static_cast<uint8_t>((nlen16 >> 8u) & 0xFFu));
            zlibStream.insert(
                zlibStream.end(),
                rawScanlines.data() + offset,
                rawScanlines.data() + offset + chunkLen);
            offset += chunkLen;
        }
        appendBe32(zlibStream, adler32);

        std::FILE *fp = std::fopen(path, "wb");
        if (fp == nullptr)
        {
            return false;
        }

        static constexpr uint8_t kPngSignature[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
        bool ok = std::fwrite(kPngSignature, 1u, 8u, fp) == 8u;

        if (ok)
        {
            std::vector<uint8_t> ihdr;
            ihdr.reserve(13u);
            appendBe32(ihdr, width);
            appendBe32(ihdr, height);
            ihdr.push_back(8u); // bit depth
            ihdr.push_back(6u); // color type RGBA
            ihdr.push_back(0u); // compression method
            ihdr.push_back(0u); // filter method
            ihdr.push_back(0u); // interlace method
            ok = writePngChunk(fp, "IHDR", ihdr.data(), static_cast<uint32_t>(ihdr.size()));
        }

        if (ok)
        {
            ok = writePngChunk(fp, "IDAT", zlibStream.data(), static_cast<uint32_t>(zlibStream.size()));
        }

        if (ok)
        {
            ok = writePngChunk(fp, "IEND", nullptr, 0u);
        }

        if (std::ferror(fp))
        {
            ok = false;
        }
        if (std::fclose(fp) != 0)
        {
            ok = false;
        }
        return ok;
    }

    /// Populates exact LH [0, 1] View-Projection rows for world-to-clip dot-product projection.
    void populateOrbitCamera(
        PCGForestSample::PCGForestParams &params,
        float orbitYaw,
        float orbitPitch,
        float orbitDistance,
        float aspect,
        const glm::vec3 &target = glm::vec3(0.0f, 1.5f, 4.0f))
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
        constexpr float kZNear = 0.5f;
        constexpr float kZFar = 480.0f;
        const float focalLength = 1.0f / std::tan(kFovY * 0.5f);
        const float xScale = focalLength / safeAspect;
        const float yScale = -focalLength;
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
        params.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.46f, 0.78f, -0.42f)), 0.0f);
    }

    /// Owns one concrete executor + TaskRuntime bundle with safe reverse-order destruction.
    struct RuntimeBundle
    {
        GVM::Samples::PCGExecutorMode mode = GVM::Samples::PCGExecutorMode::ThreadPool;
        uint32_t workerCount = 1u;
        bool timestampFeedback = true;

        eastl::unique_ptr<GVM::Core::ThreadExecutor> threadExecutor;
        OptionalMarlAdapter marlAdapter;
        eastl::unique_ptr<GVM::Core::TaskRuntime> runtime;

        RuntimeBundle(
            GVM::RHI::DeviceImpl &device,
            GVM::Samples::PCGExecutorMode execMode,
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

            if (execMode == GVM::Samples::PCGExecutorMode::Serial)
            {
                workerCount = 1u;
                threadExecutor = eastl::make_unique<GVM::Core::ThreadExecutor>(1u);
                runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *threadExecutor, desc);
                return;
            }

            const uint32_t effectiveWorkers = workerOverride > 0u ? workerOverride : parallelWorkers;
            if (execMode == GVM::Samples::PCGExecutorMode::MarlFibers)
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

    const char *executorModeLabel(GVM::Samples::PCGExecutorMode mode)
    {
        switch (mode)
        {
        case GVM::Samples::PCGExecutorMode::Serial: return "Serial (ThreadExecutor x1, Nested)";
        case GVM::Samples::PCGExecutorMode::ThreadPool: return "ThreadExecutor (Parallel Pool, Nested)";
        case GVM::Samples::PCGExecutorMode::MarlFibers:
            return kHasUpstreamMarl
                ? "MarlTaskExecutor (google/marl Fibers, Nested)"
                : "MarlTaskExecutor (Fallback -> ThreadExecutor)";
        }
        return "Unknown";
    }

    template <class T>
    T awaitExecutionOrThrow(
        GVM::Core::Execution<T> &execution,
        uint64_t timeoutNs,
        const char *timeoutMessage)
    {
        if (!execution.wait(timeoutNs))
        {
            execution.cancel();
            execution.wait();
            throw std::runtime_error(timeoutMessage);
        }
        if (execution.status() != GVM::Core::ExecutionStatus::Succeeded)
        {
            throw std::runtime_error(execution.error().empty()
                ? timeoutMessage
                : execution.error().c_str());
        }
        return execution.get();
    }

    // =========================================================================
    // Adaptive Quadtree Construction Helpers (Shared Bounds & Determinism)
    // =========================================================================

    PCGForestSample::QuadtreeNodeSpec makeQuadrantRootSpec(
        uint32_t quadrant,
        const PCGForestSample::PCGForestParams &params)
    {
        PCGForestSample::QuadtreeNodeSpec spec;
        spec.quadrantIndex = quadrant;
        spec.leafCode = quadrant;
        spec.depth = 0u;
        spec.nodeSize = params.worldHalfSize;
        spec.minX = (quadrant & 1u) == 0u ? -params.worldHalfSize : 0.0f;
        spec.minZ = (quadrant & 2u) == 0u ? -params.worldHalfSize : 0.0f;
        return spec;
    }

    PCGForestSample::QuadtreeNodeSpec makeChildNodeSpec(
        const PCGForestSample::QuadtreeNodeSpec &parent,
        uint32_t childIdx)
    {
        const uint32_t cx = childIdx & 1u;
        const uint32_t cz = (childIdx >> 1u) & 1u;
        PCGForestSample::QuadtreeNodeSpec child;
        child.quadrantIndex = parent.quadrantIndex;
        child.leafCode = (parent.leafCode << 2u) | childIdx;
        child.depth = parent.depth + 1u;
        child.nodeSize = parent.nodeSize * 0.5f;
        child.minX = parent.minX + float(cx) * child.nodeSize;
        child.minZ = parent.minZ + float(cz) * child.nodeSize;
        return child;
    }

    PCGForestSample::QuadtreeLeafInput toLeafInput(
        const PCGForestSample::QuadtreeNodeSpec &node,
        const PCGForestSample::PCGForestParams &params)
    {
        PCGForestSample::QuadtreeLeafInput leaf;
        leaf.params = params;
        leaf.quadrantIndex = node.quadrantIndex;
        leaf.leafCode = node.leafCode;
        leaf.depth = node.depth;
        leaf.minX = node.minX;
        leaf.minZ = node.minZ;
        leaf.leafSize = node.nodeSize;
        return leaf;
    }

    // =========================================================================
    // Unified Portable Non-Blocking Recursive Nested TaskGraph Execution
    // =========================================================================

    struct EvaluateQuadtreeNodeSubgraph
    {
        std::shared_ptr<std::atomic<uint32_t>> nestedSubgraphCount;

        GVM::Core::Task<PCGForestSample::ChunkGeometryPack> operator()(
            GVM::Core::TaskGraph &subGraph,
            PCGForestSample::QuadtreeNodeSpec node,
            PCGForestSample::PCGForestParams params) const
        {
            const bool split = PCGForestSample::shouldSubdivideQuadtreeNode(
                node.minX, node.minZ, node.nodeSize, node.depth, params);

            nestedSubgraphCount->fetch_add(1u, std::memory_order_relaxed);
            if (!split)
            {
                return subGraph.add(
                    PCGForestSample::Entries::buildQuadtreeLeafChunk,
                    toLeafInput(node, params));
            }

            GVM::Core::Task<PCGForestSample::ChunkGeometryPack> children[4];
            for (uint32_t c = 0u; c < 4u; ++c)
            {
                children[c] = subGraph.add(
                    EvaluateQuadtreeNodeSubgraph{nestedSubgraphCount},
                    makeChildNodeSpec(node, c),
                    params);
            }
            auto m01 = subGraph.add(PCGForestSample::mergeChunkGeometry, children[0], children[1]);
            auto m23 = subGraph.add(PCGForestSample::mergeChunkGeometry, children[2], children[3]);
            return subGraph.add(PCGForestSample::mergeChunkGeometry, m01, m23);
        }
    };

    // =========================================================================
    // Unified Outer Graph Submission (Windowed & Headless)
    // =========================================================================

    PCGForestSample::ForestFrameSummary executeSwapchainFrame(
        RuntimeBundle &bundle,
        const GVM::RHI::SwapchainQueryResult &swapchainTarget,
        const PCGForestSample::PCGForestParams &params)
    {
        auto nestedCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        PCGForestSample::ForestSwapchainRequest req;
        req.swapchainTarget = swapchainTarget;
        req.params = params;

        auto execution = bundle.runtime->submit(
            [nestedCounter](const PCGForestSample::ForestSwapchainRequest &r)
            {
                GVM::Core::TaskGraph graph;
                auto windLut = graph.add(PCGForestSample::BakeEnvironmentLutNode{}, r.params);

                GVM::Core::Task<PCGForestSample::ChunkGeometryPack> cpuQuads[4];
                GVM::Core::Task<PCGForestSample::QuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(
                        EvaluateQuadtreeNodeSubgraph{nestedCounter},
                        makeQuadrantRootSpec(q, r.params),
                        r.params);
                    gpuQuads[q] = graph.add(
                        PCGForestSample::PrepareQuadrantGpuNode{},
                        r.params,
                        windLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    [nestedCounter](
                        PCGForestSample::ChunkGeometryPack q0,
                        PCGForestSample::ChunkGeometryPack q1,
                        PCGForestSample::ChunkGeometryPack q2,
                        PCGForestSample::ChunkGeometryPack q3)
                    {
                        return PCGForestSample::summarizeQuadrants(
                            q0, q1, q2, q3, nestedCounter->load(std::memory_order_acquire));
                    },
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);

                auto sceneColor = graph.add(
                    PCGForestSample::RenderPCGForestSceneNode{},
                    r.params,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto presented = graph.add(
                    PCGForestSample::PresentForestSwapchainNode{},
                    r.swapchainTarget,
                    sceneColor);
                return graph.add(PCGForestSample::finalizeSwapchainSummary, summary, presented);
            },
            req);

        return awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test08 PCG Forest TaskGraph frame timed out.");
    }

    PCGForestSample::ForestBenchmarkResult executeBenchmarkFrame(
        RuntimeBundle &bundle,
        const PCGForestSample::PCGForestParams &params)
    {
        auto nestedCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        auto execution = bundle.runtime->submit(
            [nestedCounter](const PCGForestSample::PCGForestParams &p)
            {
                GVM::Core::TaskGraph graph;
                auto windLut = graph.add(PCGForestSample::BakeEnvironmentLutNode{}, p);

                GVM::Core::Task<PCGForestSample::ChunkGeometryPack> cpuQuads[4];
                GVM::Core::Task<PCGForestSample::QuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(
                        EvaluateQuadtreeNodeSubgraph{nestedCounter},
                        makeQuadrantRootSpec(q, p),
                        p);
                    gpuQuads[q] = graph.add(
                        PCGForestSample::PrepareQuadrantGpuNode{},
                        p,
                        windLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    [nestedCounter](
                        PCGForestSample::ChunkGeometryPack q0,
                        PCGForestSample::ChunkGeometryPack q1,
                        PCGForestSample::ChunkGeometryPack q2,
                        PCGForestSample::ChunkGeometryPack q3)
                    {
                        return PCGForestSample::summarizeQuadrants(
                            q0, q1, q2, q3, nestedCounter->load(std::memory_order_acquire));
                    },
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);

                auto sceneColor = graph.add(
                    PCGForestSample::RenderPCGForestSceneNode{},
                    p,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(PCGForestSample::evaluateBenchmarkPixels, summary, pixels);
            },
            params);

        return awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test08 benchmark execution failed or timed out.");
    }

    struct ForestCaptureFrame
    {
        PCGForestSample::ForestFrameSummary summary;
        eastl::vector<uint8_t> pixels;
    };

    ForestCaptureFrame executeCaptureFrame(
        RuntimeBundle &bundle,
        const PCGForestSample::PCGForestParams &params)
    {
        auto combineCapture = [](
            PCGForestSample::ForestFrameSummary summary,
            eastl::vector<uint8_t> pixels)
        {
            ForestCaptureFrame frame;
            frame.summary = summary;
            frame.pixels = std::move(pixels);
            return frame;
        };

        auto nestedCounter = std::make_shared<std::atomic<uint32_t>>(0u);
        auto execution = bundle.runtime->submit(
            [nestedCounter, combineCapture](const PCGForestSample::PCGForestParams &p)
            {
                GVM::Core::TaskGraph graph;
                auto windLut = graph.add(PCGForestSample::BakeEnvironmentLutNode{}, p);

                GVM::Core::Task<PCGForestSample::ChunkGeometryPack> cpuQuads[4];
                GVM::Core::Task<PCGForestSample::QuadrantGpuPack> gpuQuads[4];
                for (uint32_t q = 0u; q < 4u; ++q)
                {
                    cpuQuads[q] = graph.add(
                        EvaluateQuadtreeNodeSubgraph{nestedCounter},
                        makeQuadrantRootSpec(q, p),
                        p);
                    gpuQuads[q] = graph.add(
                        PCGForestSample::PrepareQuadrantGpuNode{},
                        p,
                        windLut,
                        cpuQuads[q]);
                }

                auto summary = graph.add(
                    [nestedCounter](
                        PCGForestSample::ChunkGeometryPack q0,
                        PCGForestSample::ChunkGeometryPack q1,
                        PCGForestSample::ChunkGeometryPack q2,
                        PCGForestSample::ChunkGeometryPack q3)
                    {
                        return PCGForestSample::summarizeQuadrants(
                            q0, q1, q2, q3, nestedCounter->load(std::memory_order_acquire));
                    },
                    cpuQuads[0], cpuQuads[1], cpuQuads[2], cpuQuads[3]);

                auto sceneColor = graph.add(
                    PCGForestSample::RenderPCGForestSceneNode{},
                    p,
                    gpuQuads[0], gpuQuads[1], gpuQuads[2], gpuQuads[3]);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(combineCapture, summary, pixels);
            },
            params);

        return awaitExecutionOrThrow(
            execution,
            30'000'000'000ull,
            "Test08 showcase capture execution failed or timed out.");
    }
} // namespace

namespace GVM::Samples
{
    struct Test08PCGForestTaskGraphApp::Impl
    {
        uint32_t parallelWorkers = resolveParallelWorkerCount(0u);
        PCGExecutorMode activeMode = kHasUpstreamMarl
            ? PCGExecutorMode::MarlFibers
            : PCGExecutorMode::ThreadPool;
        bool enableTimestampFeedback = true;
        eastl::unique_ptr<RuntimeBundle> bundle;

        PCGForestSample::PCGForestParams pcgParams;
        PCGForestSample::ForestFrameSummary lastSummary;

        float orbitYaw = 0.32f;
        float orbitPitch = 0.42f;
        float orbitDistance = 96.0f;
        bool autoOrbit = true;
        bool draggingMouse = false;
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

        double smoothedExecMs = 0.0;
        double baselineSerialMs = 0.0;
    };

    Test08PCGForestTaskGraphApp::Test08PCGForestTaskGraphApp(
        SampleWindowManager &window,
        GVM::RHI::GraphicsBackend preferredBackend)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test08 PCG Forest Adaptive Quadtree TaskGraph",
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

    Test08PCGForestTaskGraphApp::~Test08PCGForestTaskGraphApp()
    {
        mImpl = nullptr;
    }

    void Test08PCGForestTaskGraphApp::onWindowEvent(const SampleWindowEvent &event)
    {
        if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
        {
            mImpl->draggingMouse = false;
            return;
        }
        if (event.type == SampleWindowEvent::Type::PointerDown)
        {
            mImpl->draggingMouse = true;
            mImpl->lastMouseX = event.x;
            mImpl->lastMouseY = event.y;
        }
        else if (event.type == SampleWindowEvent::Type::PointerUp)
        {
            mImpl->draggingMouse = false;
        }
        else if (event.type == SampleWindowEvent::Type::PointerMove && mImpl->draggingMouse)
        {
            const float dx = event.x - mImpl->lastMouseX;
            const float dy = event.y - mImpl->lastMouseY;
            mImpl->lastMouseX = event.x;
            mImpl->lastMouseY = event.y;
            mImpl->orbitYaw += dx * 0.006f;
            mImpl->orbitPitch = std::clamp(mImpl->orbitPitch + dy * 0.005f, 0.12f, 1.28f);
        }
    }

    void Test08PCGForestTaskGraphApp::updateFrame(const SampleFrameContext &context)
    {
        mImpl->pcgParams.timeSeconds += context.deltaSeconds;
        if (mImpl->autoOrbit && !mImpl->draggingMouse)
        {
            mImpl->orbitYaw += context.deltaSeconds * 0.16f;
        }
        const uint32_t w = context.drawableWidth > 0u ? context.drawableWidth : 1u;
        const uint32_t h = context.drawableHeight > 0u ? context.drawableHeight : 1u;
        mImpl->pcgParams.extent = glm::uvec2(w, h);
        populateOrbitCamera(
            mImpl->pcgParams,
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

    void Test08PCGForestTaskGraphApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 126.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.80f);
        if (ImGui::Begin("PCG Forest Adaptive Quadtree TaskGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int modeIdx = static_cast<int>(mImpl->activeMode);
            ImGui::TextUnformatted("Executor Backend:");
            if (ImGui::RadioButton("Serial (1 Worker, Recursive Nested)", modeIdx == 0))
            {
                mImpl->activeMode = PCGExecutorMode::Serial;
            }
            if (ImGui::RadioButton("ThreadExecutor Pool (Recursive Nested)", modeIdx == 1))
            {
                mImpl->activeMode = PCGExecutorMode::ThreadPool;
            }
            if (kHasUpstreamMarl)
            {
                if (ImGui::RadioButton("MarlTaskExecutor (Recursive Nested)", modeIdx == 2))
                {
                    mImpl->activeMode = PCGExecutorMode::MarlFibers;
                }
            }
            else
            {
                ImGui::BeginDisabled();
                ImGui::RadioButton("MarlTaskExecutor (build with -DGVM_ENABLE_UPSTREAM_MARL=ON)", false);
                ImGui::EndDisabled();
            }
            ImGui::Checkbox("GPU Pass Timestamp Feedback", &mImpl->enableTimestampFeedback);
            ImGui::Checkbox("Auto-Orbit Camera (Drag LMB to Orbit)", &mImpl->autoOrbit);
            ImGui::SliderFloat("Camera Distance", &mImpl->orbitDistance, 45.0f, 195.0f, "%.1f m");

            ImGui::Separator();
            ImGui::TextUnformatted("Adaptive Quadtree LOD:");
            bool visLod = mImpl->pcgParams.visualizeLodLevels != 0u;
            if (ImGui::Checkbox("Visualize LOD Depth Tint", &visLod))
            {
                mImpl->pcgParams.visualizeLodLevels = visLod ? 1u : 0u;
            }
            int maxDepth = static_cast<int>(mImpl->pcgParams.maxQuadtreeDepth);
            if (ImGui::SliderInt("Max Quadtree Depth", &maxDepth, 1, 3))
            {
                mImpl->pcgParams.maxQuadtreeDepth = static_cast<uint32_t>(maxDepth);
            }
            ImGui::SliderFloat("Roughness Split Threshold", &mImpl->pcgParams.roughnessSplitThreshold, 0.80f, 6.00f, "%.2f");
            ImGui::SliderFloat("LOD Distance Scale", &mImpl->pcgParams.lodDistanceScale, 0.40f, 2.00f, "%.2fx");

            ImGui::Separator();
            int seedVal = static_cast<int>(mImpl->pcgParams.seed);
            if (ImGui::SliderInt("PCG World Seed", &seedVal, 1, 9999))
            {
                mImpl->pcgParams.seed = static_cast<uint32_t>(seedVal);
            }
            int subdivs = static_cast<int>(mImpl->pcgParams.terrainSubdivisions);
            if (ImGui::SliderInt("Terrain Grid / Leaf", &subdivs, 12, 40))
            {
                mImpl->pcgParams.terrainSubdivisions = static_cast<uint32_t>(subdivs);
            }
            int blades = static_cast<int>(mImpl->pcgParams.grassBladesPerCluster);
            if (ImGui::SliderInt("Grass Blades / Cluster", &blades, 1, 9))
            {
                mImpl->pcgParams.grassBladesPerCluster = static_cast<uint32_t>(blades);
            }
            ImGui::SliderFloat("Trail Curve Amplitude", &mImpl->pcgParams.pathAmplitude, 0.0f, 28.0f, "%.1f m");
            ImGui::SliderFloat("Wind Strength", &mImpl->pcgParams.windStrength, 0.0f, 2.5f, "%.2f");

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
            ImGui::Text("Quadtree Leaves: %u (LOD1: %u | LOD2: %u | LOD3: %u)",
                mImpl->lastSummary.chunkCount,
                mImpl->lastSummary.lodLeafCounts[1],
                mImpl->lastSummary.lodLeafCounts[2],
                mImpl->lastSummary.lodLeafCounts[3]);
            ImGui::Text("Nested Subgraphs (Runtime Submit): %u", mImpl->lastSummary.nestedSubgraphCount);
            ImGui::Text("Terrain Triangles: %u", mImpl->lastSummary.terrainTriangleCount);
            ImGui::Text("Trees: %u | Rocks: %u | Grass Blades: %u",
                mImpl->lastSummary.treeCount,
                mImpl->lastSummary.rockCount,
                mImpl->lastSummary.grassBladeCount);

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

    void Test08PCGForestTaskGraphApp::renderSampleFrame()
    {
        auto nextTexture = swapchain()->queryNextTexture();
        const auto t0 = std::chrono::steady_clock::now();
        const auto summary = executeSwapchainFrame(*mImpl->bundle, nextTexture, mImpl->pcgParams);
        const auto t1 = std::chrono::steady_clock::now();
        const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        mImpl->smoothedExecMs = mImpl->smoothedExecMs <= 0.0
            ? execMs
            : (0.85 * mImpl->smoothedExecMs + 0.15 * execMs);
        if (mImpl->bundle->mode == PCGExecutorMode::Serial)
        {
            mImpl->baselineSerialMs = mImpl->smoothedExecMs;
        }

        mImpl->lastSummary = summary;
        swapchain()->present();
    }

    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test08PCGForestTaskGraphApp>(window);
    }

    int runPCGForestHeadlessBenchmark(
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
            std::fprintf(stderr, "[Test08 Benchmark] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test08 Benchmark] Failed to create RHI device.\n");
            return 1;
        }

        const char *backendStr = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ? "Metal" : "Vulkan";
        std::printf("==========================================================================================\n");
        std::printf(" GVM Test_08 Adaptive Quadtree PCG Forest + Nested TaskGraph Benchmark (%s)\n", backendStr);
        std::printf(" 240m x 240m Domain (120m->60m->30m->15m) | Skirts | UGL [[Export]] Leaf Subgraphs\n");
        std::printf(" Warmup: %u iters | Measured: %u iters | Parallel Workers: %u\n",
            warmupIterations, measuredIterations, parallelWorkers);
        std::printf("==========================================================================================\n");

        PCGForestSample::PCGForestParams params;
        params.extent = glm::uvec2(640u, 480u);
        params.terrainSubdivisions = 24u;
        params.fbmOctaves = 6u;
        params.worldHalfSize = 120.0f;
        params.minQuadtreeDepth = 1u;
        params.maxQuadtreeDepth = 3u;
        params.roughnessSplitThreshold = 3.60f;
        params.lodDistanceScale = 1.0f;
        params.visualizeLodLevels = 0u;
        params.grassCellSize = 0.80f;
        params.grassBladesPerCluster = 6u;
        params.seed = 1337u;
        params.timeSeconds = 1.25f;
        populateOrbitCamera(params, 0.32f, 0.42f, 96.0f, 640.0f / 480.0f);

        struct BenchRunSpec
        {
            const char *label;
            PCGExecutorMode mode;
            uint32_t workerOverride;
        };

        std::vector<BenchRunSpec> runs = {
            {"Serial (1 Worker, Recursive Nested)", PCGExecutorMode::Serial, 1u},
            {"ThreadExecutor (Pool, Recursive Nested)", PCGExecutorMode::ThreadPool, parallelWorkers},
        };
        if (kHasUpstreamMarl)
        {
            runs.push_back({"Marl (1 Worker, Recursive Nested)", PCGExecutorMode::MarlFibers, 1u});
            runs.push_back({"Marl (N Workers, Recursive Nested)", PCGExecutorMode::MarlFibers, parallelWorkers});
        }

        struct ModeBenchRecord
        {
            const char *label = "";
            PCGExecutorMode mode = PCGExecutorMode::Serial;
            uint32_t workers = 1u;
            double avgMs = 0.0;
            double minMs = 0.0;
            PCGForestSample::ForestBenchmarkResult result;
            eastl::vector<GVM::Core::PassProfileFeedbackEntry> passSnapshot;
        };

        std::vector<ModeBenchRecord> records;
        for (const auto &spec : runs)
        {
            RuntimeBundle bundle(*device, spec.mode, parallelWorkers, /*enableTimestamps=*/true, spec.workerOverride);
            try
            {
                for (uint32_t w = 0u; w < warmupIterations; ++w)
                {
                    (void)executeBenchmarkFrame(bundle, params);
                }

                double totalMs = 0.0;
                double bestMs = 1e9;
                PCGForestSample::ForestBenchmarkResult lastRes{};
                for (uint32_t iter = 0u; iter < measuredIterations; ++iter)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    lastRes = executeBenchmarkFrame(bundle, params);
                    const auto t1 = std::chrono::steady_clock::now();
                    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    totalMs += ms;
                    bestMs = std::min(bestMs, ms);
                }

                ModeBenchRecord rec;
                rec.label = spec.label;
                rec.mode = spec.mode;
                rec.workers = bundle.workerCount;
                rec.avgMs = totalMs / double(std::max(measuredIterations, 1u));
                rec.minMs = bestMs;
                rec.result = lastRes;
                rec.passSnapshot = bundle.runtime->passProfileSnapshot();
                records.push_back(std::move(rec));
            }
            catch (const std::exception &err)
            {
                std::fprintf(stderr, "[Test08 Benchmark] Failed (%s): %s\n", spec.label, err.what());
                return 1;
            }
        }

        const auto &ref = records.front().result;
        std::printf("Quadtree LOD: %u Leaves (LOD1=%u, LOD2=%u, LOD3=%u)\n",
            ref.summary.chunkCount,
            ref.summary.lodLeafCounts[1],
            ref.summary.lodLeafCounts[2],
            ref.summary.lodLeafCounts[3]);
        std::printf("PCG Output  : %u Terrain Tris | %u Trees | %u Rocks | %u Grass Blades\n",
            ref.summary.terrainTriangleCount,
            ref.summary.treeCount,
            ref.summary.rockCount,
            ref.summary.grassBladeCount);
        std::printf("Pixel Check : Non-Sky Pixels=%u | Deterministic Checksum=0x%08x\n",
            ref.nonSkyPixelCount,
            ref.pixelChecksum);
        std::printf("------------------------------------------------------------------------------------------\n");
        std::printf("%-38s | %7s | %8s | %9s | %9s | %8s\n",
            "Executor Mode", "Workers", "Subgraphs", "Avg (ms)", "Best (ms)", "Speedup");
        std::printf("------------------------------------------------------------------------------------------\n");

        const double serialAvg = records.front().avgMs;
        for (const auto &rec : records)
        {
            const double speedup = rec.avgMs > 0.0 ? (serialAvg / rec.avgMs) : 1.0;
            std::printf("%-38s | %7u | %9u | %9.2f | %9.2f | %7.2fx\n",
                rec.label,
                rec.workers,
                rec.result.summary.nestedSubgraphCount,
                rec.avgMs,
                rec.minMs,
                speedup);

            // Determinism, mixed-LOD, and pixel sanity checks across all execution engines.
            if (rec.result.summary.chunkCount != ref.summary.chunkCount ||
                rec.result.summary.lodLeafCounts[0] != ref.summary.lodLeafCounts[0] ||
                rec.result.summary.lodLeafCounts[1] != ref.summary.lodLeafCounts[1] ||
                rec.result.summary.lodLeafCounts[2] != ref.summary.lodLeafCounts[2] ||
                rec.result.summary.lodLeafCounts[3] != ref.summary.lodLeafCounts[3] ||
                rec.result.summary.terrainTriangleCount != ref.summary.terrainTriangleCount ||
                rec.result.summary.treeCount != ref.summary.treeCount ||
                rec.result.summary.rockCount != ref.summary.rockCount ||
                rec.result.summary.grassBladeCount != ref.summary.grassBladeCount ||
                rec.result.nonSkyPixelCount != ref.nonSkyPixelCount ||
                rec.result.pixelChecksum != ref.pixelChecksum)
            {
                std::fprintf(stderr,
                    "[Test08 Benchmark] Determinism mismatch in %s! (checksum=0x%08x vs 0x%08x, nonSky=%u vs %u)\n",
                    rec.label,
                    rec.result.pixelChecksum,
                    ref.pixelChecksum,
                    rec.result.nonSkyPixelCount,
                    ref.nonSkyPixelCount);
                return 1;
            }
            if (rec.result.summary.lodLeafCounts[1] == 0u ||
                rec.result.summary.lodLeafCounts[2] == 0u ||
                rec.result.summary.lodLeafCounts[3] == 0u)
            {
                std::fprintf(stderr,
                    "[Test08 Benchmark] Expected mixed LOD leaves across depths 1,2,3 (got %u/%u/%u)!\n",
                    rec.result.summary.lodLeafCounts[1],
                    rec.result.summary.lodLeafCounts[2],
                    rec.result.summary.lodLeafCounts[3]);
                return 1;
            }
            if (rec.result.summary.nestedSubgraphCount == 0u)
            {
                std::fprintf(stderr, "[Test08 Benchmark] Expected nestedSubgraphCount > 0 for %s!\n", rec.label);
                return 1;
            }
            if (rec.result.nonSkyPixelCount <= 100000u ||
                rec.result.summary.treeCount == 0u ||
                rec.result.summary.grassBladeCount == 0u)
            {
                std::fprintf(stderr,
                    "[Test08 Benchmark] Rendered scene validation failed (nonSky=%u, trees=%u, grass=%u)!\n",
                    rec.result.nonSkyPixelCount,
                    rec.result.summary.treeCount,
                    rec.result.summary.grassBladeCount);
                return 1;
            }
        }
        std::printf("------------------------------------------------------------------------------------------\n");

        const auto &lastSnapshot = records.back().passSnapshot;
        if (!lastSnapshot.empty())
        {
            std::printf("Learned GPU Pass Timestamp Feedback (%zu passes):\n", lastSnapshot.size());
            const size_t maxPrint = std::min<size_t>(lastSnapshot.size(), 8u);
            for (size_t i = 0; i < maxPrint; ++i)
            {
                const auto &e = lastSnapshot[i];
                std::printf("  - %-28s : %8.3f ms  (samples=%u, weight=%u)\n",
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
        std::printf("==========================================================================================\n");
        return 0;
    }

    int runPCGForestShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir,
        uint32_t workerThreads)
    {
        if (captureDir == nullptr || captureDir[0] == '\0')
        {
            std::fprintf(stderr, "[Test08 Capture] Missing --capture-dir path.\n");
            return 1;
        }

        std::error_code fsErr;
        const std::filesystem::path outDir(captureDir);
        std::filesystem::create_directories(outDir, fsErr);
        if (fsErr)
        {
            std::fprintf(stderr, "[Test08 Capture] Failed to create capture directory '%s': %s\n",
                captureDir, fsErr.message().c_str());
            return 1;
        }

        const uint32_t parallelWorkers = resolveParallelWorkerCount(workerThreads);
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::RHI::createInstance({
                .preferredBackend = preferredBackend,
                .diagnosticsOverlay = {.enabled = GVM::RHI::False},
            }),
            GVM::RHI::destroyInstance);
        if (instance == nullptr)
        {
            std::fprintf(stderr, "[Test08 Capture] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test08 Capture] Failed to create RHI device.\n");
            return 1;
        }

        const PCGExecutorMode execMode = kHasUpstreamMarl
            ? PCGExecutorMode::MarlFibers
            : PCGExecutorMode::ThreadPool;
        RuntimeBundle bundle(*device, execMode, parallelWorkers, /*enableTimestamps=*/true, parallelWorkers);

        constexpr uint32_t kWidth = 1280u;
        constexpr uint32_t kHeight = 800u;
        constexpr float kAspect = float(kWidth) / float(kHeight);

        struct ShowcaseShot
        {
            const char *filename;
            const char *title;
            glm::vec3 target;
            float yaw;
            float pitch;
            float distance;
            float lodDistanceScale;
            uint32_t visualizeLod;
        };

        const ShowcaseShot shots[2] = {
            {
                "test08_01_forest_road_vista.png",
                "Forest Road Vista",
                glm::vec3(2.0f, 5.0f, 16.0f),
                0.24f,
                0.22f,
                64.0f,
                1.0f,
                0u,
            },
            {
                "test08_02_quadtree_lod_heatmap.png",
                "Quadtree LOD Heatmap",
                glm::vec3(2.0f, 2.0f, 6.0f),
                0.38f,
                0.56f,
                82.0f,
                1.50f,
                1u,
            },
        };

        const char *backendStr = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ? "Metal" : "Vulkan";
        std::printf("[Test08 Capture] Backend=%s | Engine=%s (%u workers) | Resolution=%ux%u\n",
            backendStr, executorModeLabel(bundle.mode), bundle.workerCount, kWidth, kHeight);

        for (const auto &shot : shots)
        {
            PCGForestSample::PCGForestParams params;
            params.extent = glm::uvec2(kWidth, kHeight);
            params.terrainSubdivisions = 24u;
            params.fbmOctaves = 6u;
            params.worldHalfSize = 120.0f;
            params.minQuadtreeDepth = 1u;
            params.maxQuadtreeDepth = 3u;
            params.roughnessSplitThreshold = 3.60f;
            params.lodDistanceScale = shot.lodDistanceScale;
            params.visualizeLodLevels = shot.visualizeLod;
            params.grassCellSize = 0.80f;
            params.grassBladesPerCluster = 6u;
            params.seed = 1337u;
            params.timeSeconds = 1.25f;
            populateOrbitCamera(params, shot.yaw, shot.pitch, shot.distance, kAspect, shot.target);

            const auto t0 = std::chrono::steady_clock::now();
            const ForestCaptureFrame frame = executeCaptureFrame(bundle, params);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            const size_t expectedBytes = size_t(kWidth) * size_t(kHeight) * 4u;
            if (frame.pixels.size() != expectedBytes)
            {
                std::fprintf(stderr,
                    "[Test08 Capture] Unexpected readback buffer size for '%s': got %zu, expected %zu\n",
                    shot.filename, frame.pixels.size(), expectedBytes);
                return 1;
            }

            const std::filesystem::path filePath = outDir / shot.filename;
            const std::string filePathStr = filePath.string();
            if (!writeRgba8Png(filePathStr.c_str(), kWidth, kHeight, frame.pixels.data()))
            {
                std::fprintf(stderr, "[Test08 Capture] Failed to write PNG '%s'\n", filePathStr.c_str());
                return 1;
            }

            std::printf(
                "[Test08 Capture] Saved %s (%s) in %.2f ms | Leaves=%u (LOD1=%u, LOD2=%u, LOD3=%u) | "
                "Subgraphs=%u | TerrainTris=%u | Trees=%u | Rocks=%u | Grass=%u\n",
                filePathStr.c_str(),
                shot.title,
                ms,
                frame.summary.chunkCount,
                frame.summary.lodLeafCounts[1],
                frame.summary.lodLeafCounts[2],
                frame.summary.lodLeafCounts[3],
                frame.summary.nestedSubgraphCount,
                frame.summary.terrainTriangleCount,
                frame.summary.treeCount,
                frame.summary.rockCount,
                frame.summary.grassBladeCount);
        }

        return 0;
    }
} // namespace GVM::Samples
