#pragma once
#include <cstdint>
#include <string>
namespace UGLC::CodeGen
{
    const std::string mUGLCTORName = "UGLCTORFUNC";
    const std::string mUGLBindGroupBaseName = "UGL::IBindGroup";
    const std::string mUGLBindGroupName = "UGL::BindGroup";
    const std::string mUGLFrameBufferBaseName = "UGL::IFrameBuffer";
    const std::string mUGLRenderClassBaseName = "UGL::IRenderClass";
    const std::string mUGLPixelLocalRenderClassBaseName = "UGL::IPixelLocalRenderClass";
    const std::string mUGLComputeClassBaseName = "UGL::IComputeClass";
    const std::string mUGLRenderSetBaseName = "UGL::IRenderSet";
    const std::string mUGLCTORFunctionName = "create";
    const std::string mUGLColorAttachmentName = "UGL::ColorAttachment";
    const std::string mUGLDepthAttachmentName = "UGL::DepthStencilAttachment";
    const std::string mUGLPixelLocalColorAttachmentName = "UGL::PixelLocalColorAttachment";
    const std::string mUGLPixelLocalDepthAttachmentName = "UGL::PixelLocalDepthAttachment";
    const std::string mUGLShaderUniformBufferName = "UGL::UniformBuffer";
    const std::string mUGLShaderStructuredBufferName = "UGL::StructuredBuffer";
    const std::string mUGLShaderRWStructuredBufferName = "UGL::RWStructuredBuffer";
    const std::string mUGLShaderLegacyStorageBufferName = "UGL::StorageBuffer";
    const std::string mUGLShaderTexture2DName = "UGL::Texture2D";
    const std::string mUGLShaderRWTexture2DName = "UGL::RWTexture2D";
    const std::string mUGLShaderSamplerName = "UGL::Sampler";
    const std::string mUGLShaderAtomicName = "UGL::Atomic";
    const std::string mUGLHostBufferName = "UGL::Buffer";
    const std::string mUGLHostBufferRangeName = "UGL::BufferRange";
    const std::string mUGLHostTextureName = "UGL::Texture";
    const std::string mUGLHostTextureViewName = "UGL::TextureView";
    const std::string mUGLHostDeviceName = "UGL::Device";
    const std::string mUGLHostQueueName = "UGL::Queue";
    const std::string mUGLFunctionWaveGetLaneIndexName = "UGL::WaveGetLaneIndex";
    const std::string mUGLFunctionWaveGetLaneCountName = "UGL::WaveGetLaneCount";
    const std::string mUGLFunctionWaveReadLaneAtName = "UGL::WaveReadLaneAt";
    const std::string mUGLFunctionWaveReadLaneFirstName = "UGL::WaveReadLaneFirst";
    const std::string mUGLFunctionWaveActiveBallotName = "UGL::WaveActiveBallot";
    const std::string mUGLFunctionWaveActiveCountBitsName = "UGL::WaveActiveCountBits";
    const std::string mUGLFunctionWavePrefixCountBitsName = "UGL::WavePrefixCountBits";
    const std::string mUGLFunctionWavePrefixSumName = "UGL::WavePrefixSum";
    const std::string mUGLFunctionWaveMatchName = "UGL::WaveMatch";
    const std::string mUGLFunctionWaveReadAcrossXName = "UGL::WaveReadAcrossX";
    const std::string mUGLFunctionWaveReadAcrossYName = "UGL::WaveReadAcrossY";
    const std::string mUGLFunctionWaveReadAcrossDiagonalName = "UGL::WaveReadAcrossDiagonal";
    const std::string mUGLFunctionQuadReadLaneAtName = "UGL::QuadReadLaneAt";
    const std::string mUGLFunctionQuadReadAcrossXName = "UGL::QuadReadAcrossX";
    const std::string mUGLFunctionQuadReadAcrossYName = "UGL::QuadReadAcrossY";
    const std::string mUGLFunctionQuadReadAcrossDiagonalName = "UGL::QuadReadAcrossDiagonal";
    const std::string mUGLFunctionDiscardFragmentName = "UGL::discard_fragment";
    const std::string mUGLFunctionClipName = "UGL::clip";
    const std::string mUGLFunctionGroupMemoryBarrierName = "UGL::GroupMemoryBarrier";
    const std::string mUGLFunctionGroupMemoryBarrierWithGroupSyncName = "UGL::GroupMemoryBarrierWithGroupSync";
    const std::string mUGLFunctionDeviceMemoryBarrierName = "UGL::DeviceMemoryBarrier";
    const std::string mUGLFunctionDeviceMemoryBarrierWithGroupSyncName = "UGL::DeviceMemoryBarrierWithGroupSync";
    const std::string mUGLFunctionAllMemoryBarrierWithGroupSyncName = "UGL::AllMemoryBarrierWithGroupSync";
    const std::string mUGLAttributeSlotName = "Slot";
    const std::string mUGLAttributeBindingName = "Binding";
    const std::string mUGLAttributeVertexInputName = "VertexInput";
    const std::string mUGLAttributeVertexIDName = "VertexID";
    const std::string mUGLAttributePositionName = "Position";
    const std::string mUGLAttributeInstanceIDName = "InstanceID";
    const std::string mUGLAttributePrimitiveIDName = "PrimitiveID";
    const std::string mUGLAttributeRenderEntityIDName = "RenderEntityID";
    const std::string mUGLAttributeRenderEntityInstanceIDName = "RenderEntityInstanceID";
    const std::string mUGLAttributeAttributeName = "Attribute";
    const std::string mUGLAttributeBarycentricsName = "Barycentrics";
    const std::string mUGLAttributePixelLocalInputName = "PixelLocalInput";
    const std::string mUGLAttributePixelCoordName = "PixelCoord";
    const std::string mUGLAttributeSampleIndexName = "SampleIndex";
    const std::string mUGLAttributeDispatchThreadIDName = "DispatchThreadID";
    const std::string mUGLAttributeGroupThreadIDName = "GroupThreadID";
    const std::string mUGLAttributeWaveLaneIndexName = "WaveLaneIndex";
    const std::string mUGLAttributeWaveLaneCountName = "WaveLaneCount";
    const std::string mUGLAttributeGroupIDName = "GroupID";
    const std::string mUGLAttributeGroupIndexName = "GroupIndex";
    const std::string mUGLAttributeINName = "IN";
    const std::string mUGLAttributeOUTName = "OUT";
    const std::string mUGLAttributeINOUTName = "INOUT";
    const std::string mUGLAttributeExportName = "Export";
    const std::string mUGLAttributeDomainLocationName = "DomainLocation";
    const std::string mUGLVertexShaderFunctionName = "vertex";
    const std::string mUGLHullShaderFunctionName = "hull";
    const std::string mUGLDomainShaderFunctionName = "domain";
    const std::string mUGLFragmentShaderFunctionName = "fragment";
    const std::string mUGLPixelShaderFunctionName = "pixel";
    const std::string mUGLComputeShaderFunctionName = "compute";
    const std::string mUGLConstantsHullShaderFunctionName = "ConstantsHullFunc";
    const std::string mUGLAbstractRendererClassName = "UGL::AbstractRenderer";
    const std::string mUGLDeviceImplName = "UGL::DeviceImpl";
    const std::string mUGLRenderSetName = "UGL::RenderSet";
    const std::string mUGLRenderSetVertexBuffer = "RenderSetVertexBuffer";
    const std::string mUGLRenderSetIndexBuffer = "RenderSetIndexBuffer";
    const std::string mUGLRenderSetBufferComponentName = "BufferComponent";
    const std::string mUGLRenderSetTextureComponentName = "TextureComponent";
    const std::string mUGLTexture2DArrayName = "Texture2DArray";
    const std::string mUGLRWTexture2DArrayName = "RWTexture2DArray";
    const std::string mUGLTexture3DName = "Texture3D";
    const std::string mUGLRWTexture3DName = "RWTexture3D";

