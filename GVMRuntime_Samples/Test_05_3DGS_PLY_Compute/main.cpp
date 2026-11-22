#include "SampleWindowManager.hpp"
#include "Test05GaussianPlyComputeApp.hpp"

#include <cstdio>
#include <exception>

/// Runs the desktop Test05 3DGS PLY compute viewer through the shared SampleWindowManager host.
int main(int argc, char **argv)
{
    try
    {
        const GaussianSplattingShared::GaussianViewerCliOptions options =
            GaussianSplattingShared::parseGaussianViewerCliOptions(argc, argv);

        GVM::Samples::SampleWindowCreateInfo windowInfo;
        windowInfo.title = options.backend == GVM::RHI::GraphicsBackend::Metal
            ? "GVM 3DGS PLY Compute Viewer (Metal)"
            : "GVM 3DGS PLY Compute Viewer (Vulkan)";
        windowInfo.width = 1280;
        windowInfo.height = 720;
        windowInfo.preferredGraphicsBackend = options.backend;

        GVM::Samples::SampleWindowManager window(windowInfo);
        auto app = eastl::make_unique<GVM::Samples::Test05GaussianPlyComputeApp>(window, options);

        std::uint32_t renderedFrames = 0u;
        while (!app->shouldQuit())
        {
            app->processPendingEvents();
            app->renderFrame();
            ++renderedFrames;
            if (options.maxFrames > 0u && renderedFrames >= options.maxFrames)
            {
                break;
            }
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[3dgs-viewer] fatal: %s\n", error.what());
        return 1;
    }
}
