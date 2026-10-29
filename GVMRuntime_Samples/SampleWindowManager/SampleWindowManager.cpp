#include "SampleWindowManager.hpp"

#if !defined(__ANDROID__) && !defined(__OHOS__)
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(__APPLE__)
#include <SDL_metal.h>
#else
#include <SDL_vulkan.h>
#endif
#endif

#include <imgui.h>

#include <EASTL/algorithm.h>
#include <EASTL/deque.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

#include <stdexcept>

namespace GVM::Samples
{
    struct SampleWindowManager::Impl
    {
        SampleWindowCreateInfo createInfo = {};
        GVM::RHI::NativeSurfaceDescriptor nativeSurface = {};
        eastl::deque<SampleWindowEvent> queuedEvents;
        bool ownsSdlVideo = false;
#if !defined(__ANDROID__) && !defined(__OHOS__)
        SDL_Window *window = nullptr;
#if defined(__APPLE__)
        SDL_MetalView metalView = nullptr;
#endif
#endif
    };

#if !defined(__ANDROID__) && !defined(__OHOS__)
    /// Converts the sample window creation request into SDL window flags.
    static uint32_t buildSdlWindowFlags(const SampleWindowCreateInfo &createInfo)
    {
        uint32_t flags = SDL_WINDOW_SHOWN;
        if (createInfo.resizable)
        {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (createInfo.highDpi)
        {
            flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        }
#if defined(__APPLE__)
        flags |= SDL_WINDOW_METAL;
#else
        flags |= SDL_WINDOW_VULKAN;
#endif
        return flags;
    }

    /// Throws a runtime error carrying the latest SDL error string.
    [[noreturn]]
    static void throwSdlError(const char *apiName)
    {
        const eastl::string message = eastl::string(apiName) + " failed: " + SDL_GetError();
        throw std::runtime_error(message.c_str());
    }

    /// Returns the SDL key code stored in a keyboard event.
    static int toSampleKey(const SDL_KeyboardEvent &event)
    {
        return static_cast<int>(event.keysym.sym);
    }
#endif

    /// Returns the default typed surface kind for one SampleWindowManager backend.
    static GVM::RHI::NativeSurfaceKind defaultNativeSurfaceKind(SampleWindowBackend backend)
    {
        switch (backend)
        {
        case SampleWindowBackend::AndroidNative:
            return GVM::RHI::NativeSurfaceKind::AndroidWindow;
        case SampleWindowBackend::OhosXComponent:
            return GVM::RHI::NativeSurfaceKind::OhosWindow;
        case SampleWindowBackend::DesktopSdl:
        default:
            return GVM::RHI::NativeSurfaceKind::Undefined;
        }
    }

    /// Applies backend defaults and a conservative extent hint to a wrapped native surface.
    static GVM::RHI::NativeSurfaceDescriptor normalizeNativeSurface(
        SampleWindowBackend backend,
        const GVM::RHI::NativeSurfaceDescriptor &nativeSurface,
        uint32_t fallbackWidth,
        uint32_t fallbackHeight)
    {
        GVM::RHI::NativeSurfaceDescriptor normalized = nativeSurface;
        if (normalized.kind == GVM::RHI::NativeSurfaceKind::Undefined && normalized.surface != nullptr)
        {
            normalized.kind = defaultNativeSurfaceKind(backend);
        }
        if (normalized.hasExtentHint == GVM::RHI::False)
        {
            normalized.extentHint = {eastl::max(fallbackWidth, 1u), eastl::max(fallbackHeight, 1u), 1u};
            normalized.hasExtentHint = GVM::RHI::True;
        }
        return normalized;
    }

    /// Updates cached window dimensions from a surface extent hint.
    static void applyExtentHintToCreateInfo(
        SampleWindowCreateInfo &createInfo,
        const GVM::RHI::NativeSurfaceDescriptor &nativeSurface)
    {
        if (nativeSurface.hasExtentHint == GVM::RHI::True)
        {
            createInfo.width = eastl::max(nativeSurface.extentHint.width, 1u);
            createInfo.height = eastl::max(nativeSurface.extentHint.height, 1u);
        }
    }

    /// Maps a sample key code to the nearest Dear ImGui key identifier.
    static ImGuiKey toImGuiKey(int key)
    {
#if !defined(__ANDROID__) && !defined(__OHOS__)
        switch (key)
        {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_a: return ImGuiKey_A;
        case SDLK_c: return ImGuiKey_C;
        case SDLK_v: return ImGuiKey_V;
        case SDLK_x: return ImGuiKey_X;
        case SDLK_y: return ImGuiKey_Y;
        case SDLK_z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
        }
#else
        (void)key;
        return ImGuiKey_None;
#endif
    }

    SampleWindowManager::SampleWindowManager(const SampleWindowCreateInfo &createInfo)
        : mImpl(eastl::make_unique<Impl>())
    {
        mImpl->createInfo = createInfo;
        mImpl->nativeSurface = normalizeNativeSurface(
            createInfo.backend,
            createInfo.nativeSurface,
            createInfo.width,
            createInfo.height);
        applyExtentHintToCreateInfo(mImpl->createInfo, mImpl->nativeSurface);

        if (createInfo.backend != SampleWindowBackend::DesktopSdl)
        {
            return;
        }

#if defined(__ANDROID__) || defined(__OHOS__)
        throw std::runtime_error("SampleWindowManager desktop SDL backend is not available on this platform.");
#else
        mImpl->ownsSdlVideo = (SDL_WasInit(SDL_INIT_VIDEO) == 0);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            throwSdlError("SDL_InitSubSystem");
        }

        const uint32_t flags = buildSdlWindowFlags(createInfo);
        mImpl->window = SDL_CreateWindow(
            createInfo.title != nullptr ? createInfo.title : "GVM Sample",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            static_cast<int>(eastl::max(createInfo.width, 1u)),
            static_cast<int>(eastl::max(createInfo.height, 1u)),
            flags);
        if (mImpl->window == nullptr)
        {
            throwSdlError("SDL_CreateWindow");
        }

#if defined(__APPLE__)
        mImpl->metalView = SDL_Metal_CreateView(mImpl->window);
        if (mImpl->metalView == nullptr)
        {
            throwSdlError("SDL_Metal_CreateView");
        }
#endif
#endif
    }

