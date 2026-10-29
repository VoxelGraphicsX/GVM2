#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace GVM::Samples
{
    /// Selects the platform window implementation used by SampleWindowManager.
    enum class SampleWindowBackend
    {
        DesktopSdl,
        AndroidNative,
        OhosXComponent,
    };

    /// Describes the window or native surface requested by a sample application.
    struct SampleWindowCreateInfo
    {
        const char *title = "GVM Sample";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool resizable = true;
        bool highDpi = true;
        SampleWindowBackend backend = SampleWindowBackend::DesktopSdl;
        GVM::RHI::GraphicsBackend preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan;
        GVM::RHI::NativeSurfaceDescriptor nativeSurface = {};
    };

    /// Carries one normalized input or window event emitted by SampleWindowManager.
    struct SampleWindowEvent
    {
        enum class Type
        {
            Quit,
            Resized,
            KeyDown,
            KeyUp,
            PointerDown,
            PointerUp,
            PointerMove,
        };

        Type type = Type::Quit;
        int key = 0;
        int pointerId = 0;
        float x = 0.0f;
        float y = 0.0f;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// Owns sample-level window, event, and native-surface state behind an SDL-like C++ interface.
    class SampleWindowManager
    {
    public:
        /// Creates the requested sample window or wraps the supplied platform-native surface.
        explicit SampleWindowManager(const SampleWindowCreateInfo &createInfo);

        /// Releases the owned desktop window resources and leaves wrapped native surfaces untouched.
        ~SampleWindowManager();

        /// Polls one pending window event and returns false when no event is available.
        bool pollEvent(SampleWindowEvent &event);

        /// Updates the desktop window title; native mobile wrappers may ignore this request.
        void setTitle(const char *title);

        /// Writes the current drawable size in physical pixels when the platform exposes it.
        void getDrawableSize(uint32_t &width, uint32_t &height) const;

        /// Writes the current logical window size in platform-independent screen coordinates.
        void getWindowSize(uint32_t &width, uint32_t &height) const;

        /// Returns whether a keyboard key is currently held down according to the platform event backend.
        bool isKeyDown(int key) const;

        /// Returns the typed native surface payload used to create a GVMRHI swapchain.
        GVM::RHI::NativeSurfaceDescriptor getNativeSurface() const;

        /// Reports whether a native surface is currently available for swapchain creation.
        bool isSurfaceAvailable() const;

        /// Replaces the wrapped mobile native surface without taking ownership of the platform object.
        void updateNativeSurface(const GVM::RHI::NativeSurfaceDescriptor &nativeSurface);

        /// Marks the wrapped mobile native surface unavailable until a new surface is supplied.
        void clearNativeSurface();

        /// Appends a platform event that will be returned by pollEvent before backend-native events.
        void pushEvent(const SampleWindowEvent &event);

        /// Updates Dear ImGui display timing and size from the current sample window state.
        void beginImGuiFrame(float deltaSeconds) const;

        /// Forwards one normalized window event into Dear ImGui for windowed samples.
        void handleImGuiEvent(const SampleWindowEvent &event) const;

    private:
        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
