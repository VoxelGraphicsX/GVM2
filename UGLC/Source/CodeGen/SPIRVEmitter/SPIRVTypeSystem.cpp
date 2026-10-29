#include "SPIRVTypeSystem.hpp"

#include <algorithm>

namespace UGLC::CodeGen::SPIRVEmitter
{
    namespace
    {
        /** Returns one scalar/vector value type from structured scalar metadata. */
        UGLIR::ValueTypeDescription makeValueType(UGLIR::ScalarKind scalarKind, uint32_t vectorWidth)
        {
            return UGLIR::ValueTypeDescription{
                .scalarKind = scalarKind,
                .bitWidth = scalarKind == UGLIR::ScalarKind::Half ? 16u : scalarKind == UGLIR::ScalarKind::Bool ? 1u : 32u,
                .vectorWidth = std::max<uint32_t>(1u, vectorWidth),
            };
        }

        /** Returns the SPIR-V sampled scalar category used by an image object with this visible value type. */
        UGLIR::ScalarKind imageSampledScalarKind(const UGLIR::ValueTypeDescription &visibleValueType)
        {
            if (visibleValueType.scalarKind == UGLIR::ScalarKind::UInt)
            {
                return UGLIR::ScalarKind::UInt;
            }
            if (visibleValueType.scalarKind == UGLIR::ScalarKind::Int)
            {
                return UGLIR::ScalarKind::Int;
            }
            return UGLIR::ScalarKind::Float;
        }
    } // namespace

    std::string spvScalarTypeKey(UGLIR::ScalarKind scalarKind)
    {
        switch (scalarKind)
        {
        case UGLIR::ScalarKind::Bool:
            return "bool";
        case UGLIR::ScalarKind::Int:
            return "i32";
        case UGLIR::ScalarKind::UInt:
            return "u32";
        case UGLIR::ScalarKind::Float:
            return "f32";
        case UGLIR::ScalarKind::Half:
            return "f16";
        case UGLIR::ScalarKind::None:
            return {};
        }
        return {};
    }

    std::string spvVectorTypeKey(UGLIR::ScalarKind scalarKind, uint32_t vectorWidth)
    {
        if (vectorWidth <= 1u)
        {
            return spvScalarTypeKey(scalarKind);
        }
        return spvScalarTypeKey(scalarKind) + "x" + std::to_string(vectorWidth);
    }

    std::string spvValueTypeKey(const UGLIR::ValueTypeDescription &valueType)
    {
        if (valueType.matrixColumns != 0u && valueType.matrixRows != 0u)
        {
            return spvScalarTypeKey(valueType.scalarKind) + std::to_string(valueType.matrixColumns) + "x" + std::to_string(valueType.matrixRows);
        }
        return spvVectorTypeKey(valueType.scalarKind, spvVectorWidth(valueType));
    }

    std::string spvScalarTypeKey(const UGLIR::ValueTypeDescription &valueType)
    {
        return spvScalarTypeKey(valueType.scalarKind);
    }

    uint32_t spvVectorWidth(const UGLIR::ValueTypeDescription &valueType)
    {
        return std::max<uint32_t>(1u, valueType.vectorWidth);
    }

