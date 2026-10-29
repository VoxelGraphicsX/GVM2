#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace UGLC::CodeGen::UGLIR
{
    /** Describes one scalar, vector, or matrix value type in a spelling-independent form. */
    struct ValueTypeDescription
    {
        ScalarKind scalarKind = ScalarKind::None;
        uint32_t bitWidth = 0;
        uint32_t vectorWidth = 1;
        uint32_t matrixColumns = 0;
        uint32_t matrixRows = 0;
    };

    /** Returns the stable dump spelling for one scalar lane category. */
    const char *toString(ScalarKind kind);

    /** Returns the stable dump spelling for one builtin shader ABI semantic category. */
    const char *toString(BuiltinSemanticKind kind);

    /** Returns the stable dump spelling for one helper parameter passing mode. */
    const char *toString(ParameterPassingMode mode);

    /** Returns the stable dump spelling for one reflected resource role. */
    const char *toString(ResourceRole role);

    /** Returns the stable dump spelling for one texture dimensionality value. */
    const char *toString(TextureDimension dimension);

    /** Returns the stable dump spelling for one texture format value. */
    const char *toString(TextureFormat format);

    /** Parses a public UGL texture dimension token into a backend-neutral enum. */
    TextureDimension textureDimensionFromToken(const std::string &token);

    /** Parses a public UGL texture format token into a backend-neutral enum. */
    TextureFormat textureFormatFromToken(const std::string &token);

    /** Returns the public UGL texture format token represented by a backend-neutral enum. */
    std::string textureFormatToken(TextureFormat format);

    /** Returns the shader-visible value type used when a sampled or storage texture format is read or written. */
    std::string textureFormatValueTypeName(TextureFormat format);

    /** Returns the shader-visible framebuffer payload type for one attachment format. */
    std::string framebufferTextureFormatValueTypeName(TextureFormat format);

    /** Returns true when one texture format represents a depth value rather than color data. */
    bool isDepthTextureFormat(TextureFormat format);

    /** Returns a human-readable semantic spelling from a structured builtin semantic. */
    std::string semanticDisplayName(BuiltinSemanticKind kind, uint32_t index);

    /** Parses a public shader ABI semantic spelling into a structured builtin semantic. */
    BuiltinSemantic builtinSemanticFromToken(const std::string &token);

    /** Parses a public helper parameter passing annotation into a structured mode. */
    ParameterPassingMode parameterPassingModeFromToken(const std::string &token);

    /** Returns the reflected type record with the requested name when it exists. */
    const Type *findTypeByName(const Module &module, const std::string &typeName);

    /** Returns a scalar category from a canonical scalar type name. */
    ScalarKind scalarKindFromTypeName(const std::string &typeName);

    /** Returns the canonical scalar type spelling for one scalar category. */
    std::string scalarTypeName(ScalarKind kind);

    /** Returns a compact vector type spelling for one scalar kind and lane count. */
    std::string makeVectorTypeName(ScalarKind kind, uint32_t width);

    /** Describes a scalar, vector, or matrix type by consulting UGLIR type metadata first. */
    std::optional<ValueTypeDescription> describeValueType(const Module &module, const std::string &typeName);

    /** Describes a registered scalar, vector, or matrix type without parsing source spelling aliases. */
    std::optional<ValueTypeDescription> describeRegisteredValueType(const Module &module, const std::string &typeName);

    /** Rewrites builtin scalar alias type references in a module to canonical UGLIR type names. */
    void normalizeBuiltinTypeReferences(Module &module);

    /** Returns true when the supplied type is a scalar or vector value type. */
    bool isScalarOrVectorType(const Module &module, const std::string &typeName);

    /** Returns the lane count for a scalar or vector value type, or one for unknown types. */
    uint32_t vectorWidth(const Module &module, const std::string &typeName);

    /** Returns the scalar lane category for a scalar or vector type, or None for unknown types. */
    ScalarKind scalarKind(const Module &module, const std::string &typeName);

    /** Returns the compact backend-neutral value spelling for a scalar or vector type. */
    std::string canonicalValueTypeName(const Module &module, const std::string &typeName);

    /** Returns the byte size for one scalar/vector/matrix value type when it is known. */
    uint32_t byteSize(const Module &module, const std::string &typeName);

    /** Returns the ABI alignment for one scalar/vector/matrix value type when it is known. */
    uint32_t alignment(const Module &module, const std::string &typeName);
} // namespace UGLC::CodeGen::UGLIR
