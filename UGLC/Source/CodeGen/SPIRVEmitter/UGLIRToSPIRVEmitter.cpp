#include "UGLIRToSPIRVEmitter.hpp"

#include "SPIRVModuleBuilder.hpp"
#include "SPIRVPreflight.hpp"
#include "SPIRVValidation.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/UGLIR/UGLIRVerifier.hpp>
#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <sstream>
#include <utility>

namespace UGLC::CodeGen::SPIRVEmitter
{
    namespace
    {
        /** Maps a UGLIR stage to the public shader binary stage enum. */
        UGLC::CodeGen::ShaderStageKind toShaderStageKind(UGLIR::ShaderStage stage)
        {
            switch (stage)
            {
            case UGLIR::ShaderStage::Vertex:
                return UGLC::CodeGen::ShaderStageKind::Vertex;
            case UGLIR::ShaderStage::Fragment:
                return UGLC::CodeGen::ShaderStageKind::Fragment;
            case UGLIR::ShaderStage::Compute:
                return UGLC::CodeGen::ShaderStageKind::Compute;
            default:
                return UGLC::CodeGen::ShaderStageKind::Compute;
            }
        }

        /** Returns the runtime entry-point name for a UGLIR entry kind. */
        std::string entryPointNameForEntryKind(UGLIR::ShaderEntryKind entryKind)
        {
            switch (entryKind)
            {
            case UGLIR::ShaderEntryKind::Vertex:
                return UGLC::CodeGen::VertexShaderEntryName;
            case UGLIR::ShaderEntryKind::Fragment:
            case UGLIR::ShaderEntryKind::PixelLocal:
                return UGLC::CodeGen::FragmentShaderEntryName;
            case UGLIR::ShaderEntryKind::Compute:
                return UGLC::CodeGen::ComputeShaderEntryName;
            default:
                return UGLC::CodeGen::ComputeShaderEntryName;
            }
        }

        /** Finds the entry function in a lowered single-entry UGLIR module. */
        const UGLIR::Function *findEntryFunction(const UGLIR::Module &module)
        {
            for (const UGLIR::Function &function : module.functions)
            {
                if (function.isEntryPoint)
                {
                    return &function;
                }
            }
            return nullptr;
        }

        /** Formats a single SPIR-V emitter or validator diagnostic with a clang-style source location when available. */
        std::string formatDiagnostic(const UGLIRToSPIRVEmissionDiagnostic &diagnostic)
        {
            const std::string prefix = diagnostic.kind == UGLIRToSPIRVDiagnosticKind::Validator
                                           ? "UGLIR SPIR-V validator: "
                                           : "UGLIR SPIR-V emitter: ";
            const std::string message = prefix + diagnostic.message;
            if (!diagnostic.sourceLocation.file.empty() && diagnostic.sourceLocation.line != 0 && diagnostic.sourceLocation.column != 0)
            {
                return UGLC::CodeGen::formatClangStyleDiagnostic(diagnostic.sourceLocation.file,
                                                                  diagnostic.sourceLocation.line,
                                                                  diagnostic.sourceLocation.column,
                                                                  message);
            }
            return UGLC::CodeGen::formatUnlocatedDiagnostic(message);
        }
    } // namespace

