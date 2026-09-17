#pragma once

#include <cstdint>
#include <string>

namespace clang
{
    class CXXRecordDecl;
    class FunctionDecl;
}

namespace UGLC::CodeGen::UGLIR
{
    /** Returns the stable UGLIR symbol name for a concrete C++ record, including class-template specialization arguments when present. */
    std::string makeUGLIRRecordSymbolName(const clang::CXXRecordDecl &recordDecl);

    /** Returns the stable UGLIR symbol name for a concrete C++ function or method, including function-template specialization arguments when present. */
    std::string makeUGLIRFunctionSymbolName(const clang::FunctionDecl &functionDecl);

    /** Returns a stable internal UGLIR function symbol for a lambda lowered inside the requested owner function. */
    std::string makeUGLIRLambdaSymbolName(const std::string &ownerFunctionSymbol, uint32_t ordinal);

    /** Returns a deterministic filesystem-safe artifact stem for a UGLIR module name or function symbol. */
    std::string makeUGLIRDebugArtifactStem(const std::string &moduleName);
} // namespace UGLC::CodeGen::UGLIR
