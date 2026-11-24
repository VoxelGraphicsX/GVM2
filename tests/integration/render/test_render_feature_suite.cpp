#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRenderTestOptions.hpp>

#if defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>
#else
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(__APPLE__)
#include <SDL_metal.h>
#else
#include <SDL_vulkan.h>
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "generate_result.hpp"

namespace
{
    void logRenderPhase(const std::string &message)
    {
        std::fprintf(stderr, "[gvm-render-tests] %s\n", message.c_str());
        std::fflush(stderr);
    }

    struct RenderFeatureContext
    {
#if defined(__OHOS__)
        OH_NativeImage *consumerSurface = nullptr;
        OHNativeWindow *window = nullptr;
#else
        SDL_Window *window = nullptr;
#if defined(__APPLE__)
        SDL_MetalView metalView = nullptr;
#endif
#endif
        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;
        GVM::RHI::Swapchain swapchain = nullptr;
        MyRenderer renderer = nullptr;
        int width = 960;
        int height = 640;
        int simpleFrameCount = 120;
        int complexFrameCount = 180;
        int frameDelayMs = 16;
        int warmupMs = 350;
        int casePauseMs = 250;
        std::string backendName = "default";
        bool quitRequested = false;
        bool available = false;
        std::string unavailableReason;
    };

    /// Holds the raw RGBA8 image copied back from a render-feature output texture.
    struct RenderReadbackImage
    {
        std::vector<std::uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// Summarizes a render readback so visual corruption can be compared across generated shader pipelines.
    struct RenderReadbackStats
    {
        uint64_t byteCount = 0;
        uint64_t totalPixels = 0;
        uint64_t nearBlackPixels = 0;
        uint64_t fullyBlackPixels = 0;
        uint64_t brightPixels = 0;
        uint64_t zeroAlphaPixels = 0;
        uint64_t darkTileCount = 0;
        uint64_t brightTileCount = 0;
        double minLuma = 0.0;
        double maxLuma = 0.0;
        double meanLuma = 0.0;
        double nearBlackRatio = 0.0;
        double fullyBlackRatio = 0.0;
    };

    /// Releases the generated renderer at the end of a case even when the test exits through a fatal assertion.
    struct ScopedRendererDestroy
    {
        RenderFeatureContext &ctx;

        ~ScopedRendererDestroy()
        {
            if (ctx.renderer != nullptr)
            {
                ctx.renderer->destroy();
                ctx.renderer = nullptr;
            }
        }
    };

#if defined(__OHOS__)
    /** Drains all presented buffers from an OpenHarmony consumer surface so the swapchain producer never stalls. */
    void drainOhosConsumerBuffers(OH_NativeImage *consumerSurface)
    {
        if (consumerSurface == nullptr)
        {
            return;
        }
        OHNativeWindowBuffer *buffer = nullptr;
        int fenceFd = -1;
        while (OH_NativeImage_AcquireNativeWindowBuffer(consumerSurface, &buffer, &fenceFd) == 0 && buffer != nullptr)
        {
            (void)OH_NativeImage_ReleaseNativeWindowBuffer(consumerSurface, buffer, fenceFd);
            buffer = nullptr;
            fenceFd = -1;
        }
    }

    /** Resizes an OpenHarmony consumer surface and its paired producer native window. */
    void resizeOhosConsumerWindow(RenderFeatureContext &ctx, int width, int height)
    {
        const int clampedWidth = std::max(width, 1);
        const int clampedHeight = std::max(height, 1);
        if (ctx.consumerSurface != nullptr)
        {
            (void)OH_ConsumerSurface_SetDefaultSize(ctx.consumerSurface, clampedWidth, clampedHeight);
        }
        if (ctx.window != nullptr)
        {
            (void)OH_NativeWindow_NativeWindowHandleOpt(ctx.window, SET_BUFFER_GEOMETRY, clampedWidth, clampedHeight);
        }
    }

    /** Drains OpenHarmony consumer buffers during a bounded settle window. */
    void pumpWindowEvents(RenderFeatureContext &ctx, int warmupMs)
    {
        drainOhosConsumerBuffers(ctx.consumerSurface);
        if (warmupMs > 0)
        {
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < warmupMs)
            {
                drainOhosConsumerBuffers(ctx.consumerSurface);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
#else
    /// Pumps events without showing or focusing the hidden test window; close requests cancel execution.
    void pumpWindowEvents(RenderFeatureContext &ctx, int warmupMs)
    {
        SDL_Window *window = ctx.window;
        if (window == nullptr)
        {
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < warmupMs)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0)
            {
                if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
                {
                    throw std::runtime_error("Window was closed before render test case finished.");
                }
            }
            SDL_Delay(10);
        }
    }
#endif

    /// Restores the SDL window's original size when the scope exits, even on fatal assertions.
    struct ScopedWindowSizeRestore
    {
        RenderFeatureContext &ctx;
        int originalWidth = 0;
        int originalHeight = 0;

        explicit ScopedWindowSizeRestore(RenderFeatureContext &context) : ctx(context)
        {
#if defined(__OHOS__)
            if (ctx.window != nullptr)
            {
                (void)OH_NativeWindow_NativeWindowHandleOpt(ctx.window, GET_BUFFER_GEOMETRY, &originalHeight, &originalWidth);
            }
#else
            SDL_GetWindowSize(ctx.window, &originalWidth, &originalHeight);
#endif
        }

