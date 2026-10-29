#include "SampleApplication.hpp"
#include "SampleWindowManager.hpp"

#if defined(__ANDROID__)

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EASTL/algorithm.h>
#include <EASTL/unique_ptr.h>

#include <exception>

namespace
{
    constexpr const char *AndroidLogTag = "GVMSample";

    /// Stores the Android NativeActivity state owned by the shared sample host.
    struct AndroidSampleHostState
    {
        android_app *nativeApp = nullptr;
        eastl::unique_ptr<GVM::Samples::SampleWindowManager> window;
        eastl::unique_ptr<GVM::Samples::ISampleApplication> application;
        bool quitRequested = false;
    };

    /// Writes one error line to Android logcat.
    void logAndroidError(const char *message)
    {
        __android_log_print(ANDROID_LOG_ERROR, AndroidLogTag, "%s", message != nullptr ? message : "unknown error");
    }

    /// Builds a typed native surface descriptor from the current Android native window.
    GVM::RHI::NativeSurfaceDescriptor makeAndroidSurfaceDescriptor(ANativeWindow *nativeWindow)
    {
        const uint32_t width = static_cast<uint32_t>(eastl::max(ANativeWindow_getWidth(nativeWindow), 1));
        const uint32_t height = static_cast<uint32_t>(eastl::max(ANativeWindow_getHeight(nativeWindow), 1));

        GVM::RHI::NativeSurfaceDescriptor descriptor;
        descriptor.kind = GVM::RHI::NativeSurfaceKind::AndroidWindow;
        descriptor.surface = nativeWindow;
        descriptor.extentHint = {width, height, 1u};
        descriptor.hasExtentHint = GVM::RHI::True;
        return descriptor;
    }

    /// Builds the common window creation info used by Android sample shared libraries.
    GVM::Samples::SampleWindowCreateInfo makeAndroidWindowCreateInfo(ANativeWindow *nativeWindow)
    {
        const GVM::RHI::NativeSurfaceDescriptor surface = makeAndroidSurfaceDescriptor(nativeWindow);

        GVM::Samples::SampleWindowCreateInfo createInfo;
        createInfo.title = "GVM Sample";
        createInfo.width = surface.extentHint.width;
        createInfo.height = surface.extentHint.height;
        createInfo.backend = GVM::Samples::SampleWindowBackend::AndroidNative;
        createInfo.preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;
        createInfo.nativeSurface = surface;
        return createInfo;
    }

    /// Releases the current sample application before the native surface is destroyed or replaced.
    void destroyApplication(AndroidSampleHostState &state)
    {
        state.application.reset();
        if (state.window != nullptr)
        {
            state.window->clearNativeSurface();
        }
        state.window.reset();
    }

    /// Creates the platform window wrapper and then asks the linked sample to create its application.
    void createApplicationForCurrentWindow(AndroidSampleHostState &state)
    {
        if (state.nativeApp == nullptr || state.nativeApp->window == nullptr)
        {
            return;
        }

        destroyApplication(state);
        state.window = eastl::make_unique<GVM::Samples::SampleWindowManager>(
            makeAndroidWindowCreateInfo(state.nativeApp->window));
        state.application = GVM::Samples::createSampleApplication(*state.window);
    }

    /// Recreates the active sample after Android reports a native-window size or configuration change.
    void recreateApplicationForCurrentWindow(AndroidSampleHostState &state)
    {
        if (state.nativeApp == nullptr || state.nativeApp->window == nullptr)
        {
            destroyApplication(state);
            return;
        }

        createApplicationForCurrentWindow(state);
    }

    /// Handles NativeActivity app lifecycle commands.
    void handleAppCommand(android_app *nativeApp, int32_t command)
    {
        auto *state = static_cast<AndroidSampleHostState *>(nativeApp->userData);
        if (state == nullptr)
        {
            return;
        }

        try
        {
            switch (command)
            {
            case APP_CMD_INIT_WINDOW:
                createApplicationForCurrentWindow(*state);
                break;
            case APP_CMD_TERM_WINDOW:
                destroyApplication(*state);
                break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
                recreateApplicationForCurrentWindow(*state);
                break;
            case APP_CMD_DESTROY:
                state->quitRequested = true;
                break;
            default:
                break;
            }
        }
        catch (const std::exception &error)
        {
            logAndroidError(error.what());
            state->quitRequested = true;
            if (nativeApp->activity != nullptr)
            {
                ANativeActivity_finish(nativeApp->activity);
            }
        }
    }

