#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/MSLEmitter/UGLIRToMSLEmitter.hpp>
#include <llvm/ADT/StringMap.h>
#include <array>
#include <cstdint>

namespace clang
{
    class ASTContext;
    class Sema;
    class SourceManager;
}
namespace UGLC::CodeGen { class IShaderDebugOutputSink; }

namespace UGLC::CodeGen::ShaderEmitter
{
    /** Owns a shader module and its Metal mapping derived from the same logical interface. */
    struct PreparedShader
    {
        UGLIR::Module module;
        MSLEmitter::MSLResourceLayout metalLayout;
    };

    /** Owns verified shader modules and input digests independently of Clang AST lifetime. */
    struct PreparedShaderTranslationUnit
    {
        std::vector<PreparedShader> shaders;
        llvm::StringMap<std::array<uint8_t, 32>> inputDigests;
    };

    /** Instantiates concrete shader entries and exports verified, AST-independent modules. */
    PreparedShaderTranslationUnit prepareShaderTranslationUnit(clang::ASTContext &context, clang::Sema &sema);

    /** Rejects input changes between isolated shader analysis and host code generation. */
    void verifyShaderFrontendInputs(const PreparedShaderTranslationUnit &shaders, const clang::SourceManager &sources);

    /** Writes each prepared module's debug representation once before backend emission. */
    void storePreparedShaderDebugOutputs(const PreparedShaderTranslationUnit &shaders, IShaderDebugOutputSink &sink);
}
