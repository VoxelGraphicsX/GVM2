#pragma once

#include "VKCommon.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

namespace GVM::RHI::Vulkan
{
    vk::BufferUsageFlags translateBufferUsage(BufferUsageFlags usage);
    vk::ImageUsageFlags translateTextureUsage(TextureUsageFlags usage, TextureFormat format);
    vk::Format translateTextureFormat(TextureFormat format);
    vk::ImageType translateTextureDimension(TextureDimension dimension);
    vk::ImageViewType translateTextureViewDimension(TextureViewDimension dimension);
    vk::Filter translateFilter(FilterMode filter);
    vk::SamplerMipmapMode translateMipmapFilter(MipmapFilterMode filter);
    vk::SamplerAddressMode translateAddressMode(AddressMode mode);
    vk::CompareOp translateCompareFunction(CompareFunction function);
    vk::VertexInputRate translateVertexStepMode(VertexStepMode mode);
    vk::VertexInputAttributeDescription translateVertexAttribute(const VertexAttribute &attribute, uint32_t binding);
    vk::PrimitiveTopology translatePrimitiveTopology(PrimitiveTopology topology);
    vk::FrontFace translateFrontFace(FrontFace frontFace);
    vk::CullModeFlags translateCullMode(CullMode cullMode);
    vk::DescriptorType translateDescriptorType(const BindGroupLayoutEntry &entry);
    vk::ShaderStageFlags translateShaderStages(ShaderStageFlags stages);
    vk::PipelineStageFlags translateShaderStagesToPipelineStages(ShaderStageFlags stages);
    vk::BlendFactor translateBlendFactor(BlendFactor factor);
    vk::BlendOp translateBlendOperation(BlendOperation operation);
    vk::ColorComponentFlags translateColorWriteMask(ColorWriteMaskFlags writeMask);
    vk::AttachmentLoadOp translateLoadOp(LoadOp loadOp);
    vk::AttachmentStoreOp translateStoreOp(StoreOp storeOp);
    vk::IndexType translateIndexFormat(IndexFormat format);
    vk::ImageAspectFlags resolveTextureAspect(TextureFormat format, TextureAspectFlags aspectMask);
    bool isDepthStencilFormat(TextureFormat format);
    bool hasStencilAspect(TextureFormat format);
} // namespace GVM::RHI::Vulkan
