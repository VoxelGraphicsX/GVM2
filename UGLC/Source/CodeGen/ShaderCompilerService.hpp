#pragma once

#include <CodeGen/ShaderBackendCapabilities.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    enum class ShaderStageKind : int
    {
        Vertex = 1,
        Fragment,
        Compute,
        Hull,
        Domain,
    };

    inline const char *getShaderStageDisplayName(ShaderStageKind stage)
    {
        switch (stage)
        {
        case ShaderStageKind::Vertex:
            return "vertex";
        case ShaderStageKind::Fragment:
            return "fragment";
        case ShaderStageKind::Compute:
            return "compute";
        case ShaderStageKind::Hull:
            return "hull";
        case ShaderStageKind::Domain:
            return "domain";
        }
        return "unknown";
    }

    // This is the backend-neutral handoff between text emitters and any future
    // binary compiler such as DXC. Keeping prelude and source separated makes it
    // possible to preserve stable debug names and line-mapping policy.
    struct EmittedShaderSource
    {
        ShaderBackendKind backend = ShaderBackendKind::MSL;
        ShaderStageKind stage = ShaderStageKind::Compute;
        std::string backendName = "MSL";
        std::string stageName = getShaderStageDisplayName(ShaderStageKind::Compute);
        std::string entryPoint;
        std::string debugName;
        std::string sourceName;
        std::string preludeText;
        std::string sourceText;
        bool enableLineDirectives = false;
    };

    struct ShaderCompileDiagnostics
    {
        bool success = false;
        ShaderBackendKind backend = ShaderBackendKind::HLSLSPIRV;
        ShaderStageKind stage = ShaderStageKind::Compute;
        std::string backendName = "HLSL/SPIR-V";
        std::string stageName = getShaderStageDisplayName(ShaderStageKind::Compute);
        std::string entryPoint;
        std::string sourceName;
        std::string compilerOutput;
    };

    struct CompiledShaderBinary
    {
        ShaderBackendKind backend = ShaderBackendKind::HLSLSPIRV;
        ShaderStageKind stage = ShaderStageKind::Compute;
        std::string backendName = "HLSL/SPIR-V";
        std::string entryPoint;
        std::string sourceName;
        std::vector<uint32_t> spirvWords;
    };

    struct ShaderCompileResult
    {
        std::optional<CompiledShaderBinary> binary;
        ShaderCompileDiagnostics diagnostics;

        [[nodiscard]] bool succeeded() const
        {
            return binary.has_value() && diagnostics.success;
        }
    };

    class IShaderBinaryCompiler
    {
    public:
        virtual ~IShaderBinaryCompiler() = default;

        virtual ShaderCompileResult compileToSpirv(const EmittedShaderSource &source) = 0;
    };

    struct ShaderCompilerFeatureFlags
    {
        bool hasDxcCompilerService = false;
        bool enableShaderLineDirectives = false;
    };
} // namespace UGLC::CodeGen
