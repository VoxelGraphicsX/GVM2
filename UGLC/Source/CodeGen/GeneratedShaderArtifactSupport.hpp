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
#include <cstring>
namespace UGLC::Generated
{
    /** Encodes one scalar into a fixed 32-bit Args word without copying CPU padding. */
    template<typename T>
    inline void WriteArgumentScalar(eastl::span<uint8_t> values, uint32_t offset, T value)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            const uint32_t word = value ? 1u : 0u;
            std::memcpy(values.data() + offset, &word, sizeof(word));
        }
        else if constexpr (std::is_same_v<T, GVM::Core::Math::ShaderHalf>)
        {
            const uint32_t word = value.bits;
            std::memcpy(values.data() + offset, &word, sizeof(word));
        }
        else
        {
            static_assert(sizeof(T) == 4, "Args scalar encoding requires a 32-bit word.");
            std::memcpy(values.data() + offset, &value, sizeof(value));
        }
    }

    struct ShaderArtifact
    {
        eastl::string mslSource;
        eastl::string hlslSource;
        const uint32_t *spv = nullptr;
        uint32_t spvWordCount = 0;
        eastl::vector<uint8_t> metallib;

        [[nodiscard]] bool hasSpv() const
        {
            return spv != nullptr && spvWordCount > 0;
        }
    };

    inline ShaderArtifact MakeShaderArtifact(eastl::string mslSource,
                                             eastl::string hlslSource = {},
                                             const uint32_t *spv = nullptr,
                                             uint32_t spvWordCount = 0,
                                             eastl::vector<uint8_t> metallib = {})
    {
        ShaderArtifact artifact;
        artifact.mslSource = mslSource;
        artifact.hlslSource = hlslSource;
        artifact.spv = spv;
        artifact.spvWordCount = spvWordCount;
        artifact.metallib = eastl::move(metallib);
        return artifact;
    }

    inline GVM::RHI::ShaderModuleDescriptor MakeShaderModuleDescriptor(const char *label,
                                                                       const ShaderArtifact &artifact)
    {
        GVM::RHI::ShaderModuleDescriptor descriptor = {};
        descriptor.label = label;
        if (artifact.metallib.empty())
        {
            descriptor.code = artifact.mslSource;
        }
        descriptor.metallib = artifact.metallib;
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
