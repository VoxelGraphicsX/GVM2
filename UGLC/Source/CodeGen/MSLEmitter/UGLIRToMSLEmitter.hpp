#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/ShaderCompilerService.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen::MSLEmitter
{
    /** Describes one source-bound failure produced while converting UGLIR into Metal Shading Language. */
    struct UGLIRToMSLEmissionDiagnostic
    {
        UGLIR::SourceLocation sourceLocation;
        std::string message;
    };

    /** Stores the generated MSL source or the diagnostics that explain why emission failed. */
    struct UGLIRToMSLEmissionResult
    {
        std::optional<UGLC::CodeGen::EmittedShaderSource> source;
        std::vector<UGLIRToMSLEmissionDiagnostic> diagnostics;
    };

    /** Describes one resource field inside a Metal argument-buffer bind group. */
    struct MSLResourceBindingLayout
    {
        std::string resourceName;
        std::string resourceFieldName;
        uint32_t bindingIndex = 0;
    };

    /** Describes one host-computed Metal argument-buffer bind group slot. */
    struct MSLBindGroupLayout
    {
        std::string bindGroupName;
        std::string bindGroupTypeName;
        uint32_t metalBufferIndex = 0;
        std::vector<MSLResourceBindingLayout> resources;
    };

    /** Describes the complete Metal resource layout required to emit one UGLIR module. */
    struct MSLResourceLayout
    {
        std::vector<MSLBindGroupLayout> bindGroups;
    };

    /** Emits UGLIR as a Metal shader source payload using the supplied host-computed Metal resource layout. */
    UGLIRToMSLEmissionResult emitModuleAsMSL(const UGLIR::Module &module, const MSLResourceLayout &resourceLayout);

    /** Emits UGLIR as a Metal shader source payload for modules that do not reference resources. */
    UGLIRToMSLEmissionResult emitModuleAsMSL(const UGLIR::Module &module);

    /** Formats MSL emission diagnostics using the same clang-style shape as other UGLC diagnostics. */
    std::string formatUGLIRToMSLEmissionDiagnostics(const UGLIRToMSLEmissionResult &result);
} // namespace UGLC::CodeGen::MSLEmitter
