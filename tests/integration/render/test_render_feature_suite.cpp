#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRenderTestOptions.hpp>

#include <SDL.h>
#include <SDL_metal.h>

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
#include <sstream>
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
        SDL_Window *window = nullptr;
        SDL_MetalView metalView = nullptr;
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
        uint64_t fnv1a64 = 0;
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

    /// Pumps events without showing or focusing the hidden test window; close requests cancel execution.
    void pumpWindowEvents(SDL_Window *window, int warmupMs)
    {
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

    /// Converts an unsigned 64-bit value to a fixed-width lowercase hexadecimal string.
    std::string formatUint64Hex(uint64_t value)
    {
        std::ostringstream output;
        output << "0x" << std::hex;
        output.width(16);
        output.fill('0');
        output << value;
        return output.str();
    }

    /// Computes a stable FNV-1a hash for readback bytes written to JSON diagnostics.
    uint64_t calculateFnv1a64(const std::vector<std::uint8_t> &bytes)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const std::uint8_t byte : bytes)
        {
            hash ^= uint64_t(byte);
            hash *= 1099511628211ull;
        }
        return hash;
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

    /// Computes luminance, hash, and coarse tile metrics from an RGBA8 readback.
    RenderReadbackStats analyzeRenderReadback(const RenderReadbackImage &image)
    {
        RenderReadbackStats stats;
        stats.byteCount = image.pixels.size();
        stats.totalPixels = uint64_t(image.width) * uint64_t(image.height);
        stats.fnv1a64 = calculateFnv1a64(image.pixels);
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
            + " fnv1a64=" + formatUint64Hex(stats.fnv1a64)
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
        output << "  \"fnv1a64\": \"" << formatUint64Hex(stats.fnv1a64) << "\",\n";
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
            }
            FAIL() << "Pixel-local final-screen readback failed: " << ex.what();
            return;
        }

        context.swapchain->present();
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

            SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE);
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
            SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
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
            ctx.window = SDL_CreateWindow(
                "GVM Render Feature Tests",
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                ctx.width,
                ctx.height,
                SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL
            );
            if (ctx.window == nullptr)
            {
                ctx.unavailableReason = SDL_GetError();
                return;
            }

            pumpWindowEvents(ctx.window, ctx.warmupMs);

            logRenderPhase("Creating SDL Metal view.");
            ctx.metalView = SDL_Metal_CreateView(ctx.window);
            if (ctx.metalView == nullptr)
            {
                ctx.unavailableReason = SDL_GetError();
                return;
            }

            logRenderPhase("Creating GVM instance.");
            GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
            descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
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
            SDL_Metal_GetDrawableSize(ctx.window, &drawableWidth, &drawableHeight);
            GVM::RHI::Extent3D swapchainExtent = {
                .width = static_cast<uint32_t>(std::max(drawableWidth, 1)),
                .height = static_cast<uint32_t>(std::max(drawableHeight, 1)),
                .depth = 1,
            };
            ctx.swapchain = ctx.instance->createSwapchain({
                .A = SDL_Metal_GetLayer(ctx.metalView),
                .B = &swapchainExtent,
            });
            if (ctx.swapchain == nullptr)
            {
                ctx.unavailableReason = "Failed to create swapchain for render feature host.";
                return;
            }
            ctx.available = true;

            logRenderPhase("Render host window and swapchain are ready.");
            pumpWindowEvents(ctx.window, ctx.warmupMs);
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
            if (ctx.metalView != nullptr)
            {
                SDL_Metal_DestroyView(ctx.metalView);
                ctx.metalView = nullptr;
            }
            if (ctx.window != nullptr)
            {
                SDL_DestroyWindow(ctx.window);
                ctx.window = nullptr;
            }
            SDL_Quit();
        }

        void runMode(const char *windowTitle, int renderMode, int frameCount)
        {
            auto &ctx = context();
            ASSERT_TRUE(ctx.available) << ctx.unavailableReason;
            ASSERT_NE(ctx.window, nullptr);
            ASSERT_NE(ctx.device, nullptr);
            ASSERT_NE(ctx.swapchain, nullptr);
            ASSERT_NE(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_HIDDEN, 0u) << "Automated render tests must remain hidden.";
            ASSERT_EQ(SDL_GetWindowFlags(ctx.window) & SDL_WINDOW_INPUT_FOCUS, 0u) << "Automated render tests must not own keyboard focus.";

            ScopedRendererDestroy rendererDestroy{ctx};

            const std::string backendWindowTitle = std::string(windowTitle) + " [" + ctx.backendName + "]";
            SDL_SetWindowTitle(ctx.window, backendWindowTitle.c_str());
            pumpWindowEvents(ctx.window, ctx.warmupMs);
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
                SDL_Event event;
                while (SDL_PollEvent(&event) != 0)
                {
                    if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
                    {
                        ctx.quitRequested = true;
                    }
                }

                ASSERT_FALSE(ctx.quitRequested) << "Window was closed before render test case finished: " << windowTitle;
                try
                {
                    ctx.renderer->render();
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
                    SDL_Delay(static_cast<Uint32>((targetFrameTime - elapsedTime).count()));
                }
            }

            writeFinalRenderReadbackArtifact(ctx, windowTitle);
            if (renderMode == 8)
            {
                validatePixelLocalPresentReadback(ctx, windowTitle);
            }
            writeRenderArtifact(windowTitle, renderMode, frameCount, ctx);
            logRenderPhase(std::string("Finished case: ") + windowTitle);
            pumpWindowEvents(ctx.window, ctx.casePauseMs);
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
} // namespace
