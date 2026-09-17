#ifndef GAUSSIAN_SPLATTING_SHARED_DEBUG_SCENE_FACTORY_HPP
#define GAUSSIAN_SPLATTING_SHARED_DEBUG_SCENE_FACTORY_HPP

#include "../TestUtils/GaussianSplattingDslShared/CpuGaussianScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DebugSceneFactory
{
    inline void setConstantColorAsSh3(CpuGaussianSplat &splat, float r, float g, float b)
    {
        static constexpr float kShC0 = 0.28209479177387814f;
        splat.color[0] = r;
        splat.color[1] = g;
        splat.color[2] = b;
        splat.shDc[0] = (r - 0.5f) / kShC0;
        splat.shDc[1] = (g - 0.5f) / kShC0;
        splat.shDc[2] = (b - 0.5f) / kShC0;
    }

    inline CpuGaussianSplat makeAxisAlignedSplat(
        float px,
        float py,
        float pz,
        float axisX,
        float axisY,
        float axisZ,
        float r,
        float g,
        float b,
        float opacity = 0.95f
    )
    {
        CpuGaussianSplat splat;
        splat.position[0] = px;
        splat.position[1] = py;
        splat.position[2] = pz;

        splat.axis0[0] = axisX;
        splat.axis0[1] = 0.0f;
        splat.axis0[2] = 0.0f;

        splat.axis1[0] = 0.0f;
        splat.axis1[1] = axisY;
        splat.axis1[2] = 0.0f;

        splat.axis2[0] = 0.0f;
        splat.axis2[1] = 0.0f;
        splat.axis2[2] = axisZ;

        setConstantColorAsSh3(splat, r, g, b);
        splat.opacity = opacity;
        return splat;
    }

    inline CpuGaussianScene makeCalibrationScene(std::uint32_t gridResolution = 13, float spacing = 0.18f)
    {
        CpuGaussianScene scene;
        scene.shDegree = 3u;
        scene.splats.reserve(gridResolution * gridResolution + 48u);

        const float halfExtent = (static_cast<float>(gridResolution) - 1.0f) * spacing * 0.5f;
        const float floorRadius = spacing * 0.16f;
        const float floorThickness = 0.018f;

        for (std::uint32_t iy = 0; iy < gridResolution; ++iy)
        {
            for (std::uint32_t ix = 0; ix < gridResolution; ++ix)
            {
                const float px = -halfExtent + static_cast<float>(ix) * spacing;
                const float py = -halfExtent + static_cast<float>(iy) * spacing;
                const bool checker = ((ix + iy) & 1u) == 0u;

                float r = checker ? 0.92f : 0.18f;
                float g = checker ? 0.92f : 0.58f;
                float b = checker ? 0.22f : 0.92f;
                float opacity = checker ? 0.96f : 0.88f;

                if (ix == gridResolution / 2u)
                {
                    r = 0.96f;
                    g = 0.28f;
                    b = 0.24f;
                    opacity = 0.98f;
                }
                if (iy == gridResolution / 2u)
                {
                    r = 0.24f;
                    g = 0.84f;
                    b = 0.32f;
                    opacity = 0.98f;
                }

                CpuGaussianSplat splat = makeAxisAlignedSplat(px, py, 0.0f, floorRadius, floorRadius, floorThickness, r, g, b, opacity);
                scene.expandBounds(splat);
                scene.splats.push_back(splat);
            }
        }

        const float pillarHeights[3] = {1.10f, 0.78f, 0.52f};
        const float pillarX[3] = {-spacing * 2.2f, 0.0f, spacing * 2.2f};
        const float pillarColors[3][3] = {
            {0.96f, 0.26f, 0.22f},
            {0.22f, 0.84f, 0.30f},
            {0.22f, 0.48f, 0.96f},
        };

        for (std::uint32_t pillarIndex = 0; pillarIndex < 3u; ++pillarIndex)
        {
            const std::uint32_t layerCount = static_cast<std::uint32_t>(std::ceil(pillarHeights[pillarIndex] / 0.14f));
            for (std::uint32_t layerIndex = 0u; layerIndex < layerCount; ++layerIndex)
            {
                const float layerY = -halfExtent * 0.72f + static_cast<float>(layerIndex) * 0.14f;
                CpuGaussianSplat splat = makeAxisAlignedSplat(
                    pillarX[pillarIndex],
                    layerY,
                    0.18f,
                    spacing * 0.12f,
                    spacing * 0.12f,
                    spacing * 0.12f,
                    pillarColors[pillarIndex][0],
                    pillarColors[pillarIndex][1],
                    pillarColors[pillarIndex][2],
                    0.92f
                );
                scene.expandBounds(splat);
                scene.splats.push_back(splat);
            }
        }

        CpuGaussianSplat center = makeAxisAlignedSplat(0.0f, 0.0f, 0.22f, spacing * 0.18f, spacing * 0.18f, spacing * 0.18f, 0.96f, 0.96f, 0.96f, 0.94f);
        scene.expandBounds(center);
        scene.splats.push_back(center);

        // Blend probe: three splats at the exact same center so blending errors are unmistakable.
        const float probeRadius = spacing * 0.20f;
        const float probeY = halfExtent * 0.55f;
        const float probeZ = 0.36f;
        CpuGaussianSplat probeFront = makeAxisAlignedSplat(0.0f, probeY, probeZ, probeRadius, probeRadius, probeRadius, 0.96f, 0.18f, 0.18f, 0.35f);
        CpuGaussianSplat probeMiddle = makeAxisAlignedSplat(0.0f, probeY, probeZ, probeRadius, probeRadius, probeRadius, 0.18f, 0.96f, 0.18f, 0.35f);
        CpuGaussianSplat probeBack = makeAxisAlignedSplat(0.0f, probeY, probeZ, probeRadius, probeRadius, probeRadius, 0.96f, 0.96f, 0.96f, 0.35f);
        scene.expandBounds(probeFront);
        scene.expandBounds(probeMiddle);
        scene.expandBounds(probeBack);
        scene.splats.push_back(probeFront);
        scene.splats.push_back(probeMiddle);
        scene.splats.push_back(probeBack);

        CpuGaussianSplat backMarker = makeAxisAlignedSplat(0.0f, -halfExtent * 0.72f, 0.56f, spacing * 0.18f, spacing * 0.18f, spacing * 0.18f, 0.98f, 0.74f, 0.18f, 0.94f);
        scene.expandBounds(backMarker);
        scene.splats.push_back(backMarker);

        scene.focusCenter[0] = 0.0f;
        scene.focusCenter[1] = 0.0f;
        scene.focusCenter[2] = 0.18f;
        scene.focusRadius = std::max(halfExtent * 1.35f, spacing * 3.0f);
        scene.hasFocusHint = true;

        return scene;
    }
} // namespace DebugSceneFactory

#endif
