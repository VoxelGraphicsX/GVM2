#pragma once

#include <GVMRHI/Private/RHIDiagnosticsOverlay.hpp>

namespace GVM::RHI::Vulkan
{
    /// Returns the Vulkan-private shader provider used by the RHI diagnostics overlay queue wrapper.
    GVM::RHI::Private::DiagnosticsOverlayShaderProgramProvider createVulkanDiagnosticsOverlayShaderProvider();
}
