#pragma once
#include "MDefines.hpp"
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/ComputePassValidator.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{

    class MArguments;

    class MComputePassEncoder final : public ComputePassEncoderImpl
    {
    public:
        MComputePassEncoder();
        ~MComputePassEncoder();
        void init(MDevice *device, GVM::RHI::CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor);
        virtual void setPipeline(ComputePipeline pipeline) override;
        virtual void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        /** Records and binds a snapshot compatible with the selected compute pipeline. */
        void setArguments(Arguments arguments) override;
        virtual void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) override;
        virtual void dispatchWorkgroupsIndirect(BufferRange indirectBuffer) override;
        virtual void end() override;
        MTL::ComputeCommandEncoder *getNativeEncoder() const;

    private:
        /** Encodes a retained native dispatch input after public handle resolution. */
        void recordDispatchIndirect(MTL::Buffer &buffer, uint64_t offset);
        /** Returns whether the selected pipeline declares an Args layout that requires a snapshot. */
        bool pipelineDeclaresArguments() const;
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
        /// Throws std::runtime_error when a pass-scoped API is called after end().
        void ensureOpen(const char *apiName) const;
        MArguments *mCurrentArguments = nullptr;
        Private::ComputePassValidator mValidator;
    };

} // namespace GVM::RHI::Metal
