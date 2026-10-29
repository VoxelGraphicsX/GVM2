#pragma once

#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>

#include <memory>
#include <string>
#include <vector>

namespace clang
{
    class ASTContext;
} // namespace clang

namespace UGLC::CodeGen
{
    // Describes one backend-specific shader prelude/global symbol that the
    // generated single-header output can prepend before embedded shader source.
    struct EmbeddedShaderPreludeDefinition
    {
        std::string backendName;
        std::string variableName;
        std::string sourceText;
    };

    // Couples a backend identity with the concrete emitter instance currently
    // selected to generate shader text for the host wrapper.
    struct ShaderEmitterInstance
    {
        std::string backendName;
        std::string preludeVariableName;
        std::unique_ptr<IShaderSourceEmitter> emitter;
    };

    std::vector<EmbeddedShaderPreludeDefinition> collectRegisteredShaderBackendPreludes();
    std::unique_ptr<IShaderSourceEmitter> createShaderEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context);
    /** Creates a backend emitter while allowing shader source-pipeline options to override companion backends. */
    std::unique_ptr<IShaderSourceEmitter> createShaderEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context, const ShaderSourcePipelineOptions &options);
    /** Creates an optional backend binary emitter for pipelines that can bypass source-to-SPIR-V compilation. */
    std::unique_ptr<IShaderBinaryEmitter> createShaderBinaryEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context, const ShaderSourcePipelineOptions &options);
    ShaderEmitterInstance createPrimaryShaderEmitter(clang::ASTContext *context, const ShaderSourcePipelineOptions &options = {});
    const ShaderBackendCapabilities &getPrimaryShaderBackendCapabilities();
} // namespace UGLC::CodeGen
