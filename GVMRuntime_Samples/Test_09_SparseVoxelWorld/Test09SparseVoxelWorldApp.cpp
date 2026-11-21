#include "Test09SparseVoxelWorldApp.hpp"

#include <GVMCore/Public/TaskRuntime.hpp>
#include <GVMCore/Public/ThreadExecutor.hpp>

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
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

    /// Populates exact LH [0, 1] View-Projection rows for an explicit eye and target look-at camera.
    void populateVoxelLookAtCamera(
        SparseVoxelSample::SparseVoxelParams &params,
        const glm::vec3 &eye,
        const glm::vec3 &target,
        float aspect,
        float fovDeg = 48.0f)
    {
        const float safeAspect = aspect > 0.05f ? aspect : (16.0f / 9.0f);
        const glm::vec3 forward = glm::normalize(target - eye);
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
        const glm::vec3 up = glm::cross(forward, right);

        const float fovY = fovDeg * (3.14159265359f / 180.0f);
        constexpr float kZNear = 0.4f;
        constexpr float kZFar = 280.0f;
        const float yScale = 1.0f / std::tan(fovY * 0.5f);
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
        params.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.45f, 0.78f, -0.43f)), 0.0f);
    }

    /// Populates exact LH [0, 1] View-Projection rows for world-to-clip dot-product projection.
    void populateVoxelOrbitCamera(
        SparseVoxelSample::SparseVoxelParams &params,
        float orbitYaw,
        float orbitPitch,
        float orbitDistance,
        float aspect)
    {
        const glm::vec3 target(
            params.streamingCenterXZ.x,
            3.2f,
            params.streamingCenterXZ.y - 2.0f);
        const float cosPitch = std::cos(orbitPitch);
        const glm::vec3 eye = target + glm::vec3(
            std::sin(orbitYaw) * cosPitch * orbitDistance,
            std::sin(orbitPitch) * orbitDistance,
            -std::cos(orbitYaw) * cosPitch * orbitDistance);
        populateVoxelLookAtCamera(params, eye, target, aspect);
    }

    uint32_t pngCrc32Update(uint32_t crc, const uint8_t *data, size_t len)
    {
        uint32_t c = ~crc;
        for (size_t i = 0; i < len; ++i)
        {
            c ^= static_cast<uint32_t>(data[i]);
            for (int bit = 0; bit < 8; ++bit)
            {
                c = (c >> 1u) ^ ((c & 1u) ? 0xEDB88320u : 0u);
            }
        }
        return ~c;
    }

    uint32_t pngAdler32Compute(const uint8_t *data, size_t len)
    {
        constexpr uint32_t kModAdler = 65521u;
        uint32_t s1 = 1u;
        uint32_t s2 = 0u;
        for (size_t i = 0; i < len; ++i)
        {
            s1 = (s1 + data[i]) % kModAdler;
            s2 = (s2 + s1) % kModAdler;
        }
        return (s2 << 16u) | s1;
    }

    void appendBe32(std::vector<uint8_t> &dst, uint32_t val)
    {
        dst.push_back(static_cast<uint8_t>((val >> 24u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>((val >> 16u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>((val >> 8u) & 0xFFu));
        dst.push_back(static_cast<uint8_t>(val & 0xFFu));
    }

    void appendPngChunk(
        std::vector<uint8_t> &dst,
        const char type[4],
        const uint8_t *payload,
        size_t len)
    {
        appendBe32(dst, static_cast<uint32_t>(len));
        const uint8_t typeBytes[4] = {
            static_cast<uint8_t>(type[0]),
            static_cast<uint8_t>(type[1]),
            static_cast<uint8_t>(type[2]),
            static_cast<uint8_t>(type[3]),
        };
        dst.insert(dst.end(), typeBytes, typeBytes + 4);
        if (len > 0 && payload != nullptr)
        {
            dst.insert(dst.end(), payload, payload + len);
        }
        uint32_t crc = pngCrc32Update(0u, typeBytes, 4);
        if (len > 0 && payload != nullptr)
        {
            crc = pngCrc32Update(crc, payload, len);
        }
        appendBe32(dst, crc);
    }

    /// Self-contained lossless RGBA8 PNG writer (PNG signature + IHDR + uncompressed Deflate BTYPE=00 IDAT + Adler-32 + CRC-32 + IEND).
    bool writeRgba8Png(const char *path, uint32_t width, uint32_t height, const uint8_t *rgba)
    {
        if (path == nullptr || width == 0u || height == 0u || rgba == nullptr)
        {
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4u;
        const size_t scanlineStride = 1u + rowBytes;
        const size_t rawSize = static_cast<size_t>(height) * scanlineStride;

        std::vector<uint8_t> rawScanlines(rawSize);
        for (uint32_t y = 0u; y < height; ++y)
        {
            uint8_t *dstRow = rawScanlines.data() + static_cast<size_t>(y) * scanlineStride;
            dstRow[0] = 0u; // PNG filter type 0 (None)
            std::memcpy(dstRow + 1u, rgba + static_cast<size_t>(y) * rowBytes, rowBytes);
        }

        // Build zlib stream with uncompressed Deflate stored blocks (BTYPE=00, up to 65535 bytes each).
        const size_t maxBlockSize = 65535u;
        const size_t numBlocks = (rawSize + maxBlockSize - 1u) / maxBlockSize;
        std::vector<uint8_t> zlibStream;
        zlibStream.reserve(2u + rawSize + numBlocks * 5u + 4u);
        zlibStream.push_back(0x78u); // CMF: Deflate, 32KB window
        zlibStream.push_back(0x01u); // FLG: FCHECK valid ((0x78*256 + 0x01) % 31 == 0), no dict, fastest

        size_t offset = 0u;
        while (offset < rawSize)
        {
            const size_t chunkLen = std::min<size_t>(maxBlockSize, rawSize - offset);
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
        appendBe32(zlibStream, pngAdler32Compute(rawScanlines.data(), rawScanlines.size()));

        std::vector<uint8_t> png;
        png.reserve(8u + 25u + 12u + zlibStream.size() + 12u);
        static constexpr uint8_t kPngSignature[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
        png.insert(png.end(), kPngSignature, kPngSignature + 8);

        std::vector<uint8_t> ihdr;
        ihdr.reserve(13u);
        appendBe32(ihdr, width);
        appendBe32(ihdr, height);
        ihdr.push_back(8u); // bit depth
        ihdr.push_back(6u); // color type RGBA
        ihdr.push_back(0u); // compression method
        ihdr.push_back(0u); // filter method
        ihdr.push_back(0u); // interlace method
        appendPngChunk(png, "IHDR", ihdr.data(), ihdr.size());
        appendPngChunk(png, "IDAT", zlibStream.data(), zlibStream.size());
        appendPngChunk(png, "IEND", nullptr, 0u);

        std::FILE *fp = std::fopen(path, "wb");
        if (fp == nullptr)
        {
            return false;
        }
        const size_t written = std::fwrite(png.data(), 1u, png.size(), fp);
        const int closed = std::fclose(fp);
        return written == png.size() && closed == 0;
    }

    struct RuntimeBundle
    {
        GVM::Samples::SparseVoxelExecutorMode mode = GVM::Samples::SparseVoxelExecutorMode::ThreadPool;
        uint32_t workerCount = 1u;
        bool timestampFeedback = true;

        eastl::unique_ptr<GVM::Core::ThreadExecutor> threadExecutor;
        OptionalMarlAdapter marlAdapter;
        eastl::unique_ptr<GVM::Core::TaskRuntime> runtime;

        RuntimeBundle(
            GVM::RHI::DeviceImpl &device,
            GVM::Samples::SparseVoxelExecutorMode execMode,
            uint32_t parallelWorkers,
            bool enableTimestamps)
            : mode(execMode)
            , timestampFeedback(enableTimestamps)
        {
            GVM::Core::TaskRuntimeDescriptor desc;
            desc.enablePassTimestampFeedback = enableTimestamps;
            desc.maximumActiveExecutions = 64u;
            desc.maximumTasksPerExecution = 65536u;
            desc.maximumPassesPerExecution = 1024u;

            if (execMode == GVM::Samples::SparseVoxelExecutorMode::Serial)
            {
                workerCount = 1u;
                threadExecutor = eastl::make_unique<GVM::Core::ThreadExecutor>(1u);
                runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *threadExecutor, desc);
                return;
            }

            if (execMode == GVM::Samples::SparseVoxelExecutorMode::MarlFibers)
            {
                if (auto *marlExec = marlAdapter.init(parallelWorkers))
                {
                    workerCount = parallelWorkers;
                    runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *marlExec, desc);
                    return;
                }
            }

            workerCount = parallelWorkers;
            threadExecutor = eastl::make_unique<GVM::Core::ThreadExecutor>(parallelWorkers);
            runtime = eastl::make_unique<GVM::Core::TaskRuntime>(device, *threadExecutor, desc);
        }

        ~RuntimeBundle()
        {
            runtime = nullptr;
            marlAdapter.reset();
            threadExecutor = nullptr;
        }
    };

    const char *executorModeLabel(GVM::Samples::SparseVoxelExecutorMode mode)
    {
        switch (mode)
        {
        case GVM::Samples::SparseVoxelExecutorMode::Serial: return "Serial (ThreadExecutor x1)";
        case GVM::Samples::SparseVoxelExecutorMode::ThreadPool: return "ThreadExecutor (Parallel Pool)";
        case GVM::Samples::SparseVoxelExecutorMode::MarlFibers:
            return kHasUpstreamMarl
                ? "MarlTaskExecutor (google/marl Fibers)"
                : "MarlTaskExecutor (Fallback -> ThreadExecutor)";
        }
        return "Unknown";
    }

    struct SnappedGridKey
    {
        int32_t cx = 0;
        int32_t cz = 0;

        bool operator==(const SnappedGridKey &other) const noexcept
        {
            return cx == other.cx && cz == other.cz;
        }

        bool operator!=(const SnappedGridKey &other) const noexcept
        {
            return !(*this == other);
        }
    };

    inline SnappedGridKey computeSnappedGridKey(const SparseVoxelSample::SparseVoxelParams &params)
    {
        const float vs = SparseVoxelSample::svMax(params.voxelSize, 0.05f);
        const float clusterWorldSpan = vs * float(SparseVoxelSample::kClusterDim);
        return {
            SparseVoxelSample::svFloorToInt(params.streamingCenterXZ.x / clusterWorldSpan),
            SparseVoxelSample::svFloorToInt(params.streamingCenterXZ.y / clusterWorldSpan),
        };
    }

    struct WorldClusterKey
    {
        int32_t wcx = 0;
        int32_t wcy = 0;
        int32_t wcz = 0;

        bool operator==(const WorldClusterKey &other) const noexcept
        {
            return wcx == other.wcx && wcy == other.wcy && wcz == other.wcz;
        }
    };

    struct WorldClusterKeyHash
    {
        size_t operator()(const WorldClusterKey &k) const noexcept
        {
            uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.wcx)) * 0x9e3779b185ebca87ull;
            h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.wcy)) * 0xc2b2ae3d27d4eb4full;
            h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.wcz)) * 0x165667b19e3779f9ull;
            h ^= h >> 33u;
            h *= 0xff51afd7ed558ccdull;
            h ^= h >> 33u;
            return static_cast<size_t>(h);
        }
    };

    inline WorldClusterKey makeWorldClusterKey(
        const SnappedGridKey &snapped,
        const SparseVoxelSample::OctreeNodeSpec &clusterNode)
    {
        constexpr int32_t kHalfGridClustersX =
            static_cast<int32_t>((SparseVoxelSample::kGridDimX / 2u) / SparseVoxelSample::kClusterDim);
        constexpr int32_t kHalfGridClustersZ =
            static_cast<int32_t>((SparseVoxelSample::kGridDimZ / 2u) / SparseVoxelSample::kClusterDim);
        return {
            snapped.cx + static_cast<int32_t>(clusterNode.gx / SparseVoxelSample::kClusterDim) - kHalfGridClustersX,
            static_cast<int32_t>(clusterNode.gy / SparseVoxelSample::kClusterDim),
            snapped.cz + static_cast<int32_t>(clusterNode.gz / SparseVoxelSample::kClusterDim) - kHalfGridClustersZ,
        };
    }

    struct WorldClusterCache
    {
        static constexpr size_t kMaxEntries = 4096u;
        mutable std::mutex mutex;
        std::unordered_map<WorldClusterKey, SparseVoxelSample::VoxelQuadrantPack, WorldClusterKeyHash> entries;
        std::deque<WorldClusterKey> insertionOrder;

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex);
            entries.clear();
            insertionOrder.clear();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return entries.size();
        }

        bool tryGet(const WorldClusterKey &key, SparseVoxelSample::VoxelQuadrantPack &outPack) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = entries.find(key);
            if (it == entries.end())
            {
                return false;
            }
            outPack = it->second;
            return true;
        }

        void insert(const WorldClusterKey &key, const SparseVoxelSample::VoxelQuadrantPack &pack)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (entries.find(key) != entries.end())
            {
                return;
            }
            while (entries.size() >= kMaxEntries && !insertionOrder.empty())
            {
                const WorldClusterKey oldest = insertionOrder.front();
                insertionOrder.pop_front();
                entries.erase(oldest);
            }
            entries.emplace(key, pack);
            insertionOrder.push_back(key);
        }
    };

    struct OctreeQuadrantResult
    {
        SparseVoxelSample::VoxelQuadrantPack pack;
        uint32_t interiorCount = 0u;
    };

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

    /// Portable non-blocking recursive Sparse Voxel Octree subgraph builder.
    /// Executes identically on Serial (ThreadExecutor x1), ThreadExecutor Pool, and MarlTaskExecutor
    /// via native continuation-based nested TaskGraph execution and eastl::vector<Task<T>> fan-in.
    struct EvaluateOctreeNodeSubgraph
    {
        std::shared_ptr<WorldClusterCache> clusterCache;
        SnappedGridKey snappedKey{};
        std::shared_ptr<std::atomic<uint32_t>> frontierBuiltCounter;

        GVM::Core::Task<OctreeQuadrantResult> operator()(
            GVM::Core::TaskGraph &subGraph,
            SparseVoxelSample::OctreeNodeSpec node,
            SparseVoxelSample::SparseVoxelParams params) const
        {
            const float vs = SparseVoxelSample::svMax(params.voxelSize, 0.05f);
            const glm::vec3 bMin = SparseVoxelSample::nodeBoxMin(node, params) - glm::vec3(vs);
            const glm::vec3 bMax = SparseVoxelSample::nodeBoxMax(node, params) + glm::vec3(vs);
            const auto tileLandmarks = SparseVoxelSample::gatherLandmarksForXZBox(bMin, bMax, params);
            const uint32_t interval = SparseVoxelSample::classifyBoundsInterval(bMin, bMax, params, tileLandmarks);

            if (interval != SparseVoxelSample::kIntervalMixedActive)
            {
                const uint32_t pruned = SparseVoxelSample::countBricksInOctreeNode(node.sizeVoxels);
                return subGraph.add([pruned]()
                {
                    OctreeQuadrantResult out;
                    out.pack.prunedBrickCount = pruned;
                    return out;
                });
            }

            if (node.depth >= 3u)
            {
                const WorldClusterKey key = makeWorldClusterKey(snappedKey, node);
                if (clusterCache != nullptr)
                {
                    SparseVoxelSample::VoxelQuadrantPack hitPack;
                    if (clusterCache->tryGet(key, hitPack))
                    {
                        hitPack.nestedSubgraphCount = 0u;
                        return subGraph.add([hitPack = std::move(hitPack)]() mutable
                        {
                            OctreeQuadrantResult out;
                            out.pack = std::move(hitPack);
                            return out;
                        });
                    }
                }

                SparseVoxelSample::VoxelClusterInput clusterIn;
                clusterIn.params = params;
                clusterIn.clusterNode = node;
                auto packTask = subGraph.add(SparseVoxelSample::Entries::buildVoxelBrickCluster, clusterIn);
                return subGraph.add(
                    [cache = clusterCache, key, counter = frontierBuiltCounter](
                        SparseVoxelSample::VoxelQuadrantPack pack)
                    {
                        if (cache != nullptr)
                        {
                            cache->insert(key, pack);
                            if (counter != nullptr)
                            {
                                counter->fetch_add(1u, std::memory_order_relaxed);
                            }
                        }
                        OctreeQuadrantResult out;
                        out.pack = std::move(pack);
                        return out;
                    },
                    packTask);
            }

            eastl::vector<GVM::Core::Task<OctreeQuadrantResult>> childTasks;
            childTasks.reserve(8u);
            uint32_t prunedHere = 0u;

            for (uint32_t octant = 0u; octant < 8u; ++octant)
            {
                const auto child = SparseVoxelSample::makeOctreeChildSpec(node, octant);
                const glm::vec3 cMin = SparseVoxelSample::nodeBoxMin(child, params) - glm::vec3(vs);
                const glm::vec3 cMax = SparseVoxelSample::nodeBoxMax(child, params) + glm::vec3(vs);
                const uint32_t cState = SparseVoxelSample::classifyBoundsInterval(cMin, cMax, params, tileLandmarks);

                if (cState != SparseVoxelSample::kIntervalMixedActive)
                {
                    prunedHere += SparseVoxelSample::countBricksInOctreeNode(child.sizeVoxels);
                    continue;
                }

                if (child.depth >= 3u)
                {
                    const WorldClusterKey key = makeWorldClusterKey(snappedKey, child);
                    if (clusterCache != nullptr)
                    {
                        SparseVoxelSample::VoxelQuadrantPack hitPack;
                        if (clusterCache->tryGet(key, hitPack))
                        {
                            hitPack.nestedSubgraphCount = 0u;
                            childTasks.push_back(subGraph.add([hitPack = std::move(hitPack)]() mutable
                            {
                                OctreeQuadrantResult r;
                                r.pack = std::move(hitPack);
                                return r;
                            }));
                            continue;
                        }
                    }

                    SparseVoxelSample::VoxelClusterInput clusterIn;
                    clusterIn.params = params;
                    clusterIn.clusterNode = child;
                    auto packTask = subGraph.add(SparseVoxelSample::Entries::buildVoxelBrickCluster, clusterIn);
                    childTasks.push_back(subGraph.add(
                        [cache = clusterCache, key, counter = frontierBuiltCounter](
                            SparseVoxelSample::VoxelQuadrantPack pack)
                        {
                            if (cache != nullptr)
                            {
                                cache->insert(key, pack);
                                if (counter != nullptr)
                                {
                                    counter->fetch_add(1u, std::memory_order_relaxed);
                                }
                            }
                            OctreeQuadrantResult r;
                            r.pack = std::move(pack);
                            return r;
                        },
                        packTask));
                }
                else
                {
                    childTasks.push_back(subGraph.add(
                        EvaluateOctreeNodeSubgraph{clusterCache, snappedKey, frontierBuiltCounter},
                        child,
                        params));
                }
            }

            if (childTasks.empty())
            {
                return subGraph.add([prunedHere]()
                {
                    OctreeQuadrantResult out;
                    out.interiorCount = 1u;
                    out.pack.prunedBrickCount = prunedHere;
                    return out;
                });
            }

            return subGraph.add(
                [prunedHere](eastl::vector<OctreeQuadrantResult> children)
                {
                    OctreeQuadrantResult out;
                    out.interiorCount = 1u;
                    for (auto &c : children)
                    {
                        out.pack = SparseVoxelSample::mergeVoxelBrickMeshes(out.pack, c.pack);
                        out.interiorCount += c.interiorCount;
                    }
                    out.pack.prunedBrickCount += prunedHere;
                    return out;
                },
                std::move(childTasks));
        }
    };

    struct UnifiedSwapchainInput
    {
        GVM::RHI::SwapchainQueryResult swapchainTarget;
        SparseVoxelSample::SparseVoxelParams params;
        std::shared_ptr<WorldClusterCache> clusterCache;
        SnappedGridKey snappedKey{};
        std::shared_ptr<std::atomic<uint32_t>> frontierBuiltCounter;
    };

    struct UnifiedSwapchainFrameOutput
    {
        SparseVoxelSample::SparseVoxelFrameSummary summary;
        SparseVoxelSample::PrebuiltVoxelScene scene;
    };

    struct UnifiedCaptureOutput
    {
        SparseVoxelSample::SparseVoxelFrameSummary summary;
        eastl::vector<uint8_t> pixels;
    };

    struct UnifiedCaptureInput
    {
        SparseVoxelSample::SparseVoxelParams params;
        std::shared_ptr<WorldClusterCache> clusterCache;
        SnappedGridKey snappedKey{};
    };

    /// Launches unified outer GPU+CPU TaskGraph on any ITaskExecutor:
    /// BakeVoxelAtmosphereLutNode launches concurrently with 4 recursive quadrant sparse octree subgraphs.
    SparseVoxelSample::SparseVoxelBenchmarkResult runUnifiedBenchmarkFrame(
        GVM::Core::TaskRuntime &runtime,
        const SparseVoxelSample::SparseVoxelParams &params)
    {
        auto exec = runtime.submit(
            [](SparseVoxelSample::SparseVoxelParams inParams)
            {
                GVM::Core::TaskGraph graph;
                auto atmosLut = graph.add(SparseVoxelSample::BakeVoxelAtmosphereLutNode{}, inParams);

                EvaluateOctreeNodeSubgraph evalNode{};
                auto r0 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(0u), inParams);
                auto r1 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(1u), inParams);
                auto r2 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(2u), inParams);
                auto r3 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(3u), inParams);

                auto extractPack = [](OctreeQuadrantResult r) { return r.pack; };
                auto q0 = graph.add(extractPack, r0);
                auto q1 = graph.add(extractPack, r1);
                auto q2 = graph.add(extractPack, r2);
                auto q3 = graph.add(extractPack, r3);

                auto sumInterior = [](OctreeQuadrantResult a, OctreeQuadrantResult b, OctreeQuadrantResult c, OctreeQuadrantResult d)
                {
                    return a.interiorCount + b.interiorCount + c.interiorCount + d.interiorCount;
                };
                auto interiorCount = graph.add(sumInterior, r0, r1, r2, r3);

                auto gq0 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, inParams, atmosLut, q0);
                auto gq1 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, inParams, atmosLut, q1);
                auto gq2 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, inParams, atmosLut, q2);
                auto gq3 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, inParams, atmosLut, q3);

                auto summary = graph.add(SparseVoxelSample::summarizeVoxelQuadrants, q0, q1, q2, q3, interiorCount);
                auto sceneColor = graph.add(SparseVoxelSample::RenderSparseVoxelWorldNode{}, inParams, gq0, gq1, gq2, gq3);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(SparseVoxelSample::evaluateBenchmarkPixels, summary, pixels);
            },
            params);

        return awaitExecutionOrThrow(exec, 20'000'000'000ull, "Test09 unified TaskGraph benchmark failed or timed out.");
    }

    UnifiedCaptureOutput runUnifiedCaptureFrame(
        GVM::Core::TaskRuntime &runtime,
        const SparseVoxelSample::SparseVoxelParams &params,
        std::shared_ptr<WorldClusterCache> clusterCache = nullptr,
        SnappedGridKey snappedKey = {})
    {
        UnifiedCaptureInput outerIn{params, std::move(clusterCache), snappedKey};
        auto exec = runtime.submit(
            [](UnifiedCaptureInput in)
            {
                GVM::Core::TaskGraph graph;
                auto atmosLut = graph.add(SparseVoxelSample::BakeVoxelAtmosphereLutNode{}, in.params);

                EvaluateOctreeNodeSubgraph evalNode{in.clusterCache, in.snappedKey, nullptr};
                auto r0 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(0u), in.params);
                auto r1 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(1u), in.params);
                auto r2 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(2u), in.params);
                auto r3 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(3u), in.params);

                auto extractPack = [](OctreeQuadrantResult r) { return r.pack; };
                auto q0 = graph.add(extractPack, r0);
                auto q1 = graph.add(extractPack, r1);
                auto q2 = graph.add(extractPack, r2);
                auto q3 = graph.add(extractPack, r3);

                auto sumInterior = [](OctreeQuadrantResult a, OctreeQuadrantResult b, OctreeQuadrantResult c, OctreeQuadrantResult d)
                {
                    return a.interiorCount + b.interiorCount + c.interiorCount + d.interiorCount;
                };
                auto interiorCount = graph.add(sumInterior, r0, r1, r2, r3);

                auto gq0 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q0);
                auto gq1 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q1);
                auto gq2 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q2);
                auto gq3 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q3);

                auto summary = graph.add(SparseVoxelSample::summarizeVoxelQuadrants, q0, q1, q2, q3, interiorCount);
                auto sceneColor = graph.add(SparseVoxelSample::RenderSparseVoxelWorldNode{}, in.params, gq0, gq1, gq2, gq3);
                auto pixels = graph.add(GVM::Core::ReadbackTextureNode<uint8_t>{}, sceneColor);
                return graph.add(
                    [](SparseVoxelSample::SparseVoxelFrameSummary s, eastl::vector<uint8_t> px)
                    {
                        UnifiedCaptureOutput out;
                        out.summary = s;
                        out.pixels = std::move(px);
                        return out;
                    },
                    summary,
                    pixels);
            },
            std::move(outerIn));

        return awaitExecutionOrThrow(exec, 20'000'000'000ull, "Test09 unified TaskGraph capture failed or timed out.");
    }

    UnifiedSwapchainFrameOutput runUnifiedSwapchainFrame(
        GVM::Core::TaskRuntime &runtime,
        const GVM::RHI::SwapchainQueryResult &swapchainTarget,
        const SparseVoxelSample::SparseVoxelParams &params,
        std::shared_ptr<WorldClusterCache> clusterCache = nullptr,
        SnappedGridKey snappedKey = {},
        std::shared_ptr<std::atomic<uint32_t>> frontierBuiltCounter = nullptr)
    {
        UnifiedSwapchainInput outerIn{
            swapchainTarget,
            params,
            std::move(clusterCache),
            snappedKey,
            std::move(frontierBuiltCounter),
        };
        auto exec = runtime.submit(
            [](UnifiedSwapchainInput in)
            {
                GVM::Core::TaskGraph graph;
                auto atmosLut = graph.add(SparseVoxelSample::BakeVoxelAtmosphereLutNode{}, in.params);

                EvaluateOctreeNodeSubgraph evalNode{in.clusterCache, in.snappedKey, in.frontierBuiltCounter};
                auto r0 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(0u), in.params);
                auto r1 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(1u), in.params);
                auto r2 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(2u), in.params);
                auto r3 = graph.add(evalNode, SparseVoxelSample::makeQuadrantRootSpec(3u), in.params);

                auto extractPack = [](OctreeQuadrantResult r) { return r.pack; };
                auto q0 = graph.add(extractPack, r0);
                auto q1 = graph.add(extractPack, r1);
                auto q2 = graph.add(extractPack, r2);
                auto q3 = graph.add(extractPack, r3);

                auto sumInterior = [](OctreeQuadrantResult a, OctreeQuadrantResult b, OctreeQuadrantResult c, OctreeQuadrantResult d)
                {
                    return a.interiorCount + b.interiorCount + c.interiorCount + d.interiorCount;
                };
                auto interiorCount = graph.add(sumInterior, r0, r1, r2, r3);

                auto gq0 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q0);
                auto gq1 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q1);
                auto gq2 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q2);
                auto gq3 = graph.add(SparseVoxelSample::PrepareVoxelQuadrantGpuNode{}, in.params, atmosLut, q3);

                auto summary = graph.add(SparseVoxelSample::summarizeVoxelQuadrants, q0, q1, q2, q3, interiorCount);
                auto sceneColor = graph.add(SparseVoxelSample::RenderSparseVoxelWorldNode{}, in.params, gq0, gq1, gq2, gq3);
                auto presented = graph.add(SparseVoxelSample::PresentVoxelSwapchainNode{}, in.swapchainTarget, sceneColor);
                auto finalSummary = graph.add(SparseVoxelSample::finalizeSwapchainSummary, summary, presented);

                return graph.add(
                    [p = in.params, captureScene = (in.clusterCache != nullptr)](
                        SparseVoxelSample::SparseVoxelFrameSummary s,
                        SparseVoxelSample::VoxelQuadrantPack p0,
                        SparseVoxelSample::VoxelQuadrantPack p1,
                        SparseVoxelSample::VoxelQuadrantPack p2,
                        SparseVoxelSample::VoxelQuadrantPack p3,
                        uint32_t interiors)
                    {
                        UnifiedSwapchainFrameOutput out;
                        out.summary = s;
                        if (captureScene)
                        {
                            out.scene.params = p;
                            out.scene.q0 = std::move(p0);
                            out.scene.q1 = std::move(p1);
                            out.scene.q2 = std::move(p2);
                            out.scene.q3 = std::move(p3);
                            out.scene.octreeInteriorCount = interiors;
                        }
                        return out;
                    },
                    finalSummary,
                    q0,
                    q1,
                    q2,
                    q3,
                    interiorCount);
            },
            std::move(outerIn));

        return awaitExecutionOrThrow(exec, 20'000'000'000ull, "Test09 unified TaskGraph swapchain frame failed or timed out.");
    }
} // namespace

