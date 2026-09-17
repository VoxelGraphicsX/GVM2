#include "SampleApplication.hpp"
#include "SampleWindowManager.hpp"

#if defined(__OHOS__)

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <native_window/external_window.h>

#include <EASTL/algorithm.h>
#include <EASTL/unique_ptr.h>

#include <exception>

#ifndef GVM_OHOS_NAPI_MODULE_NAME
#define GVM_OHOS_NAPI_MODULE_NAME "gvm_test01_triangle"
#endif

namespace
{
    constexpr unsigned int OhosLogDomain = 0xD001A00;
    constexpr const char *OhosLogTag = "GVMSample";

    /// Stores the OHOS XComponent state owned by the shared sample host.
    struct OhosSampleHostState
    {
        OH_NativeXComponent *nativeXComponent = nullptr;
        OHNativeWindow *nativeWindow = nullptr;
        eastl::unique_ptr<GVM::Samples::SampleWindowManager> window;
        eastl::unique_ptr<GVM::Samples::ISampleApplication> application;
        bool renderingFrame = false;
        bool frameCallbackRegistered = false;
    };

    /// Returns the single sample host state used by one XComponent-backed native module.
    OhosSampleHostState &sampleHostState()
    {
        static OhosSampleHostState state;
        return state;
    }

    /// Writes one informational line to the OHOS hilog stream.
    void logOhosInfo(const char *message)
    {
        OH_LOG_Print(LOG_APP, LOG_INFO, OhosLogDomain, OhosLogTag, "%{public}s", message != nullptr ? message : "");
    }

    /// Writes one error line to the OHOS hilog stream.
    void logOhosError(const char *message)
    {
        OH_LOG_Print(LOG_APP, LOG_ERROR, OhosLogDomain, OhosLogTag, "%{public}s", message != nullptr ? message : "unknown error");
    }

    /// Writes one OHOS API failure line to the OHOS hilog stream.
    void logOhosApiFailure(const char *apiName, int32_t result)
    {
        OH_LOG_Print(
            LOG_APP,
            LOG_ERROR,
            OhosLogDomain,
            OhosLogTag,
            "%{public}s failed: %{public}d",
            apiName != nullptr ? apiName : "OHOS API",
            result);
    }

