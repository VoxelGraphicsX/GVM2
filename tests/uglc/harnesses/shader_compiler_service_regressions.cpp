#include <CodeGen/ShaderCompilerRegistry.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
    namespace Spirv
    {
        constexpr uint32_t kMagicNumber = 0x07230203u;
        constexpr uint16_t kOpCapability = 17u;
        constexpr uint16_t kOpDecorate = 71u;

        constexpr uint32_t kCapabilityGeometry = 2u;
        constexpr uint32_t kCapabilityTessellation = 3u;

        constexpr uint32_t kDecorationBuiltIn = 11u;
        constexpr uint32_t kBuiltInPrimitiveId = 7u;
    } // namespace Spirv

    bool spirvContainsCapability(const std::vector<uint32_t> &spirvWords, uint32_t capability)
    {
        if (spirvWords.size() < 5 || spirvWords.front() != Spirv::kMagicNumber)
        {
            return false;
        }

        size_t wordOffset = 5;
        while (wordOffset < spirvWords.size())
        {
            const uint32_t instructionHeader = spirvWords[wordOffset];
            const uint16_t wordCount = static_cast<uint16_t>(instructionHeader >> 16u);
            const uint16_t opcode = static_cast<uint16_t>(instructionHeader & 0xFFFFu);
            if (wordCount == 0u || (wordOffset + wordCount) > spirvWords.size())
            {
                return false;
            }

            if (opcode == Spirv::kOpCapability && wordCount >= 2u && spirvWords[wordOffset + 1] == capability)
            {
                return true;
            }

            wordOffset += wordCount;
        }

        return false;
    }

    bool spirvContainsPrimitiveIdBuiltIn(const std::vector<uint32_t> &spirvWords)
    {
        if (spirvWords.size() < 5 || spirvWords.front() != Spirv::kMagicNumber)
        {
            return false;
        }

        size_t wordOffset = 5;
        while (wordOffset < spirvWords.size())
        {
            const uint32_t instructionHeader = spirvWords[wordOffset];
            const uint16_t wordCount = static_cast<uint16_t>(instructionHeader >> 16u);
            const uint16_t opcode = static_cast<uint16_t>(instructionHeader & 0xFFFFu);
            if (wordCount == 0u || (wordOffset + wordCount) > spirvWords.size())
            {
                return false;
            }

            if (opcode == Spirv::kOpDecorate &&
                wordCount >= 4u &&
                spirvWords[wordOffset + 2] == Spirv::kDecorationBuiltIn &&
                spirvWords[wordOffset + 3] == Spirv::kBuiltInPrimitiveId)
            {
                return true;
            }

            wordOffset += wordCount;
        }

        return false;
    }
} // namespace

