#pragma once
#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/RenderPassRecorder.hpp>
#include <Metal/Metal.hpp>
#include <vector>
namespace GVM::RHI::Metal
{

    class MBindGroup final : public BindGroupImpl
    {
    public:
        struct ResourceUseInfo : public GVM::RHI::RefCountedObject
        {
            eastl::vector<MTL::Resource *> resources;
            MTL::ResourceUsage usage;
        };
        MBindGroup();
        ~MBindGroup();
        void init(MDevice *device, const BindGroupDescriptor &descriptor);
        MTL::Buffer *getNativeBuffer() const;
        const BindGroupDescriptor &getDescriptor() const;
        /** Identifies the owning device when resolving compatibility Args entries. */
        MDevice &getDevice() const { return *mDevice; }
        /** Returns the render-stage texture uses of this group in backend-neutral form for render-pass validation. */
        eastl::span<const Private::PassTextureUse> getRenderTextureUses() const { return mRenderTextureUses; }
        /** Returns the texture subresource uses this group exposes to compute shaders. */
        eastl::span<const Private::PassTextureUse> getComputeTextureUses() const { return mComputeTextureUses; }
        /** Declares indirect resources in the pipeline stages actually bound for this render group. */
        void trackUsage(MTL::RenderCommandEncoder *encoder, MTL::RenderStages stages);
        void trackUsage(MTL::ComputeCommandEncoder *encoder);

    private:
        MDevice *mDevice = nullptr;
        BindGroupDescriptor mDescriptor;
        MTL::Buffer *mNativeBuffer = nullptr;
        eastl::vector<Private::PassTextureUse> mRenderTextureUses;
        eastl::vector<Private::PassTextureUse> mComputeTextureUses;
        eastl::vector<MTL::Buffer *> mNestedArgumentBuffers;
        eastl::vector<eastl::intrusive_ptr<ResourceUseInfo>> mAllResourceUseInfos;
    };

} // namespace GVM::RHI::Metal
