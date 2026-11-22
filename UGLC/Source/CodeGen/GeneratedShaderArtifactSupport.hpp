#pragma once

#include <string>
#include <string_view>

namespace UGLC::CodeGen
{
    /** Formats shader source text as a single raw string literal when within MSVC's literal limit, or a JoinShaderChunks call when larger. */
    inline std::string FormatShaderTextExpression(std::string_view text)
    {
        constexpr size_t kMaxRawLiteralChunkBytes = 15000;
        if (text.size() <= kMaxRawLiteralChunkBytes)
        {
            return "R\"(" + std::string(text) + ")\"";
        }

        std::string result = "UGLC::Generated::JoinShaderChunks({";
        size_t offset = 0;
        bool firstChunk = true;
        while (offset < text.size())
        {
            const size_t remaining = text.size() - offset;
            size_t chunkLength = remaining <= kMaxRawLiteralChunkBytes ? remaining : kMaxRawLiteralChunkBytes;
            if (offset + chunkLength < text.size())
            {
                const size_t newlinePos = text.rfind('\n', offset + chunkLength - 1);
                if (newlinePos != std::string_view::npos && newlinePos >= offset)
                {
                    chunkLength = newlinePos - offset + 1;
                }
                else
                {
                    while (chunkLength > 1 && (static_cast<unsigned char>(text[offset + chunkLength]) & 0xC0u) == 0x80u)
                    {
                        --chunkLength;
                    }
                }
            }
            if (!firstChunk)
            {
                result += ", ";
            }
            firstChunk = false;
            result += "R\"(";
            result.append(text.data() + offset, chunkLength);
            result += ")\"";
            offset += chunkLength;
        }
        result += "})";
        return result;
    }

    // Emits the small support types that generated headers use to keep one
    // shader stage's backend artifacts bundled together. Generated host code
    // can then forward both source and optional SPIR-V payload directly to
    // the runtime descriptor without guessing which backend is active.
    inline std::string MakeGeneratedShaderArtifactSupport()
    {
        return R"UGL(
#include <cstring>
#include <initializer_list>
namespace UGLC::Generated
{
    /** Concatenates split shader source string literals so large shaders stay below MSVC's single-literal byte limit. */
    inline eastl::string JoinShaderChunks(std::initializer_list<eastl::string_view> chunks)
    {
        size_t totalLength = 0;
        for (const eastl::string_view chunk : chunks)
        {
            totalLength += chunk.size();
        }
        eastl::string joined;
        joined.reserve(totalLength);
        for (const eastl::string_view chunk : chunks)
        {
            joined.append(chunk.data(), chunk.size());
        }
        return joined;
    }
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
