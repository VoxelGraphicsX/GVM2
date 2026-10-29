#include "SampleApplication.hpp"
#include "SampleWindowManager.hpp"

#include <cstdio>
#include <exception>

/// Runs the desktop Test03 sample through the shared SampleWindowManager host.
int main()
{
    try
    {
        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = "GVM Test03 Triangle With Texture Vulkan";
        windowInfo.width = 640;
        windowInfo.height = 480;
        windowInfo.preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;

        GVM::Samples::SampleWindowManager window(windowInfo);
        eastl::unique_ptr<GVM::Samples::ISampleApplication> app = GVM::Samples::createSampleApplication(window);

        while (!app->shouldQuit())
        {
            app->processPendingEvents();
            app->renderFrame();
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[sample03] failed: %s\n", error.what());
        return 1;
    }
}
