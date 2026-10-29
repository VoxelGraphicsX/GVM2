#pragma once
#include "GRenderSet.hpp"
#include "GSync.hpp"
#include <EASTL/map.h>
#include <EASTL/shared_ptr.h>
#include <GVMRHI/GVMRHI.hpp>
namespace GVM::Core
{
    /** Holds Slot binding state that deferred tasks read and update in pass order, independent of the shader-class instance lifetime. */
    struct SlotTaskState
    {
        eastl::map<uint, GVM::RHI::BindGroup> bindGroups;
        bool vertexBufferSet = false;
        bool indexBufferSet = false;
    };

    class IComputeClass
    {
    protected:
        GVM::RHI::ShaderModule computeShader;
        GVM::RHI::ComputePipelineDescriptor pipelineDescriptor;
        GVM::RHI::ComputePipeline pipeline;
        eastl::shared_ptr<SlotTaskState> mSlotState = eastl::make_shared<SlotTaskState>();
        uint32_t workGroupX = 1;
        uint32_t workGroupY = 1;
        uint32_t workGroupZ = 1;

        eastl::intrusive_ptr<RenderSet> mRenderSet;
        int mRenderSetBindGroupIndex = -1;
        ComputePassTaskDescriptor setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup);

    public:
        GVM::RHI::Device mDevice;
        /** Returns the compute pipeline created during initialization so task lambdas can bind it explicitly. */
        const GVM::RHI::ComputePipeline &getPipeline() const { return pipeline; }
        template <class T>
        ComputePassTaskDescriptor setBindGroup(uint32_t slot, const T &bindgroupPtr)
        {
            return setBindGroup(slot, bindgroupPtr->mBindGroup);
        }
        ComputePassTaskDescriptor run(uint32_t x, uint32_t y, uint32_t z);
        ComputePassTaskDescriptor run(GVM::RHI::BufferRange indirectBuffer);
        /** Applies pass-ordered Slot bindings; the indirect buffer is validated and retained when the task is recorded into a pass. */
        ComputePassTaskDescriptor dispatchWorkgroupsIndirect(GVM::RHI::BufferRange indirectBuffer);
    };

    class IRenderClass
    {
    protected:
        GVM::RHI::ShaderModule vertexShader;
        GVM::RHI::ShaderModule fragmentShader;
        GVM::RHI::RenderPipelineDescriptor pipelineDescriptor;
        GVM::RHI::RenderPipeline pipeline;
        eastl::shared_ptr<SlotTaskState> mSlotState = eastl::make_shared<SlotTaskState>();

        void setBlendState(uint index, const GVM::RHI::BlendState &state);
        void setPrimitiveTopology(RHI::PrimitiveTopology topology);
        void setCullMode(RHI::CullMode cullMode);
        /// Enables or disables pipeline depth writes without changing depth testing or compare state.
        void setDepthWriteEnabled(bool enabled);
        /// Sets the pipeline depth compare function when a render class requires explicit depth testing behavior.
        void setDepthCompareFunction(RHI::CompareFunction function);
        /// Controls whether RenderSet draw entry points automatically bind the RenderSet vertex buffer.
        ///
        /// Keep this enabled for indexed or vertex-input RenderSet draws. Disable it for procedural
        /// vertex shaders that only need the RenderSet argument buffer, because Metal uses the same
        /// vertex buffer index space for argument buffers and ordinary vertex buffers.
        void setRenderSetVertexBufferBindingEnabled(bool enabled);
        eastl::intrusive_ptr<RenderSet> mRenderSet;
        int mRenderSetBindGroupIndex = -1;
        bool mRenderSetVertexBufferBindingEnabled = true;
        RenderPassTaskDescriptor setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup);

    public:
        GVM::RHI::Device mDevice;
        /** Returns the render pipeline created during initialization so task lambdas can bind it explicitly. */
        const GVM::RHI::RenderPipeline &getPipeline() const { return pipeline; }
        template <class T>
        RenderPassTaskDescriptor setBindGroup(uint32_t slot, const T &bindgroupPtr)
        {
            return setBindGroup(slot, bindgroupPtr->mBindGroup);
        }
        // Deprecated legacy RenderSet-only entry. Requires a bound RenderSet and may be removed at any time.
        RenderPassTaskDescriptor run();
        RenderPassTaskDescriptor run(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        RenderPassTaskDescriptor run(uint indexCount, uint instanceCount, uint firstIndex, int baseVertex, uint firstInstance);
        RenderPassTaskDescriptor run(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride);
        RenderPassTaskDescriptor drawIndirect(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride);
        /** Applies pass-ordered Slot bindings; command and count buffers are validated and retained when the task is recorded into a pass. */
        RenderPassTaskDescriptor drawIndirectCount(RHI::BufferRange commands, RHI::BufferRange count, uint32_t maxDrawCount, uint32_t stride = 0);
        /** Applies pass-ordered Slot bindings; index, command, and count buffers are validated and retained when the task is recorded into a pass. */
        RenderPassTaskDescriptor drawIndexedIndirectCount(RHI::BufferRange indices, RHI::IndexFormat format,
            RHI::BufferRange commands, RHI::BufferRange count, uint32_t maxDrawCount, uint32_t stride = 0);
        RenderPassTaskDescriptor setVertexBuffer(GVM::RHI::BufferRange buffer, uint32_t slot = 0);
        RenderPassTaskDescriptor setIndexBuffer(GVM::RHI::BufferRange buffer, GVM::RHI::IndexFormat format = GVM::RHI::IndexFormat::Uint32);
    };

    class IPixelLocalRenderClass
    {
    protected:
        GVM::RHI::ShaderModule fragmentShader;
        GVM::RHI::RenderPipelineDescriptor pipelineDescriptor;
        GVM::RHI::RenderPipeline pipeline;
        eastl::shared_ptr<SlotTaskState> mSlotState = eastl::make_shared<SlotTaskState>();

        eastl::intrusive_ptr<RenderSet> mRenderSet;
        int mRenderSetBindGroupIndex = -1;
        RenderPassTaskDescriptor setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup);

    public:
        GVM::RHI::Device mDevice;
        template <class T>
        RenderPassTaskDescriptor setBindGroup(uint32_t slot, const T &bindgroupPtr)
        {
            return setBindGroup(slot, bindgroupPtr->mBindGroup);
        }
        RenderPassTaskDescriptor run();
    };

} // namespace GVM::Core