    /// Builds a typed native surface descriptor from the current OHOS XComponent window.
    GVM::RHI::NativeSurfaceDescriptor makeOhosSurfaceDescriptor(
        OH_NativeXComponent *nativeXComponent,
        OHNativeWindow *nativeWindow)
    {
        uint64_t width = 1;
        uint64_t height = 1;
        const int32_t result = OH_NativeXComponent_GetXComponentSize(
            nativeXComponent,
            nativeWindow,
            &width,
            &height);
        if (result != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
        {
            logOhosApiFailure("OH_NativeXComponent_GetXComponentSize", result);
        }

        GVM::RHI::NativeSurfaceDescriptor descriptor;
        descriptor.kind = GVM::RHI::NativeSurfaceKind::OhosWindow;
        descriptor.surface = nativeWindow;
        descriptor.extentHint = {
            static_cast<uint32_t>(eastl::max<uint64_t>(width, 1)),
            static_cast<uint32_t>(eastl::max<uint64_t>(height, 1)),
            1u,
        };
        descriptor.hasExtentHint = GVM::RHI::True;
        return descriptor;
    }

    /// Builds the common window creation info used by OHOS XComponent sample libraries.
    GVM::Samples::SampleWindowCreateInfo makeOhosWindowCreateInfo(
        OH_NativeXComponent *nativeXComponent,
        OHNativeWindow *nativeWindow)
    {
        const GVM::RHI::NativeSurfaceDescriptor surface =
            makeOhosSurfaceDescriptor(nativeXComponent, nativeWindow);

        GVM::Samples::SampleWindowCreateInfo createInfo;
        createInfo.title = "GVM Sample";
        createInfo.width = surface.extentHint.width;
        createInfo.height = surface.extentHint.height;
        createInfo.backend = GVM::Samples::SampleWindowBackend::OhosXComponent;
        createInfo.preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;
        createInfo.nativeSurface = surface;
        return createInfo;
    }

    /// Releases the current sample application before the native surface is destroyed or replaced.
    void destroyApplication(OhosSampleHostState &state)
    {
        state.application.reset();
        if (state.window != nullptr)
        {
            state.window->clearNativeSurface();
        }
        state.window.reset();
    }

    /// Creates the platform window wrapper and then asks the linked sample to create its application.
    void createApplicationForWindow(OhosSampleHostState &state, OH_NativeXComponent *component, void *window)
    {
        if (component == nullptr || window == nullptr)
        {
            return;
        }

        state.nativeXComponent = component;
        state.nativeWindow = static_cast<OHNativeWindow *>(window);
        destroyApplication(state);
        state.window = eastl::make_unique<GVM::Samples::SampleWindowManager>(
            makeOhosWindowCreateInfo(component, state.nativeWindow));
        state.application = GVM::Samples::createSampleApplication(*state.window);
    }

    /// Recreates the active sample after OHOS reports an XComponent surface size change.
    void recreateApplicationForWindow(OhosSampleHostState &state, OH_NativeXComponent *component, void *window)
    {
        createApplicationForWindow(state, component, window);
    }

    /// Handles one exception raised by the sample runtime from an OHOS callback.
    void handleCallbackException(OhosSampleHostState &state, const std::exception &error)
    {
        logOhosError(error.what());
        destroyApplication(state);
    }

    /// Handles the OHOS surface-created callback and creates the sample application.
    void onSurfaceCreated(OH_NativeXComponent *component, void *window)
    {
        try
        {
            createApplicationForWindow(sampleHostState(), component, window);
            logOhosInfo("OHOS XComponent surface created.");
        }
        catch (const std::exception &error)
        {
            handleCallbackException(sampleHostState(), error);
        }
        catch (...)
        {
            logOhosError("Unknown exception during OHOS surface creation.");
            destroyApplication(sampleHostState());
        }
    }

    /// Handles the OHOS surface-changed callback and rebuilds the sample swapchain owner.
    void onSurfaceChanged(OH_NativeXComponent *component, void *window)
    {
        try
        {
            recreateApplicationForWindow(sampleHostState(), component, window);
            logOhosInfo("OHOS XComponent surface changed.");
        }
        catch (const std::exception &error)
        {
            handleCallbackException(sampleHostState(), error);
        }
        catch (...)
        {
            logOhosError("Unknown exception during OHOS surface resize.");
            destroyApplication(sampleHostState());
        }
    }

    /// Handles the OHOS surface-destroyed callback and releases all surface-bound sample resources.
    void onSurfaceDestroyed(OH_NativeXComponent *component, void *window)
    {
        (void)component;
        (void)window;
        OhosSampleHostState &state = sampleHostState();
        destroyApplication(state);
        state.nativeWindow = nullptr;
        logOhosInfo("OHOS XComponent surface destroyed.");
    }

    /// Converts one OHOS touch event into a SampleWindowManager event.
    bool pushOhosTouchEvent(OhosSampleHostState &state, OH_NativeXComponent *component, void *window)
    {
        if (state.window == nullptr || component == nullptr || window == nullptr)
        {
            return false;
        }

        OH_NativeXComponent_TouchEvent touchEvent = {};
        const int32_t result = OH_NativeXComponent_GetTouchEvent(component, window, &touchEvent);
        if (result != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
        {
            logOhosApiFailure("OH_NativeXComponent_GetTouchEvent", result);
            return false;
        }

        GVM::Samples::SampleWindowEvent event;
        event.pointerId = touchEvent.numPoints > 0 ? static_cast<int>(touchEvent.touchPoints[0].id) : 0;
        event.x = touchEvent.x;
        event.y = touchEvent.y;

        switch (touchEvent.type)
        {
        case OH_NATIVEXCOMPONENT_DOWN:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerDown;
            break;
        case OH_NATIVEXCOMPONENT_UP:
        case OH_NATIVEXCOMPONENT_CANCEL:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerUp;
            break;
        case OH_NATIVEXCOMPONENT_MOVE:
            event.type = GVM::Samples::SampleWindowEvent::Type::PointerMove;
            break;
        default:
            return false;
        }

        state.window->pushEvent(event);
        return true;
    }

    /// Handles the OHOS touch callback and forwards supported events to SampleWindowManager.
    void onDispatchTouchEvent(OH_NativeXComponent *component, void *window)
    {
        (void)pushOhosTouchEvent(sampleHostState(), component, window);
    }

    /// Processes sample events and renders one frame when the XComponent surface is active.
    void renderFrameIfReady(OhosSampleHostState &state)
    {
        if (state.application == nullptr || state.renderingFrame)
        {
            return;
        }

        state.renderingFrame = true;
        try
        {
            state.application->processPendingEvents();
            if (state.application->shouldQuit())
            {
                destroyApplication(state);
            }
            else
            {
                state.application->renderFrame();
            }
        }
        catch (const std::exception &error)
        {
            handleCallbackException(state, error);
        }
        catch (...)
        {
            logOhosError("Unknown exception during OHOS sample frame.");
            destroyApplication(state);
        }
        state.renderingFrame = false;
    }

    /// Handles the OHOS per-frame XComponent callback and drives the sample render loop.
    void onFrame(OH_NativeXComponent *component, uint64_t timestamp, uint64_t targetTimestamp)
    {
        (void)component;
        (void)timestamp;
        (void)targetTimestamp;
        renderFrameIfReady(sampleHostState());
    }

    /// Registers the per-frame render callback for the current Native XComponent instance.
    void registerFrameCallback(OhosSampleHostState &state)
    {
        if (state.nativeXComponent == nullptr || state.frameCallbackRegistered)
        {
            return;
        }

        OH_NativeXComponent_RegisterOnFrameCallback(state.nativeXComponent, onFrame);
        state.frameCallbackRegistered = true;
    }

    /// Unregisters the per-frame render callback for the current Native XComponent instance.
    void unregisterFrameCallback(OhosSampleHostState &state)
    {
        if (state.nativeXComponent == nullptr || !state.frameCallbackRegistered)
        {
            return;
        }

        OH_NativeXComponent_UnregisterOnFrameCallback(state.nativeXComponent);
        state.frameCallbackRegistered = false;
    }

    /// Registers surface and touch callbacks for one Native XComponent instance.
    void registerXComponentCallbacks(OH_NativeXComponent *nativeXComponent)
    {
        static OH_NativeXComponent_Callback surfaceCallbacks = {
            .OnSurfaceCreated = onSurfaceCreated,
            .OnSurfaceChanged = onSurfaceChanged,
            .OnSurfaceDestroyed = onSurfaceDestroyed,
            .DispatchTouchEvent = onDispatchTouchEvent,
        };

        const int32_t callbackResult = OH_NativeXComponent_RegisterCallback(nativeXComponent, &surfaceCallbacks);
        if (callbackResult != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
        {
            logOhosApiFailure("OH_NativeXComponent_RegisterCallback", callbackResult);
        }
    }

    /// Handles the ArkTS request to start the XComponent frame callback.
    napi_value napiRegisterFrameCallback(napi_env env, napi_callback_info info)
    {
        (void)info;
        registerFrameCallback(sampleHostState());

        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    /// Handles the ArkTS request to stop the XComponent frame callback.
    napi_value napiUnregisterFrameCallback(napi_env env, napi_callback_info info)
    {
        (void)info;
        unregisterFrameCallback(sampleHostState());
        destroyApplication(sampleHostState());

        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    /// Defines the small ArkTS control surface used by XComponent onLoad and onDestroy.
    void defineNapiExports(napi_env env, napi_value exports)
    {
        napi_property_descriptor descriptors[] = {
            {
                "register",
                nullptr,
                napiRegisterFrameCallback,
                nullptr,
                nullptr,
                nullptr,
                napi_default,
                nullptr,
            },
            {
                "unregister",
                nullptr,
                napiUnregisterFrameCallback,
                nullptr,
                nullptr,
                nullptr,
                napi_default,
                nullptr,
            },
        };

        const napi_status result = napi_define_properties(env, exports, 2, descriptors);
        if (result != napi_ok)
        {
            logOhosError("napi_define_properties failed for XComponent frame controls.");
        }
    }

    /// Extracts the Native XComponent supplied by the ArkTS XComponent loader.
    OH_NativeXComponent *unwrapNativeXComponent(napi_env env, napi_value exports)
    {
        napi_value exportInstance = nullptr;
        if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok)
        {
            logOhosError("napi_get_named_property failed for OH_NATIVE_XCOMPONENT_OBJ.");
            return nullptr;
        }

        OH_NativeXComponent *nativeXComponent = nullptr;
        if (napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&nativeXComponent)) != napi_ok)
        {
            logOhosError("napi_unwrap failed for OH_NATIVE_XCOMPONENT_OBJ.");
            return nullptr;
        }
        return nativeXComponent;
    }

    /// Initializes the OHOS N-API module and binds the current Native XComponent instance.
    napi_value initializeOhosSampleModule(napi_env env, napi_value exports)
    {
        defineNapiExports(env, exports);

        OH_NativeXComponent *nativeXComponent = unwrapNativeXComponent(env, exports);
        if (nativeXComponent != nullptr)
        {
            sampleHostState().nativeXComponent = nativeXComponent;
            registerXComponentCallbacks(nativeXComponent);
        }
        return exports;
    }

    static napi_module GvmOhosSampleModule = {
        .nm_version = 1,
        .nm_flags = 0,
        .nm_filename = nullptr,
        .nm_register_func = initializeOhosSampleModule,
        .nm_modname = GVM_OHOS_NAPI_MODULE_NAME,
        .nm_priv = nullptr,
        .reserved = {nullptr},
    };
} // namespace

/// Registers the OHOS N-API module used by ArkTS XComponent library loading.
extern "C" __attribute__((constructor)) void RegisterGvmOhosSampleModule()
{
    napi_module_register(&GvmOhosSampleModule);
}

#endif // defined(__OHOS__)
