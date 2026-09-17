#include "UGLIRShaderSourceEmitter.hpp"

#include "UGLIRShaderModuleProvider.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/HLSLEmitter/UGLIRToHLSLEmitter.hpp>
#include <CodeGen/MSLEmitter/UGLIRToMSLEmitter.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/MSL/MSLPrelude.hpp>
#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace UGLC::CodeGen::ShaderEmitter
{
    namespace
    {
        /** Returns the diagnostic prefix used for one UGLIR backend emitter. */
        std::string getEmitterDiagnosticPrefix(UGLC::CodeGen::ShaderBackendKind backend)
        {
            switch (backend)
            {
            case UGLC::CodeGen::ShaderBackendKind::MSL:
                return "UGLIR MSL emitter: ";
            case UGLC::CodeGen::ShaderBackendKind::HLSLSPIRV:
                return "UGLIR HLSL emitter: ";
            }
            return "UGLIR shader emitter: ";
        }

        /** Returns the phase label used for target-specific UGLIR support diagnostics. */
        std::string getEmitterPhaseLabel(UGLC::CodeGen::ShaderBackendKind backend)
        {
            switch (backend)
            {
            case UGLC::CodeGen::ShaderBackendKind::MSL:
                return "Phase 4";
            case UGLC::CodeGen::ShaderBackendKind::HLSLSPIRV:
                return "Phase 5";
            }
            return "uglir";
        }

        /** Formats an unlocated UGLIR emitter diagnostic. */
        std::string formatUnlocatedEmitterDiagnostic(UGLC::CodeGen::ShaderBackendKind backend, const std::string &message)
        {
            return UGLC::CodeGen::formatUnlocatedDiagnostic(getEmitterDiagnosticPrefix(backend) + message);
        }

        /** Formats a source-bound UGLIR emitter diagnostic. */
        std::string formatSourceBoundEmitterDiagnostic(UGLC::CodeGen::ShaderBackendKind backend, const clang::Decl *decl, const std::string &message)
        {
            return UGLC::CodeGen::formatClangStyleDiagnostic(decl, getEmitterDiagnosticPrefix(backend) + message);
        }

        /** Returns the complete debug shader file text by prepending an explicit prelude when present. */
        std::string makeDebugShaderSource(const std::string &preludeText, const std::string &sourceText)
        {
            std::string debugSource = preludeText;
            if (!debugSource.empty() && debugSource.back() != '\n')
            {
                debugSource += '\n';
            }
            debugSource += sourceText;
            return debugSource;
        }

    } // namespace

    /** Holds per-emitter UGLIR lowering state and debug-artifact emission state. */
    class UGLIRShaderSourceEmitter::Impl
    {
    public:
        /** Creates an emitter over prepared modules and optional debug output. */
        Impl(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink, UGLC::CodeGen::ShaderBackendKind outputBackend)
            : mProvider(shaders, debugOutputSink)
            , mOutputBackend(outputBackend)
        {
        }

        /** Generates the UGLIR-derived backend payload for one shader class entry. */
        UGLC::CodeGen::EmittedShaderSource generate(const clang::CXXRecordDecl *shaderClassDecl,
                                                    const clang::FunctionDecl *entryFunction,
                                                    const clang::ClassTemplateSpecializationDecl *templateSpecialization,
                                                    const UGLC::CodeGen::BindGroupInfoMap &bindGroupInfoMap)
        {
            if (shaderClassDecl == nullptr)
            {
                throw std::runtime_error(formatUnlocatedEmitterDiagnostic(mOutputBackend, "shader class declaration is missing."));
            }
            if (entryFunction == nullptr)
            {
                throw std::runtime_error(formatSourceBoundEmitterDiagnostic(mOutputBackend,
                                                                                       shaderClassDecl,
                                                                                       getEmitterPhaseLabel(mOutputBackend) + " requires a concrete shader entry function."));
            }
            const clang::CXXRecordDecl *moduleRecordDecl = templateSpecialization != nullptr ? static_cast<const clang::CXXRecordDecl *>(templateSpecialization) : shaderClassDecl;
            UGLIRShaderModuleProvider &provider = mProvider;
            const PreparedShader &shader = provider.requireShaderForEntry(*moduleRecordDecl, entryFunction->getNameAsString(), getEmitterDiagnosticPrefix(mOutputBackend));

            (void)bindGroupInfoMap;
            return emitModuleForTargetBackend(shader);
        }

    private:
        UGLIRShaderModuleProvider mProvider;
        UGLC::CodeGen::ShaderBackendKind mOutputBackend = UGLC::CodeGen::ShaderBackendKind::MSL;

        /** Emits the requested backend source from the lowered UGLIR module. */
        UGLC::CodeGen::EmittedShaderSource emitModuleForTargetBackend(const PreparedShader &shader)
        {
            const UGLIR::Module &module = shader.module;
            switch (mOutputBackend)
            {
            case UGLC::CodeGen::ShaderBackendKind::MSL:
            {
                MSLEmitter::UGLIRToMSLEmissionResult emissionResult = MSLEmitter::emitModuleAsMSL(module, shader.metalLayout);
                if (!emissionResult.diagnostics.empty() || !emissionResult.source.has_value())
                {
                    throw std::runtime_error(MSLEmitter::formatUGLIRToMSLEmissionDiagnostics(emissionResult));
                }
                storeMSLDebugOutput(module, *emissionResult.source);
                return *emissionResult.source;
            }
            case UGLC::CodeGen::ShaderBackendKind::HLSLSPIRV:
            {
                // Diagnostic-only path: the artifact registry currently bypasses UGLIR HLSL and uses direct SPIR-V instead.
                HLSLEmitter::UGLIRToHLSLEmissionResult emissionResult = HLSLEmitter::emitModuleAsHLSL(module);
                if (!emissionResult.diagnostics.empty() || !emissionResult.source.has_value())
                {
                    throw std::runtime_error(HLSLEmitter::formatUGLIRToHLSLEmissionDiagnostics(emissionResult));
                }
                storeHLSLDebugOutput(module, *emissionResult.source);
                return *emissionResult.source;
            }
            }

            throw std::runtime_error(formatUnlocatedEmitterDiagnostic(mOutputBackend, "unsupported UGLIR shader backend target."));
        }

        /** Stores the complete prelude-plus-body MSL debug file for the emitted module. */
        void storeMSLDebugOutput(const UGLIR::Module &module, const UGLC::CodeGen::EmittedShaderSource &source)
        {
            const std::string dumpStem = UGLIR::makeUGLIRDebugArtifactStem(module.name);
            mProvider.storeDebugOutputOnce("msl/" + dumpStem + ".msl", makeDebugShaderSource(UGLC::CodeGen::MSL::MakeMSLPreludeSource(), source.sourceText));
        }

        /** Stores the complete prelude-plus-body HLSL debug file for the emitted module. */
        void storeHLSLDebugOutput(const UGLIR::Module &module, const UGLC::CodeGen::EmittedShaderSource &source)
        {
            const std::string dumpStem = UGLIR::makeUGLIRDebugArtifactStem(module.name);
            mProvider.storeDebugOutputOnce("hlsl/" + dumpStem + ".hlsl", makeDebugShaderSource(source.preludeText, source.sourceText));
        }
    };

    UGLIRShaderSourceEmitter::UGLIRShaderSourceEmitter(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink)
        : UGLIRShaderSourceEmitter(shaders, debugOutputSink, UGLC::CodeGen::ShaderBackendKind::MSL)
    {
    }

    UGLIRShaderSourceEmitter::UGLIRShaderSourceEmitter(const PreparedShaderTranslationUnit &shaders,
                                                                     UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink,
                                                                     UGLC::CodeGen::ShaderBackendKind outputBackend)
        : mImpl(std::make_shared<Impl>(shaders, debugOutputSink, outputBackend))
    {
    }

    UGLC::CodeGen::EmittedShaderSource UGLIRShaderSourceEmitter::generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                                                                       const UGLC::CodeGen::BindGroupInfoMap &bindGroupInfoMap,
                                                                                       const clang::CXXRecordDecl *shaderClassDecl,
                                                                                       const clang::FunctionDecl *entryFunction,
                                                                                       const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        (void)shaderDefs;
        return mImpl->generate(shaderClassDecl, entryFunction, templateSpecialization, bindGroupInfoMap);
    }
} // namespace UGLC::CodeGen::ShaderEmitter