    SampleWindowManager::~SampleWindowManager()
    {
#if !defined(__ANDROID__) && !defined(__OHOS__)
#if defined(__APPLE__)
        if (mImpl->metalView != nullptr)
        {
            SDL_Metal_DestroyView(mImpl->metalView);
            mImpl->metalView = nullptr;
        }
#endif
        if (mImpl->window != nullptr)
        {
            SDL_DestroyWindow(mImpl->window);
            mImpl->window = nullptr;
        }
        if (mImpl->ownsSdlVideo)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        }
#endif
    }

    bool SampleWindowManager::pollEvent(SampleWindowEvent &event)
    {
        if (!mImpl->queuedEvents.empty())
        {
            event = mImpl->queuedEvents.front();
            mImpl->queuedEvents.pop_front();
            return true;
        }

#if defined(__ANDROID__) || defined(__OHOS__)
        (void)event;
        return false;
#else
        SDL_Event sdlEvent = {};
        while (SDL_PollEvent(&sdlEvent) != 0)
        {
            event = {};
            switch (sdlEvent.type)
            {
            case SDL_QUIT:
                event.type = SampleWindowEvent::Type::Quit;
                return true;
            case SDL_WINDOWEVENT:
                if (sdlEvent.window.event == SDL_WINDOWEVENT_CLOSE)
                {
                    event.type = SampleWindowEvent::Type::Quit;
                    return true;
                }
                if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    sdlEvent.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    event.type = SampleWindowEvent::Type::Resized;
                    event.width = static_cast<uint32_t>(eastl::max(sdlEvent.window.data1, 1));
                    event.height = static_cast<uint32_t>(eastl::max(sdlEvent.window.data2, 1));
                    return true;
                }
                break;
            case SDL_KEYDOWN:
                event.type = SampleWindowEvent::Type::KeyDown;
                event.key = toSampleKey(sdlEvent.key);
                return true;
            case SDL_KEYUP:
                event.type = SampleWindowEvent::Type::KeyUp;
                event.key = toSampleKey(sdlEvent.key);
                return true;
            case SDL_MOUSEBUTTONDOWN:
                event.type = SampleWindowEvent::Type::PointerDown;
                event.pointerId = static_cast<int>(sdlEvent.button.button);
                event.x = static_cast<float>(sdlEvent.button.x);
                event.y = static_cast<float>(sdlEvent.button.y);
                return true;
            case SDL_MOUSEBUTTONUP:
                event.type = SampleWindowEvent::Type::PointerUp;
                event.pointerId = static_cast<int>(sdlEvent.button.button);
                event.x = static_cast<float>(sdlEvent.button.x);
                event.y = static_cast<float>(sdlEvent.button.y);
                return true;
            case SDL_MOUSEMOTION:
                event.type = SampleWindowEvent::Type::PointerMove;
                event.x = static_cast<float>(sdlEvent.motion.x);
                event.y = static_cast<float>(sdlEvent.motion.y);
                return true;
            default:
                break;
            }
        }
        return false;
