#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>

#include <string>
#include <vector>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Stores one backend-specific preflight diagnostic emitted before SPIR-V instructions are generated. */
    struct SPIRVPreflightDiagnostic
    {
        UGLIR::SourceLocation sourceLocation;
        std::string message;
    };

    /** Validates that one UGLIR module fits the currently supported direct SPIR-V backend ABI subset. */
    std::vector<SPIRVPreflightDiagnostic> runSPIRVBackendPreflight(const UGLIR::Module &module);
} // namespace UGLC::CodeGen::SPIRVEmitter
