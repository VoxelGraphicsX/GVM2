#pragma once

#include <clang/AST/Type.h>

#include <string>

namespace clang
{
    class FieldDecl;
} // namespace clang

namespace UGLC::CodeGen
{
    struct BindGroupFieldBindingInfo
    {
        const clang::FieldDecl *fieldDecl = nullptr;
        int bindingIndex = -1;
    };

    // This is the backend-neutral resource view that future shader backends
    // should consume instead of re-deriving descriptor semantics from raw AST
    // strings. The intent is to keep one authoritative shader-binding model.
    enum class BaseShaderResourceKind : int
    {
        UniformBuffer = 1,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
    };

    enum class BaseShaderTextureDimension : int
    {
        None = 0,
        Texture2D,
        Texture2DArray,
        Texture3D,
    };

    enum class BaseShaderTextureSampleType : int
    {
        None = 0,
        Float,
        Sint,
        Uint,
        Depth,
    };

    enum class BaseShaderResourceAccess : int
    {
        None = 0,
        ReadOnly,
        ReadWrite,
    };

    struct BaseShaderResourceBinding
    {
        const clang::FieldDecl *fieldDecl = nullptr;
        int bindingIndex = -1;
        BaseShaderResourceKind kind = BaseShaderResourceKind::UniformBuffer;
        BaseShaderTextureDimension dimension = BaseShaderTextureDimension::None;
        BaseShaderTextureSampleType sampleType = BaseShaderTextureSampleType::None;
        BaseShaderResourceAccess access = BaseShaderResourceAccess::None;
        clang::QualType resourceType;
        clang::QualType elementType;
        std::string resourceTypeName;
        std::string elementTypeName;
    };
} // namespace UGLC::CodeGen
