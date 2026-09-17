#pragma once
#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
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
        void trackUsage(MTL::RenderCommandEncoder *encoder);
        void trackUsage(MTL::ComputeCommandEncoder *encoder);

    private:
        MDevice *mDevice = nullptr;
        BindGroupDescriptor mDescriptor;
        MTL::Buffer *mNativeBuffer = nullptr;
        eastl::vector<MTL::Buffer *> mNestedArgumentBuffers;
        eastl::vector<eastl::intrusive_ptr<ResourceUseInfo>> mAllResourceUseInfos;
    };

} // namespace GVM::RHI::Metal
