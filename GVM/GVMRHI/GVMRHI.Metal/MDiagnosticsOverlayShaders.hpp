#pragma once

#include <GVMRHI/Private/RHIDiagnosticsOverlay.hpp>

namespace GVM::RHI::Metal
{
    /// Returns the Metal-private shader provider used by the RHI diagnostics overlay queue wrapper.
    GVM::RHI::Private::DiagnosticsOverlayShaderProgramProvider createMetalDiagnosticsOverlayShaderProvider();
}
