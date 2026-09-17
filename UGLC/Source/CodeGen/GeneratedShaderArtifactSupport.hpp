#pragma once

#include <string>

namespace UGLC::CodeGen
{
    // Emits the small support types that generated headers use to keep one
    // shader stage's backend artifacts bundled together. Generated host code
    // can then forward both source and optional SPIR-V payload directly to
    // the runtime descriptor without guessing which backend is active.
    inline std::string MakeGeneratedShaderArtifactSupport()
    {
        return R"UGL(
namespace UGLC::Generated
{
    struct ShaderArtifact
    {
        eastl::string mslSource;
        eastl::string hlslSource;
        const uint32_t *spv = nullptr;
        uint32_t spvWordCount = 0;

        [[nodiscard]] bool hasSpv() const
        {
            return spv != nullptr && spvWordCount > 0;
        }
    };

    inline ShaderArtifact MakeShaderArtifact(eastl::string mslSource,
                                             eastl::string hlslSource = {},
                                             const uint32_t *spv = nullptr,
                                             uint32_t spvWordCount = 0)
    {
        ShaderArtifact artifact;
        artifact.mslSource = mslSource;
        artifact.hlslSource = hlslSource;
        artifact.spv = spv;
        artifact.spvWordCount = spvWordCount;
        return artifact;
    }

    inline GVM::RHI::ShaderModuleDescriptor MakeShaderModuleDescriptor(const char *label,
                                                                       const ShaderArtifact &artifact)
    {
        GVM::RHI::ShaderModuleDescriptor descriptor = {};
        descriptor.label = label;
        descriptor.code = artifact.mslSource;
        if (artifact.hasSpv())
        {
            descriptor.spirv.assign(artifact.spv, artifact.spv + artifact.spvWordCount);
        }
        return descriptor;
    }
} // namespace UGLC::Generated
)UGL";
    }
} // namespace UGLC::CodeGen