    const std::string mUGLRenderSetBufferComponentClassName = "UGL::BufferComponent";
    const std::string mUGLRenderSetTextureComponentClassName = "UGL::TextureComponent";
    const std::string mUGLShaderTexture2DArrayName = "UGL::Texture2DArray";
    const std::string mUGLShaderRWTexture2DArrayName = "UGL::RWTexture2DArray";
    const std::string mUGLShaderTexture3DName = "UGL::Texture3D";
    const std::string mUGLShaderRWTexture3DName = "UGL::RWTexture3D";

    const std::string mUGLDepthStencilAttachmentNoWrite = "UGL::DepthStencilAttachmentWritePattern::None";
    const std::string mUGLDepthStencilAttachmentWriteLess = "UGL::DepthStencilAttachmentWritePattern::Less";
    const std::string mUGLDepthStencilAttachmentWriteGreater = "UGL::DepthStencilAttachmentWritePattern::Greater";
    const std::string mUGLPixelLocalLoadDontCare = "UGL::PixelLocalLoad::DontCare";
    const std::string mUGLPixelLocalLoadClear = "UGL::PixelLocalLoad::Clear";
    const std::string mUGLPixelLocalLoadLoad = "UGL::PixelLocalLoad::Load";
    const std::string mUGLPixelLocalStoreDiscard = "UGL::PixelLocalStore::Discard";
    const std::string mUGLPixelLocalStoreStore = "UGL::PixelLocalStore::Store";


    const std::string mUGLShaderUniformBufferDataPacker = "UGL::Private::UniformBufferDataPacker";
    const std::string mUGLShaderBaseTexture2DAccessPacker = "UGL::Private::BaseTexture2DAccessPacker";
    const std::string mUGLShaderTexture2DAccessPacker = "UGL::Private::Texture2DAccessPacker";
    const std::string mUGLShaderRWTexture2DAccessPacker = "UGL::Private::RWTexture2DAccessPacker";

    const std::string mUGLShaderBaseTexture2DArrayAccessPacker = "UGL::Private::BaseTexture2DArrayAccessPacker";
    const std::string mUGLShaderTexture2DArrayAccessPacker = "UGL::Private::Texture2DArrayAccessPacker";
    const std::string mUGLShaderRWTexture2DArrayAccessPacker = "UGL::Private::RWTexture2DArrayAccessPacker";
    const std::string mUGLShaderTexture3DAccessPacker = "UGL::Private::Texture3DAccessPacker";
    const std::string mUGLShaderRWTexture3DAccessPacker = "UGL::Private::RWTexture3DAccessPacker";


    const std::string VertexShaderEntryName = "vertexMain";
    const std::string VertexFuncNameInRenderClass = "vertex";
    const std::string FragmentShaderEntryName = "fragmentMain";
    const std::string FragmentFuncNameInRenderClass = "fragment";
    const std::string PixelFuncNameInPixelLocalRenderClass = "pixel";
    const std::string ComputeShaderEntryName = "computeMain";
    const std::string ComputeFuncNameInRenderClass = "compute";

    const std::string mUGLExportFileMacro = "__UGL_EXPORTS_HPP";


    static constexpr int MaxBindGroupCount = 8;
    static constexpr int MaxBindGroupResourceBindingCount = 32;
    static constexpr std::uint32_t RenderTextureMaxTextureCountPerEntity = 8u;
} // namespace UGLC::CodeGen
