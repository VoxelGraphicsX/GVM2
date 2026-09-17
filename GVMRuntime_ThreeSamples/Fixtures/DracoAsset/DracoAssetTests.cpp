#include "DracoAsset.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Stores one exact JS DracoLoader position and computeVertexNormals regression sample. */
    struct DracoReferenceSample final
    {
        uint32_t index;
        glm::vec3 position;
        glm::vec3 normal;
    };

    /** Verifies selected decoded values against Three r185's JS DracoLoader output. */
    void verifyThreeDecoderSamples(const GVM::ThreeSamples::DracoMeshAsset &asset)
    {
        constexpr std::array<DracoReferenceSample, 9u> Samples = {{
            {0u, {-0.03346006199717522f, 0.03374761715531349f, 0.008863698691129684f}, {0.01707819476723671f, -0.9998348951339722f, 0.006210269406437874f}},
            {1u, {-0.03452492877840996f, 0.03374761715531349f, 0.008635509759187698f}, {0.05946268513798714f, -0.9982186555862427f, -0.004874378442764282f}},
            {2u, {-0.033840373158454895f, 0.03382368013262749f, 0.006886083632707596f}, {0.07332296669483185f, -0.9972004294395447f, -0.014665599912405014f}},
            {10u, {-0.030950017273426056f, 0.12570661306381226f, 0.005516964942216873f}, {0.30028530955314636f, 0.9504522681236267f, -0.08043129742145538f}},
            {100u, {-0.05057401955127716f, 0.03389974310994148f, 0.026738274842500687f}, {-0.12440873682498932f, -0.9623630046844482f, 0.24161924421787262f}},
            {1000u, {-0.06076633185148239f, 0.1712677776813507f, -0.06088519096374512f}, {0.8878275156021118f, -0.10156068950891495f, -0.44882932305336f}},
            {10000u, {-0.06533005833625793f, 0.12699967622756958f, 0.047731395810842514f}, {0.7190632224082947f, 0.3983183205127716f, 0.569465160369873f}},
            {20000u, {0.05416340380907059f, 0.07345200330018997f, 0.01502472534775734f}, {0.21571297943592072f, 0.9764534831047058f, -0.0025540143251419067f}},
            {34833u, {-0.031330324709415436f, 0.03846346586942673f, -0.007717825472354889f}, {-0.03458407148718834f, -0.9993116855621338f, -0.013420699164271355f}},
        }};
        constexpr std::array<uint32_t, 18u> ExpectedIndices = {
            0u, 1u, 2u, 2u, 1u, 3u, 3u, 5u, 6u, 6u, 5u, 7u, 5u, 8u, 7u, 7u, 8u, 9u};
        for (size_t index = 0u; index < ExpectedIndices.size(); ++index)
        {
            if (asset.indices[index] != ExpectedIndices[index])
            {
                throw std::runtime_error("The C++ Draco index order differs from Three r185.");
            }
        }
        for (const DracoReferenceSample &sample : Samples)
        {
            const float positionDifference = glm::length(asset.positions[sample.index] - sample.position);
            const float normalDifference = glm::length(asset.normals[sample.index] - sample.normal);
            if (positionDifference > 1.0e-7f || normalDifference > 2.0e-5f)
            {
                throw std::runtime_error(
                    "The C++ Draco decoder differs from Three r185 at sample " + std::to_string(sample.index) +
                    ": position delta " + std::to_string(positionDifference) +
                    ", normal delta " + std::to_string(normalDifference) + ".");
            }
        }
    }

    /** Verifies the frozen r185 bunny topology, bounds, and generated normals. */
    void verifyLockedBunnyAsset()
    {
        const GVM::ThreeSamples::DracoMeshAsset asset = GVM::ThreeSamples::loadDracoMeshAsset(std::filesystem::path(GVM_THREE_DRACO_ASSET_ROOT) / "models/draco/bunny.drc");
        if (asset.positions.size() != 34834u || asset.indices.size() != 69451u * 3u || asset.normals.size() != asset.positions.size() ||
            asset.sourceSha256 != "3bb08f257d873f69ded447e07c2dd4e9d7a264d58a686c88978c38430c5f6eb4")
        {
            throw std::runtime_error("The locked Draco bunny topology changed.");
        }
        for (const glm::vec3 &normal : asset.normals)
        {
            const float length = glm::length(normal);
            if (!std::isfinite(length) || std::abs(length - 1.0f) > 2.0e-4f)
            {
                throw std::runtime_error("A generated Draco normal is not normalized.");
            }
        }
        verifyThreeDecoderSamples(asset);
    }
} // namespace

/** Runs deterministic decode coverage for the exact r185 bunny asset. */
int main()
{
    try
    {
        verifyLockedBunnyAsset();
        std::cout << "Draco asset tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
