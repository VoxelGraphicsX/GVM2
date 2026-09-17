#pragma once
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace UGLC::CodeGen::MSL
{
    /** Describes the existing Metal texture and attachment type mapping. */
    struct TextureFormatTypeInfo
    {
        const char *formatKey;
        const char *framebufferType;
        const char *sampledTextureScalarType;
        const char *storageTextureScalarType;
    };

    /** Returns the unqualified DSL texture-format token used by the lookup table. */
    inline std::string normalizeTextureFormatKey(const std::string &format)
    {
        // Canonical DSL spellings arrive as `UGL::TextureFormat::Foo`, but some
        // internal call sites may already pass the raw `Foo` token. Normalize
        // both shapes to one exact lookup key so substring collisions such as
        // `RGBA8Unorm` vs `RGBA8Unorm_sRGB` cannot silently pick the wrong row.
        const size_t separator = format.rfind("::");
        if (separator == std::string::npos)
        {
            return format;
        }
        return format.substr(separator + 2);
    }

    /** Returns the format mapping, or null when the format token is not recognized. */
    inline const TextureFormatTypeInfo *findTextureFormatTypeInfo(const std::string &format)
    {
        static constexpr TextureFormatTypeInfo kFormatTypeTable[] = {
            {"BGRA8Unorm", "half4", "half", "half"},
            {"RGBA8Unorm", "half4", "half", "half"},
            {"RGBA8Snorm", "half4", "half", "half"},
            {"RGBA8Uint", "ushort4", "uint", "uint"},
            {"RGBA8Sint", "short4", "int", "int"},
            {"R8Unorm", "half", "half", "half"},
            {"R8Snorm", "half", "half", "half"},
            {"R8Uint", "ushort", "uint", "uint"},
            {"R8Sint", "short", "int", "int"},
            {"RG8Unorm", "half2", "half", "half"},
            {"RG8Snorm", "half2", "half", "half"},
            {"RG8Uint", "ushort2", "uint", "uint"},
            {"RG8Sint", "short2", "int", "int"},
            {"R16Uint", "ushort", "uint", "uint"},
            {"R16Sint", "short", "int", "int"},
            {"R16Float", "half", "half", "half"},
            {"RG16Uint", "ushort2", "uint", "uint"},
            {"RG16Sint", "short2", "int", "int"},
            {"RG16Float", "half2", "half", "half"},
            {"R32Uint", "uint", "uint", "uint"},
            {"R32Sint", "int", "int", "int"},
            {"R32Float", "float", "float", "float"},
            {"RG32Uint", "uint2", "uint", "uint"},
            {"RG32Sint", "int2", "int", "int"},
            {"RG32Float", "float2", "float", "float"},
            {"RGBA8Unorm_sRGB", "half4", "half", "half"},
            {"BGRA8Unorm_sRGB", "half4", "half", "half"},
            {"RGB10A2Unorm", "half4", "half", "half"},
            {"RG11B10Ufloat", "half3", "half", "half"},
            {"RGBA16Uint", "ushort4", "uint", "uint"},
            {"RGBA16Sint", "short4", "int", "int"},
            {"RGBA16Float", "half4", "half", "half"},
            {"RGBA32Uint", "uint4", "uint", "uint"},
            {"RGBA32Sint", "int4", "int", "int"},
            {"RGBA32Float", "float4", "float", "float"},
            {"ASTC4x4Unorm", nullptr, "half", nullptr},
            {"Depth32Float", "float", "float", "float"},
            {"Depth16Unorm", "half", "half", "half"},
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

    /** Selects the existing bindless texture wrapper for its element type and write access. */
    inline std::string getBindlessTextureWrapperFromTemplateType(const bool writable, const std::string &templateTypeName)
    {
        const std::string writablePrefix = writable ? "RW" : "Sample";
        std::string UGLTextureWrapperName;
        if (templateTypeName.find("float") != templateTypeName.npos)
        {
            UGLTextureWrapperName = "UGL" + writablePrefix + "FloatTextureArrayWraper";
        }
        else if (templateTypeName.find("half") != templateTypeName.npos)
        {
            UGLTextureWrapperName = "UGL" + writablePrefix + "HalfTextureArrayWraper";
        }
        else if (templateTypeName.find("uint") != templateTypeName.npos)
        {
            UGLTextureWrapperName = "UGL" + writablePrefix + "UIntTextureArrayWraper";
        }
        else if (templateTypeName.find("int") != templateTypeName.npos)
        {
            UGLTextureWrapperName = "UGL" + writablePrefix + "IntTextureArrayWraper";
        }
        return UGLTextureWrapperName;
    }


    /** Returns the attachment value type, preserving the declared channel count and rejecting unsupported formats. */
    inline std::string MakeFormatToVectorTypeForFrameBuffer(const std::string &format)
    {
        // Framebuffer records must preserve the declared attachment arity and
        // storage width because fragment return types participate in the MSL
        // ABI directly. This must not be inferred indirectly from substrings.
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->framebufferType == nullptr || info->framebufferType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + format + " cannot be used as a framebuffer or color attachment format.");
            }
            return info->framebufferType;
        }
        throw std::runtime_error("Unknown format " + format);
    }
    /** Returns the sampled texture element type for a DSL format or scalar type spelling. */
    inline std::string MakeFormatToVectorTypeForTexture(const std::string &format)
    {
        // Texture object template arguments follow Metal's scalar element type
        // rules, which are related to but not identical to framebuffer return
        // records. Keep the mapping explicit so fixing attachment ABI types
        // cannot accidentally break texture declarations.
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->sampledTextureScalarType == nullptr || info->sampledTextureScalarType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + format + " cannot be used as a sampled texture format.");
            }
            return info->sampledTextureScalarType;
        }

        std::string fmt = format;
        std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
        if (fmt.find("uint") != fmt.npos)
        {
            return "uint";
        }
        else if (fmt.find("int") != fmt.npos)
        {
            return "int";
        }
        else if (fmt.find("float") != fmt.npos)
        {
            return "float";
        }
        else if (fmt.find("half") != fmt.npos)
        {
            return "half";
        }
        return format;
    }

    /** Returns the writable texture element type and rejects unsupported storage formats. */
    inline std::string MakeFormatToVectorTypeForStorageTexture(const std::string &format)
    {
        if (const auto *info = findTextureFormatTypeInfo(format))
        {
            if (info->storageTextureScalarType == nullptr || info->storageTextureScalarType[0] == '\0')
            {
                throw std::runtime_error("Texture format " + format + " cannot be used as a read-write storage texture format.");
            }
            return info->storageTextureScalarType;
        }

        std::string fmt = format;
        std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
        if (fmt.find("uint") != fmt.npos)
        {
            return "uint";
        }
        else if (fmt.find("int") != fmt.npos)
        {
            return "int";
        }
        else if (fmt.find("float") != fmt.npos)
        {
            return "float";
        }
        else if (fmt.find("half") != fmt.npos)
        {
            return "half";
        }
        else
        {
            throw std::runtime_error("unknown texture format");
        }
        return "float";
    }

}
