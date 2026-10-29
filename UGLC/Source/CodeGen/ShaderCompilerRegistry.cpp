#include "ShaderCompilerRegistry.hpp"

#if UGLC_ENABLE_LEGACY
#include <CodeGen/Legacy/HLSL/DxcShaderBinaryCompiler.hpp>
#endif

#include <utility>

#ifndef UGLC_ENABLE_LEGACY
#define UGLC_ENABLE_LEGACY 0
#endif

#ifndef UGLC_ENABLE_SHADER_LINE_DIRECTIVES
#define UGLC_ENABLE_SHADER_LINE_DIRECTIVES 0
#endif

namespace UGLC::CodeGen
{
    namespace
    {
        class DisabledShaderBinaryCompiler final : public IShaderBinaryCompiler
        {
        public:
            explicit DisabledShaderBinaryCompiler(std::string compilerName)
                : mCompilerName(std::move(compilerName))
            {
            }

            ShaderCompileResult compileToSpirv(const EmittedShaderSource &source) override
            {
                ShaderCompileDiagnostics diagnostics;
                diagnostics.success = false;
                diagnostics.backend = source.backend;
                diagnostics.stage = source.stage;
                diagnostics.backendName = source.backendName;
                diagnostics.stageName = source.stageName.empty() ? getShaderStageDisplayName(source.stage) : source.stageName;
                diagnostics.entryPoint = source.entryPoint;
                diagnostics.sourceName = source.sourceName;
                diagnostics.compilerOutput = mCompilerName + " compiler service is disabled in this UGLC build cache. Reconfigure with -DUGLC_ENABLE_LEGACY=ON so UGLC can fetch the pinned DirectXShaderCompiler source through CPM and build the DXC-backed SPIR-V path.";
                return {
                    .binary = std::nullopt,
                    .diagnostics = std::move(diagnostics),
                };
            }

        private:
            std::string mCompilerName;
        };
    } // namespace

    ShaderCompilerFeatureFlags getShaderCompilerFeatureFlags()
    {
        return {
            .hasDxcCompilerService = UGLC_ENABLE_LEGACY != 0,
            .enableShaderLineDirectives = UGLC_ENABLE_SHADER_LINE_DIRECTIVES != 0,
        };
    }

    std::unique_ptr<IShaderBinaryCompiler> createSpirvCompilerForBackend(ShaderBackendKind backend)
    {
        if (backend == ShaderBackendKind::HLSLSPIRV)
        {
#if UGLC_ENABLE_LEGACY
            return std::make_unique<HLSL::DxcShaderBinaryCompiler>();
#else
            // Keep a stable diagnostics-shaped service even in builds that do
            // not enable the real DXC-backed compiler.
            return std::make_unique<DisabledShaderBinaryCompiler>("DXC");
#endif
        }
        return nullptr;
    }
} // namespace UGLC::CodeGen
