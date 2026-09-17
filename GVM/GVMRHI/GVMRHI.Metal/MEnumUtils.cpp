#include "MEnumUtils.hpp"

namespace GVM::RHI::Metal
{

    MTL::ClearColor translateClearColorToMTL(const Color &color)
    {
        MTL::ClearColor mtlColor;
        mtlColor.red = color.r;
        mtlColor.green = color.g;
        mtlColor.blue = color.b;
        mtlColor.alpha = color.a;
        return mtlColor;
    }

    MTL::LoadAction translateLoadActionToMTL(LoadOp op)
    {
        MTL::LoadAction mtlLoadAction = MTL::LoadAction::LoadActionDontCare;
        switch (op)
        {
        case LoadOp::Clear:
            mtlLoadAction = MTL::LoadAction::LoadActionClear;
            break;
        case LoadOp::Load:
            mtlLoadAction = MTL::LoadAction::LoadActionLoad;
            break;
        case LoadOp::Undefined:
            mtlLoadAction = MTL::LoadAction::LoadActionDontCare;
            break;
        default:
            throw std::runtime_error("Unsupported load action");
        }
        return mtlLoadAction;
    }

    MTL::StoreAction translateStoreActionToMTL(StoreOp op)
    {
        MTL::StoreAction mtlStoreAction = MTL::StoreAction::StoreActionDontCare;
        switch (op)
        {
        case StoreOp::Store:
            mtlStoreAction = MTL::StoreAction::StoreActionStore;
            break;
        case StoreOp::Discard:
            mtlStoreAction = MTL::StoreAction::StoreActionDontCare;
            break;
        default:
            mtlStoreAction = MTL::StoreAction::StoreActionDontCare;
            break;
        }
        return mtlStoreAction;
    }