        ~ScopedWindowSizeRestore()
        {
            if (ctx.window != nullptr)
            {
#if defined(__OHOS__)
                resizeOhosConsumerWindow(ctx, originalWidth, originalHeight);
#else
                SDL_SetWindowSize(ctx.window, originalWidth, originalHeight);
#endif
                pumpWindowEvents(ctx, ctx.casePauseMs);
            }
        }
    };

    std::string sanitizeFileStem(std::string value)
    {
        for (char &ch : value)
        {
            const bool isAlphaNumeric = (ch >= 'a' && ch <= 'z')
                || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9');
            if (!isAlphaNumeric)
            {
                ch = '_';
            }
        }
        return value;
    }

    /// Converts an 8-bit RGB triplet to display-space luminance for readback diagnostics.
    double calculateLuma(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
    {
        return 0.2126 * double(red) + 0.7152 * double(green) + 0.0722 * double(blue);
    }

    /// Copies a render-feature texture into CPU memory using the active graphics queue.
    RenderReadbackImage readTextureToImage(const RenderFeatureContext &context, const GVM::RHI::Texture &texture, const char *description)
    {
        RenderReadbackImage image;
        if (texture.isNull() || texture.get() == nullptr)
        {
            throw std::runtime_error(std::string("Render readback requires a valid ") + description + " texture.");
        }

        image.width = texture->getWidth();
        image.height = texture->getHeight();
        const uint64_t byteCount = uint64_t(image.width) * uint64_t(image.height) * 4u;
        image.pixels.resize(static_cast<size_t>(byteCount));

        GVM::Core::DeviceProxy coreDevice(context.device);
        coreDevice->graphicsQueue(0)
            ->readTexture(texture, image.pixels.data(), byteCount)
            ->submit();
        return image;
    }

    /// Copies the generated renderer's current present texture into CPU memory.
    RenderReadbackImage readPixelLocalPresentTexture(const RenderFeatureContext &context)
    {
        return readTextureToImage(context, context.renderer->getPresentTextureHandle(), "generated present");
    }

    /// Copies the last swapchain target written by renderToSwapchain into CPU memory before it is presented.
    RenderReadbackImage readPixelLocalSwapchainTexture(const RenderFeatureContext &context)
    {
        return readTextureToImage(context, context.renderer->getSwapchainStagingTextureHandle(), "swapchain target");
    }

    /// Copies the render mode's final visible source texture into CPU memory.
    RenderReadbackImage readFinalRenderTexture(const RenderFeatureContext &context)
    {
        return readTextureToImage(context, context.renderer->getReadbackTextureHandle(), "final render output");
    }

    /// Computes luminance and coarse tile metrics from an RGBA8 readback.
    RenderReadbackStats analyzeRenderReadback(const RenderReadbackImage &image)
    {
        RenderReadbackStats stats;
        stats.byteCount = image.pixels.size();
        stats.totalPixels = uint64_t(image.width) * uint64_t(image.height);
        if (stats.totalPixels == 0u || image.pixels.size() < stats.totalPixels * 4u)
        {
            return stats;
        }

        double totalLuma = 0.0;
        stats.minLuma = std::numeric_limits<double>::max();
        for (uint64_t pixelIndex = 0; pixelIndex < stats.totalPixels; ++pixelIndex)
        {
            const size_t byteIndex = static_cast<size_t>(pixelIndex * 4u);
            const std::uint8_t red = image.pixels[byteIndex + 0u];
            const std::uint8_t green = image.pixels[byteIndex + 1u];
            const std::uint8_t blue = image.pixels[byteIndex + 2u];
            const std::uint8_t alpha = image.pixels[byteIndex + 3u];
            const double luma = calculateLuma(red, green, blue);

            totalLuma += luma;
            stats.minLuma = std::min(stats.minLuma, luma);
            stats.maxLuma = std::max(stats.maxLuma, luma);
            if (luma < 5.0)
            {
                ++stats.nearBlackPixels;
            }
            if (red == 0u && green == 0u && blue == 0u)
            {
                ++stats.fullyBlackPixels;
            }
            if (luma > 48.0)
            {
                ++stats.brightPixels;
            }
            if (alpha == 0u)
            {
                ++stats.zeroAlphaPixels;
            }
        }

        stats.meanLuma = totalLuma / double(stats.totalPixels);
        stats.nearBlackRatio = double(stats.nearBlackPixels) / double(stats.totalPixels);
        stats.fullyBlackRatio = double(stats.fullyBlackPixels) / double(stats.totalPixels);

        constexpr uint32_t TileSize = 16u;
        for (uint32_t tileY = 0u; tileY < image.height; tileY += TileSize)
        {
            for (uint32_t tileX = 0u; tileX < image.width; tileX += TileSize)
            {
                double tileLuma = 0.0;
                uint64_t tilePixels = 0u;
                const uint32_t endY = std::min(tileY + TileSize, image.height);
                const uint32_t endX = std::min(tileX + TileSize, image.width);
                for (uint32_t y = tileY; y < endY; ++y)
                {
                    for (uint32_t x = tileX; x < endX; ++x)
                    {
                        const size_t byteIndex = (size_t(y) * size_t(image.width) + size_t(x)) * 4u;
                        tileLuma += calculateLuma(image.pixels[byteIndex + 0u], image.pixels[byteIndex + 1u], image.pixels[byteIndex + 2u]);
                        ++tilePixels;
                    }
                }

                const double averageTileLuma = tilePixels == 0u ? 0.0 : tileLuma / double(tilePixels);
                if (averageTileLuma < 5.0)
                {
                    ++stats.darkTileCount;
                }
                if (averageTileLuma > 48.0)
                {
                    ++stats.brightTileCount;
                }
            }
        }

        return stats;
    }

    /// Formats readback statistics for stderr so backend artifacts and local test logs carry the same diagnosis.
    std::string formatRenderReadbackSummary(const RenderReadbackStats &stats)
    {
        return std::string("render readback: totalPixels=") + std::to_string(stats.totalPixels)
            + " byteCount=" + std::to_string(stats.byteCount)
            + " meanLuma=" + std::to_string(stats.meanLuma)
            + " maxLuma=" + std::to_string(stats.maxLuma)
            + " nearBlackRatio=" + std::to_string(stats.nearBlackRatio)
            + " fullyBlackRatio=" + std::to_string(stats.fullyBlackRatio)
            + " darkTiles=" + std::to_string(stats.darkTileCount)
            + " brightTiles=" + std::to_string(stats.brightTileCount)
            + " zeroAlphaPixels=" + std::to_string(stats.zeroAlphaPixels);
    }

    /// Writes the exact RGBA8 readback bytes used by the parity comparator.
    void writeRenderReadbackRaw(const std::filesystem::path &path, const RenderReadbackImage &image)
    {
        std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
    }

    /// Writes a compact RGB PPM image for manual inspection of a render readback.
    void writeRenderReadbackPpm(const std::filesystem::path &path, const RenderReadbackImage &image)
    {
        std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
        output << "P6\n" << image.width << " " << image.height << "\n255\n";
        for (uint64_t pixelIndex = 0u; pixelIndex < uint64_t(image.width) * uint64_t(image.height); ++pixelIndex)
        {
            const size_t byteIndex = static_cast<size_t>(pixelIndex * 4u);
            output.put(static_cast<char>(image.pixels[byteIndex + 0u]));
            output.put(static_cast<char>(image.pixels[byteIndex + 1u]));
            output.put(static_cast<char>(image.pixels[byteIndex + 2u]));
        }
    }

    /// Writes JSON statistics, exact RGBA8 bytes, and a compact RGB preview for a render readback.
    void writeRenderReadbackArtifact(const std::string &caseName, const std::string &artifactSuffix, const RenderReadbackImage &image, const RenderReadbackStats &stats)
    {
        const auto &artifactDir = GVM::Tests::renderTestOptions.artifactDirectory;
        if (artifactDir.empty())
        {
            return;
        }

        std::filesystem::create_directories(artifactDir);

        const auto artifactStem = sanitizeFileStem(caseName) + "_" + artifactSuffix;
        const auto jsonPath = artifactDir / (artifactStem + ".json");
        std::ofstream output(jsonPath, std::ios::out | std::ios::trunc);
        output << "{\n";
        output << "  \"case\": \"" << caseName << "\",\n";
        output << "  \"suffix\": \"" << artifactSuffix << "\",\n";
        output << "  \"width\": " << image.width << ",\n";
        output << "  \"height\": " << image.height << ",\n";
        output << "  \"imagesWritten\": " << (GVM::Tests::renderTestOptions.writeImages ? "true" : "false") << ",\n";
        output << "  \"byteCount\": " << stats.byteCount << ",\n";
        output << "  \"totalPixels\": " << stats.totalPixels << ",\n";
        output << "  \"meanLuma\": " << stats.meanLuma << ",\n";
        output << "  \"minLuma\": " << stats.minLuma << ",\n";
        output << "  \"maxLuma\": " << stats.maxLuma << ",\n";
        output << "  \"nearBlackPixels\": " << stats.nearBlackPixels << ",\n";
        output << "  \"nearBlackRatio\": " << stats.nearBlackRatio << ",\n";
        output << "  \"fullyBlackPixels\": " << stats.fullyBlackPixels << ",\n";
        output << "  \"fullyBlackRatio\": " << stats.fullyBlackRatio << ",\n";
        output << "  \"brightPixels\": " << stats.brightPixels << ",\n";
        output << "  \"zeroAlphaPixels\": " << stats.zeroAlphaPixels << ",\n";
        output << "  \"darkTileCount\": " << stats.darkTileCount << ",\n";
        output << "  \"brightTileCount\": " << stats.brightTileCount << "\n";
        output << "}\n";

        if (GVM::Tests::renderTestOptions.writeImages)
        {
            writeRenderReadbackRaw(artifactDir / (artifactStem + ".rgba"), image);
            writeRenderReadbackPpm(artifactDir / (artifactStem + ".ppm"), image);
        }
    }

    /// Validates one pixel-local readback image and records artifacts for backend debugging.
    void validatePixelLocalReadbackImage(const std::string &caseName, const std::string &artifactSuffix, const std::string &description, const RenderReadbackImage &image)
    {
        const RenderReadbackStats stats = analyzeRenderReadback(image);
        writeRenderReadbackArtifact(caseName, artifactSuffix + "_readback", image, stats);
        logRenderPhase(description + ": " + formatRenderReadbackSummary(stats));

        ASSERT_GT(stats.totalPixels, 0u);
        EXPECT_GT(stats.maxLuma, 96.0) << description << " should contain visible specular highlights.";
        EXPECT_GT(stats.meanLuma, 10.0) << description << " is too dark for the configured white-sphere lighting scene.";
        EXPECT_EQ(stats.nearBlackPixels, 0u) << description << " contains near-black pixels, which indicates missing pixel-local tile writes.";
        EXPECT_EQ(stats.fullyBlackPixels, 0u) << description << " contains fully black pixels, which indicates missing pixel-local tile writes.";
        EXPECT_EQ(stats.darkTileCount, 0u) << description << " contains dark 16x16 tiles, which indicates pixel-local attachment loss.";
        EXPECT_LT(stats.nearBlackRatio, 0.20) << description << " contains too many near-black pixels; this catches black-tile/mosaic corruption.";
        EXPECT_LT(stats.fullyBlackRatio, 0.08) << description << " contains too many fully black pixels.";
        EXPECT_EQ(stats.zeroAlphaPixels, 0u) << description << " must be opaque.";
    }

    /// Renders a controlled readback frame and compares both the present texture and the swapchain target written by renderToSwapchain.
    void validatePixelLocalPresentReadback(RenderFeatureContext &context, const std::string &caseName)
    {
        RenderReadbackImage presentImage;
        RenderReadbackImage swapchainImage;
        bool hasOutstandingSwapchainImage = false;
        try
        {
            context.renderer->renderPixelLocalDeferredReadbackFrame();
            hasOutstandingSwapchainImage = true;
            presentImage = readPixelLocalPresentTexture(context);
            swapchainImage = readPixelLocalSwapchainTexture(context);
        }
        catch (const std::exception &ex)
        {
            if (hasOutstandingSwapchainImage)
            {
                context.swapchain->present();
#if defined(__OHOS__)
                drainOhosConsumerBuffers(context.consumerSurface);
#endif
            }
            FAIL() << "Pixel-local final-screen readback failed: " << ex.what();
            return;
        }

        context.swapchain->present();
#if defined(__OHOS__)
        drainOhosConsumerBuffers(context.consumerSurface);
#endif
        validatePixelLocalReadbackImage(caseName, "present", "Pixel-local deferred present texture", presentImage);
        validatePixelLocalReadbackImage(caseName, "swapchain", "Pixel-local swapchain target", swapchainImage);
    }

    /// Writes the final render-mode readback artifact used by the temporary legacy-vs-experimental comparator.
    void writeFinalRenderReadbackArtifact(RenderFeatureContext &context, const std::string &caseName)
    {
        const RenderReadbackImage image = readFinalRenderTexture(context);
        const RenderReadbackStats stats = analyzeRenderReadback(image);
        writeRenderReadbackArtifact(caseName, "final", image, stats);
        logRenderPhase(std::string("Final output ") + formatRenderReadbackSummary(stats));
        ASSERT_GT(stats.totalPixels, 0u) << "Final render readback must contain pixels for " << caseName;
    }

    void writeRenderArtifact(const std::string &caseName, int renderMode, int frameCount, const RenderFeatureContext &context)
    {
        const auto &artifactDir = GVM::Tests::renderTestOptions.artifactDirectory;
        if (artifactDir.empty())
        {
            return;
        }

        std::filesystem::create_directories(artifactDir);

        const auto artifactPath = artifactDir / (sanitizeFileStem(caseName) + ".json");
        std::ofstream output(artifactPath, std::ios::out | std::ios::trunc);
        output << "{\n";
        output << "  \"case\": \"" << caseName << "\",\n";
        output << "  \"renderMode\": " << renderMode << ",\n";
        output << "  \"frames\": " << frameCount << ",\n";
        output << "  \"windowWidth\": " << context.width << ",\n";
        output << "  \"windowHeight\": " << context.height << ",\n";
        output << "  \"singleHostExecutable\": true\n";
        output << "}\n";
    }

    /** Queries the current drawable pixel size of the render test window across Metal, OHOS, and desktop Vulkan surfaces. */
    void queryRenderWindowDrawableSize(const RenderFeatureContext &ctx, int &drawableWidth, int &drawableHeight)
    {
        drawableWidth = 0;
        drawableHeight = 0;
#if defined(__OHOS__)
        if (ctx.window != nullptr)
        {
            (void)OH_NativeWindow_NativeWindowHandleOpt(ctx.window, GET_BUFFER_GEOMETRY, &drawableHeight, &drawableWidth);
        }
#elif defined(__APPLE__)
        SDL_Metal_GetDrawableSize(ctx.window, &drawableWidth, &drawableHeight);
#else
        SDL_Vulkan_GetDrawableSize(ctx.window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0)
        {
            SDL_GetWindowSizeInPixels(ctx.window, &drawableWidth, &drawableHeight);
        }
#endif
    }

    /** Builds a platform-native swapchain surface descriptor from the render test window. */
    GVM::RHI::NativeSurfaceDescriptor makeRenderWindowNativeSurface(const RenderFeatureContext &ctx, const GVM::RHI::Extent3D &extentHint)
    {
        GVM::RHI::NativeSurfaceDescriptor surface = {};
        surface.extentHint = extentHint;
        surface.hasExtentHint = GVM::RHI::True;
#if defined(__OHOS__)
        surface.kind = GVM::RHI::NativeSurfaceKind::OhosWindow;
        surface.surface = ctx.window;
#elif defined(__APPLE__)
        surface.kind = GVM::RHI::NativeSurfaceKind::MetalLayer;
        surface.surface = SDL_Metal_GetLayer(ctx.metalView);
#elif defined(_WIN32)
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(ctx.window, &info) == SDL_TRUE)
        {
            surface.kind = GVM::RHI::NativeSurfaceKind::Win32Window;
            surface.surface = info.info.win.window;
            surface.displayOrInstance = info.info.win.hinstance;
        }
#elif defined(__linux__)
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(ctx.window, &info) == SDL_TRUE)
        {
            if (info.subsystem == SDL_SYSWM_WAYLAND)
            {
                surface.kind = GVM::RHI::NativeSurfaceKind::WaylandSurface;
                surface.surface = info.info.wl.surface;
                surface.displayOrInstance = info.info.wl.display;
            }
            else if (info.subsystem == SDL_SYSWM_X11)
            {
                surface.kind = GVM::RHI::NativeSurfaceKind::XlibWindow;
                surface.surface = reinterpret_cast<void *>(static_cast<uintptr_t>(info.info.x11.window));
                surface.displayOrInstance = info.info.x11.display;
            }
        }
#endif
        return surface;
    }

