#pragma once

#include <CodeGen/ShaderSourceEmitter.hpp>

#include <memory>

namespace clang
{
    class ASTContext;
}

namespace UGLC::CodeGen::ShaderEmitter
{
    struct PreparedShaderTranslationUnit;

    /** Emits primary shader source from the UGLIR pipeline for artifact generation. */
    class UGLIRShaderSourceEmitter final : public UGLC::CodeGen::IShaderSourceEmitter
    {
    public:
        /** Creates an emitter that consumes prepared modules and optionally writes debug artifacts. */
        UGLIRShaderSourceEmitter(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink);

        /** Creates an emitter that targets one backend while sharing the prepared UGLIR modules. */
        UGLIRShaderSourceEmitter(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink, UGLC::CodeGen::ShaderBackendKind outputBackend);

        /** Generates a compute-stage backend source payload from the UGLIR module that matches the shader class. */
        UGLC::CodeGen::EmittedShaderSource generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                                          const UGLC::CodeGen::BindGroupInfoMap &bindGroupInfoMap,
                                                          const clang::CXXRecordDecl *shaderClassDecl,
                                                          const clang::FunctionDecl *entryFunction = nullptr,
                                                          const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) override;

    private:
        class Impl;
        std::shared_ptr<Impl> mImpl;
    };
} // namespace UGLC::CodeGen::ShaderEmitter
