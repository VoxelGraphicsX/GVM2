#pragma once

#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Private
{
    /// Identifies the diagnostics overlay shader stage requested from a backend-private shader provider.
    enum class DiagnosticsOverlayShaderStage
    {
        Vertex,
        Fragment,
    };

    /// Creates one backend-native diagnostics overlay shader module without exposing backend shader details publicly.
    using CreateDiagnosticsOverlayShaderModuleFn = ShaderModule (*)(Device device, DiagnosticsOverlayShaderStage stage);

    /// Provides backend-native shader creation callbacks and entry point names for the private overlay renderer.
    struct DiagnosticsOverlayShaderProgramProvider
    {
        CreateDiagnosticsOverlayShaderModuleFn createShaderModule = nullptr;
        const char *vertexEntryPoint = nullptr;
        const char *fragmentEntryPoint = nullptr;
    };

    /// Wraps a backend queue with diagnostics overlay pass injection when runtime overlay diagnostics are enabled.
    Queue createDiagnosticsOverlayQueue(Device device, Queue innerQueue, DiagnosticsOverlayShaderProgramProvider shaderProvider);
}