namespace GVM::Samples
{
    struct Test09SparseVoxelWorldApp::Impl
    {
        uint32_t parallelWorkers = resolveParallelWorkerCount(0u);
        SparseVoxelExecutorMode activeMode = kHasUpstreamMarl
            ? SparseVoxelExecutorMode::MarlFibers
            : SparseVoxelExecutorMode::ThreadPool;
        bool enableTimestampFeedback = true;
        eastl::unique_ptr<RuntimeBundle> bundle;

        SparseVoxelSample::SparseVoxelParams voxelParams;
        SparseVoxelSample::SparseVoxelFrameSummary lastSummary;

        std::shared_ptr<WorldClusterCache> clusterCache = std::make_shared<WorldClusterCache>();
        SparseVoxelSample::PrebuiltVoxelScene cachedScene;
        bool hasCachedScene = false;
        SnappedGridKey cachedSnappedKey{};
        uint32_t cachedSeed = 0u;
        float cachedVoxelSize = 0.0f;
        float cachedCanyonHeightScale = 0.0f;
        SparseVoxelExecutorMode cachedMode = SparseVoxelExecutorMode::MarlFibers;

        bool stressRebuildEveryFrame = false;
        bool forceColdRebuild = true;

        float orbitYaw = 0.44f;
        float orbitPitch = 0.34f;
        float orbitDistance = 42.0f;
        bool autoOrbit = false;
        bool autoRoamStreaming = false;
        bool draggingMouse = false;
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