    MTL::PixelFormat translateTextureFormatToMTL(TextureFormat format)
    {
        MTL::PixelFormat mtlFormat = MTL::PixelFormat::PixelFormatInvalid;
        switch (format)
        {
        case TextureFormat::R8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatR8Unorm;
            break;
        case TextureFormat::R8Snorm:
            mtlFormat = MTL::PixelFormat::PixelFormatR8Snorm;
            break;
        case TextureFormat::R8Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatR8Uint;
            break;
        case TextureFormat::R8Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatR8Sint;
            break;
        case TextureFormat::R16Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatR16Uint;
            break;
        case TextureFormat::R16Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatR16Sint;
            break;
        case TextureFormat::R16Float:
            mtlFormat = MTL::PixelFormat::PixelFormatR16Float;
            break;
        case TextureFormat::RG8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatRG8Unorm;
            break;
        case TextureFormat::RG8Snorm:
            mtlFormat = MTL::PixelFormat::PixelFormatRG8Snorm;
            break;
        case TextureFormat::RG8Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG8Uint;
            break;
        case TextureFormat::RG8Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG8Sint;
            break;
        case TextureFormat::R32Float:
            mtlFormat = MTL::PixelFormat::PixelFormatR32Float;
            break;
        case TextureFormat::R32Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatR32Uint;
            break;
        case TextureFormat::R32Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatR32Sint;
            break;
        case TextureFormat::RG16Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG16Uint;
            break;
        case TextureFormat::RG16Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG16Sint;
            break;
        case TextureFormat::RG16Float:
            mtlFormat = MTL::PixelFormat::PixelFormatRG16Float;
            break;
        case TextureFormat::RGBA8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA8Unorm;
            break;
        case TextureFormat::RGBA8UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA8Unorm_sRGB;
            break;
        case TextureFormat::RGBA8Snorm:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA8Snorm;
            break;
        case TextureFormat::RGBA8Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA8Uint;
            break;
        case TextureFormat::RGBA8Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA8Sint;
            break;
        case TextureFormat::BGRA8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBGRA8Unorm;
            break;
        case TextureFormat::BGRA8UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB;
            break;
        case TextureFormat::RGB10A2Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGB10A2Uint;
            break;
        case TextureFormat::RGB10A2Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatRGB10A2Unorm;
            break;
        case TextureFormat::RG11B10Ufloat:
            mtlFormat = MTL::PixelFormat::PixelFormatRG11B10Float;
            break;
        case TextureFormat::RGB9E5Ufloat:
            mtlFormat = MTL::PixelFormat::PixelFormatRGB9E5Float;
            break;
        case TextureFormat::RG32Float:
            mtlFormat = MTL::PixelFormat::PixelFormatRG32Float;
            break;
        case TextureFormat::RG32Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG32Uint;
            break;
        case TextureFormat::RG32Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRG32Sint;
            break;
        case TextureFormat::RGBA16Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA16Uint;
            break;
        case TextureFormat::RGBA16Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA16Sint;
            break;
        case TextureFormat::RGBA16Float:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA16Float;
            break;
        case TextureFormat::RGBA32Float:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA32Float;
            break;
        case TextureFormat::RGBA32Uint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA32Uint;
            break;
        case TextureFormat::RGBA32Sint:
            mtlFormat = MTL::PixelFormat::PixelFormatRGBA32Sint;
            break;
        case TextureFormat::Stencil8:
            mtlFormat = MTL::PixelFormat::PixelFormatStencil8;
            break;
        case TextureFormat::Depth16Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatDepth16Unorm;
            break;
        case TextureFormat::Depth24Plus:
            mtlFormat = MTL::PixelFormat::PixelFormatDepth24Unorm_Stencil8;
            break;
        case TextureFormat::Depth24PlusStencil8:
            mtlFormat = MTL::PixelFormat::PixelFormatDepth24Unorm_Stencil8;
            break;
        case TextureFormat::Depth32Float:
            mtlFormat = MTL::PixelFormat::PixelFormatDepth32Float;
            break;
        case TextureFormat::Depth32FloatStencil8:
            mtlFormat = MTL::PixelFormat::PixelFormatDepth32Float_Stencil8;
            break;
        case TextureFormat::BC1RGBAUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC1_RGBA;
            break;
        case TextureFormat::BC1RGBAUnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatBC1_RGBA_sRGB;
            break;
        case TextureFormat::BC2RGBAUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC2_RGBA;
            break;
        case TextureFormat::BC2RGBAUnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatBC2_RGBA_sRGB;
            break;
        case TextureFormat::BC3RGBAUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC3_RGBA;
            break;
        case TextureFormat::BC3RGBAUnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatBC3_RGBA_sRGB;
            break;
        case TextureFormat::BC4RUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC4_RUnorm;
            break;
        case TextureFormat::BC4RSnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC4_RSnorm;
            break;

        case TextureFormat::BC5RGUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC5_RGUnorm;
            break;
        case TextureFormat::BC5RGSnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC5_RGSnorm;
            break;
        case TextureFormat::BC6HRGBUfloat:
            mtlFormat = MTL::PixelFormat::PixelFormatBC6H_RGBUfloat;
            break;
        case TextureFormat::BC6HRGBFloat:
            mtlFormat = MTL::PixelFormat::PixelFormatBC6H_RGBFloat;
            break;
        case TextureFormat::BC7RGBAUnorm:
            mtlFormat = MTL::PixelFormat::PixelFormatBC7_RGBAUnorm;
            break;
        case TextureFormat::BC7RGBAUnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatBC7_RGBAUnorm_sRGB;
            break;
        case TextureFormat::ETC2RGB8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGB8;
            break;
        case TextureFormat::ETC2RGB8UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGB8_sRGB;
            break;
        case TextureFormat::ETC2RGB8A1Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGB8A1;
            break;
        case TextureFormat::ETC2RGB8A1UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGB8A1_sRGB;
            break;
            /*         case TextureFormat::ETC2RGBA8Unorm:
                        mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGBA8;
                        break;
                    case TextureFormat::ETC2RGBA8UnormSrgb:
                        mtlFormat = MTL::PixelFormat::PixelFormatETC2_RGBA8_sRGB;
                        break; */
        case TextureFormat::EACR11Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatEAC_R11Unorm;
            break;
        case TextureFormat::EACR11Snorm:
            mtlFormat = MTL::PixelFormat::PixelFormatEAC_R11Snorm;
            break;
        case TextureFormat::EACRG11Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatEAC_RG11Unorm;
            break;
        case TextureFormat::EACRG11Snorm:
            mtlFormat = MTL::PixelFormat::PixelFormatEAC_RG11Snorm;
            break;
        case TextureFormat::ASTC4x4Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_4x4_LDR;
            break;
        case TextureFormat::ASTC4x4UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_4x4_sRGB;
            break;
        case TextureFormat::ASTC5x4Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_5x4_LDR;
            break;
        case TextureFormat::ASTC5x4UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_5x4_sRGB;
            break;
        case TextureFormat::ASTC5x5Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_5x5_LDR;
            break;
        case TextureFormat::ASTC5x5UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_5x5_sRGB;
            break;
        case TextureFormat::ASTC6x5Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_6x5_LDR;
            break;
        case TextureFormat::ASTC6x5UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_6x5_sRGB;
            break;
        case TextureFormat::ASTC6x6Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_6x6_LDR;
            break;
        case TextureFormat::ASTC6x6UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_6x6_sRGB;
            break;
        case TextureFormat::ASTC8x5Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x5_LDR;
            break;
        case TextureFormat::ASTC8x5UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x5_sRGB;
            break;
        case TextureFormat::ASTC8x6Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x6_LDR;
            break;
        case TextureFormat::ASTC8x6UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x6_sRGB;
            break;
        case TextureFormat::ASTC8x8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x8_LDR;
            break;
        case TextureFormat::ASTC8x8UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_8x8_sRGB;
            break;
        case TextureFormat::ASTC10x5Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x5_LDR;
            break;
        case TextureFormat::ASTC10x5UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x5_sRGB;
            break;
        case TextureFormat::ASTC10x6Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x6_LDR;
            break;
        case TextureFormat::ASTC10x6UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x6_sRGB;
            break;
        case TextureFormat::ASTC10x8Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x8_LDR;
            break;
        case TextureFormat::ASTC10x8UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x8_sRGB;
            break;
        case TextureFormat::ASTC10x10Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x10_LDR;
            break;
        case TextureFormat::ASTC10x10UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_10x10_sRGB;
            break;
        case TextureFormat::ASTC12x10Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_12x10_LDR;
            break;
        case TextureFormat::ASTC12x10UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_12x10_sRGB;
            break;
        case TextureFormat::ASTC12x12Unorm:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_12x12_LDR;
            break;
        case TextureFormat::ASTC12x12UnormSrgb:
            mtlFormat = MTL::PixelFormat::PixelFormatASTC_12x12_sRGB;
            break;
        default:
            throw std::runtime_error("Unsupported texture format");
        }
        return mtlFormat;
    }

