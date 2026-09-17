#include "UGLIRShaderBinaryEmitter.hpp"

#include "UGLIRShaderModuleProvider.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/SPIRVEmitter/UGLIRToSPIRVEmitter.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace UGLC::CodeGen::ShaderEmitter
{
    namespace
    {
        /** Formats an unlocated direct SPIR-V emitter diagnostic. */
        std::string formatUnlocatedSPIRVEmitterDiagnostic(const std::string &message)
        {
            return UGLC::CodeGen::formatUnlocatedDiagnostic("UGLIR SPIR-V emitter: " + message);
        }

        /** Formats a declaration-bound direct SPIR-V emitter diagnostic. */
        std::string formatSourceBoundSPIRVEmitterDiagnostic(const clang::Decl *decl, const std::string &message)
        {
            return UGLC::CodeGen::formatClangStyleDiagnostic(decl, "UGLIR SPIR-V emitter: " + message);
        }
    } // namespace

    /** Holds per-emitter UGLIR lowering state and direct SPIR-V debug-artifact emission state. */
    class UGLIRShaderBinaryEmitter::Impl
    {
    public:
        /** Creates a binary emitter over prepared modules and optional debug output. */
        Impl(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink, bool optimizeSPIRV)
            : mProvider(shaders, debugOutputSink)
            , mDebugOutputSink(debugOutputSink)
            , mOptimizeSPIRV(optimizeSPIRV)
        {
        }

        /** Generates a direct SPIR-V binary for one shader class entry. */
        UGLC::CodeGen::CompiledShaderBinary generate(const clang::CXXRecordDecl *shaderClassDecl,
                                                     const clang::FunctionDecl *entryFunction,
                                                     const clang::ClassTemplateSpecializationDecl *templateSpecialization)
        {
            if (shaderClassDecl == nullptr)
            {
                throw std::runtime_error(formatUnlocatedSPIRVEmitterDiagnostic("shader class declaration is missing."));
            }
            if (entryFunction == nullptr)
            {
                throw std::runtime_error(formatSourceBoundSPIRVEmitterDiagnostic(shaderClassDecl, "Phase 8 direct SPIR-V emission requires a concrete shader entry function."));
            }
            const clang::CXXRecordDecl *moduleRecordDecl = templateSpecialization != nullptr ? static_cast<const clang::CXXRecordDecl *>(templateSpecialization) : shaderClassDecl;
            UGLIRShaderModuleProvider &provider = mProvider;
            const UGLIR::Module &module = provider.requireShaderForEntry(*moduleRecordDecl, entryFunction->getNameAsString(), "UGLIR SPIR-V emitter: ").module;

            SPIRVEmitter::UGLIRToSPIRVEmissionResult emissionResult = SPIRVEmitter::emitModuleAsSPIRV(module, mOptimizeSPIRV);
            if (!emissionResult.diagnostics.empty() || !emissionResult.binary.has_value())
            {
                throw std::runtime_error(SPIRVEmitter::formatUGLIRToSPIRVEmissionDiagnostics(emissionResult));
            }
            storeSPIRVDebugOutputs(module, emissionResult);
            return *emissionResult.binary;
        }

    private:
        UGLIRShaderModuleProvider mProvider;
        UGLC::CodeGen::IShaderDebugOutputSink *mDebugOutputSink = nullptr;
        bool mOptimizeSPIRV = true;

        /** Stores the actual runtime SPIR-V artifacts and raw writer diagnostics for the emitted module. */
        void storeSPIRVDebugOutputs(const UGLIR::Module &module, const SPIRVEmitter::UGLIRToSPIRVEmissionResult &emissionResult)
        {
            if (mDebugOutputSink == nullptr)
            {
                return;
            }

            const std::string dumpStem = UGLIR::makeUGLIRDebugArtifactStem(module.name);
            if (!emissionResult.rawWordDump.empty())
            {
                mProvider.storeDebugOutputOnce("spv/" + dumpStem + ".raw.spv.txt", emissionResult.rawWordDump);
            }
            if (!emissionResult.rawDisassembly.empty())
            {
                mProvider.storeDebugOutputOnce("spv/" + dumpStem + ".raw.spvasm", emissionResult.rawDisassembly);
            }
            mProvider.storeDebugOutputOnce("spv/" + dumpStem + ".spv.txt", emissionResult.wordDump);
            mProvider.storeDebugOutputOnce("spv/" + dumpStem + ".spvasm", emissionResult.disassembly);
        }
    };

    UGLIRShaderBinaryEmitter::UGLIRShaderBinaryEmitter(const PreparedShaderTranslationUnit &shaders, UGLC::CodeGen::IShaderDebugOutputSink *debugOutputSink, bool optimizeSPIRV)
        : mImpl(std::make_shared<Impl>(shaders, debugOutputSink, optimizeSPIRV))
    {
    }

    UGLC::CodeGen::CompiledShaderBinary UGLIRShaderBinaryEmitter::generateShaderBinary(const std::vector<const clang::Decl *> &shaderDefs,
                                                                                              const UGLC::CodeGen::BindGroupInfoMap &bindGroupInfoMap,
                                                                                              const clang::CXXRecordDecl *shaderClassDecl,
                                                                                              const clang::FunctionDecl *entryFunction,
                                                                                              const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        (void)shaderDefs;
        (void)bindGroupInfoMap;
        return mImpl->generate(shaderClassDecl, entryFunction, templateSpecialization);
    }
} // namespace UGLC::CodeGen::ShaderEmitter
