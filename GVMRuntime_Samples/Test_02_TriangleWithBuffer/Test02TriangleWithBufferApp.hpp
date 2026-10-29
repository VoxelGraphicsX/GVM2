#pragma once

#include "SampleApplication.hpp"

#include <EASTL/unique_ptr.h>

namespace GVM::Samples
{
    /// Runs the Test02 buffered triangle renderer through the shared sample window and RHI lifecycle.
    class Test02TriangleWithBufferApp final : public SampleApplication
    {
    public:
        /// Creates the buffered triangle renderer using the SampleApplication device and swapchain.
        explicit Test02TriangleWithBufferApp(SampleWindowManager &window);

        /// Releases the renderer before the shared sample RHI objects are destroyed.
        ~Test02TriangleWithBufferApp() override;

    private:
        /// Renders one buffered triangle frame and the finalized common ImGui overlay.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