    class DslRenderFeatureTests : public ::testing::Test
    {
    protected:
        static RenderFeatureContext &context()
        {
            static RenderFeatureContext ctx;
            return ctx;
        }

        static void SetUpTestSuite()
        {
            auto &ctx = context();

#if !defined(__OHOS__)
#if defined(__APPLE__)
            SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE);
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#endif
            SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
#endif
            ctx.quitRequested = false;
            ctx.available = false;
            ctx.unavailableReason.clear();
            const auto &options = GVM::Tests::renderTestOptions;
            ctx.simpleFrameCount = options.simpleFrames;
            ctx.complexFrameCount = options.complexFrames;
            ctx.frameDelayMs = options.frameDelayMs;
            ctx.warmupMs = options.warmupMs;
            ctx.casePauseMs = options.casePauseMs;
            ctx.backendName = options.backendName.c_str();

#if defined(__OHOS__)
            logRenderPhase("Creating OpenHarmony consumer surface and native window.");
            ctx.consumerSurface = OH_ConsumerSurface_Create();
            if (ctx.consumerSurface == nullptr)
            {
                ctx.unavailableReason = "OH_ConsumerSurface_Create returned nullptr.";
                return;
            }
            (void)OH_ConsumerSurface_SetDefaultSize(ctx.consumerSurface, ctx.width, ctx.height);
            (void)OH_ConsumerSurface_SetDefaultUsage(
                ctx.consumerSurface,
                NATIVEBUFFER_USAGE_CPU_READ | NATIVEBUFFER_USAGE_HW_RENDER | NATIVEBUFFER_USAGE_HW_TEXTURE);
            ctx.window = OH_NativeImage_AcquireNativeWindow(ctx.consumerSurface);
            if (ctx.window == nullptr)
            {
                ctx.unavailableReason = "OH_NativeImage_AcquireNativeWindow returned nullptr.";
                return;
            }
            resizeOhosConsumerWindow(ctx, ctx.width, ctx.height);
            pumpWindowEvents(ctx, ctx.warmupMs);
#else
            logRenderPhase("Initializing SDL video subsystem.");
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
            {
                ctx.unavailableReason = SDL_GetError();
                return;
            }