    std::optional<UGLIR::ValueTypeDescription> textureFormatValueType(UGLIR::TextureFormat textureFormat)
    {
        switch (textureFormat)
        {
        case UGLIR::TextureFormat::R8Uint:
        case UGLIR::TextureFormat::R16Uint:
        case UGLIR::TextureFormat::R32Uint:
            return makeValueType(UGLIR::ScalarKind::UInt, 1u);
        case UGLIR::TextureFormat::R8Sint:
        case UGLIR::TextureFormat::R16Sint:
        case UGLIR::TextureFormat::R32Sint:
            return makeValueType(UGLIR::ScalarKind::Int, 1u);
        case UGLIR::TextureFormat::R8Unorm:
        case UGLIR::TextureFormat::R8Snorm:
        case UGLIR::TextureFormat::R16Unorm:
        case UGLIR::TextureFormat::R16Snorm:
        case UGLIR::TextureFormat::R16Float:
        case UGLIR::TextureFormat::R32Float:
        case UGLIR::TextureFormat::Depth16Unorm:
        case UGLIR::TextureFormat::Depth32Float:
            return makeValueType(UGLIR::ScalarKind::Float, 1u);
        case UGLIR::TextureFormat::RG8Uint:
        case UGLIR::TextureFormat::RG16Uint:
        case UGLIR::TextureFormat::RG32Uint:
            return makeValueType(UGLIR::ScalarKind::UInt, 2u);
        case UGLIR::TextureFormat::RG8Sint:
        case UGLIR::TextureFormat::RG16Sint:
        case UGLIR::TextureFormat::RG32Sint:
            return makeValueType(UGLIR::ScalarKind::Int, 2u);
        case UGLIR::TextureFormat::RG8Unorm:
        case UGLIR::TextureFormat::RG8Snorm:
        case UGLIR::TextureFormat::RG16Unorm:
        case UGLIR::TextureFormat::RG16Snorm:
        case UGLIR::TextureFormat::RG16Float:
        case UGLIR::TextureFormat::RG32Float:
            return makeValueType(UGLIR::ScalarKind::Float, 2u);
        case UGLIR::TextureFormat::RGBA8Uint:
        case UGLIR::TextureFormat::RGBA16Uint:
        case UGLIR::TextureFormat::RGBA32Uint:
        case UGLIR::TextureFormat::RGB10A2Uint:
            return makeValueType(UGLIR::ScalarKind::UInt, 4u);
        case UGLIR::TextureFormat::RGBA8Sint:
        case UGLIR::TextureFormat::RGBA16Sint:
        case UGLIR::TextureFormat::RGBA32Sint:
            return makeValueType(UGLIR::ScalarKind::Int, 4u);
        case UGLIR::TextureFormat::RGBA8Unorm:
        case UGLIR::TextureFormat::RGBA8UnormSrgb:
        case UGLIR::TextureFormat::RGBA8Snorm:
        case UGLIR::TextureFormat::BGRA8Unorm:
        case UGLIR::TextureFormat::BGRA8UnormSrgb:
        case UGLIR::TextureFormat::RGBA16Float:
        case UGLIR::TextureFormat::RGBA32Float:
        case UGLIR::TextureFormat::RGB10A2Unorm:
        case UGLIR::TextureFormat::RG11B10Ufloat:
        case UGLIR::TextureFormat::RGB9E5Ufloat:
        case UGLIR::TextureFormat::ASTC4x4Unorm:
        case UGLIR::TextureFormat::PreferredSwapchain:
            return makeValueType(UGLIR::ScalarKind::Float, 4u);
        case UGLIR::TextureFormat::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<SPIRVImagePayloadType> imagePayloadType(const UGLIR::ResourceBinding &resource,
                                                         const std::optional<UGLIR::ValueTypeDescription> &elementValueType,
                                                         const std::optional<UGLIR::ValueTypeDescription> &expressionValueType)
    {
        std::optional<UGLIR::ValueTypeDescription> visibleValueType;
        if (resource.textureFormat != UGLIR::TextureFormat::Unknown)
        {
            visibleValueType = textureFormatValueType(resource.textureFormat);
        }
        else if (elementValueType.has_value() && elementValueType->scalarKind != UGLIR::ScalarKind::None)
        {
            visibleValueType = elementValueType;
        }

        if (!visibleValueType.has_value())
        {
            return std::nullopt;
        }

        if (expressionValueType.has_value() &&
            expressionValueType->scalarKind != UGLIR::ScalarKind::None &&
            spvVectorWidth(*expressionValueType) > spvVectorWidth(*visibleValueType))
        {
            visibleValueType->vectorWidth = spvVectorWidth(*expressionValueType);
        }

        const UGLIR::ScalarKind sampledScalarKind = imageSampledScalarKind(*visibleValueType);
        const UGLIR::ValueTypeDescription instructionTexelType = makeValueType(sampledScalarKind, 4u);
        return SPIRVImagePayloadType{
            .visibleValueType = *visibleValueType,
            .instructionTexelType = instructionTexelType,
            .sampledScalarKind = sampledScalarKind,
            .visibleWidth = spvVectorWidth(*visibleValueType),
            .instructionTexelWidth = spvVectorWidth(instructionTexelType),
        };
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