int main()
{
    using namespace UGLC::CodeGen;

    if (std::string(getShaderStageDisplayName(ShaderStageKind::Hull)) != "hull")
    {
        throw std::runtime_error("Hull stage display names should stay stable once shader emitters expose structured stage metadata.");
    }
    if (std::string(getShaderStageDisplayName(ShaderStageKind::Domain)) != "domain")
    {
        throw std::runtime_error("Domain stage display names should stay stable once shader emitters expose structured stage metadata.");
    }

    const ShaderCompilerFeatureFlags featureFlags = getShaderCompilerFeatureFlags();
    if (featureFlags.enableShaderLineDirectives)
    {
        throw std::runtime_error("Shader line directives should remain disabled by default while the current bug investigation is still in progress.");
    }

    auto spirvCompiler = createSpirvCompilerForBackend(ShaderBackendKind::HLSLSPIRV);
    if (!spirvCompiler)
    {
        throw std::runtime_error("HLSL/SPIR-V should always expose either the placeholder compiler service or the real DXC-backed implementation.");
    }

    EmittedShaderSource emittedSource;
    emittedSource.backend = ShaderBackendKind::HLSLSPIRV;
    emittedSource.stage = ShaderStageKind::Compute;
    emittedSource.backendName = "HLSL/SPIR-V";
    emittedSource.stageName = getShaderStageDisplayName(ShaderStageKind::Compute);
    emittedSource.entryPoint = "computeMain";
    emittedSource.debugName = "FutureHlsl::compute";
    emittedSource.sourceName = "tests/uglc/fixtures/future-hlsl/FutureHlsl.hpp";
    emittedSource.sourceText = "[numthreads(8, 1, 1)] void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {}";

    const ShaderCompileResult result = spirvCompiler->compileToSpirv(emittedSource);
    if (result.diagnostics.backend != ShaderBackendKind::HLSLSPIRV)
    {
        throw std::runtime_error("The DXC compiler service lost the backend kind in diagnostics.");
    }
    if (result.diagnostics.stage != ShaderStageKind::Compute)
    {
        throw std::runtime_error("The DXC compiler service lost the shader stage in diagnostics.");
    }
    if (result.diagnostics.entryPoint != emittedSource.entryPoint)
    {
        throw std::runtime_error("The DXC compiler service lost the entry-point name in diagnostics.");
    }
    if (result.diagnostics.sourceName != emittedSource.sourceName)
    {
        throw std::runtime_error("The DXC compiler service lost the source name in diagnostics.");
    }
    if (emittedSource.debugName != "FutureHlsl::compute")
    {
        throw std::runtime_error("Structured emitted shader metadata should retain the caller-provided debug name.");
    }

    if (!featureFlags.hasDxcCompilerService)
    {
        if (result.succeeded())
        {
            throw std::runtime_error("The placeholder DXC compiler service should not report success before the real DXC integration exists.");
        }
        if (result.binary.has_value())
        {
            throw std::runtime_error("The placeholder DXC compiler service must not fabricate SPIR-V output.");
        }
        if (result.diagnostics.success)
        {
            throw std::runtime_error("The placeholder DXC compiler service must report a failed diagnostic result.");
        }
        const bool hasLegacyDisabledText = result.diagnostics.compilerOutput.find("DXC compiler service is not enabled") != std::string::npos;
        const bool hasCurrentDisabledText = result.diagnostics.compilerOutput.find("DXC compiler service is disabled in this UGLC build cache") != std::string::npos;
        if (!hasLegacyDisabledText && !hasCurrentDisabledText)
        {
            throw std::runtime_error("The placeholder DXC compiler service should explain why compilation is unavailable.");
        }

        std::cout << "shader_compiler_service_placeholder_ok\n";
        return 0;
    }

    if (!result.succeeded())
    {
        std::ostringstream stream;
        stream << "The DXC compiler service should successfully compile HLSL to SPIR-V when enabled. Diagnostics:\n"
               << result.diagnostics.compilerOutput;
        throw std::runtime_error(stream.str());
    }
    if (!result.binary.has_value())
    {
        throw std::runtime_error("The enabled DXC compiler service should return a SPIR-V binary payload.");
    }
    if (!result.diagnostics.success)
    {
        throw std::runtime_error("The enabled DXC compiler service should report successful diagnostics.");
    }
    if (result.binary->spirvWords.empty())
    {
        throw std::runtime_error("The enabled DXC compiler service returned an empty SPIR-V module.");
    }
    if (result.binary->spirvWords.front() != 0x07230203u)
    {
        throw std::runtime_error("The enabled DXC compiler service returned a binary whose first word is not the SPIR-V magic number.");
    }

    EmittedShaderSource primitiveIdFragmentSource;
    primitiveIdFragmentSource.backend = ShaderBackendKind::HLSLSPIRV;
    primitiveIdFragmentSource.stage = ShaderStageKind::Fragment;
    primitiveIdFragmentSource.backendName = "HLSL/SPIR-V";
    primitiveIdFragmentSource.stageName = getShaderStageDisplayName(ShaderStageKind::Fragment);
    primitiveIdFragmentSource.entryPoint = "fragmentMain";
    primitiveIdFragmentSource.debugName = "FutureHlsl::primitiveIdFragment";
    primitiveIdFragmentSource.sourceName = "tests/uglc/fixtures/future-hlsl/PrimitiveIdFragment.hpp";
    primitiveIdFragmentSource.sourceText = R"(struct PSInput
{
    float4 position : SV_Position;
};
float4 fragmentMain(PSInput input, uint primitiveID : SV_PrimitiveID) : SV_Target0
{
    return float4((primitiveID & 255u) / 255.0, 0.0, 0.0, 1.0);
})";

    const ShaderCompileResult primitiveIdResult = spirvCompiler->compileToSpirv(primitiveIdFragmentSource);
    if (!primitiveIdResult.succeeded() || !primitiveIdResult.binary.has_value())
    {
        std::ostringstream stream;
        stream << "Fragment PrimitiveID shaders should still compile through the DXC service. Diagnostics:\n"
               << primitiveIdResult.diagnostics.compilerOutput;
        throw std::runtime_error(stream.str());
    }
    if (!spirvContainsPrimitiveIdBuiltIn(primitiveIdResult.binary->spirvWords))
    {
        throw std::runtime_error("The PrimitiveID regression module lost its PrimitiveId built-in decoration.");
    }
    if (spirvContainsCapability(primitiveIdResult.binary->spirvWords, Spirv::kCapabilityGeometry))
    {
        throw std::runtime_error("Fragment PrimitiveID SPIR-V should no longer carry Geometry capability after the Vulkan compatibility fix.");
    }
    if (!spirvContainsCapability(primitiveIdResult.binary->spirvWords, Spirv::kCapabilityTessellation))
    {
        throw std::runtime_error("Fragment PrimitiveID SPIR-V should carry Tessellation capability after the Vulkan compatibility fix.");
    }

    EmittedShaderSource invalidSource = emittedSource;
    invalidSource.sourceText = R"( [numthreads(8, 1, 1)] void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    missingValue = dispatchThreadID.x;
})";

    const ShaderCompileResult invalidResult = spirvCompiler->compileToSpirv(invalidSource);
    if (invalidResult.succeeded())
    {
        throw std::runtime_error("The enabled DXC compiler service should fail when HLSL compilation errors occur.");
    }
    if (invalidResult.binary.has_value())
    {
        throw std::runtime_error("The enabled DXC compiler service must not return a SPIR-V payload for invalid HLSL.");
    }
    if (invalidResult.diagnostics.success)
    {
        throw std::runtime_error("The enabled DXC compiler service must report failed diagnostics for invalid HLSL.");
    }
    if (invalidResult.diagnostics.entryPoint != invalidSource.entryPoint)
    {
        throw std::runtime_error("The failing DXC compiler-service path should preserve the entry-point name.");
    }
    if (invalidResult.diagnostics.sourceName != invalidSource.sourceName)
    {
        throw std::runtime_error("The failing DXC compiler-service path should preserve the source name.");
    }
    if (invalidResult.diagnostics.compilerOutput.find("undeclared identifier") == std::string::npos &&
        invalidResult.diagnostics.compilerOutput.find("use of undeclared identifier") == std::string::npos)
    {
        throw std::runtime_error("The failing DXC compiler-service path should surface the compiler diagnostic text.");
    }

    std::cout << "shader_compiler_service_dxc_ok\n";
    return 0;
}
