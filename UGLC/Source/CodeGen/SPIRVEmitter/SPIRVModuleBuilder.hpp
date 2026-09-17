#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Stores one direct SPIR-V module build diagnostic with the UGLIR source location that caused it. */
    struct SPIRVModuleBuildDiagnostic
    {
        UGLIR::SourceLocation sourceLocation;
        std::string message;
    };

    /** Stores the direct SPIR-V module words or diagnostics produced from one UGLIR module. */
    struct SPIRVModuleBuildResult
    {
        std::vector<uint32_t> words;
        std::vector<SPIRVModuleBuildDiagnostic> diagnostics;
    };

    /** Builds a validated-by-construction SPIR-V compute module from the Phase-6 UGLIR compute subset. */
    SPIRVModuleBuildResult buildSPIRVModuleFromUGLIR(const UGLIR::Module &module);
} // namespace UGLC::CodeGen::SPIRVEmitter