    TextureFormat translateMTLTextureFormatToGVM(MTL::PixelFormat format)
    {
        TextureFormat outFormat = TextureFormat::Undefined;
        switch (format)
        {
        case MTL::PixelFormat::PixelFormatR8Unorm:
            outFormat = TextureFormat::R8Unorm;
            break;
        case MTL::PixelFormat::PixelFormatR8Snorm:
            outFormat = TextureFormat::R8Snorm;
            break;
        case MTL::PixelFormat::PixelFormatR8Uint:
            outFormat = TextureFormat::R8Uint;
            break;
        case MTL::PixelFormat::PixelFormatR8Sint:
            outFormat = TextureFormat::R8Sint;
            break;
        case MTL::PixelFormat::PixelFormatR16Uint:
            outFormat = TextureFormat::R16Uint;
            break;
        case MTL::PixelFormat::PixelFormatR16Sint:
            outFormat = TextureFormat::R16Sint;
            break;
        case MTL::PixelFormat::PixelFormatR16Float:
            outFormat = TextureFormat::R16Float;
            break;
        case MTL::PixelFormat::PixelFormatRG8Unorm:
            outFormat = TextureFormat::RG8Unorm;
            break;
        case MTL::PixelFormat::PixelFormatRG8Snorm:
            outFormat = TextureFormat::RG8Snorm;
            break;
        case MTL::PixelFormat::PixelFormatRG8Uint:
            outFormat = TextureFormat::RG8Uint;
            break;
        case MTL::PixelFormat::PixelFormatRG8Sint:
            outFormat = TextureFormat::RG8Sint;
            break;
        case MTL::PixelFormat::PixelFormatR32Float:
            outFormat = TextureFormat::R32Float;
            break;
        case MTL::PixelFormat::PixelFormatR32Uint:
            outFormat = TextureFormat::R32Uint;
            break;
        case MTL::PixelFormat::PixelFormatR32Sint:
            outFormat = TextureFormat::R32Sint;
            break;
        case MTL::PixelFormat::PixelFormatRG16Uint:
            outFormat = TextureFormat::RG16Uint;
            break;
        case MTL::PixelFormat::PixelFormatRG16Sint:
            outFormat = TextureFormat::RG16Sint;
            break;
        case MTL::PixelFormat::PixelFormatRG16Float:
            outFormat = TextureFormat::RG16Float;
            break;
        case MTL::PixelFormat::PixelFormatRGBA8Unorm:
            outFormat = TextureFormat::RGBA8Unorm;
            break;
        case MTL::PixelFormat::PixelFormatRGBA8Unorm_sRGB:
            outFormat = TextureFormat::RGBA8UnormSrgb;
            break;
        case MTL::PixelFormat::PixelFormatRGBA8Snorm:
            outFormat = TextureFormat::RGBA8Snorm;
            break;
        case MTL::PixelFormat::PixelFormatRGBA8Uint:
            outFormat = TextureFormat::RGBA8Uint;
            break;
        case MTL::PixelFormat::PixelFormatRGBA8Sint:

            outFormat = TextureFormat::RGBA8Sint;
            break;
        case MTL::PixelFormat::PixelFormatBGRA8Unorm:
            outFormat = TextureFormat::BGRA8Unorm;
            break;
        case MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB:
            outFormat = TextureFormat::BGRA8UnormSrgb;
            break;
        case MTL::PixelFormat::PixelFormatRGB10A2Uint:
            outFormat = TextureFormat::RGB10A2Uint;
            break;
        case MTL::PixelFormat::PixelFormatRG11B10Float:
            outFormat = TextureFormat::RG11B10Ufloat;
            break;
        case MTL::PixelFormat::PixelFormatRGB9E5Float:
            outFormat = TextureFormat::RGB9E5Ufloat;
            break;
        case MTL::PixelFormat::PixelFormatRG32Float:
            outFormat = TextureFormat::RG32Float;
            break;
        case MTL::PixelFormat::PixelFormatRG32Uint:
            outFormat = TextureFormat::RG32Uint;
            break;
        case MTL::PixelFormat::PixelFormatRG32Sint:
            outFormat = TextureFormat::RG32Sint;
            break;
        case MTL::PixelFormat::PixelFormatRGBA16Uint:
            outFormat = TextureFormat::RGBA16Uint;
            break;
        case MTL::PixelFormat::PixelFormatRGBA16Sint:
            outFormat = TextureFormat::RGBA16Sint;
            break;
        case MTL::PixelFormat::PixelFormatRGBA16Float:
            outFormat = TextureFormat::RGBA16Float;
            break;
        case MTL::PixelFormat::PixelFormatRGBA32Float:
            outFormat = TextureFormat::RGBA32Float;
            break;
        case MTL::PixelFormat::PixelFormatRGBA32Uint:
            outFormat = TextureFormat::RGBA32Uint;
            break;
        case MTL::PixelFormat::PixelFormatRGBA32Sint:
            outFormat = TextureFormat::RGBA32Sint;
            break;
        case MTL::PixelFormat::PixelFormatStencil8:
            outFormat = TextureFormat::Stencil8;
            break;
        case MTL::PixelFormat::PixelFormatDepth16Unorm:
            outFormat = TextureFormat::Depth16Unorm;
            break;
        case MTL::PixelFormat::PixelFormatDepth24Unorm_Stencil8:
            outFormat = TextureFormat::Depth24Plus;
            break;
        case MTL::PixelFormat::PixelFormatDepth32Float:
            outFormat = TextureFormat::Depth32Float;
            break;
        case MTL::PixelFormat::PixelFormatDepth32Float_Stencil8:
            outFormat = TextureFormat::Depth32FloatStencil8;
            break;
        default:
            throw std::runtime_error("Unsupported texture format");
        }
        return outFormat;
    }