    /// Converts one Android motion event into a SampleWindowManager event.
    bool pushAndroidMotionEvent(AndroidSampleHostState &state, AInputEvent *inputEvent)
    {
        if (state.window == nullptr)
        {
            return false;
        }

        const int32_t action = AMotionEvent_getAction(inputEvent);
        const int32_t maskedAction = action & AMOTION_EVENT_ACTION_MASK;
        const int32_t pointerIndex =
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        GVM::Samples::SampleWindowEvent event;
        event.pointerId = static_cast<int>(AMotionEvent_getPointerId(inputEvent, pointerIndex));
        event.x = AMotionEvent_getX(inputEvent, pointerIndex);
        event.y = AMotionEvent_getY(inputEvent, pointerIndex);

        switch (maskedAction)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerDown;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerUp;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerMove;
            break;
        default:
            return false;
        }

        state.window->pushEvent(event);
        return true;
    }

    /// Converts one Android key event into a SampleWindowManager event.
    bool pushAndroidKeyEvent(AndroidSampleHostState &state, AInputEvent *inputEvent)
    {
        if (state.window == nullptr)
        {
            return false;
        }

        const int32_t keyCode = AKeyEvent_getKeyCode(inputEvent);
        const int32_t action = AKeyEvent_getAction(inputEvent);
        if (keyCode == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN)
        {
            GVM::Samples::SampleWindowEvent quitEvent;
            quitEvent.type = GVM::Samples::SampleWindowEvent::Type::Quit;
            state.window->pushEvent(quitEvent);
            return true;
        }

        GVM::Samples::SampleWindowEvent event;
        event.key = keyCode;
        if (action == AKEY_EVENT_ACTION_DOWN)
        {
            event.type = GVM::Samples::SampleWindowEvent::Type::KeyDown;
        }
        else if (action == AKEY_EVENT_ACTION_UP)
        {
            event.type = GVM::Samples::SampleWindowEvent::Type::KeyUp;
        }
        else
        {
            return false;
        }

        state.window->pushEvent(event);
        return true;
    }

    /// Handles Android input callbacks and forwards supported events to SampleWindowManager.
    int32_t handleInputEvent(android_app *nativeApp, AInputEvent *inputEvent)
    {
        auto *state = static_cast<AndroidSampleHostState *>(nativeApp->userData);
        if (state == nullptr || inputEvent == nullptr)
        {
            return 0;
        }

        switch (AInputEvent_getType(inputEvent))
        {
        case AINPUT_EVENT_TYPE_MOTION:
            return pushAndroidMotionEvent(*state, inputEvent) ? 1 : 0;
        case AINPUT_EVENT_TYPE_KEY:
            return pushAndroidKeyEvent(*state, inputEvent) ? 1 : 0;
        default:
            return 0;
        }
    }

    /// Pumps all pending NativeActivity commands and input events.
    void processAndroidEvents(AndroidSampleHostState &state)
    {
        android_poll_source *source = nullptr;
        int timeoutMs = state.application != nullptr ? 0 : -1;
        while (ALooper_pollOnce(timeoutMs, nullptr, nullptr, reinterpret_cast<void **>(&source)) >= 0)
        {
            if (source != nullptr)
            {
                source->process(state.nativeApp, source);
            }
            if (state.nativeApp->destroyRequested != 0)
            {
                state.quitRequested = true;
                break;
            }
            timeoutMs = 0;
        }
    }

    /// Processes sample events and renders one frame when the sample application is active.
    void renderFrameIfReady(AndroidSampleHostState &state)
    {
        if (state.application == nullptr)
        {
            return;
        }

        state.application->processPendingEvents();
        if (state.application->shouldQuit())
        {
            state.quitRequested = true;
            if (state.nativeApp != nullptr && state.nativeApp->activity != nullptr)
            {
                ANativeActivity_finish(state.nativeApp->activity);
            }
            return;
        }
        state.application->renderFrame();
    }
} // namespace

/// NativeActivity entry point shared by Android windowed sample packages.
void android_main(android_app *nativeApp)
{
    app_dummy();

    AndroidSampleHostState state;
    state.nativeApp = nativeApp;
    nativeApp->userData = &state;
    nativeApp->onAppCmd = handleAppCommand;
    nativeApp->onInputEvent = handleInputEvent;

    while (!state.quitRequested)
    {
        processAndroidEvents(state);
        try
        {
            renderFrameIfReady(state);
        }
        catch (const std::exception &error)
        {
            logAndroidError(error.what());
            state.quitRequested = true;
            if (nativeApp->activity != nullptr)
            {
                ANativeActivity_finish(nativeApp->activity);
            }
        }
    }

    destroyApplication(state);
}

#endif // defined(__ANDROID__)
