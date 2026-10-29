#include "SampleApplication.hpp"
#include "SampleWindowManager.hpp"

#include <cstdio>
#include <exception>

int main()
{
    try
    {
        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = "GVM Test01 Triangle Vulkan";
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
        std::fprintf(stderr, "[sample01] failed: %s\n", error.what());
        return 1;
    }
}
