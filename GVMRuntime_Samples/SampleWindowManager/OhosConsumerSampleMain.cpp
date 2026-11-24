#include "SampleApplication.hpp"
#include "SampleWindowManager.hpp"

#if defined(__OHOS__)

#include <native_buffer/native_buffer.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>

#include <EASTL/algorithm.h>
#include <EASTL/unique_ptr.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace
{
    /** Holds command-line parameters for automated OpenHarmony ConsumerSurface sample execution. */
    struct OhosSampleRunOptions
    {
        uint32_t width = 1280u;
        uint32_t height = 720u;
        uint32_t frameCount = 8u;
    };

    /** Parses a positive unsigned integer suffix after a matching CLI flag prefix. */
    bool tryParseUnsignedOption(const char *argument, const char *prefix, uint32_t &outValue)
    {
        const size_t prefixLength = std::strlen(prefix);
        if (std::strncmp(argument, prefix, prefixLength) != 0)
        {
            return false;
        }
        const char *digits = argument + prefixLength;
        if (*digits == '\0')
        {
            return false;
        }
        uint32_t parsedValue = 0u;
        const auto result = std::from_chars(digits, digits + std::strlen(digits), parsedValue);
        if (result.ec != std::errc{} || *result.ptr != '\0' || parsedValue == 0u)
        {
            return false;
        }
        outValue = parsedValue;
        return true;
    }

    /** Parses explicit CLI flags controlling frame count and surface resolution. */
    OhosSampleRunOptions parseSampleRunOptions(int argc, char **argv)
    {
        OhosSampleRunOptions options = {};
        for (int index = 1; index < argc; ++index)
        {
            const char *arg = argv[index];
            if (tryParseUnsignedOption(arg, "--frames=", options.frameCount))
            {
                continue;
            }
            if (tryParseUnsignedOption(arg, "--width=", options.width))
            {
                continue;
            }
            if (tryParseUnsignedOption(arg, "--height=", options.height))
            {
                continue;
            }
            if (std::strcmp(arg, "--frames") == 0 && index + 1 < argc)
            {
                (void)tryParseUnsignedOption(argv[++index], "", options.frameCount);
            }
        }
        return options;
    }

    /** Drains all presented buffers from an OpenHarmony consumer surface and returns the drained count. */
    uint32_t drainPresentedBuffers(OH_NativeImage *consumerSurface)
    {
        uint32_t drainedCount = 0u;
        if (consumerSurface == nullptr)
        {
            return drainedCount;
        }

        OHNativeWindowBuffer *buffer = nullptr;
        int fenceFd = -1;
        while (OH_NativeImage_AcquireNativeWindowBuffer(consumerSurface, &buffer, &fenceFd) == 0 && buffer != nullptr)
        {
            (void)OH_NativeImage_ReleaseNativeWindowBuffer(consumerSurface, buffer, fenceFd);
            buffer = nullptr;
            fenceFd = -1;
            ++drainedCount;
        }
        return drainedCount;
    }

    /** Owns an OpenHarmony ConsumerSurface and its paired producer OHNativeWindow for RAII cleanup. */
    class ScopedOhosConsumerSurface
    {
    public:
        /** Creates a consumer surface and configures its initial geometry and buffer usage. */
        ScopedOhosConsumerSurface(uint32_t width, uint32_t height)
        {
            mConsumer = OH_ConsumerSurface_Create();
            if (mConsumer == nullptr)
            {
                throw std::runtime_error("OH_ConsumerSurface_Create failed.");
            }
            const int32_t clampedWidth = static_cast<int32_t>(eastl::max(width, 1u));
            const int32_t clampedHeight = static_cast<int32_t>(eastl::max(height, 1u));
            (void)OH_ConsumerSurface_SetDefaultSize(mConsumer, clampedWidth, clampedHeight);
            (void)OH_ConsumerSurface_SetDefaultUsage(
                mConsumer,
                NATIVEBUFFER_USAGE_CPU_READ | NATIVEBUFFER_USAGE_HW_RENDER | NATIVEBUFFER_USAGE_HW_TEXTURE);
            mWindow = OH_NativeImage_AcquireNativeWindow(mConsumer);
            if (mWindow == nullptr)
            {
                OH_NativeImage_Destroy(&mConsumer);
                mConsumer = nullptr;
                throw std::runtime_error("OH_NativeImage_AcquireNativeWindow failed.");
            }
            (void)OH_NativeWindow_NativeWindowHandleOpt(mWindow, SET_BUFFER_GEOMETRY, clampedWidth, clampedHeight);
        }

        ScopedOhosConsumerSurface(const ScopedOhosConsumerSurface &) = delete;
        ScopedOhosConsumerSurface &operator=(const ScopedOhosConsumerSurface &) = delete;

        /** Drains remaining buffers and destroys the underlying OpenHarmony consumer surface. */
        ~ScopedOhosConsumerSurface()
        {
            (void)drainPresentedBuffers(mConsumer);
            mWindow = nullptr;
            if (mConsumer != nullptr)
            {
                OH_NativeImage_Destroy(&mConsumer);
                mConsumer = nullptr;
            }
        }

        /** Returns the consumer surface used to drain presented frames. */
        OH_NativeImage *consumer() const noexcept
        {
            return mConsumer;
        }

        /** Returns the producer native window supplied to VK_OHOS_surface. */
        OHNativeWindow *window() const noexcept
        {
            return mWindow;
        }

    private:
        OH_NativeImage *mConsumer = nullptr;
        OHNativeWindow *mWindow = nullptr;
    };
} // namespace

