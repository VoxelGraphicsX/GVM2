#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/ConstructorArgumentContract.hpp>
#include <CodeGen/MSLEmitter/UGLIRToMSLEmitter.hpp>

namespace clang
{
    class ASTContext;
    class Sema;
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

    /** Owns verified shader modules and constructor contracts independently of Clang AST lifetime. */
    struct PreparedShaderTranslationUnit
    {
        std::vector<PreparedShader> shaders;
        std::vector<ConstructorArgumentContract> argumentContracts;
    };

    /** Instantiates concrete shader entries and exports verified, AST-independent modules. */
    PreparedShaderTranslationUnit prepareShaderTranslationUnit(clang::ASTContext &context, clang::Sema &sema,
                                                              bool argumentContractsOnly = false);

    /** Writes each prepared module's debug representation once before backend emission. */
    void storePreparedShaderDebugOutputs(const PreparedShaderTranslationUnit &shaders, IShaderDebugOutputSink &sink);
}
