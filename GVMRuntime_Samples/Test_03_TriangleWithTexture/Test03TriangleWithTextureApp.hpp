#pragma once

#include "SampleApplication.hpp"

#include <EASTL/unique_ptr.h>

namespace GVM::Samples
{
    /// Runs the Test03 textured triangle renderer through the shared sample window and RHI lifecycle.
    class Test03TriangleWithTextureApp final : public SampleApplication
    {
    public:
        /// Creates the textured triangle renderer using the SampleApplication device and swapchain.
        explicit Test03TriangleWithTextureApp(SampleWindowManager &window);

        /// Releases the renderer before the shared sample RHI objects are destroyed.
        ~Test03TriangleWithTextureApp() override;

    private:
        /// Renders one textured triangle frame and the finalized common ImGui overlay.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
