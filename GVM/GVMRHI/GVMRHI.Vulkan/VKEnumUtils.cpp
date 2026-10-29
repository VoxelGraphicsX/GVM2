#include "VKEnumUtils.hpp"

#include <stdexcept>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <GVMRHI/Private/TextureFormatTable.hpp>

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

    namespace
    {
        /** GVM to Vulkan pixel format table indexed by TextureFormat value; eUndefined marks formats Vulkan cannot create here. */
        constexpr eastl::array<vk::Format, Private::TextureFormatCount> VKTextureFormatTable = {
            vk::Format::eUndefined, // TextureFormat::Undefined
            vk::Format::eR8Unorm, // TextureFormat::R8Unorm
            vk::Format::eR8Snorm, // TextureFormat::R8Snorm
            vk::Format::eR8Uint, // TextureFormat::R8Uint
            vk::Format::eR8Sint, // TextureFormat::R8Sint
            vk::Format::eR16Uint, // TextureFormat::R16Uint
            vk::Format::eR16Sint, // TextureFormat::R16Sint
            vk::Format::eR16Sfloat, // TextureFormat::R16Float
            vk::Format::eR8G8Unorm, // TextureFormat::RG8Unorm
            vk::Format::eR8G8Snorm, // TextureFormat::RG8Snorm
            vk::Format::eR8G8Uint, // TextureFormat::RG8Uint
            vk::Format::eR8G8Sint, // TextureFormat::RG8Sint
            vk::Format::eR32Sfloat, // TextureFormat::R32Float
            vk::Format::eR32Uint, // TextureFormat::R32Uint
            vk::Format::eR32Sint, // TextureFormat::R32Sint
            vk::Format::eR16G16Uint, // TextureFormat::RG16Uint
            vk::Format::eR16G16Sint, // TextureFormat::RG16Sint
            vk::Format::eR16G16Sfloat, // TextureFormat::RG16Float
            vk::Format::eR8G8B8A8Unorm, // TextureFormat::RGBA8Unorm
            vk::Format::eR8G8B8A8Srgb, // TextureFormat::RGBA8UnormSrgb
            vk::Format::eR8G8B8A8Snorm, // TextureFormat::RGBA8Snorm
            vk::Format::eR8G8B8A8Uint, // TextureFormat::RGBA8Uint
            vk::Format::eR8G8B8A8Sint, // TextureFormat::RGBA8Sint
            vk::Format::eB8G8R8A8Unorm, // TextureFormat::BGRA8Unorm
            vk::Format::eB8G8R8A8Srgb, // TextureFormat::BGRA8UnormSrgb
            vk::Format::eA2B10G10R10UintPack32, // TextureFormat::RGB10A2Uint
            vk::Format::eA2B10G10R10UnormPack32, // TextureFormat::RGB10A2Unorm
            vk::Format::eB10G11R11UfloatPack32, // TextureFormat::RG11B10Ufloat
            vk::Format::eE5B9G9R9UfloatPack32, // TextureFormat::RGB9E5Ufloat
            vk::Format::eR32G32Sfloat, // TextureFormat::RG32Float
            vk::Format::eR32G32Uint, // TextureFormat::RG32Uint
            vk::Format::eR32G32Sint, // TextureFormat::RG32Sint
            vk::Format::eR16G16B16A16Uint, // TextureFormat::RGBA16Uint
            vk::Format::eR16G16B16A16Sint, // TextureFormat::RGBA16Sint
            vk::Format::eR16G16B16A16Sfloat, // TextureFormat::RGBA16Float
            vk::Format::eR32G32B32A32Sfloat, // TextureFormat::RGBA32Float
            vk::Format::eR32G32B32A32Uint, // TextureFormat::RGBA32Uint
            vk::Format::eR32G32B32A32Sint, // TextureFormat::RGBA32Sint
            vk::Format::eS8Uint, // TextureFormat::Stencil8
            vk::Format::eD16Unorm, // TextureFormat::Depth16Unorm
            vk::Format::eD32Sfloat, // TextureFormat::Depth24Plus
            vk::Format::eD24UnormS8Uint, // TextureFormat::Depth24PlusStencil8
            vk::Format::eD32Sfloat, // TextureFormat::Depth32Float
            vk::Format::eD32SfloatS8Uint, // TextureFormat::Depth32FloatStencil8
            vk::Format::eBc1RgbaUnormBlock, // TextureFormat::BC1RGBAUnorm
            vk::Format::eBc1RgbaSrgbBlock, // TextureFormat::BC1RGBAUnormSrgb
            vk::Format::eBc2UnormBlock, // TextureFormat::BC2RGBAUnorm
            vk::Format::eBc2SrgbBlock, // TextureFormat::BC2RGBAUnormSrgb
            vk::Format::eBc3UnormBlock, // TextureFormat::BC3RGBAUnorm
            vk::Format::eBc3SrgbBlock, // TextureFormat::BC3RGBAUnormSrgb
            vk::Format::eBc4UnormBlock, // TextureFormat::BC4RUnorm
            vk::Format::eBc4SnormBlock, // TextureFormat::BC4RSnorm
            vk::Format::eBc5UnormBlock, // TextureFormat::BC5RGUnorm
            vk::Format::eBc5SnormBlock, // TextureFormat::BC5RGSnorm
            vk::Format::eBc6HUfloatBlock, // TextureFormat::BC6HRGBUfloat
            vk::Format::eBc6HSfloatBlock, // TextureFormat::BC6HRGBFloat
            vk::Format::eBc7UnormBlock, // TextureFormat::BC7RGBAUnorm
            vk::Format::eBc7SrgbBlock, // TextureFormat::BC7RGBAUnormSrgb
            vk::Format::eEtc2R8G8B8UnormBlock, // TextureFormat::ETC2RGB8Unorm
            vk::Format::eEtc2R8G8B8SrgbBlock, // TextureFormat::ETC2RGB8UnormSrgb
            vk::Format::eEtc2R8G8B8A1UnormBlock, // TextureFormat::ETC2RGB8A1Unorm
            vk::Format::eEtc2R8G8B8A1SrgbBlock, // TextureFormat::ETC2RGB8A1UnormSrgb
            vk::Format::eEtc2R8G8B8A8UnormBlock, // TextureFormat::ETC2RGBA8Unorm
            vk::Format::eEtc2R8G8B8A8SrgbBlock, // TextureFormat::ETC2RGBA8UnormSrgb
            vk::Format::eEacR11UnormBlock, // TextureFormat::EACR11Unorm
            vk::Format::eEacR11SnormBlock, // TextureFormat::EACR11Snorm
            vk::Format::eEacR11G11UnormBlock, // TextureFormat::EACRG11Unorm
            vk::Format::eEacR11G11SnormBlock, // TextureFormat::EACRG11Snorm
            vk::Format::eAstc4x4UnormBlock, // TextureFormat::ASTC4x4Unorm
            vk::Format::eAstc4x4SrgbBlock, // TextureFormat::ASTC4x4UnormSrgb
            vk::Format::eAstc5x4UnormBlock, // TextureFormat::ASTC5x4Unorm
            vk::Format::eAstc5x4SrgbBlock, // TextureFormat::ASTC5x4UnormSrgb
            vk::Format::eAstc5x5UnormBlock, // TextureFormat::ASTC5x5Unorm
            vk::Format::eAstc5x5SrgbBlock, // TextureFormat::ASTC5x5UnormSrgb
            vk::Format::eAstc6x5UnormBlock, // TextureFormat::ASTC6x5Unorm
            vk::Format::eAstc6x5SrgbBlock, // TextureFormat::ASTC6x5UnormSrgb
            vk::Format::eAstc6x6UnormBlock, // TextureFormat::ASTC6x6Unorm
            vk::Format::eAstc6x6SrgbBlock, // TextureFormat::ASTC6x6UnormSrgb
            vk::Format::eAstc8x5UnormBlock, // TextureFormat::ASTC8x5Unorm
            vk::Format::eAstc8x5SrgbBlock, // TextureFormat::ASTC8x5UnormSrgb
            vk::Format::eAstc8x6UnormBlock, // TextureFormat::ASTC8x6Unorm
            vk::Format::eAstc8x6SrgbBlock, // TextureFormat::ASTC8x6UnormSrgb
            vk::Format::eAstc8x8UnormBlock, // TextureFormat::ASTC8x8Unorm
            vk::Format::eAstc8x8SrgbBlock, // TextureFormat::ASTC8x8UnormSrgb
            vk::Format::eAstc10x5UnormBlock, // TextureFormat::ASTC10x5Unorm
            vk::Format::eAstc10x5SrgbBlock, // TextureFormat::ASTC10x5UnormSrgb
            vk::Format::eAstc10x6UnormBlock, // TextureFormat::ASTC10x6Unorm
            vk::Format::eAstc10x6SrgbBlock, // TextureFormat::ASTC10x6UnormSrgb
            vk::Format::eAstc10x8UnormBlock, // TextureFormat::ASTC10x8Unorm
            vk::Format::eAstc10x8SrgbBlock, // TextureFormat::ASTC10x8UnormSrgb
            vk::Format::eAstc10x10UnormBlock, // TextureFormat::ASTC10x10Unorm
            vk::Format::eAstc10x10SrgbBlock, // TextureFormat::ASTC10x10UnormSrgb
            vk::Format::eAstc12x10UnormBlock, // TextureFormat::ASTC12x10Unorm
            vk::Format::eAstc12x10SrgbBlock, // TextureFormat::ASTC12x10UnormSrgb
            vk::Format::eAstc12x12UnormBlock, // TextureFormat::ASTC12x12Unorm
            vk::Format::eAstc12x12SrgbBlock, // TextureFormat::ASTC12x12UnormSrgb
        };
    } // namespace

    vk::Format translateTextureFormat(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if (index >= Private::TextureFormatCount || VKTextureFormatTable[index] == vk::Format::eUndefined)
        {
            throw std::runtime_error(("VKEnumUtils: unsupported texture format for Vulkan phase 1: " + eastl::to_string(static_cast<uint32_t>(format))).c_str());
        }
        return VKTextureFormatTable[index];
    }

    vk::ImageType translateTextureDimension(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::e1D: return vk::ImageType::e1D;
        case TextureDimension::e2D: return vk::ImageType::e2D;
        case TextureDimension::e3D: return vk::ImageType::e3D;
        default: throw std::runtime_error("VKEnumUtils: unsupported texture dimension.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported texture view dimension.");
        }
    }

    vk::Filter translateFilter(FilterMode filter)
    {
        switch (filter)
        {
        case FilterMode::Nearest: return vk::Filter::eNearest;
        case FilterMode::Linear: return vk::Filter::eLinear;
        default: throw std::runtime_error("VKEnumUtils: unsupported filter mode.");
        }
    }

    vk::SamplerMipmapMode translateMipmapFilter(MipmapFilterMode filter)
    {
        switch (filter)
        {
        case MipmapFilterMode::Nearest: return vk::SamplerMipmapMode::eNearest;
        case MipmapFilterMode::Linear: return vk::SamplerMipmapMode::eLinear;
        default: throw std::runtime_error("VKEnumUtils: unsupported mipmap filter mode.");
        }
    }

    vk::SamplerAddressMode translateAddressMode(AddressMode mode)
    {
        switch (mode)
        {
        case AddressMode::Repeat: return vk::SamplerAddressMode::eRepeat;
        case AddressMode::MirrorRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
        case AddressMode::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
        default: throw std::runtime_error("VKEnumUtils: unsupported address mode.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported compare function.");
        }
    }

    vk::VertexInputRate translateVertexStepMode(VertexStepMode mode)
    {
        switch (mode)
        {
        case VertexStepMode::Vertex: return vk::VertexInputRate::eVertex;
        case VertexStepMode::Instance: return vk::VertexInputRate::eInstance;
        case VertexStepMode::VertexBufferNotUsed: return vk::VertexInputRate::eVertex;
        default: throw std::runtime_error("VKEnumUtils: unsupported vertex step mode.");
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
            default: throw std::runtime_error("VKEnumUtils: unsupported vertex format.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported primitive topology.");
        }
    }

    vk::FrontFace translateFrontFace(FrontFace frontFace)
    {
        switch (frontFace)
        {
        case FrontFace::CCW: return vk::FrontFace::eCounterClockwise;
        case FrontFace::CW: return vk::FrontFace::eClockwise;
        default: throw std::runtime_error("VKEnumUtils: unsupported front face.");
        }
    }

    vk::CullModeFlags translateCullMode(CullMode cullMode)
    {
        switch (cullMode)
        {
        case CullMode::None: return vk::CullModeFlagBits::eNone;
        case CullMode::Front: return vk::CullModeFlagBits::eFront;
        case CullMode::Back: return vk::CullModeFlagBits::eBack;
        default: throw std::runtime_error("VKEnumUtils: unsupported cull mode.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported bind group layout entry.");
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

    vk::PipelineStageFlags2 translateShaderStagesToPipelineStages(ShaderStageFlags stages)
    {
        vk::PipelineStageFlags2 flags{};
        if ((stages & ShaderStage::Vertex) != 0u)
        {
            flags |= vk::PipelineStageFlagBits2::eVertexShader;
        }
        if ((stages & ShaderStage::Fragment) != 0u)
        {
            flags |= vk::PipelineStageFlagBits2::eFragmentShader;
        }
        if ((stages & ShaderStage::Compute) != 0u)
        {
            flags |= vk::PipelineStageFlagBits2::eComputeShader;
        }
        if ((stages & ShaderStage::Hull) != 0u)
        {
            flags |= vk::PipelineStageFlagBits2::eTessellationControlShader;
        }
        if ((stages & ShaderStage::Domain) != 0u)
        {
            flags |= vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
        }
        return flags == vk::PipelineStageFlags2{} ? vk::PipelineStageFlagBits2::eAllGraphics : flags;
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
        default: throw std::runtime_error("VKEnumUtils: unsupported blend factor.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported blend operation.");
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
        default: throw std::runtime_error("VKEnumUtils: unsupported load op.");
        }
    }

    vk::AttachmentStoreOp translateStoreOp(StoreOp storeOp)
    {
        switch (storeOp)
        {
        case StoreOp::Store: return vk::AttachmentStoreOp::eStore;
        case StoreOp::Discard:
        case StoreOp::Undefined: return vk::AttachmentStoreOp::eDontCare;
        default: throw std::runtime_error("VKEnumUtils: unsupported store op.");
        }
    }

    vk::IndexType translateIndexFormat(IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::Uint16: return vk::IndexType::eUint16;
        case IndexFormat::Uint32: return vk::IndexType::eUint32;
        default: throw std::runtime_error("VKEnumUtils: unsupported index format.");
        }
    }

    bool hasStencilAspect(TextureFormat format)
    {
        return Private::hasStencilAspect(format);
    }

    bool isDepthStencilFormat(TextureFormat format)
    {
        return Private::isDepthStencilFormat(format);
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