/** Executes a linked GVM sample application against an OpenHarmony ConsumerSurface swapchain. */
int main(int argc, char **argv)
{
    try
    {
        const OhosSampleRunOptions options = parseSampleRunOptions(argc, argv);
        ScopedOhosConsumerSurface surface(options.width, options.height);

        GVM::RHI::NativeSurfaceDescriptor nativeSurface = {};
        nativeSurface.kind = GVM::RHI::NativeSurfaceKind::OhosWindow;
        nativeSurface.surface = surface.window();
        nativeSurface.extentHint = {options.width, options.height, 1u};
        nativeSurface.hasExtentHint = GVM::RHI::True;

        GVM::Samples::SampleWindowCreateInfo windowInfo = {};
        windowInfo.title = "GVM OHOS Sample";
        windowInfo.width = options.width;
        windowInfo.height = options.height;
        windowInfo.backend = GVM::Samples::SampleWindowBackend::OhosXComponent;
        windowInfo.preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;
        windowInfo.nativeSurface = nativeSurface;

        const auto startTime = std::chrono::steady_clock::now();
        GVM::Samples::SampleWindowManager window(windowInfo);
        eastl::unique_ptr<GVM::Samples::ISampleApplication> app = GVM::Samples::createSampleApplication(window);

        uint32_t renderedFrames = 0u;
        uint32_t consumedBuffers = 0u;
        for (uint32_t frameIndex = 0u; frameIndex < options.frameCount; ++frameIndex)
        {
            app->processPendingEvents();
            if (app->shouldQuit())
            {
                break;
            }
            app->renderFrame();
            ++renderedFrames;
            consumedBuffers += drainPresentedBuffers(surface.consumer());
        }
        consumedBuffers += drainPresentedBuffers(surface.consumer());
        app.reset();
        consumedBuffers += drainPresentedBuffers(surface.consumer());

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        std::fprintf(
            stdout,
            "[gvm-ohos-sample] status=ok renderedFrames=%u consumedBuffers=%u resolution=%ux%u elapsedMs=%lld\n",
            renderedFrames,
            consumedBuffers,
            options.width,
            options.height,
            static_cast<long long>(elapsedMs));
        std::fflush(stdout);
        return renderedFrames > 0u ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[gvm-ohos-sample] status=error message=%s\n", error.what());
        std::fflush(stderr);
        return 1;
    }
}

#endif
