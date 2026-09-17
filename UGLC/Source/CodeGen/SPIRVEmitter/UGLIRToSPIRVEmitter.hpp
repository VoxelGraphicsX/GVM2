#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/ShaderCompilerService.hpp>

#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Describes which subsystem produced a direct UGLIR-to-SPIR-V diagnostic. */
    enum class UGLIRToSPIRVDiagnosticKind
    {
        Emitter,
        Validator,
    };

    /** Stores one diagnostic from direct UGLIR-to-SPIR-V emission or validation. */
    struct UGLIRToSPIRVEmissionDiagnostic
    {
        UGLIRToSPIRVDiagnosticKind kind = UGLIRToSPIRVDiagnosticKind::Emitter;
        UGLIR::SourceLocation sourceLocation;
        std::string message;
    };

    /** Stores the binary payload, debug dumps, and diagnostics produced for one UGLIR module. */
    struct UGLIRToSPIRVEmissionResult
    {
        std::optional<UGLC::CodeGen::CompiledShaderBinary> binary;
        std::vector<UGLIRToSPIRVEmissionDiagnostic> diagnostics;
        std::string rawWordDump;
        std::string rawDisassembly;
        std::string wordDump;
        std::string disassembly;
    };

    /** Emits a direct SPIR-V compute binary from the Phase-6 supported UGLIR compute subset. */
    UGLIRToSPIRVEmissionResult emitModuleAsSPIRV(const UGLIR::Module &module, bool optimize = true);

    /** Formats direct UGLIR-to-SPIR-V diagnostics as clang-style UGLC command-line errors. */
    std::string formatUGLIRToSPIRVEmissionDiagnostics(const UGLIRToSPIRVEmissionResult &result);
} // namespace UGLC::CodeGen::SPIRVEmitter
