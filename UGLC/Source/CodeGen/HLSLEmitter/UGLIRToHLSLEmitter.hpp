#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/ShaderCompilerService.hpp>

#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen::HLSLEmitter
{
    /** Describes one source-bound failure produced while converting UGLIR into HLSL. */
    struct UGLIRToHLSLEmissionDiagnostic
    {
        UGLIR::SourceLocation sourceLocation;
        std::string message;
    };

    /** Stores the generated HLSL source or the diagnostics that explain why emission failed. */
    struct UGLIRToHLSLEmissionResult
    {
        std::optional<UGLC::CodeGen::EmittedShaderSource> source;
        std::vector<UGLIRToHLSLEmissionDiagnostic> diagnostics;
    };

    /** Emits the Phase-5 compute subset of UGLIR as an HLSL shader source payload for DXC/SPIR-V. */
    UGLIRToHLSLEmissionResult emitModuleAsHLSL(const UGLIR::Module &module);

    /** Formats HLSL emission diagnostics using the same clang-style shape as other UGLC diagnostics. */
    std::string formatUGLIRToHLSLEmissionDiagnostics(const UGLIRToHLSLEmissionResult &result);
} // namespace UGLC::CodeGen::HLSLEmitter