            logRenderPhase("Querying SDL display topology.");
            if (SDL_GetNumVideoDisplays() <= 0)
            {
                ctx.unavailableReason = "SDL reports zero video displays for the render test process.";
                return;
            }

            logRenderPhase("Creating SDL window.");
            Uint32 windowFlags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
#if defined(__APPLE__)
            windowFlags |= SDL_WINDOW_METAL;
#else
            windowFlags |= SDL_WINDOW_VULKAN;
#endif
            ctx.window = SDL_CreateWindow(
                "GVM Render Feature Tests",
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                ctx.width,
                ctx.height,
                windowFlags
            );
            if (ctx.window == nullptr)
            {
                ctx.unavailableReason = SDL_GetError();
                return;
            }

            pumpWindowEvents(ctx, ctx.warmupMs);

#if defined(__APPLE__)
            logRenderPhase("Creating SDL Metal view.");
            ctx.metalView = SDL_Metal_CreateView(ctx.window);
            if (ctx.metalView == nullptr)
            {
                ctx.unavailableReason = SDL_GetError();
                return;
            }
#endif
#endif

            logRenderPhase("Creating GVM instance.");
            GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
            descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
#endif
            ctx.instance = GVM::RHI::createInstance(descriptor);
            if (ctx.instance == nullptr)
            {
                ctx.unavailableReason = "Failed to create GVM RHI instance.";
                return;
            }