        double renderFrameMs = 0.0;
        double smoothedExecMs = 0.0;
        double lastColdRebuildMs = 0.0;
        double baselineSerialMs = 0.0;
        double lastFrontierStreamMs = 0.0;
        uint32_t lastFrontierNewClusters = 0u;
        bool hasFrontierStreamed = false;
    };

    Test09SparseVoxelWorldApp::Test09SparseVoxelWorldApp(
        SampleWindowManager &window,
        GVM::RHI::GraphicsBackend preferredBackend)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test09 Pure Sparse Voxel Open-World",
                                        .preferredGraphicsBackend = preferredBackend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->bundle = eastl::make_unique<RuntimeBundle>(
            *device(),
            mImpl->activeMode,
            mImpl->parallelWorkers,
            mImpl->enableTimestampFeedback);
        mImpl->cachedMode = mImpl->activeMode;
    }

    Test09SparseVoxelWorldApp::~Test09SparseVoxelWorldApp()
    {
        mImpl = nullptr;
    }

    void Test09SparseVoxelWorldApp::onWindowEvent(const SampleWindowEvent &event)
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
            mImpl->orbitPitch = std::clamp(mImpl->orbitPitch + dy * 0.005f, 0.08f, 1.32f);
        }
    }

    void Test09SparseVoxelWorldApp::updateFrame(const SampleFrameContext &context)
    {
        mImpl->voxelParams.timeSeconds += context.deltaSeconds;
        if (mImpl->autoOrbit && !mImpl->draggingMouse)
        {
            mImpl->orbitYaw += context.deltaSeconds * 0.14f;
        }

        // Dynamic Open-World Streaming via Auto-Roam or WASD keys
        if (mImpl->autoRoamStreaming)
        {
            mImpl->voxelParams.streamingCenterXZ.y += context.deltaSeconds * 3.5f;
            mImpl->voxelParams.streamingCenterXZ.x =
                SparseVoxelSample::evalRiverCenterX(mImpl->voxelParams.streamingCenterXZ.y);
        }
        if (ImGui::GetCurrentContext() != nullptr && !ImGui::GetIO().WantTextInput)
        {
            const float panSpeed = 12.0f * context.deltaSeconds;
            if (ImGui::IsKeyDown(ImGuiKey_W)) { mImpl->voxelParams.streamingCenterXZ.y += panSpeed; }
            if (ImGui::IsKeyDown(ImGuiKey_S)) { mImpl->voxelParams.streamingCenterXZ.y -= panSpeed; }
            if (ImGui::IsKeyDown(ImGuiKey_D)) { mImpl->voxelParams.streamingCenterXZ.x += panSpeed; }
            if (ImGui::IsKeyDown(ImGuiKey_A)) { mImpl->voxelParams.streamingCenterXZ.x -= panSpeed; }
        }

        const uint32_t w = context.drawableWidth > 0u ? context.drawableWidth : 1u;
        const uint32_t h = context.drawableHeight > 0u ? context.drawableHeight : 1u;
        mImpl->voxelParams.extent = glm::uvec2(w, h);
        populateVoxelOrbitCamera(
            mImpl->voxelParams,
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

    void Test09SparseVoxelWorldApp::drawOverlay(const SampleFrameContext &context)
    {
        SampleApplication::drawOverlay(context);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 126.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.82f);
        if (ImGui::Begin("Sparse Micro-Voxel Open World (0.125m)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int modeIdx = static_cast<int>(mImpl->activeMode);
            ImGui::TextUnformatted("TaskGraph Executor:");
            if (ImGui::RadioButton("Serial (Nested Octree, 1 Worker)", modeIdx == 0))
            {
                mImpl->activeMode = SparseVoxelExecutorMode::Serial;
            }
            if (ImGui::RadioButton("ThreadExecutor (Nested Octree, N Workers)", modeIdx == 1))
            {
                mImpl->activeMode = SparseVoxelExecutorMode::ThreadPool;
            }
            if (kHasUpstreamMarl)
            {
                if (ImGui::RadioButton("MarlTaskExecutor (Nested Octree, Fibers)", modeIdx == 2))
                {
                    mImpl->activeMode = SparseVoxelExecutorMode::MarlFibers;
                }
            }
            else
            {
                ImGui::BeginDisabled();
                ImGui::RadioButton("MarlTaskExecutor (build with -DGVM_ENABLE_UPSTREAM_MARL=ON)", false);
                ImGui::EndDisabled();
            }

            ImGui::Checkbox("GPU Pass Timestamp Feedback", &mImpl->enableTimestampFeedback);
            ImGui::Checkbox("Auto-Roam Open-World Streaming (WASD)", &mImpl->autoRoamStreaming);
            ImGui::Checkbox("Auto-Orbit Camera (LMB Drag)", &mImpl->autoOrbit);
            ImGui::Checkbox("Stress Mode: Full 67M Rebuild Every Frame", &mImpl->stressRebuildEveryFrame);
            if (ImGui::Button("Rebuild Entire World Now"))
            {
                mImpl->forceColdRebuild = true;
            }
            ImGui::SliderFloat("Orbit Distance", &mImpl->orbitDistance, 16.0f, 88.0f, "%.1f m");

            ImGui::Separator();
            int seedVal = static_cast<int>(mImpl->voxelParams.seed);
            if (ImGui::SliderInt("World Seed", &seedVal, 1, 9999))
            {
                mImpl->voxelParams.seed = static_cast<uint32_t>(seedVal);
            }
            ImGui::SliderFloat("Micro-Voxel Size", &mImpl->voxelParams.voxelSize, 0.10f, 0.25f, "%.3f m");
            ImGui::SliderFloat2("Streaming Center XZ", &mImpl->voxelParams.streamingCenterXZ.x, -128.0f, 128.0f, "%.1f m");
            ImGui::SliderFloat("Canyon Relief Scale", &mImpl->voxelParams.canyonHeightScale, 0.5f, 1.5f, "%.2f");
            ImGui::SliderFloat("Emissive Glow", &mImpl->voxelParams.emissiveGlowScale, 0.2f, 2.0f, "%.2f");
            ImGui::SliderFloat("Water Ripple", &mImpl->voxelParams.waterRippleStrength, 0.0f, 2.5f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Active Engine: %s (%u workers)",
                executorModeLabel(mImpl->bundle->mode),
                mImpl->bundle->workerCount);
            ImGui::Text("Render Frame Time: %.2f ms", mImpl->renderFrameMs);
            ImGui::Text("Last Full Cold PCG Rebuild: %.2f ms", mImpl->lastColdRebuildMs);
            if (mImpl->baselineSerialMs > 0.01 && mImpl->lastColdRebuildMs > 0.01)
            {
                ImGui::Text("Speedup vs Serial: %.2fx (Serial: %.2f ms)",
                    mImpl->baselineSerialMs / mImpl->lastColdRebuildMs,
                    mImpl->baselineSerialMs);
            }
            if (mImpl->hasFrontierStreamed)
            {
                ImGui::Text("Last Frontier Stream Step: %.2f ms (%u new cluster subgraphs | %zu cached clusters)",
                    mImpl->lastFrontierStreamMs,
                    mImpl->lastFrontierNewClusters,
                    mImpl->clusterCache->size());
            }
            else
            {
                ImGui::Text("Cluster Cache: %zu / %zu cached clusters (WASD / Auto-Roam to stream)",
                    mImpl->clusterCache->size(),
                    WorldClusterCache::kMaxEntries);
            }

            const float prunePct = mImpl->lastSummary.totalBrickCount > 0u
                ? (100.0f * float(mImpl->lastSummary.prunedBrickCount) / float(mImpl->lastSummary.totalBrickCount))
                : 0.0f;
            const float greedyRatio = mImpl->lastSummary.greedyQuadCount > 0u
                ? (float(mImpl->lastSummary.rawFaceCount) / float(mImpl->lastSummary.greedyQuadCount))
                : 1.0f;

            ImGui::Separator();
            ImGui::Text("Virtual Voxels: %llu (512x256x512)",
                static_cast<unsigned long long>(mImpl->lastSummary.virtualVoxelCount));
            ImGui::Text("Bricks: %u Active | %u Pruned (%.1f%% sparse)",
                mImpl->lastSummary.activeBrickCount,
                mImpl->lastSummary.prunedBrickCount,
                prunePct);
            ImGui::Text("Solid Micro-Voxels: %u", mImpl->lastSummary.solidVoxelCount);
            ImGui::Text("Greedy Mesh: %u Faces -> %u Quads (%.2fx) | %u Tris",
                mImpl->lastSummary.rawFaceCount,
                mImpl->lastSummary.greedyQuadCount,
                greedyRatio,
                mImpl->lastSummary.triangleCount);
            ImGui::Text("Nested Subgraphs: %u Clusters | %u Octree Nodes",
                mImpl->lastSummary.nestedSubgraphCount,
                mImpl->lastSummary.octreeInteriorCount);

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

    void Test09SparseVoxelWorldApp::renderSampleFrame()
    {
        auto nextTexture = swapchain()->queryNextTexture();
        const SnappedGridKey currentSnappedKey = computeSnappedGridKey(mImpl->voxelParams);
        const bool worldParamsChanged =
            !mImpl->hasCachedScene ||
            mImpl->cachedSeed != mImpl->voxelParams.seed ||
            mImpl->cachedVoxelSize != mImpl->voxelParams.voxelSize ||
            mImpl->cachedCanyonHeightScale != mImpl->voxelParams.canyonHeightScale ||
            mImpl->cachedMode != mImpl->bundle->mode;

        const auto submitPrebuiltSwapchain = [&](const SparseVoxelSample::PrebuiltVoxelScene &scene)
        {
            SparseVoxelSample::PrebuiltVoxelSwapchainScene req;
            req.swapchainTarget = nextTexture;
            req.params = mImpl->voxelParams;
            req.q0 = scene.q0;
            req.q1 = scene.q1;
            req.q2 = scene.q2;
            req.q3 = scene.q3;
            req.octreeInteriorCount = scene.octreeInteriorCount;

            auto exec = mImpl->bundle->runtime->submit(
                SparseVoxelSample::Entries::renderVoxelWorldSwapchainFrame,
                req);
            return awaitExecutionOrThrow(exec, 20'000'000'000ull, "Test09 swapchain TaskGraph failed or timed out.");
        };

        const auto t0 = std::chrono::steady_clock::now();
        SparseVoxelSample::SparseVoxelFrameSummary summary;

        if (mImpl->stressRebuildEveryFrame)
        {
            // Bypass both caches and execute a full 67.1M-voxel cold rebuild every frame.
            mImpl->clusterCache->clear();
            mImpl->hasCachedScene = false;
            mImpl->hasFrontierStreamed = false;

            auto out = runUnifiedSwapchainFrame(
                *mImpl->bundle->runtime,
                nextTexture,
                mImpl->voxelParams,
                nullptr);
            summary = out.summary;

            const auto t1 = std::chrono::steady_clock::now();
            const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            mImpl->renderFrameMs = execMs;
            mImpl->smoothedExecMs = mImpl->smoothedExecMs <= 0.0
                ? execMs
                : (0.85 * mImpl->smoothedExecMs + 0.15 * execMs);
            mImpl->lastColdRebuildMs = execMs;
            if (mImpl->bundle->mode == SparseVoxelExecutorMode::Serial)
            {
                mImpl->baselineSerialMs = execMs;
            }
        }
        else if (mImpl->forceColdRebuild || worldParamsChanged)
        {
            // Cold PCG rebuild: clear stale cache, build full 67.1M-voxel world, and warm both caches.
            mImpl->clusterCache->clear();
            mImpl->hasCachedScene = false;
            mImpl->hasFrontierStreamed = false;
            mImpl->forceColdRebuild = false;

            auto builtCounter = std::make_shared<std::atomic<uint32_t>>(0u);
            auto out = runUnifiedSwapchainFrame(
                *mImpl->bundle->runtime,
                nextTexture,
                mImpl->voxelParams,
                mImpl->clusterCache,
                currentSnappedKey,
                builtCounter);
            summary = out.summary;
            mImpl->cachedScene = std::move(out.scene);

            mImpl->hasCachedScene = true;
            mImpl->cachedSnappedKey = currentSnappedKey;
            mImpl->cachedSeed = mImpl->voxelParams.seed;
            mImpl->cachedVoxelSize = mImpl->voxelParams.voxelSize;
            mImpl->cachedCanyonHeightScale = mImpl->voxelParams.canyonHeightScale;
            mImpl->cachedMode = mImpl->bundle->mode;

            const auto t1 = std::chrono::steady_clock::now();
            const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            mImpl->renderFrameMs = execMs;
            mImpl->smoothedExecMs = execMs;
            mImpl->lastColdRebuildMs = execMs;
            if (mImpl->bundle->mode == SparseVoxelExecutorMode::Serial)
            {
                mImpl->baselineSerialMs = execMs;
            }
        }
        else if (currentSnappedKey != mImpl->cachedSnappedKey)
        {
            // Case B: Snapped grid origin crossed a 32-voxel cluster boundary; stream only frontier misses.
            auto builtCounter = std::make_shared<std::atomic<uint32_t>>(0u);
            auto out = runUnifiedSwapchainFrame(
                *mImpl->bundle->runtime,
                nextTexture,
                mImpl->voxelParams,
                mImpl->clusterCache,
                currentSnappedKey,
                builtCounter);
            summary = out.summary;
            mImpl->cachedScene = std::move(out.scene);
            const uint32_t frontierBuilt = builtCounter->load(std::memory_order_relaxed);

            mImpl->cachedSnappedKey = currentSnappedKey;

            const auto t1 = std::chrono::steady_clock::now();
            const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            mImpl->renderFrameMs = execMs;
            mImpl->smoothedExecMs = 0.85 * mImpl->smoothedExecMs + 0.15 * execMs;
            mImpl->lastFrontierStreamMs = execMs;
            mImpl->lastFrontierNewClusters = frontierBuilt;
            mImpl->hasFrontierStreamed = true;
        }
        else
        {
            // Case A: Camera orbit and GPU animation only; skip CPU voxelization.
            summary = submitPrebuiltSwapchain(mImpl->cachedScene);

            const auto t1 = std::chrono::steady_clock::now();
            const double execMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            mImpl->renderFrameMs = execMs;
            mImpl->smoothedExecMs = mImpl->smoothedExecMs <= 0.0
                ? execMs
                : (0.85 * mImpl->smoothedExecMs + 0.15 * execMs);
        }

        mImpl->lastSummary = summary;
        swapchain()->present();
    }

    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test09SparseVoxelWorldApp>(window);
    }

    int runSparseVoxelHeadlessBenchmark(
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
            std::fprintf(stderr, "[Test09 Benchmark] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test09 Benchmark] Failed to create RHI device.\n");
            return 1;
        }

        const char *backendStr = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ? "Metal" : "Vulkan";
        std::printf("================================================================================\n");
        std::printf(" GVM Test_09 Pure Sparse Micro-Voxel (0.125m) Open-World TaskGraph (%s)\n", backendStr);
        std::printf(" Grid: 512x256x512 (67.1M virtual voxels) | 16^3 Bricks | Greedy+AO | Nested Subgraphs\n");
        std::printf(" Warmup: %u iters | Measured: %u iters | Parallel Workers: %u\n",
            warmupIterations, measuredIterations, parallelWorkers);
        std::printf("================================================================================\n");

        SparseVoxelSample::SparseVoxelParams params;
        params.extent = glm::uvec2(640u, 480u);
        params.voxelSize = 0.125f;
        params.seed = 2026u;
        params.timeSeconds = 1.25f;
        params.streamingCenterXZ = glm::vec2(0.0f, 0.0f);
        populateVoxelOrbitCamera(params, 0.44f, 0.34f, 42.0f, 640.0f / 480.0f);

        struct ModeBenchRecord
        {
            SparseVoxelExecutorMode mode;
            double avgMs = 0.0;
            double minMs = 0.0;
            SparseVoxelSample::SparseVoxelBenchmarkResult result;
            eastl::vector<GVM::Core::PassProfileFeedbackEntry> passSnapshot;
        };

        std::vector<SparseVoxelExecutorMode> modesToRun = {
            SparseVoxelExecutorMode::Serial,
            SparseVoxelExecutorMode::ThreadPool,
        };
        if (kHasUpstreamMarl)
        {
            modesToRun.push_back(SparseVoxelExecutorMode::MarlFibers);
        }

        std::vector<ModeBenchRecord> records;
        for (const auto mode : modesToRun)
        {
            RuntimeBundle bundle(*device, mode, parallelWorkers, /*enableTimestamps=*/true);

            auto runOne = [&]() -> SparseVoxelSample::SparseVoxelBenchmarkResult
            {
                return runUnifiedBenchmarkFrame(*bundle.runtime, params);
            };

            for (uint32_t w = 0u; w < warmupIterations; ++w)
            {
                (void)runOne();
            }

            double totalMs = 0.0;
            double bestMs = 1e9;
            SparseVoxelSample::SparseVoxelBenchmarkResult lastRes{};
            for (uint32_t iter = 0u; iter < measuredIterations; ++iter)
            {
                const auto t0 = std::chrono::steady_clock::now();
                lastRes = runOne();
                const auto t1 = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                totalMs += ms;
                bestMs = std::min(bestMs, ms);
            }

            ModeBenchRecord rec;
            rec.mode = mode;
            rec.avgMs = totalMs / double(std::max(measuredIterations, 1u));
            rec.minMs = bestMs;
            rec.result = lastRes;
            rec.passSnapshot = bundle.runtime->passProfileSnapshot();
            records.push_back(std::move(rec));
        }

        const auto &ref = records.front().result;
        const float prunePct = ref.summary.totalBrickCount > 0u
            ? (100.0f * float(ref.summary.prunedBrickCount) / float(ref.summary.totalBrickCount))
            : 0.0f;
        const float greedyRatio = ref.summary.greedyQuadCount > 0u
            ? (float(ref.summary.rawFaceCount) / float(ref.summary.greedyQuadCount))
            : 1.0f;

        std::printf("Virtual Grid: %llu voxels (%u total 16^3 bricks)\n",
            static_cast<unsigned long long>(ref.summary.virtualVoxelCount),
            ref.summary.totalBrickCount);
        std::printf("Sparse Tree : %u Active Bricks | %u Pruned Bricks (%.1f%% sparse pruned) | %u Solid Voxels\n",
            ref.summary.activeBrickCount,
            ref.summary.prunedBrickCount,
            prunePct,
            ref.summary.solidVoxelCount);
        std::printf("Greedy Mesh : %u Raw Faces -> %u Greedy Quads (%.2fx) | %u Triangles\n",
            ref.summary.rawFaceCount,
            ref.summary.greedyQuadCount,
            greedyRatio,
            ref.summary.triangleCount);
        std::printf("TaskGraph   : %u Nested Cluster Subgraphs | %u Octree Interior Nodes | Non-Sky Pixels: %u | Checksum: 0x%08X\n",
            ref.summary.nestedSubgraphCount,
            ref.summary.octreeInteriorCount,
            ref.nonSkyPixelCount,
            ref.pixelChecksum);
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf("%-42s | %10s | %10s | %10s | %10s\n", "Executor Mode", "Avg (ms)", "Best (ms)", "Speedup", "Checksum");
        std::printf("--------------------------------------------------------------------------------\n");

        const double serialAvg = records.front().avgMs;
        for (const auto &rec : records)
        {
            const double speedup = rec.avgMs > 0.0 ? (serialAvg / rec.avgMs) : 1.0;
            std::printf("%-42s | %10.2f | %10.2f | %9.2fx | 0x%08X\n",
                executorModeLabel(rec.mode),
                rec.avgMs,
                rec.minMs,
                speedup,
                rec.result.pixelChecksum);

            if (rec.result.summary.activeBrickCount != ref.summary.activeBrickCount ||
                rec.result.summary.prunedBrickCount != ref.summary.prunedBrickCount ||
                rec.result.summary.solidVoxelCount != ref.summary.solidVoxelCount ||
                rec.result.summary.triangleCount != ref.summary.triangleCount ||
                rec.result.summary.nestedSubgraphCount != ref.summary.nestedSubgraphCount ||
                rec.result.summary.octreeInteriorCount != ref.summary.octreeInteriorCount ||
                rec.result.nonSkyPixelCount != ref.nonSkyPixelCount ||
                rec.result.pixelChecksum != ref.pixelChecksum)
            {
                std::fprintf(stderr, "[Test09 Benchmark] Determinism mismatch in %s!\n",
                    executorModeLabel(rec.mode));
                return 1;
            }
            if (rec.result.summary.activeBrickCount == 0u ||
                rec.result.summary.nestedSubgraphCount == 0u ||
                rec.result.summary.octreeInteriorCount == 0u ||
                rec.result.summary.prunedBrickCount <= rec.result.summary.activeBrickCount ||
                rec.result.summary.triangleCount <= 80000u ||
                rec.result.nonSkyPixelCount <= 120000u)
            {
                std::fprintf(stderr,
                    "[Test09 Benchmark] Acceptance criteria check failed (active=%u, pruned=%u, tris=%u, nonSky=%u)!\n",
                    rec.result.summary.activeBrickCount,
                    rec.result.summary.prunedBrickCount,
                    rec.result.summary.triangleCount,
                    rec.result.nonSkyPixelCount);
                return 1;
            }
        }
        std::printf("--------------------------------------------------------------------------------\n");

        const auto &lastSnapshot = records.back().passSnapshot;
        if (!lastSnapshot.empty())
        {
            std::printf("Learned GPU Pass Timestamp Feedback (%zu passes):\n", lastSnapshot.size());
            const size_t maxPrint = std::min<size_t>(lastSnapshot.size(), 10u);
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
        std::printf("================================================================================\n");
        return 0;
    }

    int runSparseVoxelShowcaseCapture(
        GVM::RHI::GraphicsBackend preferredBackend,
        const char *captureDir)
    {
        if (captureDir == nullptr || captureDir[0] == '\0')
        {
            std::fprintf(stderr, "[Test09 Capture] Invalid empty capture directory.\n");
            return 1;
        }

        std::error_code fsErr;
        std::filesystem::create_directories(captureDir, fsErr);
        if (fsErr)
        {
            std::fprintf(stderr, "[Test09 Capture] Failed to create directory '%s': %s\n",
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
            std::fprintf(stderr, "[Test09 Capture] Failed to create RHI instance.\n");
            return 1;
        }
        auto *device = instance->createDevice();
        if (device == nullptr)
        {
            std::fprintf(stderr, "[Test09 Capture] Failed to create RHI device.\n");
            return 1;
        }

        const auto execMode = kHasUpstreamMarl
            ? SparseVoxelExecutorMode::MarlFibers
            : SparseVoxelExecutorMode::ThreadPool;
        RuntimeBundle bundle(*device, execMode, parallelWorkers, /*enableTimestamps=*/true);

        constexpr uint32_t kCaptureWidth = 1280u;
        constexpr uint32_t kCaptureHeight = 800u;
        constexpr float kAspect = float(kCaptureWidth) / float(kCaptureHeight);

        SparseVoxelSample::SparseVoxelParams baseParams;
        baseParams.extent = glm::uvec2(kCaptureWidth, kCaptureHeight);
        baseParams.voxelSize = 0.125f;
        baseParams.seed = 2026u;
        baseParams.timeSeconds = 1.25f;
        baseParams.streamingCenterXZ = glm::vec2(0.0f, 0.0f);
        baseParams.emissiveGlowScale = 1.25f;
        baseParams.waterRippleStrength = 1.0f;

        // Compute exact world coordinates of key landmarks for curated framing
        const float pgZ = 5.0f;
        const float pgX = SparseVoxelSample::evalRiverCenterX(pgZ) + 12.0f;
        const float pgBaseH = SparseVoxelSample::evalTerrainColumn(pgX, pgZ, baseParams).terrainY;

        const float grZ = 6.5f;
        const float grX = SparseVoxelSample::evalRiverCenterX(grZ) - 9.2f;
        const float grFloorY = SparseVoxelSample::evalWaterSurfaceY(grZ) + 1.6f;

        struct ShowcaseShot
        {
            const char *filename;
            glm::vec2 streamingCenterXZ;
            glm::vec3 eye;
            glm::vec3 target;
            float fovDeg;
        };

        const ShowcaseShot shots[3] = {
            {
                "test09_01_canyon_pagoda_bridge.png",
                glm::vec2(0.0f, 0.0f),
                glm::vec3(1.5f, 21.0f, -34.5f),
                glm::vec3(4.5f, 4.0f, 0.5f),
                52.0f,
            },
            {
                "test09_02_crystal_grotto_closeup.png",
                glm::vec2(0.0f, 0.0f),
                glm::vec3(9.2f, 5.5f, 2.4f),
                glm::vec3(grX - 0.1f, grFloorY + 1.3f, grZ - 0.3f),
                52.0f,
            },
            {
                "test09_03_pagoda_sakura_detail.png",
                glm::vec2(0.0f, 0.0f),
                glm::vec3(pgX + 2.5f, pgBaseH + 3.8f, pgZ - 12.5f),
                glm::vec3(pgX - 2.2f, pgBaseH + 3.6f, pgZ + 0.5f),
                54.0f,
            },
        };

        auto clusterCache = std::make_shared<WorldClusterCache>();
        const std::filesystem::path dirPath(captureDir);

        for (const auto &shot : shots)
        {
            SparseVoxelSample::SparseVoxelParams params = baseParams;
            params.streamingCenterXZ = shot.streamingCenterXZ;
            populateVoxelLookAtCamera(params, shot.eye, shot.target, kAspect, shot.fovDeg);

            const SnappedGridKey snappedKey = computeSnappedGridKey(params);
            const auto t0 = std::chrono::steady_clock::now();

            const UnifiedCaptureOutput cap =
                runUnifiedCaptureFrame(*bundle.runtime, params, clusterCache, snappedKey);

            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            const size_t expectedBytes = static_cast<size_t>(kCaptureWidth) * static_cast<size_t>(kCaptureHeight) * 4u;
            if (cap.pixels.size() < expectedBytes)
            {
                std::fprintf(stderr, "[Test09 Capture] Unexpected readback buffer size (%zu < %zu) for %s.\n",
                    cap.pixels.size(), expectedBytes, shot.filename);
                return 1;
            }

            // Flip GPU readback vertically so row 0 is the top of the PNG image.
            const size_t rowBytes = static_cast<size_t>(kCaptureWidth) * 4u;
            std::vector<uint8_t> uprightRgba(expectedBytes);
            for (uint32_t y = 0u; y < kCaptureHeight; ++y)
            {
                const uint32_t srcY = kCaptureHeight - 1u - y;
                std::memcpy(
                    uprightRgba.data() + static_cast<size_t>(y) * rowBytes,
                    cap.pixels.data() + static_cast<size_t>(srcY) * rowBytes,
                    rowBytes);
            }

            const std::string outPath = (dirPath / shot.filename).string();
            if (!writeRgba8Png(outPath.c_str(), kCaptureWidth, kCaptureHeight, uprightRgba.data()))
            {
                std::fprintf(stderr, "[Test09 Capture] Failed to write PNG '%s'.\n", outPath.c_str());
                return 1;
            }

            std::printf("[Test09 Capture] Saved %s (%ux%u, %.2f ms, tris=%u, activeBricks=%u) eye=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f)\n",
                outPath.c_str(),
                kCaptureWidth,
                kCaptureHeight,
                ms,
                cap.summary.triangleCount,
                cap.summary.activeBrickCount,
                shot.eye.x, shot.eye.y, shot.eye.z,
                shot.target.x, shot.target.y, shot.target.z);
        }

        return 0;
    }
} // namespace GVM::Samples
