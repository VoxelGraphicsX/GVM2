#include "UGLIRShaderModuleProvider.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLIR/UGLIRDump.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/DeclCXX.h>

#include <stdexcept>
#include <utility>

namespace UGLC::CodeGen::ShaderEmitter
{
    UGLIRShaderModuleProvider::UGLIRShaderModuleProvider(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink)
        : mShaders(shaders)
        , mDebugOutputSink(debugOutputSink)
    {
    }

    const PreparedShader &UGLIRShaderModuleProvider::requireShaderForEntry(const clang::CXXRecordDecl &shaderClassDecl,
                                                                                       const std::string &entryName,
                                                                                       const std::string &diagnosticPrefix)
    {
        const std::string recordName = UGLIR::makeUGLIRRecordSymbolName(shaderClassDecl);
        const std::string qualifiedName = entryName == UGLC::CodeGen::mUGLComputeShaderFunctionName ? recordName : recordName + "." + entryName;
        const PreparedShader *match = nullptr;
        for (const auto &shader : mShaders.shaders)
        {
            if (shader.module.name == qualifiedName)
            {
                if (match != nullptr)
                    throw std::runtime_error(formatClangStyleDiagnostic(&shaderClassDecl, diagnosticPrefix + "ambiguous prepared shader identity: " + qualifiedName));
                match = &shader;
            }
        }
        if (match != nullptr)
            return *match;

        throw std::runtime_error(UGLC::CodeGen::formatClangStyleDiagnostic(&shaderClassDecl,
                                                                            diagnosticPrefix + "no lowered UGLIR module matched shader class \""
                                                                                + qualifiedName
                                                                                + "\" entry \"" + entryName + "\"."));
    }

    void UGLIRShaderModuleProvider::storeDebugOutputOnce(const std::string &relativePath, std::string content)
    {
        if (mDebugOutputSink == nullptr)
        {
            return;
        }

        mDebugOutputSink->storeShaderDebugOutput(relativePath, std::move(content));
    }
} // namespace UGLC::CodeGen::ShaderEmitter