    MTL::TextureType translateTextureViewDimensionToMTL(TextureViewDimension dimension)
    {
        MTL::TextureType mtlDimension = MTL::TextureType::TextureType2D;
        switch (dimension)
        {
        case TextureViewDimension::e1D:
            mtlDimension = MTL::TextureType::TextureType1D;
            break;
        case TextureViewDimension::e2D:
            mtlDimension = MTL::TextureType::TextureType2D;
            break;
        case TextureViewDimension::e2DArray:
            mtlDimension = MTL::TextureType::TextureType2DArray;
            break;
        case TextureViewDimension::Cube:
            mtlDimension = MTL::TextureType::TextureTypeCube;
            break;
        case TextureViewDimension::CubeArray:
            mtlDimension = MTL::TextureType::TextureTypeCubeArray;
            break;
        case TextureViewDimension::e3D:
            mtlDimension = MTL::TextureType::TextureType3D;
            break;
        default:
            throw std::runtime_error("Unsupported texture view dimension");
        }
        return mtlDimension;
    }

    MTL::TextureType translateTextureDimensionToMTL(TextureDimension dimension, uint32_t arrayLayers)
    {
        MTL::TextureType mtlDimension = MTL::TextureType::TextureType2D;

        if (dimension == TextureDimension::e1D)
        {
            mtlDimension = MTL::TextureType::TextureType1D;
        }
        else if (dimension == TextureDimension::e2D)
        {
            mtlDimension = arrayLayers > 1 ? MTL::TextureType::TextureType2DArray : MTL::TextureType::TextureType2D;
        }
        else if (dimension == TextureDimension::e3D)
        {
            mtlDimension = MTL::TextureType::TextureType3D;
        }
        else
        {
            throw std::runtime_error("Unsupported texture dimension");
        }
        return mtlDimension;
    }

