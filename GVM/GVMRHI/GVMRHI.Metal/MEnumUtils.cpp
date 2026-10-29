#include "MEnumUtils.hpp"

#include <EASTL/array.h>
#include <GVMRHI/Private/TextureFormatTable.hpp>

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

    namespace
    {
        /** GVM to Metal pixel format table indexed by TextureFormat value; PixelFormatInvalid marks unsupported formats. */
        constexpr eastl::array<MTL::PixelFormat, Private::TextureFormatCount> MTLTextureFormatTable = {
            MTL::PixelFormat::PixelFormatInvalid, // TextureFormat::Undefined
            MTL::PixelFormat::PixelFormatR8Unorm, // TextureFormat::R8Unorm
            MTL::PixelFormat::PixelFormatR8Snorm, // TextureFormat::R8Snorm
            MTL::PixelFormat::PixelFormatR8Uint, // TextureFormat::R8Uint
            MTL::PixelFormat::PixelFormatR8Sint, // TextureFormat::R8Sint
            MTL::PixelFormat::PixelFormatR16Uint, // TextureFormat::R16Uint
            MTL::PixelFormat::PixelFormatR16Sint, // TextureFormat::R16Sint
            MTL::PixelFormat::PixelFormatR16Float, // TextureFormat::R16Float
            MTL::PixelFormat::PixelFormatRG8Unorm, // TextureFormat::RG8Unorm
            MTL::PixelFormat::PixelFormatRG8Snorm, // TextureFormat::RG8Snorm
            MTL::PixelFormat::PixelFormatRG8Uint, // TextureFormat::RG8Uint
            MTL::PixelFormat::PixelFormatRG8Sint, // TextureFormat::RG8Sint
            MTL::PixelFormat::PixelFormatR32Float, // TextureFormat::R32Float
            MTL::PixelFormat::PixelFormatR32Uint, // TextureFormat::R32Uint
            MTL::PixelFormat::PixelFormatR32Sint, // TextureFormat::R32Sint
            MTL::PixelFormat::PixelFormatRG16Uint, // TextureFormat::RG16Uint
            MTL::PixelFormat::PixelFormatRG16Sint, // TextureFormat::RG16Sint
            MTL::PixelFormat::PixelFormatRG16Float, // TextureFormat::RG16Float
            MTL::PixelFormat::PixelFormatRGBA8Unorm, // TextureFormat::RGBA8Unorm
            MTL::PixelFormat::PixelFormatRGBA8Unorm_sRGB, // TextureFormat::RGBA8UnormSrgb
            MTL::PixelFormat::PixelFormatRGBA8Snorm, // TextureFormat::RGBA8Snorm
            MTL::PixelFormat::PixelFormatRGBA8Uint, // TextureFormat::RGBA8Uint
            MTL::PixelFormat::PixelFormatRGBA8Sint, // TextureFormat::RGBA8Sint
            MTL::PixelFormat::PixelFormatBGRA8Unorm, // TextureFormat::BGRA8Unorm
            MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB, // TextureFormat::BGRA8UnormSrgb
            MTL::PixelFormat::PixelFormatRGB10A2Uint, // TextureFormat::RGB10A2Uint
            MTL::PixelFormat::PixelFormatRGB10A2Unorm, // TextureFormat::RGB10A2Unorm
            MTL::PixelFormat::PixelFormatRG11B10Float, // TextureFormat::RG11B10Ufloat
            MTL::PixelFormat::PixelFormatRGB9E5Float, // TextureFormat::RGB9E5Ufloat
            MTL::PixelFormat::PixelFormatRG32Float, // TextureFormat::RG32Float
            MTL::PixelFormat::PixelFormatRG32Uint, // TextureFormat::RG32Uint
            MTL::PixelFormat::PixelFormatRG32Sint, // TextureFormat::RG32Sint
            MTL::PixelFormat::PixelFormatRGBA16Uint, // TextureFormat::RGBA16Uint
            MTL::PixelFormat::PixelFormatRGBA16Sint, // TextureFormat::RGBA16Sint
            MTL::PixelFormat::PixelFormatRGBA16Float, // TextureFormat::RGBA16Float
            MTL::PixelFormat::PixelFormatRGBA32Float, // TextureFormat::RGBA32Float
            MTL::PixelFormat::PixelFormatRGBA32Uint, // TextureFormat::RGBA32Uint
            MTL::PixelFormat::PixelFormatRGBA32Sint, // TextureFormat::RGBA32Sint
            MTL::PixelFormat::PixelFormatStencil8, // TextureFormat::Stencil8
            MTL::PixelFormat::PixelFormatDepth16Unorm, // TextureFormat::Depth16Unorm
            MTL::PixelFormat::PixelFormatDepth24Unorm_Stencil8, // TextureFormat::Depth24Plus
            MTL::PixelFormat::PixelFormatDepth24Unorm_Stencil8, // TextureFormat::Depth24PlusStencil8
            MTL::PixelFormat::PixelFormatDepth32Float, // TextureFormat::Depth32Float
            MTL::PixelFormat::PixelFormatDepth32Float_Stencil8, // TextureFormat::Depth32FloatStencil8
            MTL::PixelFormat::PixelFormatBC1_RGBA, // TextureFormat::BC1RGBAUnorm
            MTL::PixelFormat::PixelFormatBC1_RGBA_sRGB, // TextureFormat::BC1RGBAUnormSrgb
            MTL::PixelFormat::PixelFormatBC2_RGBA, // TextureFormat::BC2RGBAUnorm
            MTL::PixelFormat::PixelFormatBC2_RGBA_sRGB, // TextureFormat::BC2RGBAUnormSrgb
            MTL::PixelFormat::PixelFormatBC3_RGBA, // TextureFormat::BC3RGBAUnorm
            MTL::PixelFormat::PixelFormatBC3_RGBA_sRGB, // TextureFormat::BC3RGBAUnormSrgb
            MTL::PixelFormat::PixelFormatBC4_RUnorm, // TextureFormat::BC4RUnorm
            MTL::PixelFormat::PixelFormatBC4_RSnorm, // TextureFormat::BC4RSnorm
            MTL::PixelFormat::PixelFormatBC5_RGUnorm, // TextureFormat::BC5RGUnorm
            MTL::PixelFormat::PixelFormatBC5_RGSnorm, // TextureFormat::BC5RGSnorm
            MTL::PixelFormat::PixelFormatBC6H_RGBUfloat, // TextureFormat::BC6HRGBUfloat
            MTL::PixelFormat::PixelFormatBC6H_RGBFloat, // TextureFormat::BC6HRGBFloat
            MTL::PixelFormat::PixelFormatBC7_RGBAUnorm, // TextureFormat::BC7RGBAUnorm
            MTL::PixelFormat::PixelFormatBC7_RGBAUnorm_sRGB, // TextureFormat::BC7RGBAUnormSrgb
            MTL::PixelFormat::PixelFormatETC2_RGB8, // TextureFormat::ETC2RGB8Unorm
            MTL::PixelFormat::PixelFormatETC2_RGB8_sRGB, // TextureFormat::ETC2RGB8UnormSrgb
            MTL::PixelFormat::PixelFormatETC2_RGB8A1, // TextureFormat::ETC2RGB8A1Unorm
            MTL::PixelFormat::PixelFormatETC2_RGB8A1_sRGB, // TextureFormat::ETC2RGB8A1UnormSrgb
            MTL::PixelFormat::PixelFormatInvalid, // TextureFormat::ETC2RGBA8Unorm
            MTL::PixelFormat::PixelFormatInvalid, // TextureFormat::ETC2RGBA8UnormSrgb
            MTL::PixelFormat::PixelFormatEAC_R11Unorm, // TextureFormat::EACR11Unorm
            MTL::PixelFormat::PixelFormatEAC_R11Snorm, // TextureFormat::EACR11Snorm
            MTL::PixelFormat::PixelFormatEAC_RG11Unorm, // TextureFormat::EACRG11Unorm
            MTL::PixelFormat::PixelFormatEAC_RG11Snorm, // TextureFormat::EACRG11Snorm
            MTL::PixelFormat::PixelFormatASTC_4x4_LDR, // TextureFormat::ASTC4x4Unorm
            MTL::PixelFormat::PixelFormatASTC_4x4_sRGB, // TextureFormat::ASTC4x4UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_5x4_LDR, // TextureFormat::ASTC5x4Unorm
            MTL::PixelFormat::PixelFormatASTC_5x4_sRGB, // TextureFormat::ASTC5x4UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_5x5_LDR, // TextureFormat::ASTC5x5Unorm
            MTL::PixelFormat::PixelFormatASTC_5x5_sRGB, // TextureFormat::ASTC5x5UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_6x5_LDR, // TextureFormat::ASTC6x5Unorm
            MTL::PixelFormat::PixelFormatASTC_6x5_sRGB, // TextureFormat::ASTC6x5UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_6x6_LDR, // TextureFormat::ASTC6x6Unorm
            MTL::PixelFormat::PixelFormatASTC_6x6_sRGB, // TextureFormat::ASTC6x6UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_8x5_LDR, // TextureFormat::ASTC8x5Unorm
            MTL::PixelFormat::PixelFormatASTC_8x5_sRGB, // TextureFormat::ASTC8x5UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_8x6_LDR, // TextureFormat::ASTC8x6Unorm
            MTL::PixelFormat::PixelFormatASTC_8x6_sRGB, // TextureFormat::ASTC8x6UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_8x8_LDR, // TextureFormat::ASTC8x8Unorm
            MTL::PixelFormat::PixelFormatASTC_8x8_sRGB, // TextureFormat::ASTC8x8UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_10x5_LDR, // TextureFormat::ASTC10x5Unorm
            MTL::PixelFormat::PixelFormatASTC_10x5_sRGB, // TextureFormat::ASTC10x5UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_10x6_LDR, // TextureFormat::ASTC10x6Unorm
            MTL::PixelFormat::PixelFormatASTC_10x6_sRGB, // TextureFormat::ASTC10x6UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_10x8_LDR, // TextureFormat::ASTC10x8Unorm
            MTL::PixelFormat::PixelFormatASTC_10x8_sRGB, // TextureFormat::ASTC10x8UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_10x10_LDR, // TextureFormat::ASTC10x10Unorm
            MTL::PixelFormat::PixelFormatASTC_10x10_sRGB, // TextureFormat::ASTC10x10UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_12x10_LDR, // TextureFormat::ASTC12x10Unorm
            MTL::PixelFormat::PixelFormatASTC_12x10_sRGB, // TextureFormat::ASTC12x10UnormSrgb
            MTL::PixelFormat::PixelFormatASTC_12x12_LDR, // TextureFormat::ASTC12x12Unorm
            MTL::PixelFormat::PixelFormatASTC_12x12_sRGB, // TextureFormat::ASTC12x12UnormSrgb
        };

        /** Row i is true when the former MTL to GVM switch resolved the row's Metal format back to TextureFormat i. */
        constexpr eastl::array<bool, Private::TextureFormatCount> MTLTextureFormatHasGVMReverse = {
            false, // TextureFormat::Undefined
            true, // TextureFormat::R8Unorm
            true, // TextureFormat::R8Snorm
            true, // TextureFormat::R8Uint
            true, // TextureFormat::R8Sint
            true, // TextureFormat::R16Uint
            true, // TextureFormat::R16Sint
            true, // TextureFormat::R16Float
            true, // TextureFormat::RG8Unorm
            true, // TextureFormat::RG8Snorm
            true, // TextureFormat::RG8Uint
            true, // TextureFormat::RG8Sint
            true, // TextureFormat::R32Float
            true, // TextureFormat::R32Uint
            true, // TextureFormat::R32Sint
            true, // TextureFormat::RG16Uint
            true, // TextureFormat::RG16Sint
            true, // TextureFormat::RG16Float
            true, // TextureFormat::RGBA8Unorm
            true, // TextureFormat::RGBA8UnormSrgb
            true, // TextureFormat::RGBA8Snorm
            true, // TextureFormat::RGBA8Uint
            true, // TextureFormat::RGBA8Sint
            true, // TextureFormat::BGRA8Unorm
            true, // TextureFormat::BGRA8UnormSrgb
            true, // TextureFormat::RGB10A2Uint
            false, // TextureFormat::RGB10A2Unorm
            true, // TextureFormat::RG11B10Ufloat
            true, // TextureFormat::RGB9E5Ufloat
            true, // TextureFormat::RG32Float
            true, // TextureFormat::RG32Uint
            true, // TextureFormat::RG32Sint
            true, // TextureFormat::RGBA16Uint
            true, // TextureFormat::RGBA16Sint
            true, // TextureFormat::RGBA16Float
            true, // TextureFormat::RGBA32Float
            true, // TextureFormat::RGBA32Uint
            true, // TextureFormat::RGBA32Sint
            true, // TextureFormat::Stencil8
            true, // TextureFormat::Depth16Unorm
            true, // TextureFormat::Depth24Plus
            false, // TextureFormat::Depth24PlusStencil8
            true, // TextureFormat::Depth32Float
            true, // TextureFormat::Depth32FloatStencil8
            false, // TextureFormat::BC1RGBAUnorm
            false, // TextureFormat::BC1RGBAUnormSrgb
            false, // TextureFormat::BC2RGBAUnorm
            false, // TextureFormat::BC2RGBAUnormSrgb
            false, // TextureFormat::BC3RGBAUnorm
            false, // TextureFormat::BC3RGBAUnormSrgb
            false, // TextureFormat::BC4RUnorm
            false, // TextureFormat::BC4RSnorm
            false, // TextureFormat::BC5RGUnorm
            false, // TextureFormat::BC5RGSnorm
            false, // TextureFormat::BC6HRGBUfloat
            false, // TextureFormat::BC6HRGBFloat
            false, // TextureFormat::BC7RGBAUnorm
            false, // TextureFormat::BC7RGBAUnormSrgb
            false, // TextureFormat::ETC2RGB8Unorm
            false, // TextureFormat::ETC2RGB8UnormSrgb
            false, // TextureFormat::ETC2RGB8A1Unorm
            false, // TextureFormat::ETC2RGB8A1UnormSrgb
            false, // TextureFormat::ETC2RGBA8Unorm
            false, // TextureFormat::ETC2RGBA8UnormSrgb
            false, // TextureFormat::EACR11Unorm
            false, // TextureFormat::EACR11Snorm
            false, // TextureFormat::EACRG11Unorm
            false, // TextureFormat::EACRG11Snorm
            false, // TextureFormat::ASTC4x4Unorm
            false, // TextureFormat::ASTC4x4UnormSrgb
            false, // TextureFormat::ASTC5x4Unorm
            false, // TextureFormat::ASTC5x4UnormSrgb
            false, // TextureFormat::ASTC5x5Unorm
            false, // TextureFormat::ASTC5x5UnormSrgb
            false, // TextureFormat::ASTC6x5Unorm
            false, // TextureFormat::ASTC6x5UnormSrgb
            false, // TextureFormat::ASTC6x6Unorm
            false, // TextureFormat::ASTC6x6UnormSrgb
            false, // TextureFormat::ASTC8x5Unorm
            false, // TextureFormat::ASTC8x5UnormSrgb
            false, // TextureFormat::ASTC8x6Unorm
            false, // TextureFormat::ASTC8x6UnormSrgb
            false, // TextureFormat::ASTC8x8Unorm
            false, // TextureFormat::ASTC8x8UnormSrgb
            false, // TextureFormat::ASTC10x5Unorm
            false, // TextureFormat::ASTC10x5UnormSrgb
            false, // TextureFormat::ASTC10x6Unorm
            false, // TextureFormat::ASTC10x6UnormSrgb
            false, // TextureFormat::ASTC10x8Unorm
            false, // TextureFormat::ASTC10x8UnormSrgb
            false, // TextureFormat::ASTC10x10Unorm
            false, // TextureFormat::ASTC10x10UnormSrgb
            false, // TextureFormat::ASTC12x10Unorm
            false, // TextureFormat::ASTC12x10UnormSrgb
            false, // TextureFormat::ASTC12x12Unorm
            false, // TextureFormat::ASTC12x12UnormSrgb
        };
    } // namespace

    MTL::PixelFormat translateTextureFormatToMTL(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if (index >= Private::TextureFormatCount || MTLTextureFormatTable[index] == MTL::PixelFormat::PixelFormatInvalid)
        {
            throw std::runtime_error("Unsupported texture format");
        }
        return MTLTextureFormatTable[index];
    }

    TextureFormat translateMTLTextureFormatToGVM(MTL::PixelFormat format)
    {
        for (uint32_t index = 0; index < Private::TextureFormatCount; ++index)
        {
            if (MTLTextureFormatHasGVMReverse[index] && MTLTextureFormatTable[index] == format)
            {
                return static_cast<TextureFormat>(index);
            }
        }
        throw std::runtime_error("Unsupported texture format");
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
