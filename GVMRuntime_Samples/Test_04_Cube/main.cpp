#include "SampleWindowManager.hpp"
#include "Test04CubeApp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace
{
    /// Parses the preferred graphics backend from command-line arguments.
    GVM::RHI::GraphicsBackend parseBackendArg(int argc, char **argv)
    {
#if defined(__APPLE__)
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Metal;
#else
        GVM::RHI::GraphicsBackend backend = GVM::RHI::GraphicsBackend::Vulkan;
#endif
        for (int index = 1; index < argc; ++index)
        {
            if (std::strcmp(argv[index], "--backend=metal") == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Metal;
            }
            else if (std::strcmp(argv[index], "--backend=vulkan") == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Vulkan;
            }
            else if (std::strcmp(argv[index], "--backend") == 0 && index + 1 < argc)
            {
                if (std::strcmp(argv[index + 1], "metal") == 0)
                {
                    backend = GVM::RHI::GraphicsBackend::Metal;
                }
                else if (std::strcmp(argv[index + 1], "vulkan") == 0)
                {
                    backend = GVM::RHI::GraphicsBackend::Vulkan;
                }
                ++index;
            }
        }
        return backend;
    }

    /// Parses an optional frame limit used for bounded sample validation runs.
    uint32_t parseMaxFramesArg(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (std::strncmp(argv[index], "--frames=", 9) == 0)
            {
                return static_cast<uint32_t>(std::strtoul(argv[index] + 9, nullptr, 10));
            }
            if (std::strcmp(argv[index], "--frames") == 0 && index + 1 < argc)
            {
                return static_cast<uint32_t>(std::strtoul(argv[index + 1], nullptr, 10));
            }
        }
        return 0u;
    }
} // namespace

/// Runs the desktop Test04 Cube sample through the shared SampleWindowManager host.
int main(int argc, char **argv)
{
    try
    {
        const GVM::RHI::GraphicsBackend backend = parseBackendArg(argc, argv);
        const uint32_t maxFrames = parseMaxFramesArg(argc, argv);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM Test04 Cube (Metal)"
            : "GVM Test04 Cube (Vulkan)";
        windowInfo.width = 640;
        windowInfo.height = 480;
        windowInfo.preferredGraphicsBackend = backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::Test04CubeApp>(window, backend);

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
        std::fprintf(stderr, "[sample04] failed: %s\n", error.what());
        return 1;
    }
}
