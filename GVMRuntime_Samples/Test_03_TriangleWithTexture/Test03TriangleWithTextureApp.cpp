#include "Test03TriangleWithTextureApp.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include "UGLBin/generate_result.hpp"

namespace GVM::Samples
{
    /// Stores Test03-specific renderer objects that are not shared by the sample base class.
    struct Test03TriangleWithTextureApp::Impl
    {
        MyRenderer renderer;
    };

    /// Creates the Test03 renderer after SampleApplication has initialized RHI resources.
    Test03TriangleWithTextureApp::Test03TriangleWithTextureApp(SampleWindowManager &window)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test03 Triangle With Texture",
                                        .preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
    }

    /// Releases the Test03 renderer before SampleApplication tears down shared RHI resources.
    Test03TriangleWithTextureApp::~Test03TriangleWithTextureApp()
    {
        mImpl = nullptr;
    }

    /// Renders the Test03 frame using the generated renderer implementation.
    void Test03TriangleWithTextureApp::renderSampleFrame()
    {
        mImpl->renderer->render();
    }

    /// Creates the Test03 sample application for the shared desktop or mobile host.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test03TriangleWithTextureApp>(window);
    }
} // namespace GVM::Samples
