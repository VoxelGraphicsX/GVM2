#include "ExrImageDecoder.hpp"

#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    /** Decodes one OpenEXR image into unpremultiplied linear RGBA16Float texels. */
    RgbaHalfImageData decodeExrRgba16Float(
        const std::filesystem::path &assetPath)
    {
        @autoreleasepool
        {
            NSString *path = [NSString
                stringWithUTF8String:assetPath.string().c_str()];
            if (path == nil)
            {
                throw std::invalid_argument(
                    "EXR asset path is not valid UTF-8.");
            }
            NSURL *url = [NSURL fileURLWithPath:path];
            CIImage *image = [CIImage
                imageWithContentsOfURL:url
                options:@{kCIImageApplyOrientationProperty: @NO}];
            if (image == nil)
            {
                throw std::runtime_error(
                    "Core Image could not decode the pinned EXR asset.");
            }
            const CGRect extent = image.extent;
            const double roundedWidth = std::round(extent.size.width);
            const double roundedHeight = std::round(extent.size.height);
            if (roundedWidth < 1.0 || roundedHeight < 1.0 ||
                roundedWidth > double(UINT32_MAX) ||
                roundedHeight > double(UINT32_MAX) ||
                std::abs(extent.size.width - roundedWidth) > 1e-6 ||
                std::abs(extent.size.height - roundedHeight) > 1e-6)
            {
                throw std::runtime_error(
                    "Decoded EXR extent is not a bounded integral image.");
            }
            RgbaHalfImageData result;
            result.width = static_cast<uint32_t>(roundedWidth);
            result.height = static_cast<uint32_t>(roundedHeight);
            result.pixels.resize(
                size_t(result.width) * result.height * 4u);
            CIContext *context = [CIContext contextWithOptions:@{
                kCIContextWorkingColorSpace: [NSNull null],
                kCIContextOutputColorSpace: [NSNull null],
                kCIContextUseSoftwareRenderer: @YES,
            }];
            if (context == nil)
            {
                throw std::runtime_error(
                    "Core Image could not create the deterministic EXR context.");
            }
            [context render:image
                   toBitmap:result.pixels.data()
                   rowBytes:size_t(result.width) * 4u * sizeof(uint16_t)
                     bounds:extent
                     format:kCIFormatRGBAh
                 colorSpace:nil];

            return result;
        }
    }
}
