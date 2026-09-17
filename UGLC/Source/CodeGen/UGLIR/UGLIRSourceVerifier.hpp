#pragma once

#include "UGLIRShaderRoots.hpp"

#include <string>
#include <vector>

namespace clang
{
    class ASTContext;
    class VarDecl;
}

namespace UGLC::CodeGen::UGLIR
{
    /** Stores one source-level UGLIR verifier diagnostic with a resolved clang-style location when one is available. */
    struct UGLIRVerificationDiagnostic
    {
        std::string filePath;
        unsigned line = 0;
        unsigned column = 0;
        bool hasSourceLocation = false;
        std::string feature;
        std::string message;
    };

    /** Stores the complete result of validating one translation unit for the UGLIR shader pipeline. */
    struct UGLIRVerificationResult
    {
        std::vector<UGLIRVerificationDiagnostic> diagnostics;
    };

    /** Verifies shader-reachable C++ in one Clang translation unit against the UGLIR source restrictions. */
    UGLIRVerificationResult verifyTranslationUnitForUGLIR(clang::ASTContext &context, llvm::ArrayRef<ShaderClassRoot> roots);

    /** Returns true only for immutable shader constants whose initializer is proven compile-time safe by Clang or a narrow UGL value exception. */
    bool isSafeConstantVarDeclForUGLIR(const clang::VarDecl *varDecl, clang::ASTContext &context);

    /** Formats UGLIR verifier diagnostics as clang-style error text suitable for UGLC command-line output. */
    std::string formatUGLIRVerificationDiagnostics(const UGLIRVerificationResult &result);
} // namespace UGLC::CodeGen::UGLIR
