#if defined(__APPLE__)

#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>

namespace
{
    /** Rejects an upload offset that would wrap when the payload length is added. */
    TEST(MetalTextureTransferTests, RejectsWrappedUploadOffset)
    {
        const GVM::RHI::TextureDataLayout layout = {.offset = ~uint64_t(0) - 3u};
        EXPECT_THROW(GVM::RHI::Private::computeTextureCopyFootprint(
            "test", GVM::RHI::TextureFormat::RGBA8Unorm, layout, {1u, 1u, 1u}, 1u), std::length_error);
    }

    /** Rejects a staging image whose row and depth dimensions overflow its byte count. */
    TEST(MetalTextureTransferTests, RejectsWrappedFootprintProduct)
    {
        const GVM::RHI::TextureDataLayout layout = {.bytesPerRow = 0xffffff00u, .rowsPerImage = 0xffffffffu};
        EXPECT_THROW(GVM::RHI::Private::computeTextureCopyFootprint(
            "test", GVM::RHI::TextureFormat::RGBA8Unorm, layout, {1u, 1u, 2u}, 1u), std::length_error);
    }

} // namespace

#endif
