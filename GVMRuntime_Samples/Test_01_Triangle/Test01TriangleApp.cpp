#include "Test01TriangleApp.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include "UGLBin/generate_result.hpp"

namespace GVM::Samples
{
    /// Stores Test01-specific renderer objects that are not shared by the sample base class.
    struct Test01TriangleApp::Impl
    {
        MyRenderer renderer;
    };

    Test01TriangleApp::Test01TriangleApp(SampleWindowManager &window)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test01 Triangle",
                                        .preferredGraphicsBackend = GVM::RHI::GraphicsBackend::Vulkan,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
    }

    Test01TriangleApp::~Test01TriangleApp()
    {
        mImpl = nullptr;
    }

    void Test01TriangleApp::renderSampleFrame()
    {
        mImpl->renderer->render();
    }

    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test01TriangleApp>(window);
    }
} // namespace GVM::Samples
