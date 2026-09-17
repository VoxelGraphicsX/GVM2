#include "HLSLTextureTypes.hpp"
#include <algorithm>
#include <cctype>
#include <optional>

namespace UGLC::CodeGen::HLSL
{
    /** Returns the unqualified DSL texture-format token used by the lookup table. */
    std::string normalizeTextureFormatKey(const std::string &format)
    {
        const size_t separator = format.rfind("::");
        if (separator == std::string::npos)
        {
            return format;
        }
        return format.substr(separator + 2);
    }

    /** Returns the format mapping, or null when the format token is not recognized. */
    const TextureFormatTypeInfo *findTextureFormatTypeInfo(const std::string &format)
    {
        static constexpr TextureFormatTypeInfo kFormatTypeTable[] = {
            {"BGRA8Unorm", "float4", "float4", "float4", nullptr},
            {"RGBA8Unorm", "float4", "float4", "float4", "rgba8"},
            {"RGBA8Snorm", "float4", "float4", "float4", "rgba8_snorm"},
            {"RGBA8Uint", "uint4", "uint4", "uint4", "rgba8ui"},
            {"RGBA8Sint", "int4", "int4", "int4", "rgba8i"},
            {"R8Unorm", "float", "float", "float", "r8"},
            {"R8Snorm", "float", "float", "float", "r8_snorm"},
            {"R8Uint", "uint", "uint", "uint", "r8ui"},
            {"R8Sint", "int", "int", "int", "r8i"},
            {"RG8Unorm", "float2", "float2", "float2", "rg8"},
            {"RG8Snorm", "float2", "float2", "float2", "rg8_snorm"},
            {"RG8Uint", "uint2", "uint2", "uint2", "rg8ui"},
            {"RG8Sint", "int2", "int2", "int2", "rg8i"},
            {"R16Uint", "uint", "uint", "uint", "r16ui"},
            {"R16Sint", "int", "int", "int", "r16i"},
            {"R16Float", "float", "float", "float", "r16f"},
            {"RG16Uint", "uint2", "uint2", "uint2", "rg16ui"},
            {"RG16Sint", "int2", "int2", "int2", "rg16i"},
            {"RG16Float", "float2", "float2", "float2", "rg16f"},
            {"R32Uint", "uint", "uint", "uint", "r32ui"},
            {"R32Sint", "int", "int", "int", "r32i"},
            {"R32Float", "float", "float", "float", "r32f"},
            {"RG32Uint", "uint2", "uint2", "uint2", "rg32ui"},
            {"RG32Sint", "int2", "int2", "int2", "rg32i"},
            {"RG32Float", "float2", "float2", "float2", "rg32f"},
            {"RGBA8Unorm_sRGB", "float4", "float4", "float4", nullptr},
            {"BGRA8Unorm_sRGB", "float4", "float4", "float4", nullptr},
            {"RGB10A2Unorm", "float4", "float4", "float4", nullptr},
            {"RG11B10Ufloat", "float3", "float3", "float3", nullptr},
            {"RGBA16Uint", "uint4", "uint4", "uint4", "rgba16ui"},
            {"RGBA16Sint", "int4", "int4", "int4", "rgba16i"},
            {"RGBA16Float", "float4", "float4", "float4", "rgba16f"},
            {"RGBA32Uint", "uint4", "uint4", "uint4", "rgba32ui"},
            {"RGBA32Sint", "int4", "int4", "int4", "rgba32i"},
            {"RGBA32Float", "float4", "float4", "float4", "rgba32f"},
            {"ASTC4x4Unorm", "float4", nullptr, nullptr, nullptr},
            {"Depth32Float", "float", "float", "float", nullptr},
            {"Depth16Unorm", "float", "float", "float", nullptr},
        };

        const std::string formatKey = normalizeTextureFormatKey(format);
        for (const auto &entry : kFormatTypeTable)
        {
            if (formatKey == entry.formatKey)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    /** Returns the attachment value type, preserving the declared channel count and rejecting unsupported formats. */
    std::string MakeFormatToVectorTypeForFrameBuffer(const std::string &format)
    {
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->framebufferType == nullptr || info->framebufferType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + format + " cannot be used as a framebuffer or color attachment format.");
            }
            return info->framebufferType;
        }
        throw std::runtime_error("Unknown framebuffer format " + format);
    }

    /** Returns the writable texture element type and rejects unsupported storage formats. */
    std::string MakeFormatToVectorTypeForStorageTexture(const std::string &format)
    {
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->storageTextureType == nullptr || info->storageTextureType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + format + " cannot be used as a read-write storage texture format.");
            }
            return info->storageTextureType;
        }
        throw std::runtime_error("Unknown storage texture format " + format);
    }

    /** Returns the explicit Vulkan storage-image format qualifier or rejects unsupported formats. */
    std::string MakeVulkanImageFormatForStorageTexture(const std::string &format)
    {
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->vulkanStorageImageFormat != nullptr && info->vulkanStorageImageFormat[0] != '\0')
            {
                return info->vulkanStorageImageFormat;
            }

            throw std::runtime_error("Storage texture format " + format + " does not currently have a Vulkan HLSL image_format lowering.");
        }
        throw std::runtime_error("Unknown storage texture format " + format);
    }

    /** Returns the HLSL sampled texture value type for a DSL sample type. */
    std::string MakeSampledTextureType(const std::string &sampleType)
    {
        const std::string normalized = normalizeTextureFormatKey(sampleType);
        if (const auto *info = findTextureFormatTypeInfo(normalized))
        {
            if (info->sampledTextureType == nullptr || info->sampledTextureType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + sampleType + " cannot be used as a sampled texture format.");
            }
            return info->sampledTextureType;
        }

        auto preserveNumericSuffix = [&](std::string_view sourcePrefix, std::string_view targetPrefix) -> std::optional<std::string> {
            if (!normalized.starts_with(sourcePrefix))
            {
                return std::nullopt;
            }

            const std::string_view suffix = std::string_view(normalized).substr(sourcePrefix.size());
            const bool suffixIsVectorWidth = suffix.empty() ||
                                             std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
            if (!suffixIsVectorWidth)
            {
                return std::nullopt;
            }

            return std::string(targetPrefix) + std::string(suffix);
        };

        if (const auto mapped = preserveNumericSuffix("half", "float"))
        {
            return *mapped;
        }
        if (const auto mapped = preserveNumericSuffix("double", "float"))
        {
            return *mapped;
        }
        if (const auto mapped = preserveNumericSuffix("float", "float"))
        {
            return *mapped;
        }
        if (const auto mapped = preserveNumericSuffix("uint", "uint"))
        {
            return *mapped;
        }
        if (const auto mapped = preserveNumericSuffix("int", "int"))
        {
            return *mapped;
        }

        std::string lower = normalized;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lower.find("uint") != std::string::npos)
        {
            return "uint";
        }
        if (lower.find("int") != std::string::npos)
        {
            return "int";
        }
        if (lower.find("half") != std::string::npos || lower.find("float") != std::string::npos || lower.find("double") != std::string::npos)
        {
            return "float";
        }

        return normalized;
    }

}
