#include "Test04CubeApp.hpp"

#include <imgui.h>
#include <imgui_impl_GVM.h>

#include "UGLBin/generate_result.hpp"

namespace GVM::Samples
{
    /// Stores Test04-specific renderer and interactive camera input state.
    struct Test04CubeApp::Impl
    {
        MyRenderer renderer;
        bool leftMouseDown = false;
        bool hasPointerPosition = false;
        int pointerX = 0;
        int pointerY = 0;
    };

    Test04CubeApp::Test04CubeApp(
        SampleWindowManager &window,
        GVM::RHI::GraphicsBackend preferredBackend)
        : SampleApplication(window, SampleApplicationCreateInfo{
                                        .sampleName = "GVM Test04 Cube",
                                        .preferredGraphicsBackend = preferredBackend,
                                    })
        , mImpl(eastl::make_unique<Impl>())
    {
        mImpl->renderer = makeMyRenderer();
        mImpl->renderer->init(device(), swapchain());
    }

    Test04CubeApp::~Test04CubeApp()
    {
        mImpl = nullptr;
    }

    void Test04CubeApp::onWindowEvent(const SampleWindowEvent &event)
    {
        if (event.type == SampleWindowEvent::Type::PointerDown && event.pointerId == 1)
        {
            mImpl->leftMouseDown = true;
            mImpl->pointerX = static_cast<int>(event.x);
            mImpl->pointerY = static_cast<int>(event.y);
            mImpl->hasPointerPosition = true;
        }
        else if (event.type == SampleWindowEvent::Type::PointerUp && event.pointerId == 1)
        {
            mImpl->leftMouseDown = false;
        }
        else if (event.type == SampleWindowEvent::Type::PointerMove)
        {
            mImpl->pointerX = static_cast<int>(event.x);
            mImpl->pointerY = static_cast<int>(event.y);
            mImpl->hasPointerPosition = true;
        }
    }

    void Test04CubeApp::updateFrame(const SampleFrameContext &context)
    {
        int cameraMoveMask = 0;
        if (window().isKeyDown('w'))
        {
            cameraMoveMask |= 1;
        }
        if (window().isKeyDown('s'))
        {
            cameraMoveMask |= 2;
        }
        if (window().isKeyDown('a'))
        {
            cameraMoveMask |= 4;
        }
        if (window().isKeyDown('d'))
        {
            cameraMoveMask |= 8;
        }

        mImpl->renderer->setMouseLeftClick(mImpl->leftMouseDown ? 1 : 0);
        if (mImpl->hasPointerPosition)
        {
            mImpl->renderer->setMouseMoveX(mImpl->pointerX);
            mImpl->renderer->setMouseMoveY(mImpl->pointerY);
        }
        mImpl->renderer->setCameraMove(cameraMoveMask);
        mImpl->renderer->setDurationTicks(static_cast<double>(context.frameTimeMs));
    }

    void Test04CubeApp::renderSampleFrame()
    {
        mImpl->renderer->render();
    }

    /// Creates the Test04 Cube sample application bound to the supplied SampleWindowManager.
    eastl::unique_ptr<ISampleApplication> createSampleApplication(SampleWindowManager &window)
    {
        return eastl::make_unique<Test04CubeApp>(window);
    }
} // namespace GVM::Samples