    MTL::TextureUsage translateTextureUsageToMTL(TextureUsageFlags usage)
    {
        MTL::TextureUsage mtlUsage = MTL::TextureUsageUnknown;
        if (usage & TextureUsage::TextureBinding)
        {
            mtlUsage |= MTL::TextureUsageShaderRead;
        }
        if (usage & TextureUsage::StorageBinding)
        {
            mtlUsage |= MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead;
        }
        if (usage & TextureUsage::RenderAttachment)
        {
            mtlUsage |= MTL::TextureUsageRenderTarget;
        }
        return mtlUsage;
    }

    MTL::BlendFactor translateBlendFactorToMTL(BlendFactor factor)
    {
        MTL::BlendFactor mtlFactor = MTL::BlendFactor::BlendFactorZero;
        switch (factor)
        {
        case BlendFactor::Zero:
            mtlFactor = MTL::BlendFactor::BlendFactorZero;
            break;
        case BlendFactor::One:
            mtlFactor = MTL::BlendFactor::BlendFactorOne;
            break;
        case BlendFactor::Src:
            mtlFactor = MTL::BlendFactor::BlendFactorSourceColor;
            break;
        case BlendFactor::OneMinusSrc:
            mtlFactor = MTL::BlendFactor::BlendFactorOneMinusSourceColor;
            break;
        case BlendFactor::Dst:
            mtlFactor = MTL::BlendFactor::BlendFactorDestinationColor;
            break;
        case BlendFactor::OneMinusDst:
            mtlFactor = MTL::BlendFactor::BlendFactorOneMinusDestinationColor;
            break;
        case BlendFactor::SrcAlpha:
            mtlFactor = MTL::BlendFactor::BlendFactorSourceAlpha;
            break;
        case BlendFactor::OneMinusSrcAlpha:
            mtlFactor = MTL::BlendFactor::BlendFactorOneMinusSourceAlpha;
            break;
        case BlendFactor::DstAlpha:
            mtlFactor = MTL::BlendFactor::BlendFactorDestinationAlpha;
            break;
        case BlendFactor::OneMinusDstAlpha:
            mtlFactor = MTL::BlendFactor::BlendFactorOneMinusDestinationAlpha;
            break;
        case BlendFactor::SrcAlphaSaturated:
            mtlFactor = MTL::BlendFactor::BlendFactorSourceAlphaSaturated;
            break;
        default:
            throw std::runtime_error("Unsupported blend factor");
        }
        return mtlFactor;
    }

    MTL::BlendOperation translateBlendOperationToMTL(BlendOperation operation)
    {
        MTL::BlendOperation mtlOperation = MTL::BlendOperation::BlendOperationAdd;
        switch (operation)
        {
        case BlendOperation::Add:
            mtlOperation = MTL::BlendOperation::BlendOperationAdd;
            break;
        case BlendOperation::Subtract:
            mtlOperation = MTL::BlendOperation::BlendOperationSubtract;
            break;
        case BlendOperation::ReverseSubtract:
            mtlOperation = MTL::BlendOperation::BlendOperationReverseSubtract;
            break;
        case BlendOperation::Min:
            mtlOperation = MTL::BlendOperation::BlendOperationMin;
            break;
        case BlendOperation::Max:
            mtlOperation = MTL::BlendOperation::BlendOperationMax;
            break;
        default:
            throw std::runtime_error("Unsupported blend operation");
        }
        return mtlOperation;
    }

