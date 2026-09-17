#include "UGLIRTypeUtils.hpp"

#include <algorithm>
#include <cstddef>

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Removes common qualifiers and the public UGL namespace from a type spelling. */
        std::string cleanTypeName(std::string typeName)
        {
            while (!typeName.empty() && typeName.back() == '&')
            {
                typeName.pop_back();
            }
            if (typeName.rfind("const ", 0) == 0)
            {
                typeName = typeName.substr(6);
            }
            if (typeName.rfind("volatile ", 0) == 0)
            {
                typeName = typeName.substr(9);
            }
            if (typeName.rfind("UGL::", 0) == 0)
            {
                typeName = typeName.substr(5);
            }
            return typeName;
        }

        /** Returns the scalar kind encoded by one exact compact scalar alias. */
        ScalarKind scalarKindFromCompactScalarAlias(const std::string &cleanedName)
        {
            if (cleanedName == "bool")
            {
                return ScalarKind::Bool;
            }
            if (cleanedName == "i32" || cleanedName == "int" || cleanedName == "int32_t")
            {
                return ScalarKind::Int;
            }
            if (cleanedName == "u32" || cleanedName == "uint" || cleanedName == "uint32_t" || cleanedName == "unsigned int")
            {
                return ScalarKind::UInt;
            }
            if (cleanedName == "f32" || cleanedName == "float" || cleanedName == "float_t")
            {
                return ScalarKind::Float;
            }
            if (cleanedName == "f16" || cleanedName == "half" || cleanedName == "half_t")
            {
                return ScalarKind::Half;
            }
            return ScalarKind::None;
        }

        /** Returns the scalar token and vector width encoded by one exact compact vector alias. */
        bool parseCompactVectorAlias(const std::string &cleanedName, std::string &scalarToken, uint32_t &width)
        {
            if (cleanedName.size() < 2)
            {
                return false;
            }
            const char suffix = cleanedName.back();
            if (suffix < '2' || suffix > '4')
            {
                return false;
            }
            scalarToken = cleanedName.substr(0, cleanedName.size() - 1);
            if (scalarKindFromCompactScalarAlias(scalarToken) == ScalarKind::None)
            {
                return false;
            }
            width = static_cast<uint32_t>(suffix - '0');
            return true;
        }

        /** Returns the scalar kind encoded by one compact scalar or vector alias. */
        ScalarKind scalarKindFromAlias(const std::string &typeName)
        {
            const std::string cleanedName = cleanTypeName(typeName);
            if (const ScalarKind scalarKind = scalarKindFromCompactScalarAlias(cleanedName); scalarKind != ScalarKind::None)
            {
                return scalarKind;
            }
            std::string scalarToken;
            uint32_t width = 1;
            return parseCompactVectorAlias(cleanedName, scalarToken, width) ? scalarKindFromCompactScalarAlias(scalarToken) : ScalarKind::None;
        }

        /** Returns the vector width encoded by a compact vector alias. */
        uint32_t vectorWidthFromAlias(const std::string &typeName)
        {
            const std::string cleanedName = cleanTypeName(typeName);
            std::string scalarToken;
            uint32_t width = 1;
            return parseCompactVectorAlias(cleanedName, scalarToken, width) ? width : 1;
        }

        /** Parses an indexed builtin semantic token such as Attribute3. */
        bool parseIndexedSemanticToken(const std::string &token, const std::string &prefix, uint32_t &index)
        {
            if (token.rfind(prefix, 0) != 0 || token.size() == prefix.size())
            {
                return false;
            }
            uint32_t value = 0;
            for (size_t characterIndex = prefix.size(); characterIndex < token.size(); ++characterIndex)
            {
                const char character = token[characterIndex];
                if (character < '0' || character > '9')
                {
                    return false;
                }
                value = value * 10u + static_cast<uint32_t>(character - '0');
            }
            index = value;
            return true;
        }

        /** Returns the byte width for one scalar lane category. */
        uint32_t scalarByteSize(ScalarKind kind, uint32_t bitWidth)
        {
            if (bitWidth != 0)
            {
                return std::max<uint32_t>(1, bitWidth / 8);
            }
            switch (kind)
            {
            case ScalarKind::Bool:
            case ScalarKind::Int:
            case ScalarKind::UInt:
            case ScalarKind::Float:
                return 4;
            case ScalarKind::Half:
                return 2;
            case ScalarKind::None:
                return 0;
            }
            return 0;
        }

        /** Stores a bidirectional mapping between a public UGL texture format token and its enum value. */
        struct TextureFormatTokenMapping
        {
            const char *token;
            TextureFormat format;
        };

        /** Returns the complete texture format mapping table used by UGLIR lowering and emitters. */
        const TextureFormatTokenMapping *textureFormatMappings(size_t &count)
        {
            static constexpr TextureFormatTokenMapping kMappings[] = {
                {"R8Unorm", TextureFormat::R8Unorm},
                {"R8Snorm", TextureFormat::R8Snorm},
                {"R8Uint", TextureFormat::R8Uint},
                {"R8Sint", TextureFormat::R8Sint},
                {"R16Unorm", TextureFormat::R16Unorm},
                {"R16Snorm", TextureFormat::R16Snorm},
                {"R16Uint", TextureFormat::R16Uint},
                {"R16Sint", TextureFormat::R16Sint},
                {"R16Float", TextureFormat::R16Float},
                {"R32Float", TextureFormat::R32Float},
                {"R32Uint", TextureFormat::R32Uint},
                {"R32Sint", TextureFormat::R32Sint},
                {"RG8Unorm", TextureFormat::RG8Unorm},
                {"RG8Snorm", TextureFormat::RG8Snorm},
                {"RG8Uint", TextureFormat::RG8Uint},
                {"RG8Sint", TextureFormat::RG8Sint},
                {"RG16Unorm", TextureFormat::RG16Unorm},
                {"RG16Snorm", TextureFormat::RG16Snorm},
                {"RG16Uint", TextureFormat::RG16Uint},
                {"RG16Sint", TextureFormat::RG16Sint},
                {"RG16Float", TextureFormat::RG16Float},
                {"RG32Float", TextureFormat::RG32Float},
                {"RG32Uint", TextureFormat::RG32Uint},
                {"RG32Sint", TextureFormat::RG32Sint},
                {"RG11B10Ufloat", TextureFormat::RG11B10Ufloat},
                {"RGB9E5Ufloat", TextureFormat::RGB9E5Ufloat},
                {"RGB10A2Uint", TextureFormat::RGB10A2Uint},
                {"RGB10A2Unorm", TextureFormat::RGB10A2Unorm},
                {"RGBA8Unorm", TextureFormat::RGBA8Unorm},
                {"RGBA8UnormSrgb", TextureFormat::RGBA8UnormSrgb},
                {"RGBA8Unorm_sRGB", TextureFormat::RGBA8UnormSrgb},
                {"RGBA8Snorm", TextureFormat::RGBA8Snorm},
                {"RGBA8Uint", TextureFormat::RGBA8Uint},
                {"RGBA8Sint", TextureFormat::RGBA8Sint},
                {"BGRA8Unorm", TextureFormat::BGRA8Unorm},
                {"BGRA8UnormSrgb", TextureFormat::BGRA8UnormSrgb},
                {"BGRA8Unorm_sRGB", TextureFormat::BGRA8UnormSrgb},
                {"RGBA16Uint", TextureFormat::RGBA16Uint},
                {"RGBA16Sint", TextureFormat::RGBA16Sint},
                {"RGBA16Float", TextureFormat::RGBA16Float},
                {"RGBA32Float", TextureFormat::RGBA32Float},
                {"RGBA32Uint", TextureFormat::RGBA32Uint},
                {"RGBA32Sint", TextureFormat::RGBA32Sint},
                {"ASTC4x4Unorm", TextureFormat::ASTC4x4Unorm},
                {"Depth16Unorm", TextureFormat::Depth16Unorm},
                {"Depth32Float", TextureFormat::Depth32Float},
                {"PreferredSwapchain", TextureFormat::PreferredSwapchain},
            };
            count = sizeof(kMappings) / sizeof(kMappings[0]);
            return kMappings;
        }
    } // namespace

    const char *toString(ScalarKind kind)
    {
        switch (kind)
        {
        case ScalarKind::None:
            return "none";
        case ScalarKind::Bool:
            return "bool";
        case ScalarKind::Int:
            return "int";
        case ScalarKind::UInt:
            return "uint";
        case ScalarKind::Float:
            return "float";
        case ScalarKind::Half:
            return "half";
        }
        return "unknown";
    }

    const char *toString(BuiltinSemanticKind kind)
    {
        switch (kind)
        {
        case BuiltinSemanticKind::None:
            return "";
        case BuiltinSemanticKind::DispatchThreadID:
            return "DispatchThreadID";
        case BuiltinSemanticKind::GroupThreadID:
            return "GroupThreadID";
        case BuiltinSemanticKind::GroupID:
            return "GroupID";
        case BuiltinSemanticKind::GroupIndex:
            return "GroupIndex";
        case BuiltinSemanticKind::VertexID:
            return "VertexID";
        case BuiltinSemanticKind::InstanceID:
            return "InstanceID";
        case BuiltinSemanticKind::PrimitiveID:
            return "PrimitiveID";
        case BuiltinSemanticKind::PixelCoord:
            return "PixelCoord";
        case BuiltinSemanticKind::SampleIndex:
            return "SampleIndex";
        case BuiltinSemanticKind::Barycentrics:
            return "Barycentrics";
        case BuiltinSemanticKind::StageInput:
            return "StageInput";
        case BuiltinSemanticKind::VertexInput:
            return "VertexInput";
        case BuiltinSemanticKind::PixelLocalInput:
            return "PixelLocalInput";
        case BuiltinSemanticKind::Position:
            return "Position";
        case BuiltinSemanticKind::Attribute:
            return "Attribute";
        case BuiltinSemanticKind::Color:
            return "Color";
        case BuiltinSemanticKind::PixelLocalColor:
            return "PixelLocalColor";
        case BuiltinSemanticKind::Depth:
            return "Depth";
        case BuiltinSemanticKind::PixelLocalDepth:
            return "PixelLocalDepth";
        case BuiltinSemanticKind::Field:
            return "Field";
        case BuiltinSemanticKind::DrawEntityID:
            return "DrawEntityID";
        case BuiltinSemanticKind::DrawEntityInstanceID:
            return "DrawEntityInstanceID";
        }
        return "unknown";
    }

    const char *toString(ParameterPassingMode mode)
    {
        switch (mode)
        {
        case ParameterPassingMode::Value:
            return "value";
        case ParameterPassingMode::In:
            return "in";
        case ParameterPassingMode::Out:
            return "out";
        case ParameterPassingMode::InOut:
            return "inout";
        }
        return "unknown";
    }

    const char *toString(ResourceRole role)
    {
        switch (role)
        {
        case ResourceRole::None:
            return "none";
        case ResourceRole::AccessBounds:
            return "access_bounds";
        case ResourceRole::BufferIndexTable:
            return "buffer_index_table";
        case ResourceRole::TextureIndexTable:
            return "texture_index_table";
        case ResourceRole::BufferValue:
            return "buffer_value";
        case ResourceRole::TextureValue:
            return "texture_value";
        case ResourceRole::DrawInfo:
            return "draw_info";
        case ResourceRole::CommandParams:
            return "command_params";
        case ResourceRole::PixelLocalInput:
            return "pixel_local_input";
        }
        return "unknown";
    }

    const char *toString(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::None:
            return "none";
        case TextureDimension::Texture2D:
            return "2d";
        case TextureDimension::Texture2DArray:
            return "2d_array";
        case TextureDimension::Texture3D:
            return "3d";
        case TextureDimension::Subpass:
            return "subpass";
        }
        return "unknown";
    }

    const char *toString(TextureFormat format)
    {
        if (format == TextureFormat::Unknown)
        {
            return "unknown";
        }
        size_t count = 0;
        const TextureFormatTokenMapping *mappings = textureFormatMappings(count);
        for (size_t index = 0; index < count; ++index)
        {
            if (format == mappings[index].format)
            {
                return mappings[index].token;
            }
        }
        return "unknown";
    }

    TextureDimension textureDimensionFromToken(const std::string &token)
    {
        if (token == "2d")
        {
            return TextureDimension::Texture2D;
        }
        if (token == "2d_array")
        {
            return TextureDimension::Texture2DArray;
        }
        if (token == "3d")
        {
            return TextureDimension::Texture3D;
        }
        if (token == "subpass")
        {
            return TextureDimension::Subpass;
        }
        return TextureDimension::None;
    }

    TextureFormat textureFormatFromToken(const std::string &token)
    {
        size_t count = 0;
        const TextureFormatTokenMapping *mappings = textureFormatMappings(count);
        for (size_t index = 0; index < count; ++index)
        {
            if (token == mappings[index].token)
            {
                return mappings[index].format;
            }
        }
        return TextureFormat::Unknown;
    }

    std::string textureFormatToken(TextureFormat format)
    {
        if (format == TextureFormat::Unknown)
        {
            return {};
        }
        size_t count = 0;
        const TextureFormatTokenMapping *mappings = textureFormatMappings(count);
        for (size_t index = 0; index < count; ++index)
        {
            if (format == mappings[index].format)
            {
                return mappings[index].token;
            }
        }
        return {};
    }

    std::string textureFormatValueTypeName(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::R8Uint:
        case TextureFormat::R16Uint:
        case TextureFormat::R32Uint:
            return "uint";
        case TextureFormat::R8Sint:
        case TextureFormat::R16Sint:
        case TextureFormat::R32Sint:
            return "int";
        case TextureFormat::RG8Uint:
        case TextureFormat::RG16Uint:
        case TextureFormat::RG32Uint:
            return "uint2";
        case TextureFormat::RG8Sint:
        case TextureFormat::RG16Sint:
        case TextureFormat::RG32Sint:
            return "int2";
        case TextureFormat::RGBA8Uint:
        case TextureFormat::RGBA16Uint:
        case TextureFormat::RGBA32Uint:
            return "uint4";
        case TextureFormat::RGBA8Sint:
        case TextureFormat::RGBA16Sint:
        case TextureFormat::RGBA32Sint:
            return "int4";
        case TextureFormat::R8Unorm:
        case TextureFormat::R8Snorm:
        case TextureFormat::R16Unorm:
        case TextureFormat::R16Snorm:
        case TextureFormat::R16Float:
        case TextureFormat::R32Float:
        case TextureFormat::Depth16Unorm:
        case TextureFormat::Depth32Float:
            return "float";
        case TextureFormat::RG8Unorm:
        case TextureFormat::RG8Snorm:
        case TextureFormat::RG16Unorm:
        case TextureFormat::RG16Snorm:
        case TextureFormat::RG16Float:
        case TextureFormat::RG32Float:
            return "float2";
        case TextureFormat::RG11B10Ufloat:
            return "float3";
        case TextureFormat::Unknown:
        case TextureFormat::RGB9E5Ufloat:
        case TextureFormat::RGB10A2Uint:
        case TextureFormat::RGB10A2Unorm:
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::RGBA8Snorm:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::RGBA16Float:
        case TextureFormat::RGBA32Float:
        case TextureFormat::ASTC4x4Unorm:
        case TextureFormat::PreferredSwapchain:
            return "float4";
        }
        return "float4";
    }

    std::string framebufferTextureFormatValueTypeName(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::R8Uint:
        case TextureFormat::R16Uint:
        case TextureFormat::R32Uint:
            return "uint";
        case TextureFormat::R8Sint:
        case TextureFormat::R16Sint:
        case TextureFormat::R32Sint:
            return "int";
        case TextureFormat::R8Unorm:
        case TextureFormat::R8Snorm:
        case TextureFormat::R16Unorm:
        case TextureFormat::R16Snorm:
        case TextureFormat::R16Float:
        case TextureFormat::Depth16Unorm:
            return "half";
        case TextureFormat::R32Float:
        case TextureFormat::Depth32Float:
            return "float";
        case TextureFormat::RG8Uint:
        case TextureFormat::RG16Uint:
        case TextureFormat::RG32Uint:
            return "uint2";
        case TextureFormat::RG8Sint:
        case TextureFormat::RG16Sint:
        case TextureFormat::RG32Sint:
            return "int2";
        case TextureFormat::RG8Unorm:
        case TextureFormat::RG8Snorm:
        case TextureFormat::RG16Unorm:
        case TextureFormat::RG16Snorm:
        case TextureFormat::RG16Float:
            return "half2";
        case TextureFormat::RG32Float:
            return "float2";
        case TextureFormat::RG11B10Ufloat:
            return "half3";
        case TextureFormat::RGB10A2Uint:
            return "uint2";
        case TextureFormat::RGBA8Uint:
        case TextureFormat::RGBA16Uint:
        case TextureFormat::RGBA32Uint:
            return "uint4";
        case TextureFormat::RGBA8Sint:
        case TextureFormat::RGBA16Sint:
        case TextureFormat::RGBA32Sint:
            return "int4";
        case TextureFormat::Unknown:
        case TextureFormat::RGB9E5Ufloat:
        case TextureFormat::RGB10A2Unorm:
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::RGBA8Snorm:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::PreferredSwapchain:
        case TextureFormat::ASTC4x4Unorm:
        case TextureFormat::RGBA16Float:
            return "half4";
        case TextureFormat::RGBA32Float:
            return "float4";
        }
        return "float4";
    }

    bool isDepthTextureFormat(TextureFormat format)
    {
        return format == TextureFormat::Depth16Unorm || format == TextureFormat::Depth32Float;
    }

    std::string semanticDisplayName(BuiltinSemanticKind kind, uint32_t index)
    {
        switch (kind)
        {
        case BuiltinSemanticKind::VertexInput:
        case BuiltinSemanticKind::Attribute:
            return std::string(toString(kind)) + std::to_string(index);
        case BuiltinSemanticKind::None:
            return {};
        default:
            return toString(kind);
        }
    }

    BuiltinSemantic builtinSemanticFromToken(const std::string &token)
    {
        BuiltinSemantic result;
        if (token.empty())
        {
            return result;
        }

        uint32_t semanticIndex = 0;
        if (parseIndexedSemanticToken(token, "VertexInput", semanticIndex))
        {
            result.kind = BuiltinSemanticKind::VertexInput;
            result.index = semanticIndex;
            return result;
        }
        if (parseIndexedSemanticToken(token, "Attribute", semanticIndex))
        {
            result.kind = BuiltinSemanticKind::Attribute;
            result.index = semanticIndex;
            return result;
        }

        struct SemanticTokenMapping
        {
            const char *token;
            BuiltinSemanticKind kind;
        };
        static constexpr SemanticTokenMapping kMappings[] = {
            {"DispatchThreadID", BuiltinSemanticKind::DispatchThreadID},
            {"GroupThreadID", BuiltinSemanticKind::GroupThreadID},
            {"GroupID", BuiltinSemanticKind::GroupID},
            {"GroupIndex", BuiltinSemanticKind::GroupIndex},
            {"VertexID", BuiltinSemanticKind::VertexID},
            {"InstanceID", BuiltinSemanticKind::InstanceID},
            {"PrimitiveID", BuiltinSemanticKind::PrimitiveID},
            {"PixelCoord", BuiltinSemanticKind::PixelCoord},
            {"SampleIndex", BuiltinSemanticKind::SampleIndex},
            {"Barycentrics", BuiltinSemanticKind::Barycentrics},
            {"StageInput", BuiltinSemanticKind::StageInput},
            {"PixelLocalInput", BuiltinSemanticKind::PixelLocalInput},
            {"Position", BuiltinSemanticKind::Position},
            {"Color", BuiltinSemanticKind::Color},
            {"PixelLocalColor", BuiltinSemanticKind::PixelLocalColor},
            {"Depth", BuiltinSemanticKind::Depth},
            {"PixelLocalDepth", BuiltinSemanticKind::PixelLocalDepth},
            {"Field", BuiltinSemanticKind::Field},
            {"DrawEntityID", BuiltinSemanticKind::DrawEntityID},
            {"DrawEntityInstanceID", BuiltinSemanticKind::DrawEntityInstanceID},
        };
        for (const SemanticTokenMapping &mapping : kMappings)
        {
            if (token == mapping.token)
            {
                result.kind = mapping.kind;
                return result;
            }
        }
        return result;
    }

    ParameterPassingMode parameterPassingModeFromToken(const std::string &token)
    {
        if (token == "IN")
        {
            return ParameterPassingMode::In;
        }
        if (token == "OUT")
        {
            return ParameterPassingMode::Out;
        }
        if (token == "INOUT")
        {
            return ParameterPassingMode::InOut;
        }
        return ParameterPassingMode::Value;
    }

    const Type *findTypeByName(const Module &module, const std::string &typeName)
    {
        const auto iter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
            return type.name == typeName;
        });
        return iter == module.types.end() ? nullptr : &*iter;
    }

    ScalarKind scalarKindFromTypeName(const std::string &typeName)
    {
        return scalarKindFromAlias(typeName);
    }

    std::string scalarTypeName(ScalarKind kind)
    {
        switch (kind)
        {
        case ScalarKind::Bool:
            return "bool";
        case ScalarKind::Int:
            return "int";
        case ScalarKind::UInt:
            return "uint";
        case ScalarKind::Float:
            return "float";
        case ScalarKind::Half:
            return "half";
        case ScalarKind::None:
            return {};
        }
        return {};
    }

    std::string makeVectorTypeName(ScalarKind kind, uint32_t width)
    {
        const std::string scalarName = scalarTypeName(kind);
        if (scalarName.empty())
        {
            return {};
        }
        if (width <= 1)
        {
            return scalarName;
        }
        return scalarName + std::to_string(width);
    }

    std::optional<ValueTypeDescription> describeRegisteredValueType(const Module &module, const std::string &typeName)
    {
        if (const Type *type = findTypeByName(module, typeName))
        {
            switch (type->kind)
            {
            case TypeKind::Bool:
            case TypeKind::Int:
            case TypeKind::UInt:
            case TypeKind::Float:
            case TypeKind::Half:
                return ValueTypeDescription{
                    .scalarKind = type->scalarKind,
                    .bitWidth = type->bitWidth,
                    .vectorWidth = 1,
                };
            case TypeKind::Vector:
            {
                ScalarKind scalarKind = type->scalarKind;
                uint32_t bitWidth = type->bitWidth;
                if (scalarKind == ScalarKind::None && !type->elementType.empty())
                {
                    const std::optional<ValueTypeDescription> elementType = describeRegisteredValueType(module, type->elementType);
                    if (elementType.has_value())
                    {
                        scalarKind = elementType->scalarKind;
                        bitWidth = bitWidth == 0 ? elementType->bitWidth : bitWidth;
                    }
                }
                if (scalarKind == ScalarKind::None || type->vectorWidth == 0)
                {
                    return std::nullopt;
                }
                return ValueTypeDescription{
                    .scalarKind = scalarKind,
                    .bitWidth = bitWidth,
                    .vectorWidth = type->vectorWidth,
                };
            }
            case TypeKind::Matrix:
            {
                ScalarKind scalarKind = type->scalarKind;
                uint32_t bitWidth = type->bitWidth;
                if (scalarKind == ScalarKind::None && !type->elementType.empty())
                {
                    const std::optional<ValueTypeDescription> elementType = describeRegisteredValueType(module, type->elementType);
                    if (elementType.has_value())
                    {
                        scalarKind = elementType->scalarKind;
                        bitWidth = bitWidth == 0 ? elementType->bitWidth : bitWidth;
                    }
                }
                if (scalarKind == ScalarKind::None || type->matrixColumns == 0 || type->matrixRows == 0)
                {
                    return std::nullopt;
                }
                return ValueTypeDescription{
                    .scalarKind = scalarKind,
                    .bitWidth = bitWidth,
                    .vectorWidth = type->matrixColumns,
                    .matrixColumns = type->matrixColumns,
                    .matrixRows = type->matrixRows,
                };
            }
            default:
                break;
            }
        }
        return std::nullopt;
    }

    std::optional<ValueTypeDescription> describeValueType(const Module &module, const std::string &typeName)
    {
        if (const std::optional<ValueTypeDescription> registeredType = describeRegisteredValueType(module, typeName))
        {
            return registeredType;
        }
        const ScalarKind kind = scalarKindFromTypeName(typeName);
        if (kind == ScalarKind::None)
        {
            return std::nullopt;
        }
        return ValueTypeDescription{
            .scalarKind = kind,
            .bitWidth = kind == ScalarKind::Half ? 16u : kind == ScalarKind::Bool ? 1u : 32u,
            .vectorWidth = vectorWidthFromAlias(typeName),
        };
    }

    namespace
    {
        /** Returns the canonical UGLIR type name for builtin scalar aliases. */
        std::string canonicalBuiltinTypeReference(const std::string &typeName)
        {
            const std::string cleanedName = cleanTypeName(typeName);
            if (cleanedName == "int")
            {
                return "i32";
            }
            if (cleanedName == "uint")
            {
                return "u32";
            }
            if (cleanedName == "float")
            {
                return "f32";
            }
            if (cleanedName == "half")
            {
                return "f16";
            }
            return typeName;
        }

        /** Rewrites a builtin scalar type record to its canonical UGLIR metadata form. */
        void normalizeBuiltinTypeRecord(Type &type)
        {
            const std::string canonicalName = canonicalBuiltinTypeReference(type.name);
            if (canonicalName == "bool")
            {
                type.name = canonicalName;
                type.kind = TypeKind::Bool;
                type.scalarKind = ScalarKind::Bool;
                type.bitWidth = 1;
                type.elementType.clear();
                type.vectorWidth = 0;
                type.matrixColumns = 0;
                type.matrixRows = 0;
                type.fields.clear();
                return;
            }
            if (canonicalName == "i32")
            {
                type.name = canonicalName;
                type.kind = TypeKind::Int;
                type.scalarKind = ScalarKind::Int;
                type.bitWidth = 32;
                type.elementType.clear();
                type.vectorWidth = 0;
                type.matrixColumns = 0;
                type.matrixRows = 0;
                type.fields.clear();
                return;
            }
            if (canonicalName == "u32")
            {
                type.name = canonicalName;
                type.kind = TypeKind::UInt;
                type.scalarKind = ScalarKind::UInt;
                type.bitWidth = 32;
                type.elementType.clear();
                type.vectorWidth = 0;
                type.matrixColumns = 0;
                type.matrixRows = 0;
                type.fields.clear();
                return;
            }
            if (canonicalName == "f32")
            {
                type.name = canonicalName;
                type.kind = TypeKind::Float;
                type.scalarKind = ScalarKind::Float;
                type.bitWidth = 32;
                type.elementType.clear();
                type.vectorWidth = 0;
                type.matrixColumns = 0;
                type.matrixRows = 0;
                type.fields.clear();
                return;
            }
            if (canonicalName == "f16")
            {
                type.name = canonicalName;
                type.kind = TypeKind::Half;
                type.scalarKind = ScalarKind::Half;
                type.bitWidth = 16;
                type.elementType.clear();
                type.vectorWidth = 0;
                type.matrixColumns = 0;
                type.matrixRows = 0;
                type.fields.clear();
            }
        }

        /** Returns true when a later normalized type record should replace an earlier duplicate. */
        bool shouldReplaceNormalizedType(const Type &existingType, const Type &candidateType)
        {
            if (existingType.kind == TypeKind::Struct && existingType.fields.empty() && !candidateType.fields.empty())
            {
                return true;
            }
            if (existingType.scalarKind == ScalarKind::None && candidateType.scalarKind != ScalarKind::None)
            {
                return true;
            }
            return false;
        }

        /** Rewrites and deduplicates the module type table after builtin alias canonicalization. */
        void normalizeTypeTableBuiltinReferences(Module &module)
        {
            std::vector<Type> normalizedTypes;
            normalizedTypes.reserve(module.types.size());
            for (Type type : module.types)
            {
                normalizeBuiltinTypeRecord(type);
                type.elementType = canonicalBuiltinTypeReference(type.elementType);
                for (TypeField &field : type.fields)
                {
                    field.type = canonicalBuiltinTypeReference(field.type);
                }

                auto existingType = std::find_if(normalizedTypes.begin(), normalizedTypes.end(), [&type](const Type &candidate) {
                    return candidate.name == type.name;
                });
                if (existingType == normalizedTypes.end())
                {
                    normalizedTypes.push_back(std::move(type));
                    continue;
                }
                if (shouldReplaceNormalizedType(*existingType, type))
                {
                    *existingType = std::move(type);
                }
            }
            module.types = std::move(normalizedTypes);
        }

        /** Rewrites one type reference to its canonical builtin UGLIR name when needed. */
        void normalizeBuiltinTypeReference(std::string &typeName)
        {
            typeName = canonicalBuiltinTypeReference(typeName);
        }

        /** Rewrites builtin scalar type references inside one expression tree. */
        void normalizeExpressionBuiltinTypeReferences(Expression &expression)
        {
            normalizeBuiltinTypeReference(expression.type);
            normalizeBuiltinTypeReference(expression.computationType);
            for (Expression &operand : expression.operands)
            {
                normalizeExpressionBuiltinTypeReferences(operand);
            }
        }

        /** Rewrites builtin scalar type references inside one statement tree. */
        void normalizeStatementBuiltinTypeReferences(Statement &statement)
        {
            normalizeBuiltinTypeReference(statement.type);
            for (Expression &expression : statement.expressions)
            {
                normalizeExpressionBuiltinTypeReferences(expression);
            }
            for (Statement &child : statement.children)
            {
                normalizeStatementBuiltinTypeReferences(child);
            }
            for (Statement &child : statement.elseChildren)
            {
                normalizeStatementBuiltinTypeReferences(child);
            }
            for (SwitchCase &switchCase : statement.switchCases)
            {
                for (Expression &label : switchCase.labels)
                {
                    normalizeExpressionBuiltinTypeReferences(label);
                }
                for (Statement &caseStatement : switchCase.body)
                {
                    normalizeStatementBuiltinTypeReferences(caseStatement);
                }
            }
        }

        /** Rewrites builtin scalar type references inside one lowered function. */
        void normalizeFunctionBuiltinTypeReferences(Function &function)
        {
            normalizeBuiltinTypeReference(function.returnType);
            for (FunctionParameter &parameter : function.parameters)
            {
                normalizeBuiltinTypeReference(parameter.type);
            }
            for (Statement &statement : function.body)
            {
                normalizeStatementBuiltinTypeReferences(statement);
            }
        }
    } // namespace

    void normalizeBuiltinTypeReferences(Module &module)
    {
        normalizeTypeTableBuiltinReferences(module);
        for (StageIOBinding &stageInput : module.reflection.stageInputs)
        {
            normalizeBuiltinTypeReference(stageInput.type);
        }
        for (StageIOBinding &stageOutput : module.reflection.stageOutputs)
        {
            normalizeBuiltinTypeReference(stageOutput.type);
        }
        for (ResourceBinding &resource : module.reflection.resources)
        {
            normalizeBuiltinTypeReference(resource.elementType);
        }
        for (Function &function : module.functions)
        {
            normalizeFunctionBuiltinTypeReferences(function);
        }
    }

    bool isScalarOrVectorType(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        return valueType.has_value() && valueType->scalarKind != ScalarKind::None && valueType->matrixColumns == 0;
    }

    uint32_t vectorWidth(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        return valueType.has_value() ? std::max<uint32_t>(1, valueType->vectorWidth) : 1;
    }

    ScalarKind scalarKind(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        return valueType.has_value() ? valueType->scalarKind : ScalarKind::None;
    }

    std::string canonicalValueTypeName(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        if (!valueType.has_value())
        {
            return typeName;
        }
        return makeVectorTypeName(valueType->scalarKind, valueType->vectorWidth);
    }

    uint32_t byteSize(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        if (!valueType.has_value())
        {
            return 0;
        }
        const uint32_t laneSize = scalarByteSize(valueType->scalarKind, valueType->bitWidth);
        if (valueType->matrixColumns != 0 && valueType->matrixRows != 0)
        {
            return laneSize * valueType->matrixColumns * valueType->matrixRows;
        }
        return laneSize * std::max<uint32_t>(1, valueType->vectorWidth);
    }

    uint32_t alignment(const Module &module, const std::string &typeName)
    {
        const std::optional<ValueTypeDescription> valueType = describeValueType(module, typeName);
        if (!valueType.has_value())
        {
            return 0;
        }
        const uint32_t laneSize = scalarByteSize(valueType->scalarKind, valueType->bitWidth);
        if (valueType->vectorWidth >= 3)
        {
            return laneSize * 4;
        }
        return laneSize * std::max<uint32_t>(1, valueType->vectorWidth);
    }
} // namespace UGLC::CodeGen::UGLIR
