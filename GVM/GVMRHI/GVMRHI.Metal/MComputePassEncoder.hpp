#pragma once
#include "MDefines.hpp"
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MComputePassEncoder final : public ComputePassEncoderImpl
    {
    public:
        MComputePassEncoder();
        ~MComputePassEncoder();
        void init(MDevice *device, GVM::RHI::CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor);
        virtual void setPipeline(ComputePipeline pipeline) override;
        virtual void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        virtual void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) override;
        virtual void dispatchWorkgroupsIndirect(BufferRange indirectBuffer) override;
        virtual void end() override;
        MTL::ComputeCommandEncoder *getNativeEncoder() const;

    private:
        MTL::ComputeCommandEncoder *mNativeComputeEncoder = nullptr;
        MDevice *mDevice = nullptr;
        MCommandEncoder *mWeakCommandEncoder = nullptr;
        MTL::Size mLastThreadGroupSize;
        MTL::CommandBuffer *mCommandBuffer;
        struct CommandCacheData
        {
            ComputePipeline pipeline = nullptr;
            eastl::unordered_map<std::uint32_t, BindGroup> bindgroups;
        } mCacheData{};
        bool mEnded = false;
    };

} // namespace GVM::RHI::Metal
