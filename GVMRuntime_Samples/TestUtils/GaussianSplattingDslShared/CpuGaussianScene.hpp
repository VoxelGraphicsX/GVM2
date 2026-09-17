#ifndef GAUSSIAN_SPLATTING_SHARED_CPU_GAUSSIAN_SCENE_HPP
#define GAUSSIAN_SPLATTING_SHARED_CPU_GAUSSIAN_SCENE_HPP

#include <array>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <vector>

struct CpuGaussianSplat
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float axis0[3] = {0.01f, 0.0f, 0.0f};
    float axis1[3] = {0.0f, 0.01f, 0.0f};
    float axis2[3] = {0.0f, 0.0f, 0.01f};
    float color[3] = {1.0f, 1.0f, 1.0f};
    float shDc[3] = {0.0f, 0.0f, 0.0f};
    std::array<float, 45> shRest = {};
    float opacity = 1.0f;
};

struct CpuGaussianCluster
{
    uint32_t firstSplat = 0u;
    uint32_t splatCount = 0u;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
    float sphereCenter[3] = {0.0f, 0.0f, 0.0f};
    float sphereRadius = 0.0f;
};

struct CpuGaussianScene
{
    std::vector<CpuGaussianSplat> splats;
    std::vector<CpuGaussianCluster> clusters;
    uint32_t shDegree = 0u;
    float boundsMin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float boundsMax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    float focusCenter[3] = {0.0f, 0.0f, 0.0f};
    float focusRadius = 1.0f;
    bool hasFocusHint = false;
    float suggestedCameraPosition[3] = {0.0f, 0.0f, 3.0f};
    float suggestedCameraForward[3] = {0.0f, 0.0f, 1.0f};
    bool hasSuggestedCamera = false;
    float suggestedSceneUp[3] = {0.0f, -1.0f, 0.0f};
    bool hasSuggestedSceneUp = false;

    bool empty() const
    {
        return splats.empty();
    }

    bool hasViewDependentColor() const
    {
        return shDegree > 0u;
    }

    void expandBounds(const CpuGaussianSplat &splat)
    {
        boundsMin[0] = std::min(boundsMin[0], splat.position[0]);
        boundsMin[1] = std::min(boundsMin[1], splat.position[1]);
        boundsMin[2] = std::min(boundsMin[2], splat.position[2]);
        boundsMax[0] = std::max(boundsMax[0], splat.position[0]);
        boundsMax[1] = std::max(boundsMax[1], splat.position[1]);
        boundsMax[2] = std::max(boundsMax[2], splat.position[2]);
    }
};

#endif
