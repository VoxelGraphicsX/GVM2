#include "Test02TriangleWithBufferApp.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include "UGLBin/generate_result.hpp"

namespace GVM::Samples
{
    /// Stores Test02-specific renderer objects that are not shared by the sample base class.
    struct Test02TriangleWithBufferApp::Impl
    {
        MyRenderer renderer;
    };

    /// Creates the Test02 renderer after SampleApplication has initialized RHI resources.
    Test02TriangleWithBufferApp::Test02TriangleWithBufferApp(SampleWindowManager &window)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test02 Triangle With Buffer",
                                        .preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
    }

    /// Releases the Test02 renderer before SampleApplication tears down shared RHI resources.
    Test02TriangleWithBufferApp::~Test02TriangleWithBufferApp()
    {
        mImpl = nullptr;
    }

    /// Renders the Test02 frame using the generated renderer implementation.
    void Test02TriangleWithBufferApp::renderSampleFrame()
    {
        mImpl->renderer->render();
    }

    /// Creates the Test02 sample application for the shared desktop or mobile host.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test02TriangleWithBufferApp>(window);
    }
} // namespace GVM::Samples
