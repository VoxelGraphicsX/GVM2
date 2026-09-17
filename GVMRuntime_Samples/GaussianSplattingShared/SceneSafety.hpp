#ifndef GAUSSIAN_SPLATTING_SHARED_SCENE_SAFETY_HPP
#define GAUSSIAN_SPLATTING_SHARED_SCENE_SAFETY_HPP

#include "../TestUtils/GaussianSplattingDslShared/CpuGaussianScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace SceneSafety
{
    struct PreparedScene
    {
        CpuGaussianScene scene;
        std::size_t originalSplats = 0u;
        std::size_t invalidSplatsRemoved = 0u;
        std::size_t axisClamps = 0u;
        float sceneDiagonal = 1.0f;
        float minAxisLength = 0.001f;
    };

    inline bool isFinite3(const float values[3])
    {
        return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
    }

    inline float length3(const float values[3])
    {
        return std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
    }

    inline void setAxis(float axis[3], float x, float y, float z, float length)
    {
        axis[0] = x * length;
        axis[1] = y * length;
        axis[2] = z * length;
    }

    inline void sanitizeAxis(float axis[3], int axisIndex, float minAxisLength, std::size_t &axisClampCounter)
    {
        static constexpr float kFallbackAxes[3][3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };

        if (!isFinite3(axis))
        {
            setAxis(axis, kFallbackAxes[axisIndex][0], kFallbackAxes[axisIndex][1], kFallbackAxes[axisIndex][2], minAxisLength);
            ++axisClampCounter;
            return;
        }

        const float axisLength = length3(axis);
        if (!std::isfinite(axisLength) || axisLength < minAxisLength)
        {
            setAxis(axis, kFallbackAxes[axisIndex][0], kFallbackAxes[axisIndex][1], kFallbackAxes[axisIndex][2], minAxisLength);
            ++axisClampCounter;
            return;
        }
    }

    inline void recomputeBounds(CpuGaussianScene &scene)
    {
        scene.boundsMin[0] = scene.boundsMin[1] = scene.boundsMin[2] = std::numeric_limits<float>::max();
        scene.boundsMax[0] = scene.boundsMax[1] = scene.boundsMax[2] = -std::numeric_limits<float>::max();
        for (const CpuGaussianSplat &splat : scene.splats)
        {
            scene.expandBounds(splat);
        }
    }

    inline void setSceneBounds(CpuGaussianScene &scene, const float boundsMin[3], const float boundsMax[3])
    {
        scene.boundsMin[0] = boundsMin[0];
        scene.boundsMin[1] = boundsMin[1];
        scene.boundsMin[2] = boundsMin[2];
        scene.boundsMax[0] = boundsMax[0];
        scene.boundsMax[1] = boundsMax[1];
        scene.boundsMax[2] = boundsMax[2];
    }

    inline float computeSceneDiagonalFromBounds(const float boundsMin[3], const float boundsMax[3])
    {
        const float extentX = boundsMax[0] - boundsMin[0];
        const float extentY = boundsMax[1] - boundsMin[1];
        const float extentZ = boundsMax[2] - boundsMin[2];
        const float diagonal = std::sqrt(std::max(extentX * extentX + extentY * extentY + extentZ * extentZ, 0.0f));
        return std::isfinite(diagonal) && diagonal > 0.0f ? diagonal : 1.0f;
    }

    inline float computeMinAxisLengthForSceneDiagonal(float sceneDiagonal)
    {
        return std::max(sceneDiagonal * 1.0e-7f, 1.0e-6f);
    }

    inline void sanitizeSplatForStableBaseline(CpuGaussianSplat &splat, float minAxisLength, std::size_t &axisClampCounter)
    {
        sanitizeAxis(splat.axis0, 0, minAxisLength, axisClampCounter);
        sanitizeAxis(splat.axis1, 1, minAxisLength, axisClampCounter);
        sanitizeAxis(splat.axis2, 2, minAxisLength, axisClampCounter);

        splat.color[0] = std::isfinite(splat.color[0]) ? std::clamp(splat.color[0], 0.0f, 1.0f) : 1.0f;
        splat.color[1] = std::isfinite(splat.color[1]) ? std::clamp(splat.color[1], 0.0f, 1.0f) : 1.0f;
        splat.color[2] = std::isfinite(splat.color[2]) ? std::clamp(splat.color[2], 0.0f, 1.0f) : 1.0f;
        splat.shDc[0] = std::isfinite(splat.shDc[0]) ? splat.shDc[0] : 0.0f;
        splat.shDc[1] = std::isfinite(splat.shDc[1]) ? splat.shDc[1] : 0.0f;
        splat.shDc[2] = std::isfinite(splat.shDc[2]) ? splat.shDc[2] : 0.0f;
        for (float &coefficient : splat.shRest)
        {
            coefficient = std::isfinite(coefficient) ? coefficient : 0.0f;
        }
        splat.opacity = std::isfinite(splat.opacity) ? std::clamp(splat.opacity, 1.0f / 255.0f, 0.99f) : 0.99f;
    }

    inline float quantileFromSorted(const std::vector<float> &values, float quantile)
    {
        if (values.empty())
        {
            return 0.0f;
        }

        const float clampedQuantile = std::clamp(quantile, 0.0f, 1.0f);
        const std::size_t index = std::min<std::size_t>(
            static_cast<std::size_t>(clampedQuantile * float(values.size() - 1u)),
            values.size() - 1u
        );
        return values[index];
    }

    inline void computeRobustFocusHint(CpuGaussianScene &scene)
    {
        if (scene.empty())
        {
            scene.focusCenter[0] = scene.focusCenter[1] = scene.focusCenter[2] = 0.0f;
            scene.focusRadius = 1.0f;
            scene.hasFocusHint = false;
            return;
        }

        constexpr std::size_t kMaxSamples = 131072u;
        const std::size_t sampleCount = std::min<std::size_t>(scene.splats.size(), kMaxSamples);
        const std::size_t stride = std::max<std::size_t>(scene.splats.size() / sampleCount, 1u);

        std::vector<float> sampleX;
        std::vector<float> sampleY;
        std::vector<float> sampleZ;
        sampleX.reserve(sampleCount);
        sampleY.reserve(sampleCount);
        sampleZ.reserve(sampleCount);

        for (std::size_t splatIndex = 0u; splatIndex < scene.splats.size(); splatIndex += stride)
        {
            const CpuGaussianSplat &splat = scene.splats[splatIndex];
            if (!isFinite3(splat.position))
            {
                continue;
            }

            sampleX.push_back(splat.position[0]);
            sampleY.push_back(splat.position[1]);
            sampleZ.push_back(splat.position[2]);
        }

        if (sampleX.empty())
        {
            scene.focusCenter[0] = scene.focusCenter[1] = scene.focusCenter[2] = 0.0f;
            scene.focusRadius = 1.0f;
            scene.hasFocusHint = false;
            return;
        }

        std::sort(sampleX.begin(), sampleX.end());
        std::sort(sampleY.begin(), sampleY.end());
        std::sort(sampleZ.begin(), sampleZ.end());

        const float centerX = quantileFromSorted(sampleX, 0.50f);
        const float centerY = quantileFromSorted(sampleY, 0.50f);
        const float centerZ = quantileFromSorted(sampleZ, 0.50f);

        const float minX = quantileFromSorted(sampleX, 0.05f);
        const float minY = quantileFromSorted(sampleY, 0.05f);
        const float minZ = quantileFromSorted(sampleZ, 0.05f);
        const float maxX = quantileFromSorted(sampleX, 0.95f);
        const float maxY = quantileFromSorted(sampleY, 0.95f);
        const float maxZ = quantileFromSorted(sampleZ, 0.95f);

        const float extentX = std::max(maxX - minX, 0.0f);
        const float extentY = std::max(maxY - minY, 0.0f);
        const float extentZ = std::max(maxZ - minZ, 0.0f);
        const float robustDiagonal = std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);

        scene.focusCenter[0] = centerX;
        scene.focusCenter[1] = centerY;
        scene.focusCenter[2] = centerZ;
        scene.focusRadius = std::max(robustDiagonal * 0.5f, 0.25f);
        scene.hasFocusHint = true;
    }

    inline PreparedScene prepareForStableBaseline(const CpuGaussianScene &inputScene)
    {
        PreparedScene prepared;
        prepared.originalSplats = inputScene.splats.size();
        prepared.scene.shDegree = inputScene.shDegree;
        prepared.scene.hasSuggestedCamera = inputScene.hasSuggestedCamera;
        prepared.scene.suggestedCameraPosition[0] = inputScene.suggestedCameraPosition[0];
        prepared.scene.suggestedCameraPosition[1] = inputScene.suggestedCameraPosition[1];
        prepared.scene.suggestedCameraPosition[2] = inputScene.suggestedCameraPosition[2];
        prepared.scene.suggestedCameraForward[0] = inputScene.suggestedCameraForward[0];
        prepared.scene.suggestedCameraForward[1] = inputScene.suggestedCameraForward[1];
        prepared.scene.suggestedCameraForward[2] = inputScene.suggestedCameraForward[2];
        prepared.scene.hasSuggestedSceneUp = inputScene.hasSuggestedSceneUp;
        prepared.scene.suggestedSceneUp[0] = inputScene.suggestedSceneUp[0];
        prepared.scene.suggestedSceneUp[1] = inputScene.suggestedSceneUp[1];
        prepared.scene.suggestedSceneUp[2] = inputScene.suggestedSceneUp[2];

        if (inputScene.empty())
        {
            throw std::runtime_error("SceneSafety received an empty scene.");
        }

        float boundsMin[3] = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        float boundsMax[3] = {
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
        };

        for (const CpuGaussianSplat &splat : inputScene.splats)
        {
            if (!isFinite3(splat.position))
            {
                continue;
            }

            boundsMin[0] = std::min(boundsMin[0], splat.position[0]);
            boundsMin[1] = std::min(boundsMin[1], splat.position[1]);
            boundsMin[2] = std::min(boundsMin[2], splat.position[2]);
            boundsMax[0] = std::max(boundsMax[0], splat.position[0]);
            boundsMax[1] = std::max(boundsMax[1], splat.position[1]);
            boundsMax[2] = std::max(boundsMax[2], splat.position[2]);
        }

        const float extentX = boundsMax[0] - boundsMin[0];
        const float extentY = boundsMax[1] - boundsMin[1];
        const float extentZ = boundsMax[2] - boundsMin[2];
        const float diagonal = std::sqrt(std::max(extentX * extentX + extentY * extentY + extentZ * extentZ, 0.0f));

        prepared.sceneDiagonal = std::isfinite(diagonal) && diagonal > 0.0f ? diagonal : 1.0f;
        // Correctness-first path: keep original learned scales unless the data is invalid.
        prepared.minAxisLength = std::max(prepared.sceneDiagonal * 1.0e-7f, 1.0e-6f);
        prepared.scene.splats.reserve(inputScene.splats.size());
        for (const CpuGaussianSplat &source : inputScene.splats)
        {
            if (!isFinite3(source.position))
            {
                ++prepared.invalidSplatsRemoved;
                continue;
            }

            CpuGaussianSplat sanitized = source;
            sanitizeAxis(sanitized.axis0, 0, prepared.minAxisLength, prepared.axisClamps);
            sanitizeAxis(sanitized.axis1, 1, prepared.minAxisLength, prepared.axisClamps);
            sanitizeAxis(sanitized.axis2, 2, prepared.minAxisLength, prepared.axisClamps);

            sanitized.color[0] = std::isfinite(sanitized.color[0]) ? std::clamp(sanitized.color[0], 0.0f, 1.0f) : 1.0f;
            sanitized.color[1] = std::isfinite(sanitized.color[1]) ? std::clamp(sanitized.color[1], 0.0f, 1.0f) : 1.0f;
            sanitized.color[2] = std::isfinite(sanitized.color[2]) ? std::clamp(sanitized.color[2], 0.0f, 1.0f) : 1.0f;
            sanitized.shDc[0] = std::isfinite(sanitized.shDc[0]) ? sanitized.shDc[0] : 0.0f;
            sanitized.shDc[1] = std::isfinite(sanitized.shDc[1]) ? sanitized.shDc[1] : 0.0f;
            sanitized.shDc[2] = std::isfinite(sanitized.shDc[2]) ? sanitized.shDc[2] : 0.0f;
            for (float &coefficient : sanitized.shRest)
            {
                coefficient = std::isfinite(coefficient) ? coefficient : 0.0f;
            }
            sanitized.opacity = std::isfinite(sanitized.opacity) ? std::clamp(sanitized.opacity, 1.0f / 255.0f, 0.99f) : 0.99f;

            prepared.scene.expandBounds(sanitized);
            prepared.scene.splats.push_back(sanitized);
        }

        if (prepared.scene.empty())
        {
            throw std::runtime_error("SceneSafety removed all splats because the scene data was invalid.");
        }

        recomputeBounds(prepared.scene);
        computeRobustFocusHint(prepared.scene);
        return prepared;
    }

    inline std::string formatSummary(const PreparedScene &prepared)
    {
        std::ostringstream stream;
        stream
            << "scene sanitized for loading: original=" << prepared.originalSplats
            << " kept=" << prepared.scene.splats.size()
            << " shDegree=" << prepared.scene.shDegree
            << " removedInvalid=" << prepared.invalidSplatsRemoved
            << " axisClamps=" << prepared.axisClamps
            << " diagonal=" << prepared.sceneDiagonal
            << " focusCenter=("
            << prepared.scene.focusCenter[0] << ", "
            << prepared.scene.focusCenter[1] << ", "
            << prepared.scene.focusCenter[2] << ")"
            << " focusRadius=" << prepared.scene.focusRadius
            << " minAxisLength=" << prepared.minAxisLength;
        return stream.str();
    }
} // namespace SceneSafety

#endif