    MTL::VertexStepFunction translateVertexStepModeToMTL(VertexStepMode mode)
    {
        MTL::VertexStepFunction mtlMode = MTL::VertexStepFunction::VertexStepFunctionPerVertex;
        switch (mode)
        {
        case VertexStepMode::Vertex:
            mtlMode = MTL::VertexStepFunction::VertexStepFunctionPerVertex;
            break;
        case VertexStepMode::Instance:
            mtlMode = MTL::VertexStepFunction::VertexStepFunctionPerInstance;
            break;
        default:
            throw std::runtime_error("Unsupported vertex step mode");
        }
        return mtlMode;
    }

    MTL::VertexFormat translateVertexFormatToMTL(VertexFormat format)
    {
        MTL::VertexFormat mtlFormat = MTL::VertexFormat::VertexFormatInvalid;
        switch (format)
        {
        case VertexFormat::Uint8x2:
            mtlFormat = MTL::VertexFormat::VertexFormatChar2;
            break;
        case VertexFormat::Uint8x4:
            mtlFormat = MTL::VertexFormat::VertexFormatChar4;
            break;
        case VertexFormat::Sint8x2:
            mtlFormat = MTL::VertexFormat::VertexFormatChar2;
            break;
        case VertexFormat::Sint8x4:
            mtlFormat = MTL::VertexFormat::VertexFormatChar4;
            break;
        case VertexFormat::Unorm8x2:
            mtlFormat = MTL::VertexFormat::VertexFormatUChar2;
            break;
        case VertexFormat::Unorm8x4:
            mtlFormat = MTL::VertexFormat::VertexFormatUChar4;
            break;
        case VertexFormat::Snorm8x2:
            mtlFormat = MTL::VertexFormat::VertexFormatChar2;
            break;
        case VertexFormat::Snorm8x4:
            mtlFormat = MTL::VertexFormat::VertexFormatChar4;
            break;
        case VertexFormat::Uint16x2:
            mtlFormat = MTL::VertexFormat::VertexFormatUShort2;
            break;
        case VertexFormat::Uint16x4:
            mtlFormat = MTL::VertexFormat::VertexFormatUShort4;
            break;
        case VertexFormat::Sint16x2:
            mtlFormat = MTL::VertexFormat::VertexFormatShort2;
            break;
        case VertexFormat::Sint16x4:
            mtlFormat = MTL::VertexFormat::VertexFormatShort4;
            break;
        case VertexFormat::Unorm16x2:
            mtlFormat = MTL::VertexFormat::VertexFormatUShort2;
            break;
        case VertexFormat::Unorm16x4:
            mtlFormat = MTL::VertexFormat::VertexFormatUShort4;
            break;
        case VertexFormat::Snorm16x2:
            mtlFormat = MTL::VertexFormat::VertexFormatShort2;
            break;
        case VertexFormat::Snorm16x4:
            mtlFormat = MTL::VertexFormat::VertexFormatShort4;
            break;
        case VertexFormat::Float16x2:
            mtlFormat = MTL::VertexFormat::VertexFormatHalf2;
            break;
        case VertexFormat::Float16x4:
            mtlFormat = MTL::VertexFormat::VertexFormatHalf4;
            break;
        case VertexFormat::Float32:
            mtlFormat = MTL::VertexFormat::VertexFormatFloat;
            break;
        case VertexFormat::Float32x2:
            mtlFormat = MTL::VertexFormat::VertexFormatFloat2;
            break;
        case VertexFormat::Float32x3:
            mtlFormat = MTL::VertexFormat::VertexFormatFloat3;
            break;
        case VertexFormat::Float32x4:
            mtlFormat = MTL::VertexFormat::VertexFormatFloat4;
            break;
        case VertexFormat::Uint32:
            mtlFormat = MTL::VertexFormat::VertexFormatUInt;
            break;
        case VertexFormat::Uint32x2:
            mtlFormat = MTL::VertexFormat::VertexFormatUInt2;
            break;
        case VertexFormat::Uint32x3:
            mtlFormat = MTL::VertexFormat::VertexFormatUInt3;
            break;
        case VertexFormat::Uint32x4:
            mtlFormat = MTL::VertexFormat::VertexFormatUInt4;
            break;
        case VertexFormat::Sint32:
            mtlFormat = MTL::VertexFormat::VertexFormatInt;
            break;
        case VertexFormat::Sint32x2:
            mtlFormat = MTL::VertexFormat::VertexFormatInt2;
            break;
        case VertexFormat::Sint32x3:
            mtlFormat = MTL::VertexFormat::VertexFormatInt3;
            break;
        case VertexFormat::Sint32x4:
            mtlFormat = MTL::VertexFormat::VertexFormatInt4;
            break;
        default:
            throw std::runtime_error("Unsupported vertex format");
        }
        return mtlFormat;
    }

