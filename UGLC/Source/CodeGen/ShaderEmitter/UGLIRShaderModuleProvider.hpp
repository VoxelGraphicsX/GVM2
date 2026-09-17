#pragma once

#include "PreparedShaderTranslationUnit.hpp"

#include <CodeGen/ShaderSourceEmitter.hpp>

#include <memory>
#include <string>

namespace clang
{
    class ASTContext;
    class CXXRecordDecl;
} // namespace clang

namespace UGLC::CodeGen::ShaderEmitter
{
    /** Provides shared verified UGLIR modules and unique debug artifact storage for shader source and binary emitters. */
    class UGLIRShaderModuleProvider final
    {
    public:
        /** Creates a provider for one prepared translation unit and an optional debug output sink. */
        UGLIRShaderModuleProvider(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink);

        /** Returns the lowered module for the requested shader class entry or throws with the provided diagnostic prefix. */
        const PreparedShader &requireShaderForEntry(const clang::CXXRecordDecl &shaderClassDecl,
                                                         const std::string &entryName,
                                                         const std::string &diagnosticPrefix);

        /** Stores one debug artifact under a unique relative path and reports duplicate paths as compiler errors. */
        void storeDebugOutputOnce(const std::string &relativePath, std::string content);

    private:
        const PreparedShaderTranslationUnit &mShaders;
        UGLC::CodeGen::IShaderDebugOutputSink *mDebugOutputSink = nullptr;

    };
} // namespace UGLC::CodeGen::ShaderEmitter
