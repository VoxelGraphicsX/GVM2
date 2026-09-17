#include "SampleApplication.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

#include <chrono>
#include <stdexcept>

namespace
{
    /// Returns a stable display name for one GVMRHI backend.
    const char *sampleBackendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal: return "Metal";
        case GVM::RHI::GraphicsBackend::Vulkan: return "Vulkan";
        default: return "Undefined";
        }
    }

    /// Returns a conservative render-target format for the ImGui renderer backend.
    GVM::RHI::TextureFormat resolveImGuiRenderTargetFormat(GVM::RHI::Swapchain swapchain)
    {
        if (swapchain == nullptr)
        {
            return GVM::RHI::TextureFormat::BGRA8Unorm;
        }
        const GVM::RHI::TextureFormat preferredFormat = swapchain->getPreferredFormat();
        return preferredFormat != GVM::RHI::TextureFormat::Undefined
            ? preferredFormat
            : GVM::RHI::TextureFormat::BGRA8Unorm;
    }

    /// Initializes Dear ImGui for the active GVM device and swapchain.
    void initializeImGui(GVM::RHI::Device device,
                         GVM::RHI::Swapchain swapchain,
                         GVM::RHI::GraphicsBackend backend,
                         uint32_t framesInFlight)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGui_ImplGVM_InitInfo initInfo;
        initInfo.Device = device;
        initInfo.NumFramesInFlight = framesInFlight;
        initInfo.RenderTargetFormat = resolveImGuiRenderTargetFormat(swapchain);
        initInfo.DepthStencilFormat = GVM::RHI::TextureFormat::Undefined;
        initInfo.BackendType = backend;
        if (!ImGui_ImplGVM_Init(&initInfo))
        {
            ImGui::DestroyContext();
            throw std::runtime_error("ImGui_ImplGVM_Init failed.");
        }
    }

    /// Shuts down Dear ImGui for the current windowed sample.
    void shutdownImGui()
    {
        ImGui_ImplGVM_Shutdown();
        ImGui::DestroyContext();
    }

    /// Starts a new Dear ImGui frame for common sample overlay rendering.
    void beginImGuiFrame()
    {
        ImGui_ImplGVM_NewFrame();
        ImGui::NewFrame();
    }

    /// Finalizes Dear ImGui draw data for the current sample frame.
    void endImGuiFrame()
    {
        ImGui::Render();
    }
} // namespace

namespace GVM::Samples
{
    struct SampleApplication::Impl
    {
        SampleWindowManager &window;
        eastl::string sampleName;
        GVM::RHI::Instance instance;
        GVM::RHI::Device device;
        GVM::RHI::Swapchain swapchain;
        bool quitRequested = false;
        std::chrono::steady_clock::time_point previousFrameTime = std::chrono::steady_clock::now();

        /// Creates all shared RHI objects needed by a windowed sample.
        Impl(SampleWindowManager &sampleWindow, const SampleApplicationCreateInfo &createInfo)
            : window(sampleWindow)
            , sampleName(createInfo.sampleName != nullptr ? createInfo.sampleName : "GVM Sample")
        {
            if (!window.isSurfaceAvailable())
            {
                throw std::runtime_error("SampleWindowManager did not provide a native surface.");
            }

            instance = GVM::RHI::createInstance({
                .preferredBackend = createInfo.preferredGraphicsBackend,
                .diagnosticsOverlay = {
                    .enabled = createInfo.diagnosticsOverlayEnabled
                                   ? GVM::RHI::True
                                   : GVM::RHI::False},
            });
            device = instance->createDevice();
            swapchain = instance->createSwapchain({
                .nativeSurface = window.getNativeSurface(),
            });
            initializeImGui(device, swapchain, instance->getBackend(), createInfo.imguiFramesInFlight);
        }

        /// Releases common RHI objects in reverse creation order.
        ~Impl()
        {
            shutdownImGui();
            swapchain = nullptr;
            device = nullptr;
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
        }
    };

    SampleApplication::SampleApplication(SampleWindowManager &window, const SampleApplicationCreateInfo &createInfo)
        : mImpl(eastl::make_unique<Impl>(window, createInfo))
    {
    }

    SampleApplication::~SampleApplication() = default;

    void SampleApplication::processPendingEvents()
    {
        SampleWindowEvent event;
        while (mImpl->window.pollEvent(event))
        {
            mImpl->window.handleImGuiEvent(event);
            onWindowEvent(event);
            if (event.type == SampleWindowEvent::Type::Quit ||
                (event.type == SampleWindowEvent::Type::KeyDown && event.key == 27))
            {
                mImpl->quitRequested = true;
            }
        }
    }

    void SampleApplication::renderFrame()
    {
        if (!mImpl->window.isSurfaceAvailable())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<float> frameSeconds = now - mImpl->previousFrameTime;
        mImpl->previousFrameTime = now;

        SampleFrameContext context;
        context.backend = mImpl->instance->getBackend();
        context.backendName = sampleBackendName(context.backend);
        context.deltaSeconds = frameSeconds.count();
        context.frameTimeMs = context.deltaSeconds * 1000.0f;
        mImpl->window.getWindowSize(context.windowWidth, context.windowHeight);
        mImpl->window.getDrawableSize(context.drawableWidth, context.drawableHeight);

        mImpl->window.beginImGuiFrame(context.deltaSeconds);
        beginImGuiFrame();
        updateFrame(context);
        drawOverlay(context);
        endImGuiFrame();

        renderSampleFrame();
    }

    bool SampleApplication::shouldQuit() const
    {
        return mImpl->quitRequested;
    }

    SampleWindowManager &SampleApplication::window() const
    {
        return mImpl->window;
    }

    GVM::RHI::Instance SampleApplication::instance() const
    {
        return mImpl->instance;
    }

    GVM::RHI::Device SampleApplication::device() const
    {
        return mImpl->device;
    }

    GVM::RHI::Swapchain SampleApplication::swapchain() const
    {
        return mImpl->swapchain;
    }

    const char *SampleApplication::sampleName() const
    {
        return mImpl->sampleName.c_str();
    }

    void SampleApplication::requestQuit()
    {
        mImpl->quitRequested = true;
    }

    void SampleApplication::onWindowEvent(const SampleWindowEvent &event)
    {
        (void)event;
    }

    void SampleApplication::updateFrame(const SampleFrameContext &context)
    {
        (void)context;
    }

    void SampleApplication::drawOverlay(const SampleFrameContext &context)
    {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("GVM Sample Overlay", nullptr, flags))
        {
            ImGui::TextUnformatted(sampleName());
            ImGui::Text("Backend: %s", context.backendName);
            ImGui::Text("Window: %ux%u", context.windowWidth, context.windowHeight);
            ImGui::Text("Drawable: %ux%u", context.drawableWidth, context.drawableHeight);
            ImGui::Text("Frame: %.2f ms", context.frameTimeMs);
        }
        ImGui::End();
    }
} // namespace GVM::Samples