            logRenderPhase("Creating GVM device.");
            ctx.device = ctx.instance->createDevice();
            if (ctx.device == nullptr)
            {
                ctx.unavailableReason = "Failed to create GVM device.";
                return;
            }

            logRenderPhase("Creating GVM swapchain.");
            int drawableWidth = 0;
            int drawableHeight = 0;
            queryRenderWindowDrawableSize(ctx, drawableWidth, drawableHeight);
            GVM::RHI::Extent3D swapchainExtent = {
                .width = static_cast<uint32_t>(std::max(drawableWidth, 1)),
                .height = static_cast<uint32_t>(std::max(drawableHeight, 1)),
                .depth = 1,
            };
            const GVM::RHI::NativeSurfaceDescriptor nativeSurface = makeRenderWindowNativeSurface(ctx, swapchainExtent);
            if (nativeSurface.kind == GVM::RHI::NativeSurfaceKind::Undefined || nativeSurface.surface == nullptr)
            {
                ctx.unavailableReason = "Failed to extract native OS surface from SDL render test window.";
                return;
            }
            ctx.swapchain = ctx.instance->createSwapchain({
                .nativeSurface = nativeSurface,
#if defined(__APPLE__)
                .A = nativeSurface.surface,
                .B = &swapchainExtent,
#endif
            });
            if (ctx.swapchain == nullptr)
            {
                ctx.unavailableReason = "Failed to create swapchain for render feature host.";
                return;
            }
            ctx.available = true;

