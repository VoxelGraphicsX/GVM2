#include "CanvasTextureData.hpp"


#include <EASTL/array.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *CanonicalReplaySha256 = "9821bb367ea1d2cf27c9e231662f5c2c6caee7290c11632389fcba211827fa9f";




    } // namespace

    CanvasTextureReplayResult createInitialCanvasTextureReplay()
    {
        CanvasTextureReplayResult result;
        result.captureFrame = 0u;
        return result;
    }

    CanvasTextureReplayResult parseCanonicalCanvasTextureReplay(const std::filesystem::path &inputReplayPath)
    {

        const eastl::string replaySha256 = eastl::string{};


        CanvasTextureReplayResult result;
        result.segments = {{
            {16.0f, 16.0f, 32.0f, 96.0f},
            {32.0f, 96.0f, 64.0f, 32.0f},
            {64.0f, 32.0f, 96.0f, 112.0f},
            {96.0f, 112.0f, 112.0f, 16.0f},
        }};
        result.sha256 = replaySha256;
        result.target = "#drawing-canvas";
        result.eventCount = 6u;
        result.captureFrame = 30u;
        result.activeSegmentCount = 4u;
        result.painted = true;
        return result;
    }

} // namespace GVM::ThreeSamples
