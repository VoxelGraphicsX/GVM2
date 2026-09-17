#pragma once

#include <CodeGen/ShaderSourceEmitter.hpp>

#include <memory>
#include <vector>

namespace clang
{
    class ASTContext;
}

namespace UGLC::CodeGen::ShaderEmitter
{
    struct PreparedShaderTranslationUnit;

    /** Emits direct SPIR-V binaries from the UGLIR pipeline for artifact generation. */
    class UGLIRShaderBinaryEmitter final : public UGLC::CodeGen::IShaderBinaryEmitter
    {
    public:
        /** Creates an emitter that consumes prepared modules and optionally writes SPIR-V debug artifacts. */
        UGLIRShaderBinaryEmitter(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink, bool optimizeSPIRV = true);

        /** Generates a compute-stage direct SPIR-V payload from the UGLIR module that matches the shader class. */
        UGLC::CodeGen::CompiledShaderBinary generateShaderBinary(const std::vector<const clang::Decl *> &shaderDefs,
                                                                 const UGLC::CodeGen::BindGroupInfoMap &bindGroupInfoMap,
                                                                 const clang::CXXRecordDecl *shaderClassDecl,
                                                                 const clang::FunctionDecl *entryFunction = nullptr,
                                                                 const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) override;

    private:
        class Impl;
        std::shared_ptr<Impl> mImpl;
    };
} // namespace UGLC::CodeGen::ShaderEmitter
