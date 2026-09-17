#pragma once
#include <GVMRHI/GVMRHI.hpp>
namespace GVM::RHI::Private
{

    struct BlockInfo
    {
        uint64_t width;  // texels per block in X
        uint64_t height; // texels per block in Y
        uint64_t bytes;  // bytes per block
        // bool compressed; // BC/ASTC/etc
    };

    // https://stackoverflow.com/questions/56285630/how-to-set-bytesperrow-parameter-in-replaceregion-when-texture-format-is-compres
    uint64_t getTextureBlockWidth(TextureFormat format);
    uint64_t getTextureBlockHeight(TextureFormat format);
    uint64_t getTextureBytesPerBlock(TextureFormat format);
    BlockInfo getTextureBlockInfo(TextureFormat format);
    uint64_t getIndexBufferElementStorageSizeFromFormat(IndexFormat format);
    GVM::RHI::TextureViewDimension getTextureViewDimension(GVM::RHI::TextureDimension dim, uint32_t arrayLayers);
    bool isDepthFormat(TextureFormat format);

} // namespace GVM::RHI::Private
