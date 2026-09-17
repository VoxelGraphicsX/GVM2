#pragma once
#include "MDefines.hpp"
#include <EASTL/unordered_set.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MRenderPipeline final : public RenderPipelineImpl
    {
    public:
        MRenderPipeline();
        ~MRenderPipeline();
        void init(MDevice *device, const RenderPipelineDescriptor &descriptor);
        MTL::RenderPipelineState *getNativePipelineState() const;
        MTL::DepthStencilState *getNativeDepthState() const;
        int getVertexBufferCount() const;
        MTL::CullMode getCullMode() const;
        MTL::PrimitiveType getPrimitiveType() const;
        MTL::Winding getFrontFaceWinding() const;
        PrimitiveTopology getPrimitiveTopology() const;
        IndexFormat getStripIndexFormat() const;
        float getDepthBias() const;
        float getDepthBiasSlopeScale() const;
        float getDepthBiasClamp() const;
        bool usesVertexArgumentBuffer(uint32_t index) const;
        bool usesFragmentArgumentBuffer(uint32_t index) const;
        /// Reports whether this pipeline executes pixel-local drawPixels through a normal fragment framebuffer-fetch pipeline.
        bool isPixelLocalFramebufferFetchPipeline() const;
        /// Returns the RHI descriptor used to create this Metal pipeline.
        const RenderPipelineDescriptor &getDescriptor() const;

    private:
        MDevice *mDevice = nullptr;
        RenderPipelineDescriptor mDescriptor;
        MTL::RenderPipelineState *mNativePipelineState = nullptr;
        MTL::DepthStencilState *mNativeDepthStencilState = nullptr;
        MTL::CullMode mCullMode = MTL::CullModeNone;
        MTL::PrimitiveType mPrimitiveType = MTL::PrimitiveType::PrimitiveTypeTriangle;
        MTL::Winding mFrontFaceWinding = MTL::Winding::WindingCounterClockwise;
        PrimitiveTopology mPrimitiveTopology = PrimitiveTopology::TriangleList;
        IndexFormat mStripIndexFormat = IndexFormat::Undefined;
        float mDepthBias = 0.0f;
        float mDepthBiasSlopeScale = 0.0f;
        float mDepthBiasClamp = 0.0f;
        eastl::unordered_set<uint32_t> mVertexArgumentBufferIndices;
        eastl::unordered_set<uint32_t> mFragmentArgumentBufferIndices;
        ShaderModule mInjectedFullscreenVertexShader = nullptr;
        bool mIsPixelLocalFramebufferFetchPipeline = false;
    };

} // namespace GVM::RHI::Metal
