#include "HorseGlbAsset.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Throws when one frozen Horse asset contract is not satisfied. */
    void requireHorseCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Validates geometry, animation layout, and repeating interpolation. */
    void testFrozenHorseAsset()
    {
        const GVM::ThreeSamples::HorseGlbAsset asset =
            GVM::ThreeSamples::loadHorseGlbAsset(
                std::filesystem::path(GVM_THREE_HORSE_ASSET_ROOT) /
                "models" / "gltf" / "Horse.glb");
        requireHorseCondition(asset.vertices.size() == 796u,
                              "Horse vertex count is invalid.");
        requireHorseCondition(asset.indices.size() == 2952u,
                              "Horse index count is invalid.");
        requireHorseCondition(asset.morphTargetCount == 15u &&
                                  asset.morphPositions.size() == 11940u,
                              "Horse morph target layout is invalid.");
        requireHorseCondition(asset.animationTimes.size() == 16u &&
                                  asset.animationWeights.size() == 240u,
                              "Horse animation layout is invalid.");
        requireHorseCondition(asset.animationTimes.front() == 0.0f &&
                                  asset.animationTimes.back() == 1.5f,
                              "Horse animation range is invalid.");

        eastl::vector<float> startWeights;
        eastl::vector<float> repeatedWeights;
        eastl::vector<float> middleWeights;
        GVM::ThreeSamples::sampleHorseMorphWeights(asset, 0.0f, startWeights);
        GVM::ThreeSamples::sampleHorseMorphWeights(asset, 3.0f, repeatedWeights);
        GVM::ThreeSamples::sampleHorseMorphWeights(asset, 0.05f, middleWeights);
        requireHorseCondition(startWeights.size() == 15u &&
                                  repeatedWeights.size() == 15u &&
                                  middleWeights.size() == 15u,
                              "Horse sampled weight count is invalid.");
        for (uint32_t target = 0u; target < 15u; ++target)
        {
            requireHorseCondition(
                std::abs(startWeights[target] - repeatedWeights[target]) < 1.0e-6f,
                "Horse repeating animation does not wrap deterministically.");
        }
        float middleSum = 0.0f;
        for (const float weight : middleWeights)
        {
            requireHorseCondition(std::isfinite(weight),
                                  "Horse sampled weight is not finite.");
            middleSum += weight;
        }
        requireHorseCondition(middleSum > 0.99f && middleSum < 1.01f,
                              "Horse interpolated morph weights are not normalized.");

        eastl::vector<float> firstInstanceWeights;
        eastl::vector<float> secondInstanceWeights;
        GVM::ThreeSamples::sampleHorseMorphWeights(
            asset, 1.4312665462493896f, firstInstanceWeights);
        GVM::ThreeSamples::sampleHorseMorphWeights(
            asset, 1.6702079772949219f, secondInstanceWeights);
        requireHorseCondition(
            std::abs(firstInstanceWeights[0u] - 0.3126656115f) < 1.0e-5f &&
                std::abs(firstInstanceWeights[14u] - 0.6873343587f) < 1.0e-5f,
            "Horse first frozen instance weights differ from Three.js r185.");
        requireHorseCondition(
            std::abs(secondInstanceWeights[1u] - 0.2979202569f) < 1.0e-5f &&
                std::abs(secondInstanceWeights[2u] - 0.7020797730f) < 1.0e-5f,
            "Horse second frozen instance weights differ from Three.js r185.");
    }
} // namespace

/** Executes the sample-private frozen Horse GLB regression suite. */
int main()
{
    try
    {
        testFrozenHorseAsset();
        std::cout << "Horse GLB tests passed for the frozen r185 asset.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Horse GLB test failed: " << error.what() << '\n';
        return 1;
    }
}
