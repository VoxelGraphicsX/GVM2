#pragma once

#include "SampleApplication.hpp"

namespace GVM::Samples
{
    /// Runs the Test01 triangle renderer against a SampleWindowManager-provided native surface.
    class Test01TriangleApp final : public SampleApplication
    {
    public:
        /// Creates the Test01 renderer using the shared sample RHI resources.
        explicit Test01TriangleApp(SampleWindowManager &window);

        /// Releases Test01 renderer resources before the shared sample RHI resources are destroyed.
        ~Test01TriangleApp() override;

    private:
        /// Renders one Test01 triangle frame through the generated UGL renderer.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };
} // namespace GVM::Samples
