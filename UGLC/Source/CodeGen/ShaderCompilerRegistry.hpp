#pragma once

#include <CodeGen/ShaderCompilerService.hpp>

#include <memory>

namespace UGLC::CodeGen
{
    // Expose build-time feature switches through one narrow surface so later
    // HLSL/DXC work does not need to inspect CMake definitions throughout the codebase.
    ShaderCompilerFeatureFlags getShaderCompilerFeatureFlags();

    // Returns the binary compiler service that should consume emitted shader text
    // for a given backend. Backends without a binary compiler simply return null.
    std::unique_ptr<IShaderBinaryCompiler> createSpirvCompilerForBackend(ShaderBackendKind backend);
} // namespace UGLC::CodeGen
