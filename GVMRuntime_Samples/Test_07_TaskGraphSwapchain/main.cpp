#include "Test07TaskGraphSwapchainApp.hpp"
#include "SampleWindowManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace
{
    GVM::RHI::GraphicsBackend parseBackendArg(int argc, char **argv)
    {
#if defined(__APPLE__)
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Metal;
#else
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
#endif
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--backend=metal", 15) == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Metal;
            }
            else if (std::strncmp(argv[i], "--backend=vulkan", 16) == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Vulkan;
            }
        }
        return backend;
    }

    uint32_t parseMaxFramesArg(int argc, char **argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], "--frames=", 9) == 0)
            {
                return static_cast<uint32_t>(std::strtoul(argv[i] + 9, nullptr, 10));
            }
        }
        return 0u;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto backend = parseBackendArg(argc, argv);
        const uint32_t maxFrames = parseMaxFramesArg(argc, argv);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM Test07 TaskGraph Swapchain (Metal)"
            : "GVM Test07 TaskGraph Swapchain (Vulkan)";
        windowInfo.width = 960;
        windowInfo.height = 640;
        windowInfo.preferredGraphicsBackend = backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::Test07TaskGraphSwapchainApp>(window, backend);

        uint32_t renderedFrames = 0u;
        while (!app->shouldQuit())
        {
            app->processPendingEvents();
            app->renderFrame();
            ++renderedFrames;
            if (maxFrames > 0u && renderedFrames >= maxFrames)
            {
                break;
            }
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[sample07] failed: %s\n", error.what());
        return 1;
    }
}