            logRenderPhase("Render host window and swapchain are ready.");
            pumpWindowEvents(ctx, ctx.warmupMs);
        }

        static void TearDownTestSuite()
        {
            auto &ctx = context();

            if (ctx.renderer != nullptr)
            {
                ctx.renderer->destroy();
                ctx.renderer = nullptr;
            }
            if (ctx.instance != nullptr)
            {
                GVM::RHI::destroyInstance(ctx.instance);
                ctx.instance = nullptr;
            }
            ctx.swapchain = nullptr;
            ctx.device = nullptr;
#if defined(__OHOS__)
            drainOhosConsumerBuffers(ctx.consumerSurface);
            ctx.window = nullptr;
            if (ctx.consumerSurface != nullptr)
            {
                OH_NativeImage_Destroy(&ctx.consumerSurface);
                ctx.consumerSurface = nullptr;
            }
#else
#if defined(__APPLE__)
            if (ctx.metalView != nullptr)
            {
                SDL_Metal_DestroyView(ctx.metalView);
                ctx.metalView = nullptr;
            }
#endif
            if (ctx.window != nullptr)
            {
                SDL_DestroyWindow(ctx.window);
                ctx.window = nullptr;
            }
            SDL_Quit();
#endif
        }

        void runMode(const char *windowTitle, int renderMode, int frameCount)
        {
            auto &ctx = context();
            ASSERT_TRUE(ctx.available) << ctx.unavailableReason;
            ASSERT_NE(ctx.window, nullptr);
            ASSERT_NE(ctx.device, nullptr);
            ASSERT_NE(ctx.swapchain, nullptr);
#if !defined(__OHOS__)
            ASSERT_NE(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_HIDDEN, 0u) << "Automated render tests must remain hidden.";
            ASSERT_EQ(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_INPUT_FOCUS, 0u) << "Automated render tests must not own keyboard focus.";
#endif

            ScopedRendererDestroy rendererDestroy{ctx};

            const std::string backendWindowTitle = std::string(windowTitle) + " [" + ctx.backendName + "]";
#if !defined(__OHOS__)
            SDL_SetWindowTitle(ctx.window, backendWindowTitle.c_str());
#else
            (void)backendWindowTitle;
#endif
            pumpWindowEvents(ctx, ctx.warmupMs);
            ctx.quitRequested = false;

            logRenderPhase(std::string("Starting case: ") + windowTitle);
            logRenderPhase("Constructing generated renderer.");
            ctx.renderer = makeMyRenderer();
            ASSERT_NE(ctx.renderer.get(), nullptr) << "Failed to construct generated render host.";
            ctx.renderer->setRenderMode(renderMode);

            logRenderPhase("Initializing generated renderer for current mode.");
            try
            {
                ctx.renderer->init(ctx.device, ctx.swapchain);
            }
            catch (const std::exception &ex)
            {
                FAIL() << "Failed to initialize generated render host for case '" << windowTitle << "': " << ex.what();
            }
            catch (...)
            {
                FAIL() << "Failed to initialize generated render host for case '" << windowTitle << "' with an unknown exception.";
            }

            logRenderPhase("Entering render loop.");
            const int progressInterval = std::max(1, frameCount / 4);

            for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                const auto frameStartTime = std::chrono::steady_clock::now();
#if !defined(__OHOS__)
                SDL_Event event;
                while (SDL_PollEvent(&event) != 0)
                {
                    if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
                    {
                        ctx.quitRequested = true;
                    }
                }
#endif

                ASSERT_FALSE(ctx.quitRequested) << "Window was closed before render test case finished: " << windowTitle;
                try
                {
                    ctx.renderer->render();
#if defined(__OHOS__)
                    drainOhosConsumerBuffers(ctx.consumerSurface);
#endif
                }
                catch (const std::exception &ex)
                {
                    FAIL() << "Renderer threw while presenting case '" << windowTitle << "': " << ex.what();
                }
                catch (...)
                {
                    FAIL() << "Renderer threw an unknown exception while presenting case '" << windowTitle << "'.";
                }

                if (((frameIndex + 1) % progressInterval) == 0 || frameIndex + 1 == frameCount)
                {
                    logRenderPhase(std::string("Completed ") + std::to_string(frameIndex + 1) + " / " + std::to_string(frameCount) + " frames for " + windowTitle);
                }
                const auto frameEndTime = std::chrono::steady_clock::now();
                const auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEndTime - frameStartTime);
                const auto targetFrameTime = std::chrono::milliseconds(ctx.frameDelayMs);
                if (elapsedTime < targetFrameTime)
                {
#if defined(__OHOS__)
                    std::this_thread::sleep_for(targetFrameTime - elapsedTime);
#else
                    SDL_Delay(static_cast<Uint32>((targetFrameTime - elapsedTime).count()));
#endif
                }
            }

            writeFinalRenderReadbackArtifact(ctx, windowTitle);
            if (renderMode == 8)
            {
                validatePixelLocalPresentReadback(ctx, windowTitle);
            }
            writeRenderArtifact(windowTitle, renderMode, frameCount, ctx);
            logRenderPhase(std::string("Finished case: ") + windowTitle);
            pumpWindowEvents(ctx, ctx.casePauseMs);
        }

        /** Renders the given number of frames, polling window events so a manual close still aborts the case. */
        void presentResizeCaseFrames(const char *windowTitle, int frameCount)
        {
            auto &ctx = context();
            for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
#if !defined(__OHOS__)
                SDL_Event event;
                while (SDL_PollEvent(&event) != 0)
                {
                    if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
                    {
                        ctx.quitRequested = true;
                    }
                }
#endif
                ASSERT_FALSE(ctx.quitRequested) << "Window was closed before resize case finished: " << windowTitle;
                try
                {
                    ctx.renderer->render();
#if defined(__OHOS__)
                    drainOhosConsumerBuffers(ctx.consumerSurface);
#endif
                }
                catch (const std::exception &ex)
                {
                    FAIL() << "Renderer threw while presenting resize case '" << windowTitle << "': " << ex.what();
                }
            }
        }

        /** Queries the swapchain until it reports Success, asserting the acquired texture matches
            the expected drawable dimensions, and presents the acquired image. */
        void reacquireSwapchainTextureMatchingDrawable(uint32_t expectedWidth, uint32_t expectedHeight)
        {
            auto &ctx = context();
            GVM::RHI::SwapchainQueryResult nextTexture = {};
            nextTexture.status = GVM::RHI::SwapchainNextTextureQueryStatus::Outdated;
            for (int attempt = 0; attempt < 16 && nextTexture.status != GVM::RHI::SwapchainNextTextureQueryStatus::Success; ++attempt)
            {
                try
                {
                    nextTexture = ctx.swapchain->queryNextTexture();
                }
                catch (const std::exception &ex)
                {
                    FAIL() << "queryNextTexture threw after resize: " << ex.what();
                }
                if (nextTexture.status == GVM::RHI::SwapchainNextTextureQueryStatus::Success)
                {
                    break;
                }
                pumpWindowEvents(ctx, ctx.casePauseMs);
            }
            ASSERT_EQ(nextTexture.status, GVM::RHI::SwapchainNextTextureQueryStatus::Success) << "Swapchain never reacquired a texture after resize.";
            ASSERT_FALSE(nextTexture.texture.isNull());
            EXPECT_EQ(nextTexture.texture->getWidth(), expectedWidth) << "Reacquired swapchain texture width does not match the drawable size.";
            EXPECT_EQ(nextTexture.texture->getHeight(), expectedHeight) << "Reacquired swapchain texture height does not match the drawable size.";
            ctx.swapchain->present();
#if defined(__OHOS__)
            drainOhosConsumerBuffers(ctx.consumerSurface);
#endif
        }

        /** Renders a short case, shrinks then restores the window, and verifies the swapchain
            reacquires textures matching the drawable size after each resize. */
        void runResizeCase(const char *windowTitle, int renderMode)
        {
            auto &ctx = context();
            ASSERT_TRUE(ctx.available) << ctx.unavailableReason;
            ASSERT_NE(ctx.window, nullptr);
            ASSERT_NE(ctx.device, nullptr);
            ASSERT_NE(ctx.swapchain, nullptr);
#if !defined(__OHOS__)
            ASSERT_NE(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_HIDDEN, 0u) << "Automated render tests must remain hidden.";
#endif

            ScopedRendererDestroy rendererDestroy{ctx};
            ScopedWindowSizeRestore windowSizeRestore{ctx};

            const std::string backendWindowTitle = std::string(windowTitle) + " [" + ctx.backendName + "]";
#if !defined(__OHOS__)
            SDL_SetWindowTitle(ctx.window, backendWindowTitle.c_str());
#else
            (void)backendWindowTitle;
#endif
            pumpWindowEvents(ctx, ctx.warmupMs);
            ctx.quitRequested = false;

            logRenderPhase(std::string("Starting resize case: ") + windowTitle);
            ctx.renderer = makeMyRenderer();
            ASSERT_NE(ctx.renderer.get(), nullptr) << "Failed to construct generated render host.";
            ctx.renderer->setRenderMode(renderMode);
            try
            {
                ctx.renderer->init(ctx.device, ctx.swapchain);
            }
            catch (const std::exception &ex)
            {
                FAIL() << "Failed to initialize generated render host for resize case: " << ex.what();
            }

            int baselineDrawableWidth = 0;
            int baselineDrawableHeight = 0;
            queryRenderWindowDrawableSize(ctx, baselineDrawableWidth, baselineDrawableHeight);
            const int resizedWindowWidth = std::max(1, ctx.width * 3 / 4);
            const int resizedWindowHeight = std::max(1, ctx.height * 3 / 4);
            const int framesPerPhase = std::min(8, std::max(1, ctx.simpleFrameCount));

            logRenderPhase("Rendering pre-resize frames.");
            ASSERT_NO_FATAL_FAILURE(presentResizeCaseFrames(windowTitle, framesPerPhase));

            logRenderPhase("Shrinking the window to a clearly different drawable size.");
#if defined(__OHOS__)
            resizeOhosConsumerWindow(ctx, resizedWindowWidth, resizedWindowHeight);
#else
            SDL_SetWindowSize(ctx.window, resizedWindowWidth, resizedWindowHeight);
#endif
            pumpWindowEvents(ctx, ctx.casePauseMs);

            int drawableWidth = 0;
            int drawableHeight = 0;
            queryRenderWindowDrawableSize(ctx, drawableWidth, drawableHeight);
            ASSERT_NE(drawableWidth, baselineDrawableWidth) << "SDL resize did not change the drawable width.";
            ASSERT_NE(drawableHeight, baselineDrawableHeight) << "SDL resize did not change the drawable height.";

            logRenderPhase("Reacquiring swapchain textures until the swapchain reports success.");
            ASSERT_NO_FATAL_FAILURE(reacquireSwapchainTextureMatchingDrawable(
                static_cast<uint32_t>(std::max(drawableWidth, 1)),
                static_cast<uint32_t>(std::max(drawableHeight, 1))));

            logRenderPhase("Rendering post-resize frames.");
            ASSERT_NO_FATAL_FAILURE(presentResizeCaseFrames(windowTitle, framesPerPhase));

            logRenderPhase("Restoring the original window size.");
#if defined(__OHOS__)
            resizeOhosConsumerWindow(ctx, ctx.width, ctx.height);
#else
            SDL_SetWindowSize(ctx.window, ctx.width, ctx.height);
#endif
            pumpWindowEvents(ctx, ctx.casePauseMs);

            int restoredDrawableWidth = 0;
            int restoredDrawableHeight = 0;
            queryRenderWindowDrawableSize(ctx, restoredDrawableWidth, restoredDrawableHeight);
            ASSERT_NE(restoredDrawableWidth, drawableWidth) << "SDL restore did not change the drawable width.";
            ASSERT_NE(restoredDrawableHeight, drawableHeight) << "SDL restore did not change the drawable height.";

            logRenderPhase("Reacquiring swapchain textures at the restored drawable size.");
            ASSERT_NO_FATAL_FAILURE(reacquireSwapchainTextureMatchingDrawable(
                static_cast<uint32_t>(std::max(restoredDrawableWidth, 1)),
                static_cast<uint32_t>(std::max(restoredDrawableHeight, 1))));

            logRenderPhase("Rendering post-restore frames.");
            ASSERT_NO_FATAL_FAILURE(presentResizeCaseFrames(windowTitle, framesPerPhase));

            logRenderPhase(std::string("Finished resize case: ") + windowTitle);
            pumpWindowEvents(ctx, ctx.casePauseMs);
        }
    };

    TEST_F(DslRenderFeatureTests, PresentsProceduralTriangleToWindow)
    {
        runMode("Render Case 01 - Procedural Triangle", 0, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsIndexedTriangleWithVertexBuffers)
    {
        runMode("Render Case 02 - Buffered Triangle", 1, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsTexturedTriangleWithSamplerBindings)
    {
        runMode("Render Case 03 - Textured Triangle", 2, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsMultiPassCubeCompositeScene)
    {
        runMode("Render Case 04 - Multi-pass Cube", 3, context().complexFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsInstancedTriangleSwarmScene)
    {
        runMode("Render Case 05 - Instanced Triangle Swarm", 4, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsFullscreenProceduralGradientScene)
    {
        runMode("Render Case 06 - Fullscreen Gradient", 5, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsComputeGeneratedPatternScene)
    {
        runMode("Render Case 07 - Compute Pattern Present", 6, context().simpleFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsOffscreenPostProcessScene)
    {
        runMode("Render Case 08 - Offscreen Post Process", 7, context().complexFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsPixelLocalDeferredLightingScene)
    {
        runMode("Render Case 09 - Pixel Local Deferred Lighting", 8, context().complexFrameCount);
    }

    TEST_F(DslRenderFeatureTests, PresentsAfterWindowResizeMatchesDrawableSize)
    {
        runResizeCase("Render Case 10 - Window Resize Reacquire", 0);
    }

    /** Verifies that repeated queryNextTexture calls return the same texture until present, and that
        the acquired texture stays valid between present and the next acquire. */
    TEST_F(DslRenderFeatureTests, SwapchainQueryReturnsSameTextureUntilPresent)
    {
        auto &ctx = context();
        ASSERT_TRUE(ctx.available) << ctx.unavailableReason;
        ASSERT_NE(ctx.window, nullptr);
        ASSERT_NE(ctx.swapchain, nullptr);
#if !defined(__OHOS__)
        ASSERT_NE(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_HIDDEN, 0u) << "Automated render tests must remain hidden.";
#endif

        GVM::RHI::SwapchainQueryResult first = {};
        GVM::RHI::SwapchainQueryResult second = {};
        try
        {
            first = ctx.swapchain->queryNextTexture();
            second = ctx.swapchain->queryNextTexture();
        }
        catch (const std::exception &ex)
        {
            FAIL() << "queryNextTexture threw before present: " << ex.what();
        }
        ASSERT_EQ(first.status, GVM::RHI::SwapchainNextTextureQueryStatus::Success);
        ASSERT_FALSE(first.texture.isNull());
        EXPECT_EQ(first.texture, second.texture);
        EXPECT_EQ(second.status, GVM::RHI::SwapchainNextTextureQueryStatus::Success);

        try
        {
            ctx.swapchain->present();
#if defined(__OHOS__)
            drainOhosConsumerBuffers(ctx.consumerSurface);
#endif
        }
        catch (const std::exception &ex)
        {
            FAIL() << "present threw after acquire: " << ex.what();
        }
        EXPECT_FALSE(first.texture.isNull()) << "The acquired swapchain texture should stay valid after present.";

        GVM::RHI::SwapchainQueryResult afterPresent = {};
        try
        {
            afterPresent = ctx.swapchain->queryNextTexture();
        }
        catch (const std::exception &ex)
        {
            FAIL() << "queryNextTexture threw after present: " << ex.what();
        }
        ASSERT_EQ(afterPresent.status, GVM::RHI::SwapchainNextTextureQueryStatus::Success);
        try
        {
            ctx.swapchain->present();
#if defined(__OHOS__)
            drainOhosConsumerBuffers(ctx.consumerSurface);
#endif
        }
        catch (const std::exception &ex)
        {
            FAIL() << "present threw after second acquire: " << ex.what();
        }
    }
} // namespace
