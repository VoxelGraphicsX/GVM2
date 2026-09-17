#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "generate_result.hpp"
#include "DebugSceneFactory.hpp"
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "GaussianSplattingDslShared/CpuGaussianScene.hpp"
#include "PlyLoader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t kTargetSourceSplatsPerLeafCluster = 16u;
    constexpr uint32_t kMaxSourceSplatsPerLeafCluster = 32u;
    constexpr uint32_t kMinSourceSplatsPerLeafCluster = 4u;
    constexpr uint32_t kParentClusterFanout = 16u;
    constexpr uint32_t kStateCount = Test16ClusterTraversalTest::StateCount;
    constexpr auto kCompletionTimeout = std::chrono::seconds(5);

    /**
     * Stores aggregate diagnostics produced by the synthetic Test16 cluster builder.
     */
    struct ClusterBuildStats
    {
        uint32_t sourceSplatCount = 0u;
        uint32_t clusterTreeNodeCount = 0u;
        uint32_t leafClusterCount = 0u;
        float avgSourceSplatsPerLeafCluster = 0.0f;
        uint32_t maxSourceSplatsPerLeafCluster = 0u;
        uint32_t clusterTreeDepth = 0u;
        uint32_t maxChildCount = 0u;
    };

    /**
     * Owns a synthetic ClusterTreeNode array and the diagnostics needed by traversal tests.
     */
    struct SyntheticClusterTree
    {
        std::vector<Test16ClusterTraversalTest::ClusterTreeNode> nodes;
        std::vector<uint32_t> levelCounts;
        ClusterBuildStats stats = {};
    };

    /**
     * Stores one Morton-sorted source splat reference for Test16 real scene cluster builds.
     */
    struct SourceSplatMortonKey
    {
        uint64_t key = 0u;
        uint32_t splatIndex = 0u;
    };

    /**
     * Stores a contiguous range in the Morton-sorted source splat index array.
     */
    struct SourceSplatRange
    {
        uint32_t first = 0u;
        uint32_t count = 0u;
    };

    /**
     * Accumulates weighted Gaussian payload statistics for one cluster node.
     */
    struct ClusterAggregate
    {
        std::array<float, 3> center = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> color = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> shDc = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> variance = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> boundsMin = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> boundsMax = {0.0f, 0.0f, 0.0f};
        float opacity = 1.0f;
        float weightSum = 0.0f;
        float boundsRadius = 0.0f;
        float maxChildError = 0.0f;
        uint32_t sourceSplatCount = 0u;
    };

    /**
     * Stores the CPU reference frontier cut for one Test16 traversal scenario.
     */
    struct CpuTraversalResult
    {
        std::vector<uint32_t> selected;
        uint32_t processedCount = 0u;
        uint32_t maxDepthReached = 0u;
        uint32_t enqueuedCount = 0u;
        uint32_t estimatedEntryCount = 0u;
    };

    /**
     * Stores one CPU frontier item and the runtime entry budget assigned to its subtree.
     */
    struct CpuTraversalFrontierItem
    {
        uint32_t clusterIndex = 0u;
        uint32_t entryBudget = 0u;
    };

    /**
     * Describes one synthetic Test16 cluster traversal scenario.
     */
    struct Scenario
    {
        const char *label = "unnamed";
        uint32_t sourceSplatCount = 4096u;
        float thresholdPx = 1.0f;
        uint32_t projectedEntryBudget = 0u;
        uint64_t memoryBudgetBytes = 512ull << 20u;
        bool allowSkipOnBudget = false;
    };

    /**
     * Divides positive unsigned integers with upward rounding for cluster count calculations.
     */
    uint32_t ceilDivU32(uint32_t value, uint32_t divisor)
    {
        if (divisor == 0u)
        {
            throw std::invalid_argument("ceilDivU32 requires a non-zero divisor");
        }
        return value == 0u ? 0u : 1u + (value - 1u) / divisor;
    }

    /**
     * Rounds a dispatch thread count up to a full Test16 traversal workgroup.
     */
    uint32_t roundUpToWorkGroup(uint32_t value)
    {
        const uint32_t workGroupSize = Test16ClusterTraversalTest::WorkGroupSize;
        return ceilDivU32(std::max(value, 1u), workGroupSize) * workGroupSize;
    }

    /**
     * Computes a three-component Euclidean length from explicit scalar components.
     */
    float length3(float x, float y, float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    /**
     * Computes the length of one CPU Gaussian axis vector.
     */
    float splatAxisLength(const float axis[3])
    {
        return length3(axis[0], axis[1], axis[2]);
    }

    /**
     * Computes distance between a CPU splat position and an aggregate center.
     */
    float distanceToCenter(const float position[3], const std::array<float, 3> &center)
    {
        return length3(position[0] - center[0], position[1] - center[1], position[2] - center[2]);
    }

    /**
     * Expands a 10-bit coordinate so the bits can be interleaved into a Morton key.
     */
    uint64_t expandMortonBits10(uint32_t value)
    {
        uint64_t bits = uint64_t(value & 1023u);
        bits = (bits | (bits << 16u)) & 0x030000ffull;
        bits = (bits | (bits << 8u)) & 0x0300f00full;
        bits = (bits | (bits << 4u)) & 0x030c30c3ull;
        bits = (bits | (bits << 2u)) & 0x09249249ull;
        return bits;
    }

    /**
     * Quantizes one scene coordinate to the 10-bit range used by the Morton sorter.
     */
    uint32_t quantizeMortonCoordinate(float value, float minimum, float maximum)
    {
        const float extent = maximum - minimum;
        if (!std::isfinite(value) || !std::isfinite(extent) || extent <= 0.0f)
        {
            return 0u;
        }
        const float normalized = std::clamp((value - minimum) / extent, 0.0f, 1.0f);
        return static_cast<uint32_t>(normalized * 1023.0f + 0.5f);
    }

    /**
     * Computes a 30-bit Morton key for one CPU Gaussian splat inside the scene bounds.
     */
    uint64_t computeSplatMortonKey(const CpuGaussianScene &scene, const CpuGaussianSplat &splat)
    {
        const uint32_t x = quantizeMortonCoordinate(splat.position[0], scene.boundsMin[0], scene.boundsMax[0]);
        const uint32_t y = quantizeMortonCoordinate(splat.position[1], scene.boundsMin[1], scene.boundsMax[1]);
        const uint32_t z = quantizeMortonCoordinate(splat.position[2], scene.boundsMin[2], scene.boundsMax[2]);
        return expandMortonBits10(x) | (expandMortonBits10(y) << 1u) | (expandMortonBits10(z) << 2u);
    }

    /**
     * Builds a stable Morton-sorted source index array for Test16 leaf cluster generation.
     */
    std::vector<SourceSplatMortonKey> buildMortonSortedSourceKeys(const CpuGaussianScene &scene)
    {
        std::vector<SourceSplatMortonKey> keys;
        keys.reserve(scene.splats.size());
        for (uint32_t splatIndex = 0u; splatIndex < scene.splats.size(); ++splatIndex)
        {
            keys.push_back({
                .key = computeSplatMortonKey(scene, scene.splats[splatIndex]),
                .splatIndex = splatIndex,
            });
        }
        std::sort(keys.begin(), keys.end(), [](const SourceSplatMortonKey &lhs, const SourceSplatMortonKey &rhs) {
            if (lhs.key != rhs.key)
            {
                return lhs.key < rhs.key;
            }
            return lhs.splatIndex < rhs.splatIndex;
        });
        return keys;
    }

    /**
     * Splits Morton-sorted source splats into leaf cluster ranges near the Test16 target size.
     */
    std::vector<SourceSplatRange> buildLeafSourceRanges(uint32_t sourceSplatCount, uint32_t targetSplatsPerCluster)
    {
        if (sourceSplatCount == 0u)
        {
            throw std::invalid_argument("Test16 real scene cluster build requires at least one source splat");
        }
        if (targetSplatsPerCluster == 0u || targetSplatsPerCluster > kMaxSourceSplatsPerLeafCluster)
        {
            throw std::invalid_argument("Test16 real scene target cluster size is outside the supported range");
        }

        std::vector<SourceSplatRange> ranges;
        uint32_t first = 0u;
        while (first < sourceSplatCount)
        {
            const uint32_t remaining = sourceSplatCount - first;
            uint32_t count = remaining <= kMaxSourceSplatsPerLeafCluster ? remaining : targetSplatsPerCluster;
            if (remaining > targetSplatsPerCluster)
            {
                const uint32_t tailCount = remaining - targetSplatsPerCluster;
                if (tailCount < kMinSourceSplatsPerLeafCluster && targetSplatsPerCluster + tailCount <= kMaxSourceSplatsPerLeafCluster)
                {
                    count = targetSplatsPerCluster + tailCount;
                }
            }
            ranges.push_back({.first = first, .count = count});
            first += count;
        }
        return ranges;
    }

    /**
     * Initializes aggregate bounds before adding leaf splats or child clusters.
     */
    void resetAggregateBounds(ClusterAggregate &aggregate)
    {
        aggregate.boundsMin = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        aggregate.boundsMax = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
        };
    }

    /**
     * Builds weighted center/color/opacity averages for one Morton-sorted leaf source range.
     */
    ClusterAggregate makeLeafAggregateFirstPass(const CpuGaussianScene &scene,
                                                const std::vector<SourceSplatMortonKey> &sortedSources,
                                                SourceSplatRange range)
    {
        ClusterAggregate aggregate = {};
        resetAggregateBounds(aggregate);
        aggregate.sourceSplatCount = range.count;

        for (uint32_t ordinal = 0u; ordinal < range.count; ++ordinal)
        {
            const CpuGaussianSplat &splat = scene.splats[sortedSources[range.first + ordinal].splatIndex];
            const float weight = std::max(splat.opacity, 1.0f / 255.0f);
            aggregate.weightSum += weight;
            aggregate.opacity += splat.opacity;
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                aggregate.center[axis] += splat.position[axis] * weight;
                aggregate.color[axis] += splat.color[axis] * weight;
                aggregate.shDc[axis] += splat.shDc[axis] * weight;
            }
        }

        const float reciprocalWeight = aggregate.weightSum > 0.0f ? 1.0f / aggregate.weightSum : 0.0f;
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            aggregate.center[axis] *= reciprocalWeight;
            aggregate.color[axis] *= reciprocalWeight;
            aggregate.shDc[axis] *= reciprocalWeight;
        }
        aggregate.opacity = range.count > 0u ? aggregate.opacity / float(range.count) : 1.0f;
        return aggregate;
    }

    /**
     * Finishes the diagonal covariance and conservative bounds for one leaf aggregate.
     */
    void finishLeafAggregate(const CpuGaussianScene &scene,
                             const std::vector<SourceSplatMortonKey> &sortedSources,
                             SourceSplatRange range,
                             ClusterAggregate &aggregate)
    {
        for (uint32_t ordinal = 0u; ordinal < range.count; ++ordinal)
        {
            const CpuGaussianSplat &splat = scene.splats[sortedSources[range.first + ordinal].splatIndex];
            const float weight = std::max(splat.opacity, 1.0f / 255.0f);
            const float maxAxisLength = std::max({
                splatAxisLength(splat.axis0),
                splatAxisLength(splat.axis1),
                splatAxisLength(splat.axis2),
            });
            const float conservativeAxisRadius = maxAxisLength * 3.0f;
            aggregate.boundsRadius = std::max(aggregate.boundsRadius, distanceToCenter(splat.position, aggregate.center) + conservativeAxisRadius);

            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const float delta = splat.position[axis] - aggregate.center[axis];
                const float axisVariance = splat.axis0[axis] * splat.axis0[axis] +
                                           splat.axis1[axis] * splat.axis1[axis] +
                                           splat.axis2[axis] * splat.axis2[axis];
                aggregate.variance[axis] += weight * (delta * delta + axisVariance);
                aggregate.boundsMin[axis] = std::min(aggregate.boundsMin[axis], splat.position[axis] - conservativeAxisRadius);
                aggregate.boundsMax[axis] = std::max(aggregate.boundsMax[axis], splat.position[axis] + conservativeAxisRadius);
            }
        }

        const float reciprocalWeight = aggregate.weightSum > 0.0f ? 1.0f / aggregate.weightSum : 0.0f;
        for (float &variance : aggregate.variance)
        {
            variance = std::max(variance * reciprocalWeight, 1.0e-10f);
        }
    }

    /**
     * Reads one xyz component from a generated float4 value without relying on vector indexing support.
     */
    float xyzComponent(const float4 &value, uint32_t axis)
    {
        return axis == 0u ? value.x : (axis == 1u ? value.y : value.z);
    }

    /**
     * Builds an aggregate payload from an already constructed contiguous child cluster range.
     */
    ClusterAggregate makeParentAggregate(const std::vector<Test16ClusterTraversalTest::ClusterTreeNode> &nodes,
                                         uint32_t childStart,
                                         uint32_t childCount)
    {
        ClusterAggregate aggregate = {};
        resetAggregateBounds(aggregate);

        for (uint32_t childOrdinal = 0u; childOrdinal < childCount; ++childOrdinal)
        {
            const auto &child = nodes[childStart + childOrdinal];
            const float weight = std::max(float(child.clusterInfo.x), 1.0f);
            aggregate.weightSum += weight;
            aggregate.opacity += child.positionOpacityProfile.w * weight;
            aggregate.maxChildError = std::max(aggregate.maxChildError, child.errorInfo.x);
            aggregate.sourceSplatCount += child.clusterInfo.x;
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                aggregate.center[axis] += xyzComponent(child.positionOpacityProfile, axis) * weight;
                aggregate.color[axis] += xyzComponent(child.shDc, axis) * weight;
                aggregate.shDc[axis] += xyzComponent(child.shDc, axis) * weight;
            }
        }

        const float reciprocalWeight = aggregate.weightSum > 0.0f ? 1.0f / aggregate.weightSum : 0.0f;
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            aggregate.center[axis] *= reciprocalWeight;
            aggregate.color[axis] *= reciprocalWeight;
            aggregate.shDc[axis] *= reciprocalWeight;
        }
        aggregate.opacity = aggregate.weightSum > 0.0f ? aggregate.opacity * reciprocalWeight : 1.0f;

        for (uint32_t childOrdinal = 0u; childOrdinal < childCount; ++childOrdinal)
        {
            const auto &child = nodes[childStart + childOrdinal];
            const float weight = std::max(float(child.clusterInfo.x), 1.0f);
            const std::array<float, 3> childCenter = {
                child.positionOpacityProfile.x,
                child.positionOpacityProfile.y,
                child.positionOpacityProfile.z,
            };
            aggregate.boundsRadius = std::max(
                aggregate.boundsRadius,
                length3(childCenter[0] - aggregate.center[0], childCenter[1] - aggregate.center[1], childCenter[2] - aggregate.center[2]) + child.boundsCenterRadius.w);

            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const float axisLength = axis == 0u ? child.axis0.x : (axis == 1u ? child.axis1.y : child.axis2.z);
                const float delta = xyzComponent(child.positionOpacityProfile, axis) - aggregate.center[axis];
                aggregate.variance[axis] += weight * (delta * delta + axisLength * axisLength);
                aggregate.boundsMin[axis] = std::min(aggregate.boundsMin[axis], xyzComponent(child.boundsCenterRadius, axis) - child.boundsCenterRadius.w);
                aggregate.boundsMax[axis] = std::max(aggregate.boundsMax[axis], xyzComponent(child.boundsCenterRadius, axis) + child.boundsCenterRadius.w);
            }
        }

        for (float &variance : aggregate.variance)
        {
            variance = std::max(variance * reciprocalWeight, 1.0e-10f);
        }
        return aggregate;
    }

    /**
     * Converts an aggregate payload and tree topology into the official Test16 ClusterTreeNode layout.
     */
    Test16ClusterTraversalTest::ClusterTreeNode makeClusterNodeFromAggregate(const ClusterAggregate &aggregate,
                                                                            uint32_t nodeIndex,
                                                                            uint32_t level,
                                                                            uint32_t childStart,
                                                                            uint32_t childCount,
                                                                            uint32_t parentIndex)
    {
        Test16ClusterTraversalTest::ClusterTreeNode node = {};
        const float axisX = std::sqrt(std::max(aggregate.variance[0], 1.0e-10f));
        const float axisY = std::sqrt(std::max(aggregate.variance[1], 1.0e-10f));
        const float axisZ = std::sqrt(std::max(aggregate.variance[2], 1.0e-10f));
        const float featureSize = std::max({axisX, axisY, axisZ});
        const float geometricError = std::max({
            aggregate.maxChildError,
            featureSize * 0.25f,
            aggregate.boundsRadius * 0.03f,
            1.0e-5f,
        });

        node.positionOpacityProfile = float4(aggregate.center[0], aggregate.center[1], aggregate.center[2], std::clamp(aggregate.opacity, 1.0f / 255.0f, 0.99f));
        node.axis0 = float4(axisX, 0.0f, 0.0f, 0.0f);
        node.axis1 = float4(0.0f, axisY, 0.0f, 0.0f);
        node.axis2 = float4(0.0f, 0.0f, axisZ, 0.0f);
        node.shDc = float4(aggregate.shDc[0], aggregate.shDc[1], aggregate.shDc[2], 0.0f);
        node.boundsCenterRadius = float4(aggregate.center[0], aggregate.center[1], aggregate.center[2], aggregate.boundsRadius);
        node.errorInfo = float4(geometricError, 0.0f, featureSize, 0.0f);
        node.childInfo = uint4(childStart, childCount, level, childCount == 0u ? Test16ClusterTraversalTest::ClusterFlagLeaf : 0u);
        node.clusterInfo = uint4(aggregate.sourceSplatCount, nodeIndex, parentIndex, 0u);
        return node;
    }

    /**
     * Builds the formal Test16 ClusterTreeNode array from a sanitized CPU Gaussian scene.
     */
    SyntheticClusterTree buildCluster16TreeFromScene(const CpuGaussianScene &scene, uint32_t targetSplatsPerCluster, uint32_t parentFanout)
    {
        if (scene.empty())
        {
            throw std::invalid_argument("Test16 real scene cluster build requires a non-empty scene");
        }
        if (targetSplatsPerCluster == 0u || targetSplatsPerCluster > kMaxSourceSplatsPerLeafCluster)
        {
            throw std::invalid_argument("Test16 real scene target cluster size is outside the supported range");
        }
        if (parentFanout == 0u)
        {
            throw std::invalid_argument("Test16 real scene cluster build requires a non-zero parent fanout");
        }
        if (scene.splats.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::invalid_argument("Test16 real scene cluster build currently supports at most uint32_t source splats");
        }

        const auto sortedSources = buildMortonSortedSourceKeys(scene);
        const auto leafRanges = buildLeafSourceRanges(static_cast<uint32_t>(scene.splats.size()), targetSplatsPerCluster);

        std::vector<uint32_t> levelCounts;
        levelCounts.push_back(static_cast<uint32_t>(leafRanges.size()));
        while (levelCounts.back() > 1u)
        {
            levelCounts.push_back(ceilDivU32(levelCounts.back(), parentFanout));
        }
        std::reverse(levelCounts.begin(), levelCounts.end());

        std::vector<uint32_t> levelOffsets(levelCounts.size(), 0u);
        uint32_t totalNodeCount = 0u;
        for (size_t levelIndex = 0u; levelIndex < levelCounts.size(); ++levelIndex)
        {
            levelOffsets[levelIndex] = totalNodeCount;
            totalNodeCount += levelCounts[levelIndex];
        }

        SyntheticClusterTree tree = {};
        tree.nodes.resize(totalNodeCount);
        tree.levelCounts = levelCounts;

        const uint32_t maxDepth = static_cast<uint32_t>(levelCounts.size() - 1u);
        const uint32_t leafLevelIndex = maxDepth;
        const uint32_t leafLevelOffset = levelOffsets[leafLevelIndex];
        for (uint32_t leafOrdinal = 0u; leafOrdinal < leafRanges.size(); ++leafOrdinal)
        {
            const uint32_t nodeIndex = leafLevelOffset + leafOrdinal;
            const uint32_t parentIndex = leafLevelIndex == 0u
                                             ? Test16ClusterTraversalTest::InvalidIndex
                                             : levelOffsets[leafLevelIndex - 1u] + leafOrdinal / parentFanout;
            ClusterAggregate aggregate = makeLeafAggregateFirstPass(scene, sortedSources, leafRanges[leafOrdinal]);
            finishLeafAggregate(scene, sortedSources, leafRanges[leafOrdinal], aggregate);
            tree.nodes[nodeIndex] = makeClusterNodeFromAggregate(
                aggregate,
                nodeIndex,
                leafLevelIndex,
                0u,
                0u,
                parentIndex);
        }

        for (uint32_t reverseLevel = leafLevelIndex; reverseLevel > 0u; --reverseLevel)
        {
            const uint32_t levelIndex = reverseLevel - 1u;
            const uint32_t nodeCountAtLevel = levelCounts[levelIndex];
            const uint32_t childLevelOffset = levelOffsets[levelIndex + 1u];
            const uint32_t childLevelCount = levelCounts[levelIndex + 1u];
            for (uint32_t ordinal = 0u; ordinal < nodeCountAtLevel; ++ordinal)
            {
                const uint32_t nodeIndex = levelOffsets[levelIndex] + ordinal;
                const uint32_t childOrdinalStart = ordinal * parentFanout;
                const uint32_t childCount = childOrdinalStart >= childLevelCount
                                                ? 0u
                                                : std::min(parentFanout, childLevelCount - childOrdinalStart);
                const uint32_t childStart = childCount == 0u ? 0u : childLevelOffset + childOrdinalStart;
                const uint32_t parentIndex = levelIndex == 0u
                                                 ? Test16ClusterTraversalTest::InvalidIndex
                                                 : levelOffsets[levelIndex - 1u] + ordinal / parentFanout;
                ClusterAggregate aggregate = makeParentAggregate(tree.nodes, childStart, childCount);
                tree.nodes[nodeIndex] = makeClusterNodeFromAggregate(
                    aggregate,
                    nodeIndex,
                    levelIndex,
                    childStart,
                    childCount,
                    parentIndex);
            }
        }

        uint32_t maxLeafSourceCount = 0u;
        uint32_t maxChildCount = 0u;
        for (uint32_t nodeIndex = 0u; nodeIndex < totalNodeCount; ++nodeIndex)
        {
            maxChildCount = std::max(maxChildCount, tree.nodes[nodeIndex].childInfo.y);
        }
        for (uint32_t leafOrdinal = 0u; leafOrdinal < leafRanges.size(); ++leafOrdinal)
        {
            maxLeafSourceCount = std::max(maxLeafSourceCount, tree.nodes[leafLevelOffset + leafOrdinal].clusterInfo.x);
        }

        tree.stats.sourceSplatCount = static_cast<uint32_t>(scene.splats.size());
        tree.stats.clusterTreeNodeCount = totalNodeCount;
        tree.stats.leafClusterCount = static_cast<uint32_t>(leafRanges.size());
        tree.stats.avgSourceSplatsPerLeafCluster = static_cast<float>(scene.splats.size()) / static_cast<float>(leafRanges.size());
        tree.stats.maxSourceSplatsPerLeafCluster = maxLeafSourceCount;
        tree.stats.clusterTreeDepth = maxDepth;
        tree.stats.maxChildCount = maxChildCount;
        return tree;
    }

    /**
     * Reads a positive uint32 environment value or returns the provided default.
     */
    uint32_t readEnvUint(const char *name, uint32_t defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }

        const unsigned long long parsed = std::strtoull(value, nullptr, 10);
        if (parsed == 0ull || parsed > std::numeric_limits<uint32_t>::max())
        {
            return defaultValue;
        }
        return static_cast<uint32_t>(parsed);
    }

    /**
     * Reads a positive uint64 environment value or returns the provided default.
     */
    uint64_t readEnvUint64(const char *name, uint64_t defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }

        const unsigned long long parsed = std::strtoull(value, nullptr, 10);
        return parsed == 0ull ? defaultValue : static_cast<uint64_t>(parsed);
    }

    /**
     * Reads a positive floating-point environment value or returns the provided default.
     */
    float readEnvFloat(const char *name, float defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }

        const float parsed = std::strtof(value, nullptr);
        return std::isfinite(parsed) && parsed > 0.0f ? parsed : defaultValue;
    }

    /**
     * Reads a boolean environment value using the shared test parser.
     */
    bool readEnvBool(const char *name, bool defaultValue)
    {
        return GVM::Tests::readEnvBool(name, defaultValue);
    }

    /**
     * Returns a stable printable label for the active RHI backend.
     */
    const char *backendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal:
            return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan:
            return "vulkan";
        default:
            return "undefined";
        }
    }

    /**
     * Computes the same screen-space split metric as the Test16 GPU node-test pass.
     */
    float cpuClusterScreenErrorPx(const Test16ClusterTraversalTest::ClusterTreeNode &cluster,
                                  const Test16ClusterTraversalTest::ClusterTraversalGlobals &globals)
    {
        const float depth = std::max(cluster.boundsCenterRadius.z, globals.lod.z);
        const float focal = std::max(globals.projection.x, globals.projection.y);
        const float geometricError = std::max(cluster.errorInfo.x, 0.0f);
        const float appearanceError = std::max(cluster.errorInfo.y, 0.0f) * globals.lod.y;
        return std::max(geometricError, appearanceError) * focal / depth;
    }

    /**
     * Returns the conservative projected-entry cost for rendering one selected cluster.
     */
    uint32_t cpuClusterSelectedEntryCost(const Test16ClusterTraversalTest::ClusterTreeNode &cluster)
    {
        return cluster.childInfo.y == 0u ? std::max<uint32_t>(std::min<uint32_t>(cluster.clusterInfo.x, kMaxSourceSplatsPerLeafCluster), 1u) : 1u;
    }

    /**
     * Returns the minimum entry budget needed for this node to remain renderable in the CPU reference.
     */
    uint32_t cpuClusterBudgetMinEntryCost(const Test16ClusterTraversalTest::ClusterTreeNode &cluster)
    {
        return std::max(cluster.budgetInfo.x, cpuClusterSelectedEntryCost(cluster));
    }

    /**
     * Returns the budget-independent full-subtree entry demand stored in the synthetic cluster node.
     */
    uint32_t cpuClusterBudgetFullEntryCost(const Test16ClusterTraversalTest::ClusterTreeNode &cluster)
    {
        return std::max(cluster.budgetInfo.y, cpuClusterBudgetMinEntryCost(cluster));
    }

    /**
     * Computes the same deterministic budget priority used by the GPU node-test traversal.
     */
    float cpuClusterBudgetPriority(const Test16ClusterTraversalTest::ClusterTreeNode &cluster,
                                   const Test16ClusterTraversalTest::ClusterTraversalGlobals &globals)
    {
        const float depth = std::max(cluster.boundsCenterRadius.z, globals.lod.z);
        const float focal = std::max(globals.projection.x, globals.projection.y);
        const float featureSize = std::max(cluster.errorInfo.z * 2.0f * std::max(cluster.positionOpacityProfile.w, 1.0f), 0.00001f);
        return std::max(featureSize * focal / depth, 0.0001f);
    }

    /**
     * Returns true when a CPU reference cluster has a valid contiguous child range.
     */
    bool cpuClusterHasValidChildren(const SyntheticClusterTree &tree,
                                    const Test16ClusterTraversalTest::ClusterTreeNode &cluster)
    {
        const uint32_t clusterNodeCount = static_cast<uint32_t>(tree.nodes.size());
        const uint32_t childStart = cluster.childInfo.x;
        const uint32_t childCount = cluster.childInfo.y;
        return childCount > 0u && childStart < clusterNodeCount && childCount <= clusterNodeCount - childStart;
    }

    /**
     * Returns true when the CPU reference should refine a cluster by screen-space error.
     */
    bool cpuClusterShouldSplit(const SyntheticClusterTree &tree,
                               const Test16ClusterTraversalTest::ClusterTreeNode &cluster,
                               const Test16ClusterTraversalTest::ClusterTraversalGlobals &globals)
    {
        return cpuClusterHasValidChildren(tree, cluster) && cpuClusterScreenErrorPx(cluster, globals) > globals.lod.x;
    }

    /**
     * Counts children and their minimum selected-entry cost for a CPU reference split.
     */
    std::pair<uint32_t, uint32_t> cpuChildSplitCost(const SyntheticClusterTree &tree,
                                                    const Test16ClusterTraversalTest::ClusterTreeNode &cluster)
    {
        if (!cpuClusterHasValidChildren(tree, cluster))
        {
            return {0u, 0u};
        }
        const uint32_t childCount = cluster.childInfo.y;
        const uint32_t minimumEntryCost = std::max(cluster.budgetInfo.z, childCount);
        return {childCount, minimumEntryCost};
    }

    /**
     * Assigns the same deterministic child budget as the GPU node-test traversal.
     */
    uint32_t cpuChildEntryBudget(const SyntheticClusterTree &tree,
                                 const Test16ClusterTraversalTest::ClusterTraversalGlobals &globals,
                                 const Test16ClusterTraversalTest::ClusterTreeNode &parent,
                                 uint32_t parentEntryBudget,
                                 uint32_t targetChildNodeIndex)
    {
        const uint32_t clusterNodeCount = static_cast<uint32_t>(tree.nodes.size());
        if (targetChildNodeIndex >= clusterNodeCount)
        {
            return 0u;
        }

        const auto &targetChild = tree.nodes[targetChildNodeIndex];
        const uint32_t targetBaseCost = cpuClusterBudgetMinEntryCost(targetChild);
        const uint32_t targetFullCost = cpuClusterBudgetFullEntryCost(targetChild);
        const uint32_t minimumEntryTotal = std::max(parent.budgetInfo.z, parent.childInfo.y);

        if (parentEntryBudget <= minimumEntryTotal)
        {
            return targetBaseCost;
        }

        const uint32_t unlockBudgetStart = parentEntryBudget - minimumEntryTotal;
        uint32_t remainingBudget = unlockBudgetStart;
        uint32_t targetBudget = targetBaseCost;
        bool targetUnlocked = false;
        uint32_t targetChunkCost = 0u;
        uint32_t unlockedChunkTotal = 0u;
        bool hasLastUnlockedChild = false;
        float lastUnlockedPriority = std::numeric_limits<float>::max();
        uint32_t lastUnlockedNodeIndex = Test16ClusterTraversalTest::InvalidIndex;
        for (uint32_t unlockOrdinal = 0u; unlockOrdinal < parent.childInfo.y; ++unlockOrdinal)
        {
            float bestPriority = -1.0f;
            uint32_t bestNodeIndex = Test16ClusterTraversalTest::InvalidIndex;
            uint32_t bestUnlockCost = 0u;
            uint32_t bestFirstSplitCost = 0u;
            uint32_t bestChunkCost = 0u;
            for (uint32_t childOrdinal = 0u; childOrdinal < parent.childInfo.y; ++childOrdinal)
            {
                const uint32_t childNodeIndex = parent.childInfo.x + childOrdinal;
                if (childNodeIndex < clusterNodeCount)
                {
                    const auto &child = tree.nodes[childNodeIndex];
                    const uint32_t baseCost = cpuClusterBudgetMinEntryCost(child);
                    const uint32_t firstSplitCost = std::max(child.budgetInfo.z, baseCost);
                    const uint32_t unlockCost = firstSplitCost > baseCost ? firstSplitCost - baseCost : 0u;
                    const float priority = cpuClusterBudgetPriority(child, globals);
                    const bool belowLastUnlockedChild = !hasLastUnlockedChild ||
                                                        priority < lastUnlockedPriority ||
                                                        (priority == lastUnlockedPriority && childNodeIndex > lastUnlockedNodeIndex);
                    if (unlockCost > 0u && unlockCost <= remainingBudget && belowLastUnlockedChild &&
                        (bestNodeIndex == Test16ClusterTraversalTest::InvalidIndex || priority > bestPriority || (priority == bestPriority && childNodeIndex < bestNodeIndex)))
                    {
                        bestPriority = priority;
                        bestNodeIndex = childNodeIndex;
                        bestUnlockCost = unlockCost;
                        bestFirstSplitCost = firstSplitCost;
                        bestChunkCost = child.budgetInfo.w;
                    }
                }
            }
            if (bestNodeIndex == Test16ClusterTraversalTest::InvalidIndex)
            {
                break;
            }

            remainingBudget -= bestUnlockCost;
            hasLastUnlockedChild = true;
            lastUnlockedPriority = bestPriority;
            lastUnlockedNodeIndex = bestNodeIndex;
            if (bestChunkCost > 0u)
            {
                unlockedChunkTotal += bestChunkCost;
            }
            if (bestNodeIndex == targetChildNodeIndex)
            {
                targetBudget = bestFirstSplitCost;
                targetChunkCost = bestChunkCost;
                targetUnlocked = true;
            }
        }

        if (!targetUnlocked)
        {
            return targetBudget;
        }

        if (unlockedChunkTotal > 0u && targetChunkCost > 0u && targetBudget < targetFullCost)
        {
            const uint32_t fullRoundCount = remainingBudget / unlockedChunkTotal;
            uint32_t targetExtra = fullRoundCount * targetChunkCost;
            targetExtra = std::min(targetExtra, targetFullCost - targetBudget);
            targetBudget += targetExtra;
            const uint32_t roundBudget = fullRoundCount * unlockedChunkTotal;
            uint32_t leftoverBudget = remainingBudget > roundBudget ? remainingBudget - roundBudget : 0u;
            uint32_t replayRemainingBudget = unlockBudgetStart;
            bool replayHasLastUnlockedChild = false;
            float replayLastUnlockedPriority = std::numeric_limits<float>::max();
            uint32_t replayLastUnlockedNodeIndex = Test16ClusterTraversalTest::InvalidIndex;
            for (uint32_t unlockOrdinal = 0u; unlockOrdinal < parent.childInfo.y && leftoverBudget > 0u; ++unlockOrdinal)
            {
                float bestPriority = -1.0f;
                uint32_t bestNodeIndex = Test16ClusterTraversalTest::InvalidIndex;
                uint32_t bestUnlockCost = 0u;
                uint32_t bestChunkCost = 0u;
                for (uint32_t childOrdinal = 0u; childOrdinal < parent.childInfo.y; ++childOrdinal)
                {
                    const uint32_t childNodeIndex = parent.childInfo.x + childOrdinal;
                    if (childNodeIndex < clusterNodeCount)
                    {
                        const auto &child = tree.nodes[childNodeIndex];
                        const uint32_t baseCost = cpuClusterBudgetMinEntryCost(child);
                        const uint32_t firstSplitCost = std::max(child.budgetInfo.z, baseCost);
                        const uint32_t unlockCost = firstSplitCost > baseCost ? firstSplitCost - baseCost : 0u;
                        const float priority = cpuClusterBudgetPriority(child, globals);
                        const bool belowLastUnlockedChild = !replayHasLastUnlockedChild ||
                                                            priority < replayLastUnlockedPriority ||
                                                            (priority == replayLastUnlockedPriority && childNodeIndex > replayLastUnlockedNodeIndex);
                        if (unlockCost > 0u && unlockCost <= replayRemainingBudget && belowLastUnlockedChild &&
                            (bestNodeIndex == Test16ClusterTraversalTest::InvalidIndex || priority > bestPriority || (priority == bestPriority && childNodeIndex < bestNodeIndex)))
                        {
                            bestPriority = priority;
                            bestNodeIndex = childNodeIndex;
                            bestUnlockCost = unlockCost;
                            bestChunkCost = child.budgetInfo.w;
                        }
                    }
                }
                if (bestNodeIndex == Test16ClusterTraversalTest::InvalidIndex)
                {
                    break;
                }

                replayRemainingBudget -= bestUnlockCost;
                replayHasLastUnlockedChild = true;
                replayLastUnlockedPriority = bestPriority;
                replayLastUnlockedNodeIndex = bestNodeIndex;
                if (bestChunkCost > 0u && bestChunkCost <= leftoverBudget)
                {
                    if (bestNodeIndex == targetChildNodeIndex && targetBudget < targetFullCost)
                    {
                        targetBudget += std::min(bestChunkCost, targetFullCost - targetBudget);
                    }
                    leftoverBudget -= bestChunkCost;
                }
            }
        }
        return targetBudget;
    }

    /**
     * Creates the traversal constants shared by the CPU reference and GPU pass.
     */
    Test16ClusterTraversalTest::ClusterTraversalGlobals makeGlobals(uint32_t clusterNodeCount,
                                                                    uint32_t frontierCapacity,
                                                                    uint32_t maxSelectedClusterCount,
                                                                    uint32_t rootEntryBudget,
                                                                    float thresholdPx,
                                                                    uint32_t evaluatedLevelCount = 0u)
    {
        Test16ClusterTraversalTest::ClusterTraversalGlobals globals = {};
        (void)evaluatedLevelCount;
        globals.counts = uint4(clusterNodeCount, frontierCapacity, maxSelectedClusterCount, rootEntryBudget);
        globals.projection = float4(900.0f, 900.0f, 0.0f, 0.0f);
        globals.lod = float4(thresholdPx, 1.0f, 0.05f, 0.0f);
        return globals;
    }

    /**
     * Writes deterministic render and traversal payload fields for one synthetic cluster.
     */
    Test16ClusterTraversalTest::ClusterTreeNode makeSyntheticClusterNode(uint32_t nodeIndex,
                                                                        uint32_t level,
                                                                        uint32_t maxDepth,
                                                                        uint32_t childStart,
                                                                        uint32_t childCount,
                                                                        uint32_t parentIndex)
    {
        Test16ClusterTraversalTest::ClusterTreeNode node = {};
        const bool isLeaf = childCount == 0u;
        const float levelScale = 1.0f + static_cast<float>(level) * 0.15f;
        const float x = (static_cast<float>(nodeIndex & 1023u) - 511.5f) * 0.0004f * levelScale;
        const float y = (static_cast<float>((nodeIndex >> 10u) & 1023u) - 511.5f) * 0.00035f * levelScale;
        const float z = 48.0f + static_cast<float>(level) * 0.12f;
        const float radius = std::max(0.02f, 24.0f * std::pow(0.52f, static_cast<float>(level)));
        const uint32_t remainingDepth = maxDepth >= level ? maxDepth - level : 0u;
        const float geometricError = isLeaf ? 0.015f : std::max(0.05f, 0.035f * std::pow(1.8f, static_cast<float>(remainingDepth)));
        const float axisRadius = std::max(0.005f, radius * 0.025f);

        node.positionOpacityProfile = float4(x, y, z, 0.82f);
        node.axis0 = float4(axisRadius, 0.0f, 0.0f, 0.0f);
        node.axis1 = float4(0.0f, axisRadius * 0.8f, 0.0f, 0.0f);
        node.axis2 = float4(0.0f, 0.0f, axisRadius * 0.6f, 0.0f);
        node.shDc = float4(0.45f + float(nodeIndex & 7u) * 0.03f, 0.52f, 0.58f, 0.0f);
        node.boundsCenterRadius = float4(x, y, z, radius);
        node.errorInfo = float4(geometricError, 0.0f, axisRadius * 2.0f, 0.0f);
        node.childInfo = uint4(childStart, childCount, level, isLeaf ? Test16ClusterTraversalTest::ClusterFlagLeaf : 0u);
        node.clusterInfo = uint4(0u, nodeIndex, parentIndex, 0u);
        return node;
    }

    /**
     * Clamps an accumulated synthetic budget entry count to the uint32 storage used by GPU nodes.
     */
    uint32_t clampBudgetEntryCount(uint64_t value)
    {
        return static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max()));
    }

    /**
     * Populates runtime-budget-independent subtree budget metadata for a synthetic Test16 tree.
     */
    void populateSyntheticBudgetInfo(SyntheticClusterTree &tree)
    {
        for (uint32_t reverseIndex = static_cast<uint32_t>(tree.nodes.size()); reverseIndex > 0u; --reverseIndex)
        {
            auto &node = tree.nodes[reverseIndex - 1u];
            const uint32_t minEntryCost = cpuClusterSelectedEntryCost(node);
            uint64_t fullEntryCost = minEntryCost;
            uint64_t childMinEntrySum = 0u;
            uint32_t refineQuantum = 0u;
            if (node.childInfo.y > 0u)
            {
                fullEntryCost = 0u;
                for (uint32_t childOrdinal = 0u; childOrdinal < node.childInfo.y; ++childOrdinal)
                {
                    const uint32_t childIndex = node.childInfo.x + childOrdinal;
                    if (childIndex < tree.nodes.size())
                    {
                        const auto &child = tree.nodes[childIndex];
                        fullEntryCost += std::max(child.budgetInfo.y, child.budgetInfo.x);
                        const uint32_t childMinEntryCost = std::max(child.budgetInfo.x, cpuClusterSelectedEntryCost(child));
                        childMinEntrySum += childMinEntryCost;
                        const uint32_t childFirstSplitCost = child.budgetInfo.z;
                        if (childFirstSplitCost > childMinEntryCost)
                        {
                            const uint32_t candidateQuantum = childFirstSplitCost - childMinEntryCost;
                            refineQuantum = refineQuantum == 0u ? candidateQuantum : std::min(refineQuantum, candidateQuantum);
                        }
                    }
                }
                fullEntryCost = std::max<uint64_t>(fullEntryCost, minEntryCost);
            }

            const uint32_t fullEntryCostClamped = clampBudgetEntryCount(fullEntryCost);
            node.budgetInfo = uint4(
                minEntryCost,
                fullEntryCostClamped,
                clampBudgetEntryCount(childMinEntrySum),
                refineQuantum);
        }
    }

    /**
     * Builds a compact top-down cluster-16 tree with contiguous child ranges and root index zero.
     */
    SyntheticClusterTree buildSyntheticCluster16Tree(uint32_t sourceSplatCount, uint32_t targetSplatsPerCluster, uint32_t parentFanout)
    {
        if (sourceSplatCount == 0u)
        {
            throw std::invalid_argument("Test16 synthetic cluster tree requires at least one source splat");
        }
        if (targetSplatsPerCluster == 0u || targetSplatsPerCluster > kMaxSourceSplatsPerLeafCluster)
        {
            throw std::invalid_argument("Test16 synthetic cluster tree target cluster size is outside the supported range");
        }
        if (parentFanout == 0u)
        {
            throw std::invalid_argument("Test16 synthetic cluster tree requires a non-zero parent fanout");
        }

        std::vector<uint32_t> levelCounts;
        levelCounts.push_back(ceilDivU32(sourceSplatCount, targetSplatsPerCluster));
        while (levelCounts.back() > 1u)
        {
            levelCounts.push_back(ceilDivU32(levelCounts.back(), parentFanout));
        }
        std::reverse(levelCounts.begin(), levelCounts.end());

        std::vector<uint32_t> levelOffsets(levelCounts.size(), 0u);
        uint32_t totalNodeCount = 0u;
        for (size_t level = 0u; level < levelCounts.size(); ++level)
        {
            levelOffsets[level] = totalNodeCount;
            totalNodeCount += levelCounts[level];
        }

        SyntheticClusterTree tree = {};
        tree.nodes.resize(totalNodeCount);
        tree.levelCounts = levelCounts;
        const uint32_t maxDepth = static_cast<uint32_t>(levelCounts.size() - 1u);
        for (size_t levelIndex = 0u; levelIndex < levelCounts.size(); ++levelIndex)
        {
            const uint32_t level = static_cast<uint32_t>(levelIndex);
            const uint32_t nodeCountAtLevel = levelCounts[levelIndex];
            const bool isLeafLevel = levelIndex + 1u == levelCounts.size();
            const uint32_t childLevelCount = isLeafLevel ? 0u : levelCounts[levelIndex + 1u];
            const uint32_t childLevelOffset = isLeafLevel ? 0u : levelOffsets[levelIndex + 1u];

            for (uint32_t ordinal = 0u; ordinal < nodeCountAtLevel; ++ordinal)
            {
                const uint32_t nodeIndex = levelOffsets[levelIndex] + ordinal;
                const uint32_t childOrdinalStart = ordinal * parentFanout;
                const uint32_t childCount = isLeafLevel || childOrdinalStart >= childLevelCount
                                                ? 0u
                                                : std::min(parentFanout, childLevelCount - childOrdinalStart);
                const uint32_t childStart = childCount == 0u ? 0u : childLevelOffset + childOrdinalStart;
                const uint32_t parentIndex = levelIndex == 0u
                                                 ? Test16ClusterTraversalTest::InvalidIndex
                                                 : levelOffsets[levelIndex - 1u] + ordinal / parentFanout;
                auto node = makeSyntheticClusterNode(nodeIndex, level, maxDepth, childStart, childCount, parentIndex);
                if (isLeafLevel)
                {
                    const uint32_t sourceStart = ordinal * targetSplatsPerCluster;
                    const uint32_t remaining = sourceSplatCount - sourceStart;
                    node.clusterInfo.x = std::min(targetSplatsPerCluster, remaining);
                }
                tree.nodes[nodeIndex] = node;
            }
        }

        for (uint32_t reverseIndex = totalNodeCount; reverseIndex > 0u; --reverseIndex)
        {
            auto &node = tree.nodes[reverseIndex - 1u];
            const uint32_t childCount = node.childInfo.y;
            if (childCount == 0u)
            {
                continue;
            }

            uint32_t coveredSourceSplats = 0u;
            for (uint32_t childOrdinal = 0u; childOrdinal < childCount; ++childOrdinal)
            {
                coveredSourceSplats += tree.nodes[node.childInfo.x + childOrdinal].clusterInfo.x;
            }
            node.clusterInfo.x = coveredSourceSplats;
        }

        populateSyntheticBudgetInfo(tree);

        uint32_t maxLeafSourceCount = 0u;
        uint32_t maxChildCount = 0u;
        const uint32_t leafLevelOffset = levelOffsets.back();
        const uint32_t leafClusterCount = levelCounts.back();
        for (uint32_t nodeIndex = 0u; nodeIndex < totalNodeCount; ++nodeIndex)
        {
            maxChildCount = std::max(maxChildCount, tree.nodes[nodeIndex].childInfo.y);
        }
        for (uint32_t leafOrdinal = 0u; leafOrdinal < leafClusterCount; ++leafOrdinal)
        {
            maxLeafSourceCount = std::max(maxLeafSourceCount, tree.nodes[leafLevelOffset + leafOrdinal].clusterInfo.x);
        }

        tree.stats.sourceSplatCount = sourceSplatCount;
        tree.stats.clusterTreeNodeCount = totalNodeCount;
        tree.stats.leafClusterCount = leafClusterCount;
        tree.stats.avgSourceSplatsPerLeafCluster = static_cast<float>(sourceSplatCount) / static_cast<float>(leafClusterCount);
        tree.stats.maxSourceSplatsPerLeafCluster = maxLeafSourceCount;
        tree.stats.clusterTreeDepth = maxDepth;
        tree.stats.maxChildCount = maxChildCount;
        return tree;
    }

    /**
     * Builds the exact CPU frontier cut expected from the Test16 GPU cluster traversal.
     */
    CpuTraversalResult buildCpuReference(const SyntheticClusterTree &tree, float thresholdPx, uint32_t rootEntryBudget)
    {
        const uint32_t clusterNodeCount = static_cast<uint32_t>(tree.nodes.size());
        auto globals = makeGlobals(clusterNodeCount, clusterNodeCount, clusterNodeCount, rootEntryBudget, thresholdPx);

        CpuTraversalResult result = {};
        std::vector<CpuTraversalFrontierItem> frontier;
        frontier.reserve(clusterNodeCount);
        frontier.push_back({.clusterIndex = 0u, .entryBudget = std::max(rootEntryBudget, cpuClusterBudgetMinEntryCost(tree.nodes.front()))});
        result.enqueuedCount = 1u;

        for (size_t cursor = 0u; cursor < frontier.size(); ++cursor)
        {
            const CpuTraversalFrontierItem item = frontier[cursor];
            const uint32_t clusterIndex = item.clusterIndex;
            if (clusterIndex >= clusterNodeCount)
            {
                continue;
            }

            ++result.processedCount;
            const auto &cluster = tree.nodes[clusterIndex];
            result.maxDepthReached = std::max(result.maxDepthReached, cluster.childInfo.z);
            const auto [childCount, childMinEntryCost] = cpuClusterHasValidChildren(tree, cluster) ? cpuChildSplitCost(tree, cluster) : std::pair<uint32_t, uint32_t>{0u, 0u};
            if (childCount > 0u && item.entryBudget >= childMinEntryCost)
            {
                for (uint32_t childOrdinal = 0u; childOrdinal < cluster.childInfo.y; ++childOrdinal)
                {
                    const uint32_t childNodeIndex = cluster.childInfo.x + childOrdinal;
                    if (childNodeIndex < clusterNodeCount)
                    {
                        const uint32_t childEntryBudget = cpuChildEntryBudget(tree, globals, cluster, item.entryBudget, childNodeIndex);
                        frontier.push_back({.clusterIndex = childNodeIndex, .entryBudget = childEntryBudget});
                        ++result.enqueuedCount;
                    }
                }
            }
            else
            {
                result.selected.push_back(clusterIndex);
                result.estimatedEntryCount += cpuClusterSelectedEntryCost(cluster);
            }
        }
        return result;
    }

    /**
     * Estimates the GPU buffer footprint used by one Test16 node-test scenario.
     */
    uint64_t estimateGpuBytes(uint32_t clusterNodeCount, uint32_t frontierCapacity, uint32_t outputCapacity)
    {
        return uint64_t(clusterNodeCount) * sizeof(Test16ClusterTraversalTest::ClusterTreeNode) +
               uint64_t(frontierCapacity) * sizeof(uint32_t) * 2u +
               uint64_t(frontierCapacity) * sizeof(uint32_t) * 2u +
               uint64_t(outputCapacity) * sizeof(uint32_t) +
               uint64_t(kStateCount) * sizeof(uint32_t) +
               sizeof(Test16ClusterTraversalTest::ClusterTraversalGlobals) +
               sizeof(uint32_t);
    }

    /**
     * Compares GPU and CPU selected cluster outputs as exact unordered sets.
     */
    ::testing::AssertionResult selectedClusterSetsMatch(const std::vector<uint32_t> &gpuSelected, const std::vector<uint32_t> &cpuSelected)
    {
        if (gpuSelected.size() != cpuSelected.size())
        {
            return ::testing::AssertionFailure() << "selected cluster count mismatch gpu=" << gpuSelected.size() << " cpu=" << cpuSelected.size();
        }
        if (cpuSelected.empty())
        {
            return ::testing::AssertionSuccess();
        }

        const uint32_t maxCpuIndex = *std::max_element(cpuSelected.begin(), cpuSelected.end());
        const uint32_t maxGpuIndex = gpuSelected.empty() ? 0u : *std::max_element(gpuSelected.begin(), gpuSelected.end());
        const uint32_t maxIndex = std::max(maxCpuIndex, maxGpuIndex);
        std::vector<uint8_t> selectedMask(size_t(maxIndex) + 1u, 0u);
        for (uint32_t clusterIndex : cpuSelected)
        {
            selectedMask[clusterIndex] = 1u;
        }

        for (uint32_t clusterIndex : gpuSelected)
        {
            if (clusterIndex >= selectedMask.size())
            {
                return ::testing::AssertionFailure() << "gpu selected cluster " << clusterIndex << " exceeds expected mask size " << selectedMask.size();
            }
            if (selectedMask[clusterIndex] == 0u)
            {
                return ::testing::AssertionFailure() << "unexpected or duplicate gpu selected cluster " << clusterIndex;
            }
            selectedMask[clusterIndex] = 0u;
        }

        for (uint32_t clusterIndex : cpuSelected)
        {
            if (selectedMask[clusterIndex] != 0u)
            {
                return ::testing::AssertionFailure() << "missing gpu selected cluster " << clusterIndex;
            }
        }
        return ::testing::AssertionSuccess();
    }

    /**
     * Uploads one trivially copyable value through a mapped host-visible buffer.
     */
    template <typename T>
    void uploadOne(GVM::RHI::Buffer buffer, const T &value)
    {
        buffer->map();
        auto *mapped = static_cast<T *>(buffer->getMappedRange(0u, sizeof(T)));
        ASSERT_NE(mapped, nullptr);
        if (mapped != nullptr)
        {
            *mapped = value;
        }
        buffer->unmap();
    }

    /**
     * Clears the host-visible completion flag before submitting traversal work.
     */
    void resetCompletion(GVM::RHI::Buffer completion)
    {
        uint32_t zero = 0u;
        uploadOne(completion, zero);
    }

    /**
     * Waits up to the fixed Test16 watchdog interval for the GPU completion flag.
     */
    bool waitForCompletion(GVM::RHI::Buffer completion)
    {
        completion->map();
        const auto *mapped = static_cast<const volatile uint32_t *>(completion->getConstMappedRange(0u, sizeof(uint32_t)));
        if (mapped == nullptr)
        {
            completion->unmap();
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        while ((std::chrono::steady_clock::now() - start) < kCompletionTimeout)
        {
            if (*mapped == 1u)
            {
                completion->unmap();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        completion->unmap();
        return false;
    }

    /**
     * Verifies that the real scene builder emits a compact ClusterTreeNode hierarchy from CPU Gaussian data.
     */
    TEST(Cluster16BuilderTest, BuildsClusterTreeFromCpuGaussianScene)
    {
        const CpuGaussianScene scene = DebugSceneFactory::makeCalibrationScene(13u, 0.18f);
        const SyntheticClusterTree tree = buildCluster16TreeFromScene(
            scene,
            kTargetSourceSplatsPerLeafCluster,
            kParentClusterFanout);

        ASSERT_FALSE(tree.nodes.empty());
        ASSERT_FALSE(tree.levelCounts.empty());
        EXPECT_EQ(tree.nodes.front().clusterInfo.x, static_cast<uint32_t>(scene.splats.size()));
        EXPECT_EQ(tree.stats.sourceSplatCount, static_cast<uint32_t>(scene.splats.size()));
        EXPECT_GT(tree.stats.leafClusterCount, 0u);
        EXPECT_LE(tree.stats.maxSourceSplatsPerLeafCluster, kMaxSourceSplatsPerLeafCluster);
        EXPECT_LE(tree.stats.maxChildCount, kParentClusterFanout);
        EXPECT_LT(tree.stats.clusterTreeDepth, 32u);
        EXPECT_NEAR(tree.stats.avgSourceSplatsPerLeafCluster, 16.0f, 2.0f);
        EXPECT_EQ(tree.nodes.front().clusterInfo.z, Test16ClusterTraversalTest::InvalidIndex);

        for (uint32_t clusterIndex = 0u; clusterIndex < tree.nodes.size(); ++clusterIndex)
        {
            const auto &node = tree.nodes[clusterIndex];
            EXPECT_EQ(node.clusterInfo.y, clusterIndex);
            if (clusterIndex != 0u)
            {
                ASSERT_NE(node.clusterInfo.z, Test16ClusterTraversalTest::InvalidIndex);
                ASSERT_LT(node.clusterInfo.z, clusterIndex);
            }

            const uint32_t childStart = node.childInfo.x;
            const uint32_t childCount = node.childInfo.y;
            if (childCount == 0u)
            {
                EXPECT_EQ(node.childInfo.w & Test16ClusterTraversalTest::ClusterFlagLeaf, Test16ClusterTraversalTest::ClusterFlagLeaf);
                continue;
            }

            ASSERT_LT(childStart, tree.nodes.size());
            ASSERT_LE(childCount, tree.nodes.size() - childStart);
            for (uint32_t childOrdinal = 0u; childOrdinal < childCount; ++childOrdinal)
            {
                EXPECT_EQ(tree.nodes[childStart + childOrdinal].clusterInfo.z, clusterIndex);
            }
        }
    }

    /**
     * Builds Test16 ClusterTreeNode diagnostics from an optional real PLY scene path.
     */
    TEST(Cluster16BuilderTest, BuildsClusterTreeFromOptionalPlyScene)
    {
        const char *scenePath = std::getenv("GVM_TEST16_CLUSTER_PLY_PATH");
        if (scenePath == nullptr || scenePath[0] == '\0')
        {
            GTEST_SKIP() << "Set GVM_TEST16_CLUSTER_PLY_PATH to run the optional real PLY Test16 cluster builder diagnostic.";
        }

        const SceneSafety::PreparedScene prepared = PlyLoader::loadPreparedForStableBaseline(scenePath);
        const SyntheticClusterTree tree = buildCluster16TreeFromScene(
            prepared.scene,
            kTargetSourceSplatsPerLeafCluster,
            kParentClusterFanout);

        ASSERT_FALSE(tree.nodes.empty());
        EXPECT_EQ(tree.nodes.front().clusterInfo.x, static_cast<uint32_t>(prepared.scene.splats.size()));
        EXPECT_EQ(tree.stats.sourceSplatCount, static_cast<uint32_t>(prepared.scene.splats.size()));
        EXPECT_GT(tree.stats.leafClusterCount, 0u);
        EXPECT_LE(tree.stats.maxSourceSplatsPerLeafCluster, kMaxSourceSplatsPerLeafCluster);
        EXPECT_LE(tree.stats.maxChildCount, kParentClusterFanout);
        EXPECT_LT(tree.stats.clusterTreeDepth, 32u);

        std::fprintf(
            stderr,
            "[gvm-rhi-cluster16-builder] path=%s source_splats=%u cluster_nodes=%u leaf_clusters=%u avg_leaf_splats=%.3f max_leaf_splats=%u depth=%u max_child_count=%u\n",
            scenePath,
            tree.stats.sourceSplatCount,
            tree.stats.clusterTreeNodeCount,
            tree.stats.leafClusterCount,
            tree.stats.avgSourceSplatsPerLeafCluster,
            tree.stats.maxSourceSplatsPerLeafCluster,
            tree.stats.clusterTreeDepth,
            tree.stats.maxChildCount);
        std::fflush(stderr);

        RecordProperty("ply_source_splat_count", static_cast<int>(tree.stats.sourceSplatCount));
        RecordProperty("ply_cluster_tree_node_count", static_cast<int>(tree.stats.clusterTreeNodeCount));
        RecordProperty("ply_leaf_cluster_count", static_cast<int>(tree.stats.leafClusterCount));
        RecordProperty("ply_cluster_tree_depth", static_cast<int>(tree.stats.clusterTreeDepth));
    }

    /**
     * Runs Test16 Cluster-16 traversal scenarios against the active RHI backend.
     */
    class Cluster16TraversalRhiTest : public ::testing::Test
    {
    protected:
        /**
         * Creates a headless device for Metal or Vulkan traversal tests.
         */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "Test16 Cluster-16 traversal test requires Metal or Vulkan backend, got " << backendName(backend);
            }

            rawDevice = instance->createDevice();
            ASSERT_NE(rawDevice, nullptr);
            ASSERT_NE(rawDevice->getMainQueue(), nullptr);
            device = GVM::Core::DeviceProxy(rawDevice);
        }

        /**
         * Releases the headless device after each traversal scenario.
         */
        void TearDown() override
        {
            device = {};
            rawDevice = nullptr;
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
        }

        /**
         * Builds, uploads, dispatches, and validates one Test16 cluster traversal scenario.
         */
        void runScenario(const Scenario &scenario)
        {
            ASSERT_GT(scenario.sourceSplatCount, 0u);
            SyntheticClusterTree tree = buildSyntheticCluster16Tree(
                scenario.sourceSplatCount,
                kTargetSourceSplatsPerLeafCluster,
                kParentClusterFanout);
            ASSERT_FALSE(tree.nodes.empty());
            ASSERT_EQ(tree.nodes.front().clusterInfo.x, scenario.sourceSplatCount);
            ASSERT_LE(tree.stats.maxSourceSplatsPerLeafCluster, kMaxSourceSplatsPerLeafCluster);
            ASSERT_LE(tree.stats.maxChildCount, kParentClusterFanout);

            const uint32_t rootEntryBudget = scenario.projectedEntryBudget == 0u ? scenario.sourceSplatCount : scenario.projectedEntryBudget;
            const auto cpu = buildCpuReference(tree, scenario.thresholdPx, rootEntryBudget);
            ASSERT_GT(cpu.processedCount, 0u);
            ASSERT_GT(cpu.enqueuedCount, 0u);
            ASSERT_LE(cpu.estimatedEntryCount, rootEntryBudget);
            ASSERT_LE(cpu.enqueuedCount, tree.nodes.size());
            ASSERT_LE(cpu.maxDepthReached, tree.stats.clusterTreeDepth);

            const uint32_t clusterNodeCount = static_cast<uint32_t>(tree.nodes.size());
            const uint32_t frontierCapacity = clusterNodeCount;
            const uint32_t outputCapacity = std::max<uint32_t>(1u, std::min<uint32_t>(clusterNodeCount, static_cast<uint32_t>(cpu.selected.size()) + 1024u));
            const uint32_t traversalLevelCount = static_cast<uint32_t>(tree.levelCounts.size());
            const uint32_t encodedTraversalPassCount = traversalLevelCount;
            const uint32_t maxDispatchThreadCount = roundUpToWorkGroup(clusterNodeCount);
            const uint64_t totalDispatchThreadCount = uint64_t(maxDispatchThreadCount) * encodedTraversalPassCount;
            const uint64_t estimatedBytes = estimateGpuBytes(clusterNodeCount, frontierCapacity, outputCapacity);
            if (estimatedBytes > scenario.memoryBudgetBytes)
            {
                if (scenario.allowSkipOnBudget)
                {
                    GTEST_SKIP() << scenario.label << " estimated GPU bytes " << estimatedBytes
                                 << " exceeds budget " << scenario.memoryBudgetBytes;
                }
                FAIL() << scenario.label << " estimated GPU bytes " << estimatedBytes
                       << " exceeds budget " << scenario.memoryBudgetBytes;
            }

            auto globals = makeGlobals(clusterNodeCount, frontierCapacity, outputCapacity, rootEntryBudget, scenario.thresholdPx, traversalLevelCount);
            std::vector<uint32_t> frontierInitial(frontierCapacity, 0u);
            std::vector<uint32_t> frontierBudgetInitial(frontierCapacity, 0u);
            frontierInitial[0] = 0u;
            frontierBudgetInitial[0] = rootEntryBudget;
            std::vector<uint32_t> stateInitial(kStateCount, 0u);
            stateInitial[Test16ClusterTraversalTest::StateCurrentFrontierCount] = 1u;

            auto globalsBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterTraversalGlobals",
                .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
                .size = sizeof(Test16ClusterTraversalTest::ClusterTraversalGlobals),
            });
            auto clusterNodesBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterTreeNodes",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(tree.nodes.size()) * sizeof(Test16ClusterTraversalTest::ClusterTreeNode),
            });
            auto frontierABuffer = rawDevice->createBuffer({
                .label = "Test16ClusterFrontierA",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(frontierCapacity) * sizeof(uint32_t),
            });
            auto frontierBBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterFrontierB",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(frontierCapacity) * sizeof(uint32_t),
            });
            auto frontierBudgetABuffer = rawDevice->createBuffer({
                .label = "Test16ClusterFrontierBudgetA",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(frontierCapacity) * sizeof(uint32_t),
            });
            auto frontierBudgetBBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterFrontierBudgetB",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(frontierCapacity) * sizeof(uint32_t),
            });
            auto stateBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterTraversalState",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
                .size = uint64_t(kStateCount) * sizeof(uint32_t),
            });
            auto activeClusterIndexBuffer = rawDevice->createBuffer({
                .label = "Test16ActiveClusterIndex",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
                .size = uint64_t(outputCapacity) * sizeof(uint32_t),
            });
            auto completionBuffer = rawDevice->createBuffer({
                .label = "Test16ClusterTraversalCompletion",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
                .size = sizeof(uint32_t),
            });

            ASSERT_FALSE(globalsBuffer.isNull());
            ASSERT_FALSE(clusterNodesBuffer.isNull());
            ASSERT_FALSE(frontierABuffer.isNull());
            ASSERT_FALSE(frontierBBuffer.isNull());
            ASSERT_FALSE(frontierBudgetABuffer.isNull());
            ASSERT_FALSE(frontierBudgetBBuffer.isNull());
            ASSERT_FALSE(stateBuffer.isNull());
            ASSERT_FALSE(activeClusterIndexBuffer.isNull());
            ASSERT_FALSE(completionBuffer.isNull());

            auto uploadQueue = device->graphicsQueue(0);
            ASSERT_TRUE(static_cast<bool>(uploadQueue));
            uploadQueue
                ->writeBuffer(GVM::RHI::BufferRange(globalsBuffer, 0u, sizeof(globals)), &globals, sizeof(globals))
                ->writeBuffer(GVM::RHI::BufferRange(clusterNodesBuffer, 0u, uint64_t(tree.nodes.size()) * sizeof(Test16ClusterTraversalTest::ClusterTreeNode)),
                              tree.nodes.data(),
                              uint64_t(tree.nodes.size()) * sizeof(Test16ClusterTraversalTest::ClusterTreeNode))
                ->writeBuffer(GVM::RHI::BufferRange(frontierABuffer, 0u, uint64_t(frontierInitial.size()) * sizeof(uint32_t)), frontierInitial.data(), uint64_t(frontierInitial.size()) * sizeof(uint32_t))
                ->writeBuffer(GVM::RHI::BufferRange(frontierBBuffer, 0u, uint64_t(frontierInitial.size()) * sizeof(uint32_t)), frontierInitial.data(), uint64_t(frontierInitial.size()) * sizeof(uint32_t))
                ->writeBuffer(GVM::RHI::BufferRange(frontierBudgetABuffer, 0u, uint64_t(frontierBudgetInitial.size()) * sizeof(uint32_t)), frontierBudgetInitial.data(), uint64_t(frontierBudgetInitial.size()) * sizeof(uint32_t))
                ->writeBuffer(GVM::RHI::BufferRange(frontierBudgetBBuffer, 0u, uint64_t(frontierBudgetInitial.size()) * sizeof(uint32_t)), frontierBudgetInitial.data(), uint64_t(frontierBudgetInitial.size()) * sizeof(uint32_t))
                ->writeBuffer(GVM::RHI::BufferRange(stateBuffer, 0u, uint64_t(stateInitial.size()) * sizeof(uint32_t)), stateInitial.data(), uint64_t(stateInitial.size()) * sizeof(uint32_t))
                ->submit();
            resetCompletion(completionBuffer);

            auto traversalAToB = device->createBindGroup<Test16ClusterTraversalTest::ClusterTraversalBindGroup>(
                GVM::RHI::BufferRange(globalsBuffer),
                GVM::RHI::BufferRange(clusterNodesBuffer),
                GVM::RHI::BufferRange(frontierABuffer),
                GVM::RHI::BufferRange(frontierBudgetABuffer),
                GVM::RHI::BufferRange(frontierBBuffer),
                GVM::RHI::BufferRange(frontierBudgetBBuffer),
                GVM::RHI::BufferRange(stateBuffer),
                GVM::RHI::BufferRange(activeClusterIndexBuffer));
            auto traversalBToA = device->createBindGroup<Test16ClusterTraversalTest::ClusterTraversalBindGroup>(
                GVM::RHI::BufferRange(globalsBuffer),
                GVM::RHI::BufferRange(clusterNodesBuffer),
                GVM::RHI::BufferRange(frontierBBuffer),
                GVM::RHI::BufferRange(frontierBudgetBBuffer),
                GVM::RHI::BufferRange(frontierABuffer),
                GVM::RHI::BufferRange(frontierBudgetABuffer),
                GVM::RHI::BufferRange(stateBuffer),
                GVM::RHI::BufferRange(activeClusterIndexBuffer));
            auto traversalStateBindGroup = device->createBindGroup<Test16ClusterTraversalTest::ClusterTraversalStateBindGroup>(
                GVM::RHI::BufferRange(globalsBuffer),
                GVM::RHI::BufferRange(stateBuffer));
            auto completionBindGroup = device->createBindGroup<Test16ClusterTraversalTest::CompletionBindGroup>(
                GVM::RHI::BufferRange(completionBuffer));
            ASSERT_TRUE(static_cast<bool>(traversalAToB));
            ASSERT_TRUE(static_cast<bool>(traversalBToA));
            ASSERT_TRUE(static_cast<bool>(traversalStateBindGroup));
            ASSERT_TRUE(static_cast<bool>(completionBindGroup));

            auto evaluateFrontierAToBPass = device->createComputeClass<Test16ClusterTraversalTest::EvaluateClusterFrontierPass>(traversalAToB);
            auto evaluateFrontierBToAPass = device->createComputeClass<Test16ClusterTraversalTest::EvaluateClusterFrontierPass>(traversalBToA);
            auto prepareNextFrontierPass = device->createComputeClass<Test16ClusterTraversalTest::PrepareNextFrontierPass>(traversalStateBindGroup);
            auto completionPass = device->createComputeClass<Test16ClusterTraversalTest::SignalCompletionPass>(completionBindGroup);
            ASSERT_TRUE(static_cast<bool>(evaluateFrontierAToBPass));
            ASSERT_TRUE(static_cast<bool>(evaluateFrontierBToAPass));
            ASSERT_TRUE(static_cast<bool>(prepareNextFrontierPass));
            ASSERT_TRUE(static_cast<bool>(completionPass));

            auto *queue = rawDevice->getMainQueue();
            ASSERT_NE(queue, nullptr);
            auto commandEncoder = queue->createCommandEncoder();
            ASSERT_TRUE(static_cast<bool>(commandEncoder));

            const auto start = std::chrono::steady_clock::now();
            auto computePass = commandEncoder->beginComputePass({.label = "Test16ClusterTraversal"});
            ASSERT_TRUE(static_cast<bool>(computePass));
            for (uint32_t traversalPassIndex = 0u; traversalPassIndex < encodedTraversalPassCount; ++traversalPassIndex)
            {
                if ((traversalPassIndex & 1u) == 0u)
                {
                    evaluateFrontierAToBPass->run(maxDispatchThreadCount, 1u, 1u).dispatchFn(computePass);
                }
                else
                {
                    evaluateFrontierBToAPass->run(maxDispatchThreadCount, 1u, 1u).dispatchFn(computePass);
                }
                prepareNextFrontierPass->run(1u, 1u, 1u).dispatchFn(computePass);
            }
            completionPass->run(1u, 1u, 1u).dispatchFn(computePass);
            computePass->end();
            commandEncoder->end();

            eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
            encoders[0] = commandEncoder;
            queue->submit(encoders);
            ASSERT_TRUE(waitForCompletion(completionBuffer))
                << "GPU Test16 cluster traversal timed out after 2 seconds; scenario=" << scenario.label
                << " source_splats=" << scenario.sourceSplatCount
                << " cluster_nodes=" << clusterNodeCount
                << " leaf_clusters=" << tree.stats.leafClusterCount
                << " max_dispatch_threads=" << maxDispatchThreadCount
                << " total_dispatch_threads=" << totalDispatchThreadCount
                << " traversal_passes=" << encodedTraversalPassCount;
            const auto end = std::chrono::steady_clock::now();
            const double gpuMs = std::chrono::duration<double, std::milli>(end - start).count();

            std::vector<uint32_t> gpuState(kStateCount, 0u);
            device->graphicsQueue(0)
                ->readBuffer(GVM::RHI::BufferRange(stateBuffer, 0u, uint64_t(kStateCount) * sizeof(uint32_t)), gpuState.data(), uint64_t(kStateCount) * sizeof(uint32_t))
                ->submit();

            ASSERT_EQ(gpuState[Test16ClusterTraversalTest::StateOverflowCount], 0u);
            ASSERT_EQ(gpuState[Test16ClusterTraversalTest::StateCurrentFrontierCount], 0u);
            EXPECT_EQ(gpuState[Test16ClusterTraversalTest::StateProcessedClusterCount], cpu.processedCount);
            EXPECT_EQ(gpuState[Test16ClusterTraversalTest::StateSelectedClusterCount], static_cast<uint32_t>(cpu.selected.size()));
            EXPECT_EQ(gpuState[Test16ClusterTraversalTest::StateEstimatedEntryCount], cpu.estimatedEntryCount);
            EXPECT_LE(gpuState[Test16ClusterTraversalTest::StateEstimatedEntryCount], rootEntryBudget);
            EXPECT_EQ(gpuState[Test16ClusterTraversalTest::StateEvaluatedLevelCount], traversalLevelCount);
            ASSERT_LE(gpuState[Test16ClusterTraversalTest::StateSelectedClusterCount], outputCapacity);

            std::vector<uint32_t> gpuSelected(gpuState[Test16ClusterTraversalTest::StateSelectedClusterCount], 0u);
            if (!gpuSelected.empty())
            {
                device->graphicsQueue(0)
                    ->readBuffer(GVM::RHI::BufferRange(activeClusterIndexBuffer, 0u, uint64_t(gpuSelected.size()) * sizeof(uint32_t)), gpuSelected.data(), uint64_t(gpuSelected.size()) * sizeof(uint32_t))
                    ->submit();
            }

            EXPECT_TRUE(selectedClusterSetsMatch(gpuSelected, cpu.selected));

            std::fprintf(
                stderr,
                "[gvm-rhi-cluster16] label=%s algorithm=nanite-frontier-budgeted source_splats=%u cluster_nodes=%u leaf_clusters=%u avg_leaf_splats=%.3f max_leaf_splats=%u depth=%u threshold_px=%.3f root_entry_budget=%u estimated_entries=%u processed=%u selected=%zu max_dispatch_threads=%u total_dispatch_threads=%llu encoded_passes=%u evaluated_levels=%u gpu_ms=%.3f estimated_gpu_mb=%.2f\n",
                scenario.label,
                tree.stats.sourceSplatCount,
                tree.stats.clusterTreeNodeCount,
                tree.stats.leafClusterCount,
                tree.stats.avgSourceSplatsPerLeafCluster,
                tree.stats.maxSourceSplatsPerLeafCluster,
                tree.stats.clusterTreeDepth,
                scenario.thresholdPx,
                rootEntryBudget,
                cpu.estimatedEntryCount,
                cpu.processedCount,
                cpu.selected.size(),
                maxDispatchThreadCount,
                static_cast<unsigned long long>(totalDispatchThreadCount),
                encodedTraversalPassCount,
                traversalLevelCount,
                gpuMs,
                double(estimatedBytes) / double(1u << 20u));
            std::fflush(stderr);

            RecordProperty(std::string(scenario.label) + "_source_splat_count", static_cast<int>(tree.stats.sourceSplatCount));
            RecordProperty(std::string(scenario.label) + "_cluster_tree_node_count", static_cast<int>(tree.stats.clusterTreeNodeCount));
            RecordProperty(std::string(scenario.label) + "_leaf_cluster_count", static_cast<int>(tree.stats.leafClusterCount));
            RecordProperty(std::string(scenario.label) + "_selected_cluster_count", static_cast<int>(cpu.selected.size()));
            RecordProperty(std::string(scenario.label) + "_estimated_entry_count", static_cast<int>(cpu.estimatedEntryCount));
            RecordProperty(std::string(scenario.label) + "_cluster_tree_depth", static_cast<int>(tree.stats.clusterTreeDepth));
            RecordProperty(std::string(scenario.label) + "_gpu_ms", gpuMs);
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device rawDevice = nullptr;
        GVM::Core::DeviceProxy device;
    };

    /**
     * Verifies exact CPU/GPU selected cluster matches on the 4k-source small correctness case.
     */
    TEST_F(Cluster16TraversalRhiTest, ClusterTraversalMatchesCpuReferenceForSmallCluster16Tree)
    {
        runScenario({
            .label = "small-4k",
            .sourceSplatCount = 4096u,
            .thresholdPx = 1.0f,
            .memoryBudgetBytes = 128ull << 20u,
        });
    }

    /**
     * Verifies exact CPU/GPU selected cluster matches on the 64k-source large correctness case.
     */
    TEST_F(Cluster16TraversalRhiTest, ClusterTraversalMatchesCpuReferenceForLargeCluster16Tree)
    {
        runScenario({
            .label = "large-64k",
            .sourceSplatCount = 65536u,
            .thresholdPx = 1.0f,
            .projectedEntryBudget = 8192u,
            .memoryBudgetBytes = 128ull << 20u,
        });
    }

    /**
     * Verifies exact CPU/GPU selected cluster matches on the 1M-source medium correctness case.
     */
    TEST_F(Cluster16TraversalRhiTest, ClusterTraversalMatchesCpuReferenceForMediumCluster16Tree)
    {
        const uint32_t sourceSplatCount = readEnvUint("GVM_TEST16_CLUSTER_MEDIUM_SOURCE_SPLATS", 1000000u);
        const float thresholdPx = readEnvFloat("GVM_TEST16_CLUSTER_MEDIUM_THRESHOLD_PX", 1.0f);
        const uint64_t budgetMb = readEnvUint64("GVM_TEST16_CLUSTER_MEDIUM_MEMORY_BUDGET_MB", 512ull);

        runScenario({
            .label = "medium",
            .sourceSplatCount = sourceSplatCount,
            .thresholdPx = thresholdPx,
            .memoryBudgetBytes = budgetMb << 20u,
            .allowSkipOnBudget = true,
        });
    }

    /**
     * Runs the bicycle-equivalent no-frustum pressure case with exact CPU/GPU selected cluster validation.
     */
    TEST_F(Cluster16TraversalRhiTest, ClusterTraversalMatchesCpuReferenceForBicycleEquivalentPressure)
    {
        const uint32_t sourceSplatCount = readEnvUint("GVM_TEST16_CLUSTER_BICYCLE_SOURCE_SPLATS", 6131954u);
        const float thresholdPx = readEnvFloat("GVM_TEST16_CLUSTER_BICYCLE_THRESHOLD_PX", 1.0f);
        const uint32_t projectedEntryBudget = readEnvUint("GVM_TEST16_CLUSTER_BICYCLE_ENTRY_BUDGET", 1500000u);
        const uint64_t budgetMb = readEnvUint64("GVM_TEST16_CLUSTER_BICYCLE_MEMORY_BUDGET_MB", 1024ull);

        runScenario({
            .label = "bicycle-equivalent",
            .sourceSplatCount = sourceSplatCount,
            .thresholdPx = thresholdPx,
            .projectedEntryBudget = projectedEntryBudget,
            .memoryBudgetBytes = budgetMb << 20u,
            .allowSkipOnBudget = true,
        });
    }

    /**
     * Provides an opt-in 10M-source pressure case for Test16 Cluster-16 traversal scaling checks.
     */
    TEST_F(Cluster16TraversalRhiTest, DISABLED_ClusterTraversalMatchesCpuReferenceForTenMillionSourcePressure)
    {
        if (!readEnvBool("GVM_TEST16_CLUSTER_ENABLE_LARGE_PRESSURE", false))
        {
            GTEST_SKIP() << "Set GVM_TEST16_CLUSTER_ENABLE_LARGE_PRESSURE=1 and --gtest_also_run_disabled_tests to run the 10M-source Test16 pressure case.";
        }

        const uint32_t sourceSplatCount = readEnvUint("GVM_TEST16_CLUSTER_LARGE_SOURCE_SPLATS", 10000000u);
        const float thresholdPx = readEnvFloat("GVM_TEST16_CLUSTER_LARGE_THRESHOLD_PX", 1.0f);
        const uint64_t budgetMb = readEnvUint64("GVM_TEST16_CLUSTER_LARGE_MEMORY_BUDGET_MB", 1536ull);

        runScenario({
            .label = "large-10m",
            .sourceSplatCount = sourceSplatCount,
            .thresholdPx = thresholdPx,
            .memoryBudgetBytes = budgetMb << 20u,
            .allowSkipOnBudget = true,
        });
    }
} // namespace