#endif
    }

    void SampleWindowManager::setTitle(const char *title)
    {
#if !defined(__ANDROID__) && !defined(__OHOS__)
        if (mImpl->window != nullptr)
        {
            SDL_SetWindowTitle(mImpl->window, title != nullptr ? title : "");
        }
#else
        (void)title;
#endif
    }

    void SampleWindowManager::getDrawableSize(uint32_t &width, uint32_t &height) const
    {
        width = eastl::max(mImpl->createInfo.width, 1u);
        height = eastl::max(mImpl->createInfo.height, 1u);
        if (mImpl->createInfo.backend != SampleWindowBackend::DesktopSdl &&
            mImpl->nativeSurface.hasExtentHint == GVM::RHI::True)
        {
            width = eastl::max(mImpl->nativeSurface.extentHint.width, 1u);
            height = eastl::max(mImpl->nativeSurface.extentHint.height, 1u);
            return;
        }
#if !defined(__ANDROID__) && !defined(__OHOS__)
        if (mImpl->window == nullptr)
        {
            return;
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
#if defined(__APPLE__)
        SDL_Metal_GetDrawableSize(mImpl->window, &drawableWidth, &drawableHeight);
#else
        SDL_Vulkan_GetDrawableSize(mImpl->window, &drawableWidth, &drawableHeight);
#endif
        width = static_cast<uint32_t>(eastl::max(drawableWidth, 1));
        height = static_cast<uint32_t>(eastl::max(drawableHeight, 1));
#endif
    }

    void SampleWindowManager::getWindowSize(uint32_t &width, uint32_t &height) const
    {
        width = eastl::max(mImpl->createInfo.width, 1u);
        height = eastl::max(mImpl->createInfo.height, 1u);
#if !defined(__ANDROID__) && !defined(__OHOS__)
        if (mImpl->createInfo.backend != SampleWindowBackend::DesktopSdl || mImpl->window == nullptr)
        {
            return;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(mImpl->window, &windowWidth, &windowHeight);
        width = static_cast<uint32_t>(eastl::max(windowWidth, 1));
        height = static_cast<uint32_t>(eastl::max(windowHeight, 1));
#endif
    }

    bool SampleWindowManager::isKeyDown(int key) const
    {
#if !defined(__ANDROID__) && !defined(__OHOS__)
        if (mImpl->createInfo.backend != SampleWindowBackend::DesktopSdl || mImpl->window == nullptr)
        {
            return false;
        }

        SDL_PumpEvents();
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(key));
        if (scancode == SDL_SCANCODE_UNKNOWN)
        {
            return false;
        }

        const uint8_t *keyboardState = SDL_GetKeyboardState(nullptr);
        return keyboardState != nullptr && keyboardState[scancode] != 0;
#else
        (void)key;
        return false;
#endif
    }

    GVM::RHI::NativeSurfaceDescriptor SampleWindowManager::getNativeSurface() const
    {
        GVM::RHI::NativeSurfaceDescriptor surface = mImpl->nativeSurface;
        uint32_t drawableWidth = 1u;
        uint32_t drawableHeight = 1u;
        getDrawableSize(drawableWidth, drawableHeight);
        surface.extentHint = {drawableWidth, drawableHeight, 1u};
        surface.hasExtentHint = GVM::RHI::True;

#if !defined(__ANDROID__) && !defined(__OHOS__)
        if (mImpl->window == nullptr)
        {
            return surface;
        }

#if defined(__APPLE__)
        surface.kind = GVM::RHI::NativeSurfaceKind::MetalLayer;
        surface.surface = SDL_Metal_GetLayer(mImpl->metalView);
        surface.displayOrInstance = nullptr;
#elif defined(_WIN32)
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(mImpl->window, &info) == SDL_TRUE)
        {
            surface.kind = GVM::RHI::NativeSurfaceKind::Win32Window;
            surface.surface = info.info.win.window;
            surface.displayOrInstance = info.info.win.hinstance;
        }
#elif defined(__linux__)
        SDL_SysWMinfo info = {};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(mImpl->window, &info) == SDL_TRUE)
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
#endif
        return surface;
    }

    bool SampleWindowManager::isSurfaceAvailable() const
    {
        const GVM::RHI::NativeSurfaceDescriptor surface = getNativeSurface();
        return surface.kind != GVM::RHI::NativeSurfaceKind::Undefined && surface.surface != nullptr;
    }

    void SampleWindowManager::updateNativeSurface(const GVM::RHI::NativeSurfaceDescriptor &nativeSurface)
    {
        mImpl->nativeSurface = normalizeNativeSurface(
            mImpl->createInfo.backend,
            nativeSurface,
            mImpl->createInfo.width,
            mImpl->createInfo.height);
        applyExtentHintToCreateInfo(mImpl->createInfo, mImpl->nativeSurface);
    }

    void SampleWindowManager::clearNativeSurface()
    {
        mImpl->nativeSurface.surface = nullptr;
        mImpl->nativeSurface.displayOrInstance = nullptr;
        mImpl->nativeSurface.kind = defaultNativeSurfaceKind(mImpl->createInfo.backend);
    }

    void SampleWindowManager::pushEvent(const SampleWindowEvent &event)
    {
        if (event.type == SampleWindowEvent::Type::Resized)
        {
            mImpl->createInfo.width = eastl::max(event.width, 1u);
            mImpl->createInfo.height = eastl::max(event.height, 1u);
            mImpl->nativeSurface.extentHint = {mImpl->createInfo.width, mImpl->createInfo.height, 1u};
            mImpl->nativeSurface.hasExtentHint = GVM::RHI::True;
        }
        mImpl->queuedEvents.push_back(event);
    }

    void SampleWindowManager::beginImGuiFrame(float deltaSeconds) const
    {
        ImGuiIO &io = ImGui::GetIO();
        uint32_t windowWidth = 1u;
        uint32_t windowHeight = 1u;
        uint32_t drawableWidth = 1u;
        uint32_t drawableHeight = 1u;
        getWindowSize(windowWidth, windowHeight);
        getDrawableSize(drawableWidth, drawableHeight);
        io.DisplaySize = ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        io.DisplayFramebufferScale = ImVec2(
            static_cast<float>(drawableWidth) / static_cast<float>(eastl::max(windowWidth, 1u)),
            static_cast<float>(drawableHeight) / static_cast<float>(eastl::max(windowHeight, 1u)));
        io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : (1.0f / 60.0f);
    }

    void SampleWindowManager::handleImGuiEvent(const SampleWindowEvent &event) const
    {
        ImGuiIO &io = ImGui::GetIO();
        switch (event.type)
        {
        case SampleWindowEvent::Type::KeyDown:
        case SampleWindowEvent::Type::KeyUp:
        {
            const ImGuiKey key = toImGuiKey(event.key);
            if (key != ImGuiKey_None)
            {
                io.AddKeyEvent(key, event.type == SampleWindowEvent::Type::KeyDown);
            }
            break;
        }
        case SampleWindowEvent::Type::PointerDown:
        case SampleWindowEvent::Type::PointerUp:
        {
            const int button = eastl::max(event.pointerId - 1, 0);
            if (button < 5)
            {
                io.AddMouseButtonEvent(button, event.type == SampleWindowEvent::Type::PointerDown);
            }
            io.AddMousePosEvent(event.x, event.y);
            break;
        }
        case SampleWindowEvent::Type::PointerMove:
            io.AddMousePosEvent(event.x, event.y);
            break;
        default:
            break;
        }
    }
} // namespace GVM::Samples
