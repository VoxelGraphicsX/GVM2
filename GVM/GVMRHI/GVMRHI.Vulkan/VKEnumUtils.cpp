#include "VKEnumUtils.hpp"

#include <stdexcept>
#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
    vk::BufferUsageFlags translateBufferUsage(BufferUsageFlags usage)
    {
        vk::BufferUsageFlags flags{};
        if ((usage & BufferUsage::CopySrc) != 0u || (usage & BufferUsage::MapWrite) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eTransferSrc;
        }
        if ((usage & BufferUsage::CopyDst) != 0u || (usage & BufferUsage::MapRead) != 0u || (usage & BufferUsage::QueryResolve) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eTransferDst;
        }
        if ((usage & BufferUsage::Vertex) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eVertexBuffer;
        }
        if ((usage & BufferUsage::Index) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eIndexBuffer;
        }
        if ((usage & BufferUsage::Uniform) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eUniformBuffer;
        }
        if ((usage & BufferUsage::Storage) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eStorageBuffer;
        }
        if ((usage & BufferUsage::Indirect) != 0u)
        {
            flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
        }
        return flags;
    }

    vk::ImageUsageFlags translateTextureUsage(TextureUsageFlags usage, TextureFormat format)
    {
        vk::ImageUsageFlags flags{};
        if ((usage & TextureUsage::CopySrc) != 0u)
        {
            flags |= vk::ImageUsageFlagBits::eTransferSrc;
        }
        if ((usage & TextureUsage::CopyDst) != 0u)
        {
            flags |= vk::ImageUsageFlagBits::eTransferDst;
        }
        if ((usage & TextureUsage::TextureBinding) != 0u)
        {
            flags |= vk::ImageUsageFlagBits::eSampled;
        }
        if ((usage & TextureUsage::StorageBinding) != 0u)
        {
            flags |= vk::ImageUsageFlagBits::eStorage;
        }
        if ((usage & TextureUsage::PixelLocalAttachment) != 0u)
        {
            flags |= vk::ImageUsageFlagBits::eInputAttachment;
        }
        if ((usage & TextureUsage::RenderAttachment) != 0u)
        {
            flags |= isDepthStencilFormat(format)
                ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                : vk::ImageUsageFlagBits::eColorAttachment;
        }
        return flags;
    }

    vk::Format translateTextureFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::R8Unorm: return vk::Format::eR8Unorm;
        case TextureFormat::R8Snorm: return vk::Format::eR8Snorm;
        case TextureFormat::R8Uint: return vk::Format::eR8Uint;
        case TextureFormat::R8Sint: return vk::Format::eR8Sint;
        case TextureFormat::R16Uint: return vk::Format::eR16Uint;
        case TextureFormat::R16Sint: return vk::Format::eR16Sint;
        case TextureFormat::R16Float: return vk::Format::eR16Sfloat;
        case TextureFormat::RG8Unorm: return vk::Format::eR8G8Unorm;
        case TextureFormat::RG8Snorm: return vk::Format::eR8G8Snorm;
        case TextureFormat::RG8Uint: return vk::Format::eR8G8Uint;
        case TextureFormat::RG8Sint: return vk::Format::eR8G8Sint;
        case TextureFormat::R32Float: return vk::Format::eR32Sfloat;
        case TextureFormat::R32Uint: return vk::Format::eR32Uint;
        case TextureFormat::R32Sint: return vk::Format::eR32Sint;
        case TextureFormat::RG16Uint: return vk::Format::eR16G16Uint;
        case TextureFormat::RG16Sint: return vk::Format::eR16G16Sint;
        case TextureFormat::RG16Float: return vk::Format::eR16G16Sfloat;
        case TextureFormat::RGBA8Unorm: return vk::Format::eR8G8B8A8Unorm;
        case TextureFormat::RGBA8UnormSrgb: return vk::Format::eR8G8B8A8Srgb;
        case TextureFormat::RGBA8Snorm: return vk::Format::eR8G8B8A8Snorm;
        case TextureFormat::RGBA8Uint: return vk::Format::eR8G8B8A8Uint;
        case TextureFormat::RGBA8Sint: return vk::Format::eR8G8B8A8Sint;
        case TextureFormat::BGRA8Unorm: return vk::Format::eB8G8R8A8Unorm;
        case TextureFormat::BGRA8UnormSrgb: return vk::Format::eB8G8R8A8Srgb;
        case TextureFormat::RGB10A2Uint: return vk::Format::eA2B10G10R10UintPack32;
        case TextureFormat::RGB10A2Unorm: return vk::Format::eA2B10G10R10UnormPack32;
        case TextureFormat::RG11B10Ufloat: return vk::Format::eB10G11R11UfloatPack32;
        case TextureFormat::RGB9E5Ufloat: return vk::Format::eE5B9G9R9UfloatPack32;
        case TextureFormat::RG32Float: return vk::Format::eR32G32Sfloat;
        case TextureFormat::RG32Uint: return vk::Format::eR32G32Uint;
        case TextureFormat::RG32Sint: return vk::Format::eR32G32Sint;
        case TextureFormat::RGBA16Uint: return vk::Format::eR16G16B16A16Uint;
        case TextureFormat::RGBA16Sint: return vk::Format::eR16G16B16A16Sint;
        case TextureFormat::RGBA16Float: return vk::Format::eR16G16B16A16Sfloat;
        case TextureFormat::RGBA32Float: return vk::Format::eR32G32B32A32Sfloat;
        case TextureFormat::RGBA32Uint: return vk::Format::eR32G32B32A32Uint;
        case TextureFormat::RGBA32Sint: return vk::Format::eR32G32B32A32Sint;
        case TextureFormat::Stencil8: return vk::Format::eS8Uint;
        case TextureFormat::Depth16Unorm: return vk::Format::eD16Unorm;
        case TextureFormat::Depth24Plus: return vk::Format::eD32Sfloat;
        case TextureFormat::Depth24PlusStencil8: return vk::Format::eD24UnormS8Uint;
        case TextureFormat::Depth32Float: return vk::Format::eD32Sfloat;
        case TextureFormat::Depth32FloatStencil8: return vk::Format::eD32SfloatS8Uint;
        case TextureFormat::BC1RGBAUnorm: return vk::Format::eBc1RgbaUnormBlock;
        case TextureFormat::BC1RGBAUnormSrgb: return vk::Format::eBc1RgbaSrgbBlock;
        case TextureFormat::BC2RGBAUnorm: return vk::Format::eBc2UnormBlock;
        case TextureFormat::BC2RGBAUnormSrgb: return vk::Format::eBc2SrgbBlock;
        case TextureFormat::BC3RGBAUnorm: return vk::Format::eBc3UnormBlock;
        case TextureFormat::BC3RGBAUnormSrgb: return vk::Format::eBc3SrgbBlock;
        case TextureFormat::BC4RUnorm: return vk::Format::eBc4UnormBlock;
        case TextureFormat::BC4RSnorm: return vk::Format::eBc4SnormBlock;
        case TextureFormat::BC5RGUnorm: return vk::Format::eBc5UnormBlock;
        case TextureFormat::BC5RGSnorm: return vk::Format::eBc5SnormBlock;
        case TextureFormat::BC6HRGBUfloat: return vk::Format::eBc6HUfloatBlock;
        case TextureFormat::BC6HRGBFloat: return vk::Format::eBc6HSfloatBlock;
        case TextureFormat::BC7RGBAUnorm: return vk::Format::eBc7UnormBlock;
        case TextureFormat::BC7RGBAUnormSrgb: return vk::Format::eBc7SrgbBlock;
        case TextureFormat::ETC2RGB8Unorm: return vk::Format::eEtc2R8G8B8UnormBlock;
        case TextureFormat::ETC2RGB8UnormSrgb: return vk::Format::eEtc2R8G8B8SrgbBlock;
        case TextureFormat::ETC2RGB8A1Unorm: return vk::Format::eEtc2R8G8B8A1UnormBlock;
        case TextureFormat::ETC2RGB8A1UnormSrgb: return vk::Format::eEtc2R8G8B8A1SrgbBlock;
        case TextureFormat::ETC2RGBA8Unorm: return vk::Format::eEtc2R8G8B8A8UnormBlock;
        case TextureFormat::ETC2RGBA8UnormSrgb: return vk::Format::eEtc2R8G8B8A8SrgbBlock;
        case TextureFormat::EACR11Unorm: return vk::Format::eEacR11UnormBlock;
        case TextureFormat::EACR11Snorm: return vk::Format::eEacR11SnormBlock;
        case TextureFormat::EACRG11Unorm: return vk::Format::eEacR11G11UnormBlock;
        case TextureFormat::EACRG11Snorm: return vk::Format::eEacR11G11SnormBlock;
        case TextureFormat::ASTC4x4Unorm: return vk::Format::eAstc4x4UnormBlock;
        case TextureFormat::ASTC4x4UnormSrgb: return vk::Format::eAstc4x4SrgbBlock;
        case TextureFormat::ASTC5x4Unorm: return vk::Format::eAstc5x4UnormBlock;
        case TextureFormat::ASTC5x4UnormSrgb: return vk::Format::eAstc5x4SrgbBlock;
        case TextureFormat::ASTC5x5Unorm: return vk::Format::eAstc5x5UnormBlock;
        case TextureFormat::ASTC5x5UnormSrgb: return vk::Format::eAstc5x5SrgbBlock;
        case TextureFormat::ASTC6x5Unorm: return vk::Format::eAstc6x5UnormBlock;
        case TextureFormat::ASTC6x5UnormSrgb: return vk::Format::eAstc6x5SrgbBlock;
        case TextureFormat::ASTC6x6Unorm: return vk::Format::eAstc6x6UnormBlock;
        case TextureFormat::ASTC6x6UnormSrgb: return vk::Format::eAstc6x6SrgbBlock;
        case TextureFormat::ASTC8x5Unorm: return vk::Format::eAstc8x5UnormBlock;
        case TextureFormat::ASTC8x5UnormSrgb: return vk::Format::eAstc8x5SrgbBlock;
        case TextureFormat::ASTC8x6Unorm: return vk::Format::eAstc8x6UnormBlock;
        case TextureFormat::ASTC8x6UnormSrgb: return vk::Format::eAstc8x6SrgbBlock;
        case TextureFormat::ASTC8x8Unorm: return vk::Format::eAstc8x8UnormBlock;
        case TextureFormat::ASTC8x8UnormSrgb: return vk::Format::eAstc8x8SrgbBlock;
        case TextureFormat::ASTC10x5Unorm: return vk::Format::eAstc10x5UnormBlock;
        case TextureFormat::ASTC10x5UnormSrgb: return vk::Format::eAstc10x5SrgbBlock;
        case TextureFormat::ASTC10x6Unorm: return vk::Format::eAstc10x6UnormBlock;
        case TextureFormat::ASTC10x6UnormSrgb: return vk::Format::eAstc10x6SrgbBlock;
        case TextureFormat::ASTC10x8Unorm: return vk::Format::eAstc10x8UnormBlock;
        case TextureFormat::ASTC10x8UnormSrgb: return vk::Format::eAstc10x8SrgbBlock;
        case TextureFormat::ASTC10x10Unorm: return vk::Format::eAstc10x10UnormBlock;
        case TextureFormat::ASTC10x10UnormSrgb: return vk::Format::eAstc10x10SrgbBlock;
        case TextureFormat::ASTC12x10Unorm: return vk::Format::eAstc12x10UnormBlock;
        case TextureFormat::ASTC12x10UnormSrgb: return vk::Format::eAstc12x10SrgbBlock;
        case TextureFormat::ASTC12x12Unorm: return vk::Format::eAstc12x12UnormBlock;
        case TextureFormat::ASTC12x12UnormSrgb: return vk::Format::eAstc12x12SrgbBlock;
        default:
            throw makeRuntimeError("VKEnumUtils: unsupported texture format for Vulkan phase 1: " + eastl::to_string(static_cast<uint32_t>(format)));
        }
    }

    vk::ImageType translateTextureDimension(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::e1D: return vk::ImageType::e1D;
        case TextureDimension::e2D: return vk::ImageType::e2D;
        case TextureDimension::e3D: return vk::ImageType::e3D;
        default: throw makeRuntimeError("VKEnumUtils: unsupported texture dimension.");
        }
    }

    vk::ImageViewType translateTextureViewDimension(TextureViewDimension dimension)
    {
        switch (dimension)
        {
        case TextureViewDimension::e1D: return vk::ImageViewType::e1D;
        case TextureViewDimension::e2D: return vk::ImageViewType::e2D;
        case TextureViewDimension::e2DArray: return vk::ImageViewType::e2DArray;
        case TextureViewDimension::Cube: return vk::ImageViewType::eCube;
        case TextureViewDimension::CubeArray: return vk::ImageViewType::eCubeArray;
        case TextureViewDimension::e3D: return vk::ImageViewType::e3D;
        default: throw makeRuntimeError("VKEnumUtils: unsupported texture view dimension.");
        }
    }

    vk::Filter translateFilter(FilterMode filter)
    {
        switch (filter)
        {
        case FilterMode::Nearest: return vk::Filter::eNearest;
        case FilterMode::Linear: return vk::Filter::eLinear;
        default: throw makeRuntimeError("VKEnumUtils: unsupported filter mode.");
        }
    }

    vk::SamplerMipmapMode translateMipmapFilter(MipmapFilterMode filter)
    {
        switch (filter)
        {
        case MipmapFilterMode::Nearest: return vk::SamplerMipmapMode::eNearest;
        case MipmapFilterMode::Linear: return vk::SamplerMipmapMode::eLinear;
        default: throw makeRuntimeError("VKEnumUtils: unsupported mipmap filter mode.");
        }
    }

    vk::SamplerAddressMode translateAddressMode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::Repeat: return vk::SamplerAddressMode::eRepeat;
        case AddressMode::MirrorRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
        case AddressMode::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
        default: throw makeRuntimeError("VKEnumUtils: unsupported address mode.");
        }
    }

    vk::CompareOp translateCompareFunction(CompareFunction function)
    {
        switch (function)
        {
        case CompareFunction::Undefined: return vk::CompareOp::eAlways;
        case CompareFunction::Never: return vk::CompareOp::eNever;
        case CompareFunction::Less: return vk::CompareOp::eLess;
        case CompareFunction::LessEqual: return vk::CompareOp::eLessOrEqual;
        case CompareFunction::Greater: return vk::CompareOp::eGreater;
        case CompareFunction::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
        case CompareFunction::Equal: return vk::CompareOp::eEqual;
        case CompareFunction::NotEqual: return vk::CompareOp::eNotEqual;
        case CompareFunction::Always: return vk::CompareOp::eAlways;
        default: throw makeRuntimeError("VKEnumUtils: unsupported compare function.");
        }
    }

    vk::VertexInputRate translateVertexStepMode(VertexStepMode mode)
    {
        switch (mode)
        {
        case VertexStepMode::Vertex: return vk::VertexInputRate::eVertex;
        case VertexStepMode::Instance: return vk::VertexInputRate::eInstance;
        case VertexStepMode::VertexBufferNotUsed: return vk::VertexInputRate::eVertex;
        default: throw makeRuntimeError("VKEnumUtils: unsupported vertex step mode.");
        }
    }

    namespace
    {
        vk::Format translateVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Uint8x2: return vk::Format::eR8G8Uint;
            case VertexFormat::Uint8x4: return vk::Format::eR8G8B8A8Uint;
            case VertexFormat::Sint8x2: return vk::Format::eR8G8Sint;
            case VertexFormat::Sint8x4: return vk::Format::eR8G8B8A8Sint;
            case VertexFormat::Unorm8x2: return vk::Format::eR8G8Unorm;
            case VertexFormat::Unorm8x4: return vk::Format::eR8G8B8A8Unorm;
            case VertexFormat::Snorm8x2: return vk::Format::eR8G8Snorm;
            case VertexFormat::Snorm8x4: return vk::Format::eR8G8B8A8Snorm;
            case VertexFormat::Uint16x2: return vk::Format::eR16G16Uint;
            case VertexFormat::Uint16x4: return vk::Format::eR16G16B16A16Uint;
            case VertexFormat::Sint16x2: return vk::Format::eR16G16Sint;
            case VertexFormat::Sint16x4: return vk::Format::eR16G16B16A16Sint;
            case VertexFormat::Unorm16x2: return vk::Format::eR16G16Unorm;
            case VertexFormat::Unorm16x4: return vk::Format::eR16G16B16A16Unorm;
            case VertexFormat::Snorm16x2: return vk::Format::eR16G16Snorm;
            case VertexFormat::Snorm16x4: return vk::Format::eR16G16B16A16Snorm;
            case VertexFormat::Float16x2: return vk::Format::eR16G16Sfloat;
            case VertexFormat::Float16x4: return vk::Format::eR16G16B16A16Sfloat;
            case VertexFormat::Float32: return vk::Format::eR32Sfloat;
            case VertexFormat::Float32x2: return vk::Format::eR32G32Sfloat;
            case VertexFormat::Float32x3: return vk::Format::eR32G32B32Sfloat;
            case VertexFormat::Float32x4: return vk::Format::eR32G32B32A32Sfloat;
            case VertexFormat::Uint32: return vk::Format::eR32Uint;
            case VertexFormat::Uint32x2: return vk::Format::eR32G32Uint;
            case VertexFormat::Uint32x3: return vk::Format::eR32G32B32Uint;
            case VertexFormat::Uint32x4: return vk::Format::eR32G32B32A32Uint;
            case VertexFormat::Sint32: return vk::Format::eR32Sint;
            case VertexFormat::Sint32x2: return vk::Format::eR32G32Sint;
            case VertexFormat::Sint32x3: return vk::Format::eR32G32B32Sint;
            case VertexFormat::Sint32x4: return vk::Format::eR32G32B32A32Sint;
            default: throw makeRuntimeError("VKEnumUtils: unsupported vertex format.");
            }
        }
    } // namespace

    vk::VertexInputAttributeDescription translateVertexAttribute(const VertexAttribute &attribute, uint32_t binding)
    {
        vk::VertexInputAttributeDescription description = {};
        description.location = attribute.shaderLocation;
        description.binding = binding;
        description.format = translateVertexFormat(attribute.format);
        description.offset = static_cast<uint32_t>(attribute.offset);
        return description;
    }

    vk::PrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList: return vk::PrimitiveTopology::ePointList;
        case PrimitiveTopology::LineList: return vk::PrimitiveTopology::eLineList;
        case PrimitiveTopology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
        case PrimitiveTopology::TriangleList: return vk::PrimitiveTopology::eTriangleList;
        case PrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
        case PrimitiveTopology::PatchList: return vk::PrimitiveTopology::ePatchList;
        default: throw makeRuntimeError("VKEnumUtils: unsupported primitive topology.");
        }
    }

    vk::FrontFace translateFrontFace(FrontFace frontFace)
    {
        switch (frontFace)
        {
        case FrontFace::CCW: return vk::FrontFace::eCounterClockwise;
        case FrontFace::CW: return vk::FrontFace::eClockwise;
        default: throw makeRuntimeError("VKEnumUtils: unsupported front face.");
        }
    }

    vk::CullModeFlags translateCullMode(CullMode cullMode)
    {
        switch (cullMode)
        {
        case CullMode::None: return vk::CullModeFlagBits::eNone;
        case CullMode::Front: return vk::CullModeFlagBits::eFront;
        case CullMode::Back: return vk::CullModeFlagBits::eBack;
        default: throw makeRuntimeError("VKEnumUtils: unsupported cull mode.");
        }
    }

    vk::DescriptorType translateDescriptorType(const BindGroupLayoutEntry &entry)
    {
        if (entry.sampler.type != SamplerBindingType::Undefined)
        {
            return vk::DescriptorType::eSampler;
        }
        if (entry.texture.sampleType != TextureSampleType::Undefined)
        {
            return vk::DescriptorType::eSampledImage;
        }
        if (entry.storageTexture.access != StorageTextureAccess::Undefined)
        {
            return vk::DescriptorType::eStorageImage;
        }
        switch (entry.buffer.type)
        {
        case BufferBindingType::Uniform: return vk::DescriptorType::eUniformBuffer;
        case BufferBindingType::Storage:
        case BufferBindingType::ReadOnlyStorage: return vk::DescriptorType::eStorageBuffer;
        default: throw makeRuntimeError("VKEnumUtils: unsupported bind group layout entry.");
        }
    }

    vk::ShaderStageFlags translateShaderStages(ShaderStageFlags stages)
    {
        vk::ShaderStageFlags flags{};
        if ((stages & ShaderStage::Vertex) != 0u)
        {
            flags |= vk::ShaderStageFlagBits::eVertex;
        }
        if ((stages & ShaderStage::Fragment) != 0u)
        {
            flags |= vk::ShaderStageFlagBits::eFragment;
        }
        if ((stages & ShaderStage::Compute) != 0u)
        {
            flags |= vk::ShaderStageFlagBits::eCompute;
        }
        if ((stages & ShaderStage::Hull) != 0u)
        {
            flags |= vk::ShaderStageFlagBits::eTessellationControl;
        }
        if ((stages & ShaderStage::Domain) != 0u)
        {
            flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
        }
        return flags;
    }

    vk::PipelineStageFlags translateShaderStagesToPipelineStages(ShaderStageFlags stages)
    {
        vk::PipelineStageFlags flags{};
        if ((stages & ShaderStage::Vertex) != 0u)
        {
            flags |= vk::PipelineStageFlagBits::eVertexShader;
        }
        if ((stages & ShaderStage::Fragment) != 0u)
        {
            flags |= vk::PipelineStageFlagBits::eFragmentShader;
        }
        if ((stages & ShaderStage::Compute) != 0u)
        {
            flags |= vk::PipelineStageFlagBits::eComputeShader;
        }
        if ((stages & ShaderStage::Hull) != 0u)
        {
            flags |= vk::PipelineStageFlagBits::eTessellationControlShader;
        }
        if ((stages & ShaderStage::Domain) != 0u)
        {
            flags |= vk::PipelineStageFlagBits::eTessellationEvaluationShader;
        }
        return flags == vk::PipelineStageFlags{} ? vk::PipelineStageFlagBits::eAllGraphics : flags;
    }

    vk::BlendFactor translateBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero: return vk::BlendFactor::eZero;
        case BlendFactor::One: return vk::BlendFactor::eOne;
        case BlendFactor::Src: return vk::BlendFactor::eSrcColor;
        case BlendFactor::OneMinusSrc: return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::SrcAlpha: return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::OneMinusSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::Dst: return vk::BlendFactor::eDstColor;
        case BlendFactor::OneMinusDst: return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::DstAlpha: return vk::BlendFactor::eDstAlpha;
        case BlendFactor::OneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
        case BlendFactor::SrcAlphaSaturated: return vk::BlendFactor::eSrcAlphaSaturate;
        case BlendFactor::Constant: return vk::BlendFactor::eConstantColor;
        case BlendFactor::OneMinusConstant: return vk::BlendFactor::eOneMinusConstantColor;
        default: throw makeRuntimeError("VKEnumUtils: unsupported blend factor.");
        }
    }

    vk::BlendOp translateBlendOperation(BlendOperation operation)
    {
        switch (operation)
        {
        case BlendOperation::Add: return vk::BlendOp::eAdd;
        case BlendOperation::Subtract: return vk::BlendOp::eSubtract;
        case BlendOperation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
        case BlendOperation::Min: return vk::BlendOp::eMin;
        case BlendOperation::Max: return vk::BlendOp::eMax;
        default: throw makeRuntimeError("VKEnumUtils: unsupported blend operation.");
        }
    }

    vk::ColorComponentFlags translateColorWriteMask(ColorWriteMaskFlags writeMask)
    {
        vk::ColorComponentFlags flags{};
        if ((writeMask & ColorWriteMask::Red) != 0u)
        {
            flags |= vk::ColorComponentFlagBits::eR;
        }
        if ((writeMask & ColorWriteMask::Green) != 0u)
        {
            flags |= vk::ColorComponentFlagBits::eG;
        }
        if ((writeMask & ColorWriteMask::Blue) != 0u)
        {
            flags |= vk::ColorComponentFlagBits::eB;
        }
        if ((writeMask & ColorWriteMask::Alpha) != 0u)
        {
            flags |= vk::ColorComponentFlagBits::eA;
        }
        return flags;
    }

    vk::AttachmentLoadOp translateLoadOp(LoadOp loadOp)
    {
        switch (loadOp)
        {
        case LoadOp::Clear: return vk::AttachmentLoadOp::eClear;
        case LoadOp::Load: return vk::AttachmentLoadOp::eLoad;
        case LoadOp::Undefined: return vk::AttachmentLoadOp::eDontCare;
        default: throw makeRuntimeError("VKEnumUtils: unsupported load op.");
        }
    }

    vk::AttachmentStoreOp translateStoreOp(StoreOp storeOp)
    {
        switch (storeOp)
        {
        case StoreOp::Store: return vk::AttachmentStoreOp::eStore;
        case StoreOp::Discard:
        case StoreOp::Undefined: return vk::AttachmentStoreOp::eDontCare;
        default: throw makeRuntimeError("VKEnumUtils: unsupported store op.");
        }
    }

    vk::IndexType translateIndexFormat(IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::Uint16: return vk::IndexType::eUint16;
        case IndexFormat::Uint32: return vk::IndexType::eUint32;
        default: throw makeRuntimeError("VKEnumUtils: unsupported index format.");
        }
    }

    bool hasStencilAspect(TextureFormat format)
    {
        return format == TextureFormat::Stencil8 ||
            format == TextureFormat::Depth24PlusStencil8 ||
            format == TextureFormat::Depth32FloatStencil8;
    }

    bool isDepthStencilFormat(TextureFormat format)
    {
        return Private::isDepthFormat(format) || hasStencilAspect(format);
    }

    vk::ImageAspectFlags resolveTextureAspect(TextureFormat format, TextureAspectFlags aspectMask)
    {
        if (aspectMask == TextureAspect::DepthOnly)
        {
            return vk::ImageAspectFlagBits::eDepth;
        }
        if (aspectMask == TextureAspect::StencilOnly)
        {
            return vk::ImageAspectFlagBits::eStencil;
        }

        if (Private::isDepthFormat(format))
        {
            vk::ImageAspectFlags flags = vk::ImageAspectFlagBits::eDepth;
            if (hasStencilAspect(format))
            {
                flags |= vk::ImageAspectFlagBits::eStencil;
            }
            return flags;
        }
        if (hasStencilAspect(format))
        {
            return vk::ImageAspectFlagBits::eStencil;
        }
        return vk::ImageAspectFlagBits::eColor;
    }
} // namespace GVM::RHI::Vulkan
