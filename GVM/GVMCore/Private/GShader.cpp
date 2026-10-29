#include "GShader.hpp"
#include <stdexcept>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
    namespace
    {
        /** Captures the pipeline and pass-ordered Slot state bound before one task's draw or dispatch. */
        template <class TPipeline>
        struct SlotBinding
        {
            TPipeline pipeline;
            eastl::shared_ptr<SlotTaskState> state;
            eastl::intrusive_ptr<RenderSet> renderSet;
            int renderSetBindGroupIndex;

            /** Binds the pipeline, every recorded Slot group, and the reserved RenderSet group when present. */
            template <class TPass>
            void bind(TPass &pass) const
            {
                pass->setPipeline(pipeline);
                for (auto &[index, bindGroup] : state->bindGroups)
                {
                    pass->setBindGroup(bindGroup, index);
                }
                if (renderSet != nullptr)
                {
                    pass->setBindGroup(renderSet->getBindGroup(), renderSetBindGroupIndex);
                }
            }
        };

        /** Captures the RenderSet default vertex and index buffers a render task binds after its Slot state. */
        struct RenderSlotBinding
        {
            SlotBinding<GVM::RHI::RenderPipeline> slots;
            bool renderSetVertexBufferBindingEnabled;
            bool bindIndexBuffer;

            /** Binds the pipeline and Slot groups, then the RenderSet group plus its default vertex/index buffers when present. */
            void bind(GVM::RHI::RenderPassEncoder &pass) const
            {
                slots.bind(pass);
                if (slots.renderSet != nullptr)
                {
                    if (renderSetVertexBufferBindingEnabled && slots.state->vertexBufferSet == false)
                    {
                        pass->setVertexBuffer(RHI::BufferRange(slots.renderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                    }
                    if (bindIndexBuffer && slots.state->indexBufferSet == false)
                    {
                        pass->setIndexBuffer(RHI::BufferRange(slots.renderSet->getIndexBuffer()), slots.renderSet->getIndexFormat());
                    }
                    slots.state->vertexBufferSet = slots.state->indexBufferSet = false;
                }
            }
        };
    }

    void IRenderClass::setBlendState(uint index, const GVM::RHI::BlendState &state)
    {
        this->pipelineDescriptor.fragment.targets[index].blend = state;
        this->pipelineDescriptor.fragment.targets[index].blendEnabled = true;
    }

    void IRenderClass::setPrimitiveTopology(RHI::PrimitiveTopology topology)
    {
        this->pipelineDescriptor.primitive.topology = topology;
    }

    void IRenderClass::setCullMode(RHI::CullMode cullMode)
    {
        this->pipelineDescriptor.primitive.cullMode = cullMode;
    }

    void IRenderClass::setDepthWriteEnabled(bool enabled)
    {
        this->pipelineDescriptor.depthStencil.depthWriteEnabled = enabled ? RHI::True : RHI::False;
    }

    void IRenderClass::setDepthCompareFunction(RHI::CompareFunction function)
    {
        this->pipelineDescriptor.depthStencil.depthCompare = function;
    }

    void IRenderClass::setRenderSetVertexBufferBindingEnabled(bool enabled)
    {
        mRenderSetVertexBufferBindingEnabled = enabled;
    }

    RenderPassTaskDescriptor IRenderClass::setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [state = mSlotState, slot, group = bindgroup, reservedSlot = mRenderSetBindGroupIndex](GVM::RHI::RenderPassEncoder)
        {
            if (reservedSlot == slot)
            {
                return;
            }
            state->bindGroups[slot] = group;
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run()
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [binding = RenderSlotBinding{{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
                           mRenderSetVertexBufferBindingEnabled, true}](GVM::RHI::RenderPassEncoder passEncoder) {
            if (binding.slots.renderSet == nullptr)
            {
                throw std::logic_error("IRenderClass::run() is a deprecated RenderSet-only entry and requires a bound RenderSet. Use an explicit run(...) overload or drawIndirect(...) instead.");
            }
            binding.bind(passEncoder);
            passEncoder->drawIndexedIndirect(
                binding.slots.renderSet->getRenderEntityInfoBuffer(),
                binding.slots.renderSet->getMaxEntityCount(),
                sizeof(RenderEntityInfo));
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [binding = RenderSlotBinding{{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
                           mRenderSetVertexBufferBindingEnabled, true},
            vertexCount, instanceCount, firstVertex, firstInstance](GVM::RHI::RenderPassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->draw(vertexCount, instanceCount, firstVertex, firstInstance);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(uint indexCount, uint instanceCount, uint firstIndex, int baseVertex, uint firstInstance)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [binding = RenderSlotBinding{{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
                           mRenderSetVertexBufferBindingEnabled, true},
            indexCount, instanceCount, firstIndex, baseVertex, firstInstance](GVM::RHI::RenderPassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [binding = RenderSlotBinding{{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
                           mRenderSetVertexBufferBindingEnabled, true},
            indirectBuffer, indirectCommandCount, stride](GVM::RHI::RenderPassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->drawIndexedIndirect(indirectBuffer, indirectCommandCount, stride);
        };
        return desp;
    }

    /** Applies pass-ordered Slot bindings; command and count buffers are validated and retained at record time. */
    RenderPassTaskDescriptor IRenderClass::drawIndirectCount(RHI::BufferRange commands, RHI::BufferRange count,
        uint32_t maxDrawCount, uint32_t stride)
    {
        RenderPassTaskDescriptor task;
        task.drawFn = [binding = SlotBinding<GVM::RHI::RenderPipeline>{pipeline, mSlotState, nullptr, -1},
            commands, count, maxDrawCount, stride](RHI::RenderPassEncoder pass)
        {
            binding.bind(pass);
            pass->drawIndirectCount(commands, count, maxDrawCount, stride);
        };
        return task;
    }

    /** Applies pass-ordered Slot bindings; index, command, and count buffers are validated and retained at record time. */
    RenderPassTaskDescriptor IRenderClass::drawIndexedIndirectCount(RHI::BufferRange indices, RHI::IndexFormat format,
        RHI::BufferRange commands, RHI::BufferRange count,
        uint32_t maxDrawCount, uint32_t stride)
    {
        RenderPassTaskDescriptor task;
        task.drawFn = [binding = SlotBinding<GVM::RHI::RenderPipeline>{pipeline, mSlotState, nullptr, -1},
            indices, format, commands, count, maxDrawCount, stride](RHI::RenderPassEncoder pass)
        {
            binding.bind(pass);
            pass->setIndexBuffer(indices, format);
            pass->drawIndexedIndirectCount(commands, count, maxDrawCount, stride);
        };
        return task;
    }

    RenderPassTaskDescriptor IRenderClass::drawIndirect(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [binding = RenderSlotBinding{{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
                           mRenderSetVertexBufferBindingEnabled, false},
            indirectBuffer, indirectCommandCount, stride](GVM::RHI::RenderPassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->drawIndirect(indirectBuffer, indirectCommandCount, stride);
        };
        return desp;
    }

    RenderPassTaskDescriptor IPixelLocalRenderClass::setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup)
    {
        RenderPassTaskDescriptor desp;
        desp.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly;
        desp.drawFn = [state = mSlotState, slot, group = bindgroup, reservedSlot = mRenderSetBindGroupIndex](GVM::RHI::RenderPassEncoder)
        {
            if (reservedSlot == slot)
            {
                return;
            }
            state->bindGroups[slot] = group;
        };
        return desp;
    }

    RenderPassTaskDescriptor IPixelLocalRenderClass::run()
    {
        RenderPassTaskDescriptor desp;
        desp.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly;
        desp.drawFn = [binding = SlotBinding<GVM::RHI::RenderPipeline>{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex}](GVM::RHI::RenderPassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->drawPixels();
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::setVertexBuffer(GVM::RHI::BufferRange buffer, uint32_t slot)
    {
        RenderPassTaskDescriptor desp;

        desp.drawFn = [state = mSlotState, buffer, slot](GVM::RHI::RenderPassEncoder passEncoder) {
            state->vertexBufferSet = true;
            passEncoder->setVertexBuffer(buffer, slot);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::setIndexBuffer(GVM::RHI::BufferRange buffer, GVM::RHI::IndexFormat format)
    {
        RenderPassTaskDescriptor desp;

        desp.drawFn = [state = mSlotState, buffer, format](GVM::RHI::RenderPassEncoder passEncoder) {
            state->indexBufferSet = true;
            passEncoder->setIndexBuffer(buffer, format);
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [state = mSlotState, slot, group = bindgroup, reservedSlot = mRenderSetBindGroupIndex](GVM::RHI::ComputePassEncoder)
        {
            if (reservedSlot == slot)
            {
                return;
            }
            state->bindGroups[slot] = group;
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::run(uint32_t x, uint32_t y, uint32_t z)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [binding = SlotBinding<GVM::RHI::ComputePipeline>{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
            groupX = workGroupX, groupY = workGroupY, groupZ = workGroupZ,
            x, y, z](GVM::RHI::ComputePassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->dispatchWorkgroups(xGE::Math::IntRoundUp(x, groupX), xGE::Math::IntRoundUp(y, groupY), xGE::Math::IntRoundUp(z, groupZ));
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::run(GVM::RHI::BufferRange indirectBuffer)
    {
        return dispatchWorkgroupsIndirect(indirectBuffer);
    }

    ComputePassTaskDescriptor IComputeClass::dispatchWorkgroupsIndirect(GVM::RHI::BufferRange indirectBuffer)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [binding = SlotBinding<GVM::RHI::ComputePipeline>{pipeline, mSlotState, mRenderSet, mRenderSetBindGroupIndex},
            indirectBuffer](GVM::RHI::ComputePassEncoder passEncoder) {
            binding.bind(passEncoder);
            passEncoder->dispatchWorkgroupsIndirect(indirectBuffer);
        };
        return desp;
    }

} // namespace GVM::Core
