#pragma once

#include <CodeGen/ShaderCompilerService.hpp>

#include <map>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clang
{
    class ClassTemplateSpecializationDecl;
    class CXXRecordDecl;
    class Decl;
    class FunctionDecl;
} // namespace clang

namespace UGLC::CodeGen
{
    namespace ShaderEmitter { struct PreparedShaderTranslationUnit; }

    struct ShaderBindGroupInfo;
    using BindGroupInfoMap = std::map<int, ShaderBindGroupInfo>;

    /** Selects which shader source pipeline should provide the primary backend source for generated artifacts. */
    enum class ShaderSourcePipelineKind
    {
        LegacyAST,
        UGLIR,
    };

    /** Receives optional backend debug artifacts emitted by non-default shader source pipelines. */
    class IShaderDebugOutputSink
    {
    public:
        virtual ~IShaderDebugOutputSink() = default;

        /** Stores one debug artifact at a path relative to the UGLC output directory. */
        virtual void storeShaderDebugOutput(const std::string &relativePath, std::string content) = 0;
    };

    /** Configures primary shader source emitter creation with UGLIR as the default. */
    struct ShaderSourcePipelineOptions
    {
        ShaderSourcePipelineKind pipelineKind = ShaderSourcePipelineKind::UGLIR;
        IShaderDebugOutputSink *debugOutputSink = nullptr;
        bool optimizeSPIRV = true;
        const ShaderEmitter::PreparedShaderTranslationUnit *preparedShaders = nullptr;
        uint64_t frontendTimestamp = 0;
    };

    // Backends expose shader text generation through one narrow contract so the
    // host-wrapper layer can stay agnostic to whether the concrete emitter is
    // MSL today or HLSL in a later follow-up.
    class IShaderSourceEmitter
    {
    public:
        virtual ~IShaderSourceEmitter() = default;

        virtual EmittedShaderSource generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                                   const BindGroupInfoMap &bindGroupInfoMap,
                                                   const clang::CXXRecordDecl *shaderClassDecl,
                                                   const clang::FunctionDecl *entryFunction = nullptr,
                                                   const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) = 0;
    };

    /** Emits backend binary payloads for shader artifacts when a source compiler is not the desired SPIR-V producer. */
    class IShaderBinaryEmitter
    {
    public:
        virtual ~IShaderBinaryEmitter() = default;

        /** Generates a compute-stage binary payload for the requested shader class and entry function. */
        virtual CompiledShaderBinary generateShaderBinary(const std::vector<const clang::Decl *> &shaderDefs,
                                                          const BindGroupInfoMap &bindGroupInfoMap,
                                                          const clang::CXXRecordDecl *shaderClassDecl,
                                                          const clang::FunctionDecl *entryFunction = nullptr,
                                                          const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) = 0;
    };
} // namespace UGLC::CodeGen
