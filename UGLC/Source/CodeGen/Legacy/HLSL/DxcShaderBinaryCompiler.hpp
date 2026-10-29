#pragma once

#include <CodeGen/ShaderCompilerService.hpp>

#include <memory>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Compiles emitted HLSL shader sources to SPIR-V through the pinned DXC service.
     *
     * The compiler is owned by the HLSL backend because UGLC only uses DXC for the
     * HLSL-to-SPIR-V path. MSL and host C++ code generation never instantiate this
     * service.
     */
    class DxcShaderBinaryCompiler final : public IShaderBinaryCompiler
    {
    public:
        /**
         * @brief Lowers one emitted HLSL shader stage to a SPIR-V binary and diagnostics record.
         */
        ShaderCompileResult compileToSpirv(const EmittedShaderSource &source) override;
    };
} // namespace UGLC::CodeGen::HLSL