    MTL::IndexType translateIndexFormatToMTL(IndexFormat format)
    {
        MTL::IndexType mtlFormat = MTL::IndexType::IndexTypeUInt32;
        switch (format)
        {
        case IndexFormat::Uint16:
            mtlFormat = MTL::IndexType::IndexTypeUInt16;
            break;
        case IndexFormat::Uint32:
            mtlFormat = MTL::IndexType::IndexTypeUInt32;
            break;
        default:
            throw std::runtime_error("Unsupported index format");
        }
        return mtlFormat;
    }

    MTL::SamplerMinMagFilter translateSamplerFilterToMTL(FilterMode filter)
    {
        MTL::SamplerMinMagFilter mtlFilter = MTL::SamplerMinMagFilter::SamplerMinMagFilterNearest;
        switch (filter)
        {
        case FilterMode::Nearest:
            mtlFilter = MTL::SamplerMinMagFilter::SamplerMinMagFilterNearest;
            break;
        case FilterMode::Linear:
            mtlFilter = MTL::SamplerMinMagFilter::SamplerMinMagFilterLinear;
            break;
        default:
            throw std::runtime_error("Unsupported filter mode");
        }
        return mtlFilter;
    }

    MTL::SamplerMipFilter translateSamplerMipFilterToMTL(MipmapFilterMode filter)
    {
        MTL::SamplerMipFilter mtlFilter = MTL::SamplerMipFilter::SamplerMipFilterNearest;
        switch (filter)
        {
        case MipmapFilterMode::Nearest:
            mtlFilter = MTL::SamplerMipFilter::SamplerMipFilterNearest;
            break;
        case MipmapFilterMode::Linear:
            mtlFilter = MTL::SamplerMipFilter::SamplerMipFilterLinear;
            break;
        default:
            throw std::runtime_error("Unsupported mipmap filter mode");
        }
        return mtlFilter;
    }

    MTL::SamplerAddressMode translateAddressModeToMTL(AddressMode mode)
    {
        MTL::SamplerAddressMode mtlMode = MTL::SamplerAddressMode::SamplerAddressModeClampToEdge;
        switch (mode)
        {
        case AddressMode::Repeat:
            mtlMode = MTL::SamplerAddressMode::SamplerAddressModeRepeat;
            break;
        case AddressMode::MirrorRepeat:
            mtlMode = MTL::SamplerAddressMode::SamplerAddressModeMirrorRepeat;
            break;
        case AddressMode::ClampToEdge:
            mtlMode = MTL::SamplerAddressMode::SamplerAddressModeClampToEdge;
            break;
        default:
            throw std::runtime_error("Unsupported address mode");
        }
        return mtlMode;
    }

