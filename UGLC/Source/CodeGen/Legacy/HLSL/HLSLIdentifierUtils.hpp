#pragma once

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace UGLC::CodeGen::HLSL
{
    inline bool isAnonymousNamespaceSegment(std::string_view identifier)
    {
        return identifier == "(anonymous namespace)";
    }

    inline bool isReservedHLSLIdentifier(std::string_view identifier)
    {
        static constexpr std::string_view kReservedIdentifiers[] = {
            "asm",        "bool",      "break",      "Buffer",     "ByteAddressBuffer",
            "case",       "cbuffer",   "class",      "column_major","compile",
            "const",      "continue",  "default",    "discard",    "do",
            "double",     "else",      "export",     "extern",     "false",
            "float",      "for",       "groupshared","half",       "if",
            "in",         "inline",    "inout",      "int",        "interface",
            "matrix",     "namespace", "nointerpolation", "out",   "packoffset",
            "pass",       "pixelfragment", "precise","return",     "register",
            "row_major",  "RWBuffer",  "RWByteAddressBuffer", "RWStructuredBuffer", "RWTexture1D",
            "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray", "RWTexture3D", "sampler",
            "SamplerComparisonState", "SamplerState", "shared", "snorm", "stateblock",
            "static",     "string",    "struct",     "switch",     "technique",
            "technique10","technique11","Texture1D", "Texture1DArray", "Texture2D",
            "Texture2DArray", "Texture2DMS", "Texture2DMSArray", "Texture3D", "TextureCube",
            "TextureCubeArray", "true","typedef",    "uint",       "uniform",
            "unorm",      "unsigned",  "vector",     "vertexfragment", "void",
            "StructuredBuffer",
            "volatile",   "while",
        };

        return std::find(std::begin(kReservedIdentifiers), std::end(kReservedIdentifiers), identifier) != std::end(kReservedIdentifiers);
    }

    inline std::string sanitizeHLSLIdentifier(std::string_view identifier)
    {
        if (identifier.empty() || isAnonymousNamespaceSegment(identifier))
        {
            return {};
        }

        std::string sanitized;
        sanitized.reserve(identifier.size());
        for (char ch : identifier)
        {
            const bool isAsciiLetter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
            const bool isDigit = ch >= '0' && ch <= '9';
            if (isAsciiLetter || isDigit || ch == '_')
            {
                sanitized.push_back(ch);
            }
            else
            {
                sanitized.push_back('_');
            }
        }

        if (sanitized.empty())
        {
            return {};
        }
        if (sanitized.front() >= '0' && sanitized.front() <= '9')
        {
            sanitized.insert(sanitized.begin(), '_');
        }
        if (isReservedHLSLIdentifier(sanitized))
        {
            sanitized += "_";
        }
        return sanitized;
    }

    inline std::string makeQualifiedHLSLIdentifier(std::string_view qualifiedName, std::string_view separatorReplacement = "::")
    {
        std::string result;
        result.reserve(qualifiedName.size());

        size_t cursor = 0;
        while (cursor < qualifiedName.size())
        {
            const size_t separator = qualifiedName.find("::", cursor);
            const std::string_view part = separator == std::string_view::npos
                                              ? qualifiedName.substr(cursor)
                                              : qualifiedName.substr(cursor, separator - cursor);
            const std::string sanitizedPart = sanitizeHLSLIdentifier(part);

            if (!sanitizedPart.empty())
            {
                if (!result.empty())
                {
                    result += separatorReplacement;
                }
                result += sanitizedPart;
            }

            if (separator == std::string_view::npos)
            {
                break;
            }
            cursor = separator + 2;
        }

        return result;
    }

    inline std::string flattenQualifiedHLSLIdentifierForHelperName(std::string_view qualifiedName)
    {
        return makeQualifiedHLSLIdentifier(qualifiedName, "_");
    }
} // namespace UGLC::CodeGen::HLSL
