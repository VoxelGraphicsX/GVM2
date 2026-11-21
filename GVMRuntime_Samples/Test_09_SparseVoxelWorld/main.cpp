#include "Test09SparseVoxelWorldApp.hpp"
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

    uint32_t parseUintOption(int argc, char **argv, const char *prefixEq, const char *flagSpace, uint32_t fallback)
    {
        const size_t prefixLen = std::strlen(prefixEq);
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], prefixEq, prefixLen) == 0)
            {
                return static_cast<uint32_t>(std::strtoul(argv[i] + prefixLen, nullptr, 10));
            }
            if (flagSpace != nullptr && std::strcmp(argv[i], flagSpace) == 0 && i + 1 < argc)
            {
                return static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
            }
        }
        return fallback;
    }
    const char *parseStringOption(int argc, char **argv, const char *prefixEq, const char *flagSpace)
    {
        const size_t prefixLen = std::strlen(prefixEq);
        for (int i = 1; i < argc; ++i)
        {
            if (std::strncmp(argv[i], prefixEq, prefixLen) == 0)
            {
                return argv[i] + prefixLen;
            }
            if (flagSpace != nullptr && std::strcmp(argv[i], flagSpace) == 0 && i + 1 < argc)
            {
                return argv[i + 1];
            }
        }
        return nullptr;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto backend = parseBackendArg(argc, argv);
        if (const char *captureDir = parseStringOption(argc, argv, "--capture-dir=", "--capture-dir"))
        {
            if (captureDir[0] == '\0')
            {
                std::fprintf(stderr, "[sample09] --capture-dir requires a non-empty directory path.\n");
                return 1;
            }
            return GVM::Samples::runSparseVoxelShowcaseCapture(backend, captureDir);
        }

        if (hasFlag(argc, argv, "--benchmark"))
        {
            const uint32_t warmup = parseUintOption(argc, argv, "--warmup=", "--warmup", 1u);
            const uint32_t framesOrIters = parseUintOption(
                argc, argv, "--iters=", "--iters",
                parseUintOption(argc, argv, "--frames=", "--frames", 6u));
            const uint32_t workers = parseUintOption(argc, argv, "--workers=", "--workers", 0u);
            return GVM::Samples::runSparseVoxelHeadlessBenchmark(backend, warmup, framesOrIters, workers);
        }

        const uint32_t maxFrames = parseUintOption(argc, argv, "--frames=", "--frames", 0u);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM Test09 Pure Sparse Voxel Open-World (Metal)"
            : "GVM Test09 Pure Sparse Voxel Open-World (Vulkan)";
        windowInfo.width = 1280;
        windowInfo.height = 800;
        windowInfo.preferredGraphicsBackend = backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::Test09SparseVoxelWorldApp>(window, backend);

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
        std::fprintf(stderr, "[sample09] failed: %s\n", error.what());
        return 1;
    }
}
