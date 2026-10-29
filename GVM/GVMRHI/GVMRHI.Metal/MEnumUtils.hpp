#pragma once
#include "../Private/GEnumUtils.hpp"
#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    MTL::ClearColor translateClearColorToMTL(const Color &color);
    MTL::LoadAction translateLoadActionToMTL(LoadOp op);
    MTL::StoreAction translateStoreActionToMTL(StoreOp op);
    MTL::PixelFormat translateTextureFormatToMTL(TextureFormat format);
    TextureFormat translateMTLTextureFormatToGVM(MTL::PixelFormat format);
    MTL::TextureType translateTextureViewDimensionToMTL(TextureViewDimension dimension);
    MTL::TextureType translateTextureDimensionToMTL(TextureDimension dimension, uint32_t arrayLayers);
    MTL::TextureUsage translateTextureUsageToMTL(TextureUsageFlags usage);
    MTL::BlendFactor translateBlendFactorToMTL(BlendFactor factor);
    MTL::BlendOperation translateBlendOperationToMTL(BlendOperation operation);
    MTL::VertexStepFunction translateVertexStepModeToMTL(VertexStepMode mode);
    MTL::VertexFormat translateVertexFormatToMTL(VertexFormat format);
    MTL::IndexType translateIndexFormatToMTL(IndexFormat format);
    MTL::SamplerMinMagFilter translateSamplerFilterToMTL(FilterMode filter);
    MTL::SamplerMipFilter translateSamplerMipFilterToMTL(MipmapFilterMode filter);
    MTL::SamplerAddressMode translateAddressModeToMTL(AddressMode mode);
    MTL::CompareFunction translateCompareFunctionToMTL(CompareFunction function);
    MTL::ResourceUsage translateStorageTextureAccessToMTL(StorageTextureAccess access);
    MTL::ResourceUsage translateStorageBufferAccessToMTL(StorageBufferAccess access);
    NS::String *translateStringToNSString(const eastl::string &str);
    MTL::CullMode translateCullMode(CullMode cullMode);
    MTL::PrimitiveType translatePrimitiveTopologyToMTL(PrimitiveTopology topology);
    MTL::Winding translateFrontFaceToMTL(FrontFace frontFace);


} // namespace GVM::RHI::Metal
