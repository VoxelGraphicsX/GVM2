#pragma once

#include <CodeGen/DiagnosticError.hpp>
#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/DiagnosticError.hpp>
#include "UGLIRShaderRoots.hpp"

#include <CodeGen/DiagnosticError.hpp>
#include <string>
#include <CodeGen/DiagnosticError.hpp>
#include <vector>

namespace clang
{
    class ASTContext;
}

namespace UGLC::CodeGen::UGLIR
{
    /** Stores one source-level UGLIR lowering diagnostic with a resolved clang-style location when one is available. */
    struct UGLIRLoweringDiagnostic
    {
        std::string filePath;
        unsigned line = 0;
        unsigned column = 0;
        bool hasSourceLocation = false;
        std::string message;
    };

    /** Stores every module and diagnostic produced while lowering one translation unit into UGLIR. */
    struct UGLIRLoweringResult
    {
        std::vector<Module> modules;
        std::vector<UGLIRLoweringDiagnostic> diagnostics;
    };

    /** Lowers shader-reachable compute code in one Clang translation unit into UGLIR modules. */
    UGLIRLoweringResult lowerTranslationUnitToUGLIR(clang::ASTContext &context, llvm::ArrayRef<ShaderClassRoot> roots);

    /** Formats UGLIR lowering diagnostics as clang-style error text suitable for UGLC command-line output. */
    /** Converts the diagnostics into structured records for DiagnosticsEngine reporting. */
    std::vector<UGLC::CodeGen::DiagnosticRecord> collectUGLIRLoweringDiagnosticRecords(const UGLIRLoweringResult &result);
} // namespace UGLC::CodeGen::UGLIR
