#pragma once

#include "UGLIRCore.hpp"

namespace UGLC::CodeGen::UGLIR
{
    /** Identifies an invalid IR invariant before any backend publishes a shader artifact. */
    struct UGLIRValidationDiagnostic
    {
        SourceLocation sourceLocation;
        std::string message;
    };

    /** Checks structured IR invariants; source-language and target-device validation remain separate. */
    std::vector<UGLIRValidationDiagnostic> validateModule(const Module &module);
}
