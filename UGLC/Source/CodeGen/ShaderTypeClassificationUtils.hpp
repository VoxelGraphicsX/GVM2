#pragma once

#include <CodeGen/UGLC.Constants.hpp>

#include <string_view>

namespace UGLC::CodeGen
{
    inline bool isSamplerCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderSamplerName;
    }

    inline bool isStructuredBufferCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderStructuredBufferName;
    }

    inline bool isRWStructuredBufferCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderRWStructuredBufferName;
    }

    inline bool isAnyStructuredBufferCanonicalName(std::string_view typeName)
    {
        return isStructuredBufferCanonicalName(typeName) || isRWStructuredBufferCanonicalName(typeName);
    }

    inline bool isTexture2DAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderTexture2DAccessPacker) ||
               typeName.starts_with(mUGLShaderRWTexture2DAccessPacker) ||
               typeName.starts_with(mUGLShaderBaseTexture2DAccessPacker);
    }

    inline bool isTexture2DArrayAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderTexture2DArrayAccessPacker) ||
               typeName.starts_with(mUGLShaderRWTexture2DArrayAccessPacker) ||
               typeName.starts_with(mUGLShaderBaseTexture2DArrayAccessPacker);
    }

    /** Returns whether the canonical type name is a 3D texture access packer used by DSL member calls. */
    inline bool isTexture3DAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderTexture3DAccessPacker) ||
               typeName.starts_with(mUGLShaderRWTexture3DAccessPacker);
    }

    inline bool isTextureAccessPackerType(std::string_view typeName)
    {
        return isTexture2DAccessPackerType(typeName) || isTexture2DArrayAccessPackerType(typeName) || isTexture3DAccessPackerType(typeName);
    }

    inline bool isReadWriteTexture2DAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderRWTexture2DAccessPacker);
    }

    inline bool isReadWriteTexture2DArrayAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderRWTexture2DArrayAccessPacker);
    }

    /** Returns whether the canonical type name is the read-write 3D texture access packer. */
    inline bool isReadWriteTexture3DAccessPackerType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLShaderRWTexture3DAccessPacker);
    }

    inline bool isTexture2DCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderTexture2DName || typeName == mUGLShaderRWTexture2DName;
    }

    inline bool isTexture2DArrayCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderTexture2DArrayName || typeName == mUGLShaderRWTexture2DArrayName;
    }

    /** Returns whether the canonical type name is a sampled or storage 3D texture object. */
    inline bool isTexture3DCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderTexture3DName || typeName == mUGLShaderRWTexture3DName;
    }

    inline bool isTextureObjectCanonicalName(std::string_view typeName)
    {
        return isTexture2DCanonicalName(typeName) || isTexture2DArrayCanonicalName(typeName) || isTexture3DCanonicalName(typeName);
    }

    inline bool isTextureOrSamplerCanonicalName(std::string_view typeName)
    {
        return isTextureObjectCanonicalName(typeName) || isSamplerCanonicalName(typeName);
    }

    inline bool isReadWriteTextureCanonicalName(std::string_view typeName)
    {
        return typeName == mUGLShaderRWTexture2DName || typeName == mUGLShaderRWTexture2DArrayName || typeName == mUGLShaderRWTexture3DName;
    }

    inline bool isRenderSetBufferComponentType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLRenderSetBufferComponentClassName) || typeName.starts_with("UGL::BufferComponentDataPack");
    }

    inline bool isRenderSetTextureComponentType(std::string_view typeName)
    {
        return typeName.starts_with(mUGLRenderSetTextureComponentClassName) || typeName.starts_with("UGL::TextureComponentDataPack");
    }

    inline bool isRenderSetDataPackType(std::string_view typeName)
    {
        return typeName.starts_with("UGL::RenderSetDataPack");
    }

    inline std::string_view getMSLAddressSpaceQualifierForCanonicalType(std::string_view typeName)
    {
        if (isTextureOrSamplerCanonicalName(typeName) || typeName == mUGLBindGroupName)
        {
            return "";
        }
        if (typeName == mUGLShaderUniformBufferName)
        {
            return "constant";
        }
        if (typeName == mUGLShaderAtomicName || isAnyStructuredBufferCanonicalName(typeName))
        {
            return "device";
        }
        return "thread";
    }
} // namespace UGLC::CodeGen
