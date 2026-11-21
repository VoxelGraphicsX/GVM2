#include "Test08PCGForestTaskGraphApp.hpp"
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
            if (std::strcmp(argv[i], "--backend=metal") == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Metal;
            }
            else if (std::strcmp(argv[i], "--backend=vulkan") == 0)
            {
                backend = GVM::RHI::GraphicsBackend::Vulkan;
            }
            else if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
            {
                if (std::strcmp(argv[i + 1], "metal") == 0)
                {
                    backend = GVM::RHI::GraphicsBackend::Metal;
                }
                else if (std::strcmp(argv[i + 1], "vulkan") == 0)
                {
                    backend = GVM::RHI::GraphicsBackend::Vulkan;
                }
                ++i;
            }
        }
        return backend;
    }

    bool hasFlag(int argc, char **argv, const char *flag)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], flag) == 0)
            {
                return true;
            }
        }
        return false;
    }

    bool tryParseUintOption(int argc, char **argv, const char *name, uint32_t &outValue)
    {
        const size_t nameLen = std::strlen(name);
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], name, nameLen) == 0)
            {
                if (argv[i][nameLen] == '=')
                {
                    outValue = static_cast<uint32_t>(std::strtoul(argv[i] + nameLen + 1u, nullptr, 10));
                    return true;
                }
                if (argv[i][nameLen] == '\0' && i + 1 < argc)
                {
                    outValue = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
                    return true;
                }
            }
        }
        return false;
    }

    uint32_t parseUintOption(int argc, char **argv, const char *name, uint32_t fallback)
    {
        uint32_t val = fallback;
        (void)tryParseUintOption(argc, argv, name, val);
        return val;
    }

    bool tryParseStringOption(int argc, char **argv, const char *name, const char *&outValue)
    {
        const size_t nameLen = std::strlen(name);
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], name, nameLen) == 0)
            {
                if (argv[i][nameLen] == '=')
                {
                    outValue = argv[i] + nameLen + 1u;
                    return outValue[0] != '\0';
                }
                if (argv[i][nameLen] == '\0' && i + 1 < argc)
                {
                    outValue = argv[i + 1];
                    return outValue[0] != '\0';
                }
            }
        }
        return false;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto backend = parseBackendArg(argc, argv);
        const char *captureDir = nullptr;
        if (tryParseStringOption(argc, argv, "--capture-dir", captureDir))
        {
            const uint32_t workers = parseUintOption(argc, argv, "--workers", 0u);
            return GVM::Samples::runPCGForestShowcaseCapture(backend, captureDir, workers);
        }

        if (hasFlag(argc, argv, "--benchmark"))
        {
            const uint32_t warmup = parseUintOption(argc, argv, "--warmup", 2u);
            uint32_t iters = 6u;
            if (!tryParseUintOption(argc, argv, "--iters", iters))
            {
                (void)tryParseUintOption(argc, argv, "--frames", iters);
            }
            const uint32_t workers = parseUintOption(argc, argv, "--workers", 0u);
            return GVM::Samples::runPCGForestHeadlessBenchmark(backend, warmup, iters, workers);
        }

        const uint32_t maxFrames = parseUintOption(argc, argv, "--frames", 0u);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM Test08 PCG Forest TaskGraph (Metal)"
            : "GVM Test08 PCG Forest TaskGraph (Vulkan)";
        windowInfo.width = 1280;
        windowInfo.height = 800;
        windowInfo.preferredGraphicsBackend = backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::Test08PCGForestTaskGraphApp>(window, backend);

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
        std::fprintf(stderr, "[sample08] failed: %s\n", error.what());
        return 1;
    }
}
