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
    /** Resolves native draw-command packing and rejects strides or command counts exceeding a captured range. */
    uint32_t resolveIndirectDrawStride(const BufferRange &range, uint32_t count, uint32_t stride, bool indexed);
    GVM::RHI::TextureViewDimension getTextureViewDimension(GVM::RHI::TextureDimension dim, uint32_t arrayLayers);
    bool isDepthFormat(TextureFormat format);
    /** Returns whether the format carries a stencil aspect. */
    bool hasStencilAspect(TextureFormat format);
    /** Returns whether the format carries a depth or stencil aspect. */
    bool isDepthStencilFormat(TextureFormat format);
    /** Returns the shader scalar class required when sampling a view of the supplied format. */
    TextureSampleType getTextureSampleType(TextureFormat format);

} // namespace GVM::RHI::Private
