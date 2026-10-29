#include "ThreeGeneratedApplication.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <SampleWindowManager.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>

#ifndef GVM_THREE_SAMPLE_NAME
#define GVM_THREE_SAMPLE_NAME "GVM Three.js Sample"
#endif

#ifndef GVM_THREE_PIPELINE_NAME
#error "GVM_THREE_PIPELINE_NAME must identify the UGLC pipeline compiled into this host."
#endif

/// Runs one generated Three.js compatibility sample for a deterministic number of frames.
int main(int argumentCount, char **arguments)
{
    try
    {
        const GVM::ThreeSamples::ThreeSampleHostOptions options =
            GVM::ThreeSamples::parseThreeSampleHostOptions(argumentCount, arguments);
        if (options.showHelp)
        {
            GVM::ThreeSamples::printThreeSampleHostUsage(arguments[0]);
            return 0;
        }
        if (options.pipeline != GVM_THREE_PIPELINE_NAME)
        {
            throw std::invalid_argument(
                "--pipeline does not match the UGLC pipeline compiled into this host.");
        }

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = GVM_THREE_SAMPLE_NAME;
        windowInfo.width = options.width;
        windowInfo.height = options.height;
        windowInfo.resizable = false;
        windowInfo.preferredGraphicsBackend = options.backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        eastl::unique_ptr<GVM::Samples::ISampleApplication> application =
            GVM::ThreeSamples::createThreeGeneratedApplication(window, options);

        for (uint32_t frameIndex = 0;
             frameIndex < options.frameCount && !application->shouldQuit();
             ++frameIndex)
        {
            application->processPendingEvents();
            application->renderFrame();
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[three-sample-host] failed: %s\n", error.what());
        return 1;
    }
}
