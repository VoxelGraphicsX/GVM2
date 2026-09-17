#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Describes the structured value types used at SPIR-V image instruction boundaries. */
    struct SPIRVImagePayloadType
    {
        UGLIR::ValueTypeDescription visibleValueType;
        UGLIR::ValueTypeDescription instructionTexelType;
        UGLIR::ScalarKind sampledScalarKind = UGLIR::ScalarKind::Float;
        uint32_t visibleWidth = 4;
        uint32_t instructionTexelWidth = 4;
    };

    /** Returns the compact SPIR-V backend scalar key for one structured scalar category. */
    std::string spvScalarTypeKey(UGLIR::ScalarKind scalarKind);

    /** Returns the compact SPIR-V backend vector key for one structured scalar category and lane count. */
    std::string spvVectorTypeKey(UGLIR::ScalarKind scalarKind, uint32_t vectorWidth);

    /** Returns the compact SPIR-V backend type key for one structured scalar, vector, or matrix value type. */
    std::string spvValueTypeKey(const UGLIR::ValueTypeDescription &valueType);

    /** Returns the scalar lane type key for one structured scalar, vector, or matrix value type. */
    std::string spvScalarTypeKey(const UGLIR::ValueTypeDescription &valueType);

    /** Returns the vector lane count carried by one structured value type. */
    uint32_t spvVectorWidth(const UGLIR::ValueTypeDescription &valueType);

    /** Maps a structured UGL texture format to a shader-visible image payload value type. */
    std::optional<UGLIR::ValueTypeDescription> textureFormatValueType(UGLIR::TextureFormat textureFormat);

    /** Returns all structured image payload types needed by SPIR-V image read, write, sample, and gather instructions. */
    std::optional<SPIRVImagePayloadType> imagePayloadType(const UGLIR::ResourceBinding &resource,
                                                         const std::optional<UGLIR::ValueTypeDescription> &elementValueType,
                                                         const std::optional<UGLIR::ValueTypeDescription> &expressionValueType = std::nullopt);
} // namespace UGLC::CodeGen::SPIRVEmitter
