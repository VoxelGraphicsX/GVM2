#pragma once

#include <GVMCore/Private/GQueue.hpp>
#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Samples
{
    /// Adapts a prepared render pass descriptor so raw RHI samples can submit through QueueProxy and receive the diagnostics overlay.
    struct SampleFramebuffer
    {
        GVM::RHI::RenderPassDescriptor descriptor = {};

        /// Returns the render pass descriptor captured for the current swapchain texture.
        GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const
        {
            return descriptor;
        }
    };
} // namespace GVM::RHI::Samples