    MTL::CompareFunction translateCompareFunctionToMTL(CompareFunction function)
    {
        MTL::CompareFunction mtlFunction = MTL::CompareFunction::CompareFunctionNever;
        switch (function)
        {
        case CompareFunction::Never:
            mtlFunction = MTL::CompareFunction::CompareFunctionNever;
            break;
        case CompareFunction::Less:
            mtlFunction = MTL::CompareFunction::CompareFunctionLess;
            break;
        case CompareFunction::Equal:
            mtlFunction = MTL::CompareFunction::CompareFunctionEqual;
            break;
        case CompareFunction::LessEqual:
            mtlFunction = MTL::CompareFunction::CompareFunctionLessEqual;
            break;
        case CompareFunction::Greater:
            mtlFunction = MTL::CompareFunction::CompareFunctionGreater;
            break;
        case CompareFunction::NotEqual:
            mtlFunction = MTL::CompareFunction::CompareFunctionNotEqual;
            break;
        case CompareFunction::GreaterEqual:
            mtlFunction = MTL::CompareFunction::CompareFunctionGreaterEqual;
            break;
        case CompareFunction::Always:
            mtlFunction = MTL::CompareFunction::CompareFunctionAlways;
            break;
        default:
            throw std::runtime_error("Unsupported compare function");
        }
        return mtlFunction;
    }

    MTL::ResourceUsage translateStorageTextureAccessToMTL(StorageTextureAccess access)
    {
        MTL::ResourceUsage mtlAccess = MTL::ResourceUsageRead;
        switch (access)
        {
        case StorageTextureAccess::ReadOnly:
            mtlAccess = MTL::ResourceUsageRead;
            break;
        case StorageTextureAccess::WriteOnly:
            mtlAccess = MTL::ResourceUsageWrite;
            break;
        case StorageTextureAccess::ReadWrite:
            mtlAccess = MTL::ResourceUsageRead | MTL::ResourceUsageWrite;
            break;
        default:
            throw std::runtime_error("Unsupported storage texture access");
        }
        return mtlAccess;
    }

    MTL::ResourceUsage translateStorageBufferAccessToMTL(StorageBufferAccess access)
    {
        MTL::ResourceUsage mtlAccess = MTL::ResourceUsageRead;
        switch (access)
        {
        case StorageBufferAccess::ReadOnly:
            mtlAccess = MTL::ResourceUsageRead;
            break;
        case StorageBufferAccess::WriteOnly:
            mtlAccess = MTL::ResourceUsageWrite;
            break;
        case StorageBufferAccess::ReadWrite:
            mtlAccess = MTL::ResourceUsageRead | MTL::ResourceUsageWrite;
            break;
        default:
            throw std::runtime_error("Unsupported storage buffer access");
        }
        return mtlAccess;
    }

    NS::String *translateStringToNSString(const eastl::string &str)
    {
        return NS::String::string(str.c_str(), NS::UTF8StringEncoding);
    }

    MTL::CullMode translateCullMode(CullMode cullMode)
    {
        MTL::CullMode mtlCullMode;
        switch (cullMode)
        {
        case CullMode::Front: {
            mtlCullMode = MTL::CullMode::CullModeFront;
            break;
        }
        case CullMode::Back: {
            mtlCullMode = MTL::CullMode::CullModeBack;
            break;
        }
        case CullMode::None:
        case CullMode::Force32: {
            mtlCullMode = MTL::CullMode::CullModeNone;
            break;
        }
        }
        return mtlCullMode;
    }

    MTL::PrimitiveType translatePrimitiveTopologyToMTL(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList:
            return MTL::PrimitiveType::PrimitiveTypePoint;
        case PrimitiveTopology::LineList:
            return MTL::PrimitiveType::PrimitiveTypeLine;
        case PrimitiveTopology::LineStrip:
            return MTL::PrimitiveType::PrimitiveTypeLineStrip;
        case PrimitiveTopology::TriangleList:
            return MTL::PrimitiveType::PrimitiveTypeTriangle;
        case PrimitiveTopology::TriangleStrip:
            return MTL::PrimitiveType::PrimitiveTypeTriangleStrip;
        case PrimitiveTopology::PatchList:
            throw std::runtime_error("Metal backend does not yet support PrimitiveTopology::PatchList in render draw paths");
        case PrimitiveTopology::Force32:
            break;
        }
        throw std::runtime_error("Unsupported primitive topology");
    }

    MTL::Winding translateFrontFaceToMTL(FrontFace frontFace)
    {
        switch (frontFace)
        {
        case FrontFace::CW:
            return MTL::Winding::WindingClockwise;
        case FrontFace::CCW:
            return MTL::Winding::WindingCounterClockwise;
        case FrontFace::Force32:
            break;
        }
        throw std::runtime_error("Unsupported front face");
    }


} // namespace GVM::RHI::Metal
