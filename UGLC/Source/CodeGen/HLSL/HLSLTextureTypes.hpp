#pragma once
#include <stdexcept>
#include <string>

namespace UGLC::CodeGen::HLSL
{
    /** Describes the existing HLSL texture and attachment type mapping. */
    struct TextureFormatTypeInfo
    {
        const char *formatKey;
        const char *sampledTextureType;
        const char *framebufferType;
        const char *storageTextureType;
        const char *vulkanStorageImageFormat;
    };

    /** Returns the attachment value type, preserving the declared channel count and rejecting unsupported formats. */
    std::string MakeFormatToVectorTypeForFrameBuffer(const std::string &format);
    /** Returns the writable texture element type and rejects unsupported storage formats. */
    std::string MakeFormatToVectorTypeForStorageTexture(const std::string &format);
    /** Returns the explicit Vulkan storage-image format qualifier or rejects unsupported formats. */
    std::string MakeVulkanImageFormatForStorageTexture(const std::string &format);
    /** Returns the HLSL sampled texture value type for a DSL sample type. */
    std::string MakeSampledTextureType(const std::string &sampleType);
    /** Returns the unqualified DSL texture-format token used by the lookup table. */
    std::string normalizeTextureFormatKey(const std::string &format);
    /** Returns the format mapping, or null when the format token is not recognized. */
    const TextureFormatTypeInfo *findTextureFormatTypeInfo(const std::string &format);
}
