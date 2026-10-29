#include "ShaderBackendRegistry.hpp"

#if UGLC_ENABLE_LEGACY
#include <CodeGen/Legacy/HLSL/HLSLVisitor.hpp>
#endif
#include <stdexcept>
#if UGLC_ENABLE_LEGACY
#include <CodeGen/Legacy/MSL/MSLVisitor.hpp>
#endif

#include <CodeGen/MSL/MSLPrelude.hpp>
#include <CodeGen/ShaderEmitter/UGLIRShaderBinaryEmitter.hpp>
#include <CodeGen/ShaderEmitter/UGLIRShaderSourceEmitter.hpp>

#ifndef UGLC_ENABLE_LEGACY
#define UGLC_ENABLE_LEGACY 0
#endif

namespace UGLC::CodeGen
{
    namespace
    {
        constexpr const char *kMSLBackendName = "MSL";
        constexpr const char *kMSLPreludeVariableName = "__UGL__Global__MSLHeader";
    } // namespace

    std::vector<EmbeddedShaderPreludeDefinition> collectRegisteredShaderBackendPreludes()
    {
        std::vector<EmbeddedShaderPreludeDefinition> preludes;
        // Today the generated single-header output still embeds only the Metal
        // runtime helper prelude, but the registry shape keeps final artifact
        // assembly backend-agnostic so follow-up backends can register their own
        // globals without reopening main.cpp.
        preludes.push_back({
            .backendName = kMSLBackendName,
            .variableName = kMSLPreludeVariableName,
            .sourceText = UGLC::CodeGen::MSL::MakeMSLPreludeSource(),
        });
        return preludes;
    }

    std::unique_ptr<IShaderSourceEmitter> createShaderEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context)
    {
#if UGLC_ENABLE_LEGACY
        switch (backend)
        {
        case ShaderBackendKind::MSL:
            return std::make_unique<MSL::MSLVisitor>(context);
        case ShaderBackendKind::HLSLSPIRV:
            return std::make_unique<HLSL::HLSLVisitor>(context);
        }
        return nullptr;
#else
        (void)backend;
        (void)context;
        throw std::runtime_error("Legacy shader pipeline is not built; enable UGLC_ENABLE_LEGACY.");
#endif
    }

    std::unique_ptr<IShaderSourceEmitter> createShaderEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context, const ShaderSourcePipelineOptions &options)
    {
        if (options.pipelineKind == ShaderSourcePipelineKind::UGLIR)
        {
            if (backend == ShaderBackendKind::HLSLSPIRV) { return nullptr; }
            if (options.preparedShaders == nullptr)
                throw std::runtime_error("UGLIR: prepared shader translation unit is missing.");
            return std::make_unique<ShaderEmitter::UGLIRShaderSourceEmitter>(*options.preparedShaders, options.debugOutputSink);
        }
        return createShaderEmitterForBackend(backend, context);
    }

    std::unique_ptr<IShaderBinaryEmitter> createShaderBinaryEmitterForBackend(ShaderBackendKind backend, clang::ASTContext *context, const ShaderSourcePipelineOptions &options)
    {
        (void)context;
        if (options.pipelineKind == ShaderSourcePipelineKind::UGLIR && backend == ShaderBackendKind::HLSLSPIRV)
        {
            if (options.preparedShaders == nullptr)
                throw std::runtime_error("UGLIR: prepared shader translation unit is missing.");
            return std::make_unique<ShaderEmitter::UGLIRShaderBinaryEmitter>(*options.preparedShaders, options.debugOutputSink, options.optimizeSPIRV);
        }
        return nullptr;
    }

    ShaderEmitterInstance createPrimaryShaderEmitter(clang::ASTContext *context, const ShaderSourcePipelineOptions &options)
    {
        return {
            .backendName = kMSLBackendName,
            .preludeVariableName = kMSLPreludeVariableName,
            .emitter = createShaderEmitterForBackend(ShaderBackendKind::MSL, context, options),
        };
    }

    const ShaderBackendCapabilities &getPrimaryShaderBackendCapabilities()
    {
        // Keep validation and emitter selection aligned through the same
        // registry surface so future backends swap in one place instead of
        // leaving mismatched capability checks behind.
        return getMSLShaderBackendCapabilities();
    }
} // namespace UGLC::CodeGen