    UGLIRToSPIRVEmissionResult emitModuleAsSPIRV(const UGLIR::Module &module, bool optimize)
    {
        UGLIRToSPIRVEmissionResult result;
        for (const UGLIR::UGLIRValidationDiagnostic &diagnostic : UGLIR::validateModule(module))
        {
            result.diagnostics.push_back({UGLIRToSPIRVDiagnosticKind::Emitter, diagnostic.sourceLocation, diagnostic.message});
        }
        if (!result.diagnostics.empty()) { return result; }
        UGLIR::Module normalizedModule = module;
        UGLIR::normalizeBuiltinTypeReferences(normalizedModule);
        for (const SPIRVPreflightDiagnostic &diagnostic : runSPIRVBackendPreflight(normalizedModule))
        {
            result.diagnostics.push_back({
                .kind = UGLIRToSPIRVDiagnosticKind::Emitter,
                .sourceLocation = diagnostic.sourceLocation,
                .message = diagnostic.message,
            });
        }
        if (!result.diagnostics.empty())
        {
            return result;
        }

        SPIRVModuleBuildResult buildResult = buildSPIRVModuleFromUGLIR(normalizedModule);
        for (const SPIRVModuleBuildDiagnostic &diagnostic : buildResult.diagnostics)
        {
            result.diagnostics.push_back({
                .kind = UGLIRToSPIRVDiagnosticKind::Emitter,
                .sourceLocation = diagnostic.sourceLocation,
                .message = diagnostic.message,
            });
        }
        if (!result.diagnostics.empty())
        {
            return result;
        }

        SPIRVValidationResult validationResult = validateAndDisassembleSPIRVWords(buildResult.words);
        result.rawWordDump = dumpSPIRVWordsAsHexText(buildResult.words);
        result.rawDisassembly = validationResult.disassembly;
        if (!validationResult.valid)
        {
            result.diagnostics.push_back({
                .kind = UGLIRToSPIRVDiagnosticKind::Validator,
                .sourceLocation = module.sourceLocation,
                .message = validationResult.diagnostics.empty() ? std::string("SPIRV-Tools rejected the generated module.") : validationResult.diagnostics,
            });
            return result;
        }

        std::vector<uint32_t> finalWords;
        if (optimize)
        {
            SPIRVOptimizationResult optimizationResult = optimizeSPIRVWordsForRuntime(buildResult.words);
            if (!optimizationResult.optimized)
            {
                result.diagnostics.push_back({
                    .kind = UGLIRToSPIRVDiagnosticKind::Emitter,
                    .sourceLocation = module.sourceLocation,
                    .message = optimizationResult.diagnostics.empty() ? std::string("SPIRV-Tools failed to optimize the generated direct SPIR-V module.") : optimizationResult.diagnostics,
                });
                return result;
            }
            finalWords = std::move(optimizationResult.words);
        }
        else { finalWords = std::move(buildResult.words); }

        SPIRVValidationResult finalValidation = validateAndDisassembleSPIRVWords(finalWords);
        result.wordDump = dumpSPIRVWordsAsHexText(finalWords);
        result.disassembly = finalValidation.disassembly;
        if (!finalValidation.valid)
        {
            result.diagnostics.push_back({
                .kind = UGLIRToSPIRVDiagnosticKind::Validator,
                .sourceLocation = module.sourceLocation,
                .message = "Final SPIR-V validation failed: " + finalValidation.diagnostics,
            });
            return result;
        }

        UGLC::CodeGen::CompiledShaderBinary binary;
        const UGLIR::Function *entryFunction = findEntryFunction(module);
        binary.backend = UGLC::CodeGen::ShaderBackendKind::HLSLSPIRV;
        binary.stage = entryFunction == nullptr ? toShaderStageKind(module.reflection.stage) : toShaderStageKind(entryFunction->stage);
        binary.backendName = "UGLIR/SPIR-V";
        binary.entryPoint = entryFunction == nullptr ? entryPointNameForEntryKind(module.reflection.entryKind) : entryPointNameForEntryKind(entryFunction->entryKind);
        binary.sourceName = module.sourceLocation.file;
        binary.spirvWords = std::move(finalWords);
        result.binary = std::move(binary);
        return result;
    }

    std::string formatUGLIRToSPIRVEmissionDiagnostics(const UGLIRToSPIRVEmissionResult &result)
    {
        std::ostringstream stream;
        for (const UGLIRToSPIRVEmissionDiagnostic &diagnostic : result.diagnostics)
        {
            stream << formatDiagnostic(diagnostic) << '\n';
        }
        return stream.str();
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
