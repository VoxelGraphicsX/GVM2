#include "GShader.hpp"
#include <stdexcept>
#include <xGEFoundation/xMath.hpp>
namespace GVM::Core
{
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
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            if (mRenderSetBindGroupIndex == slot)
            {
                return;
            }
            bindGroups[slot] = bindgroup;
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run()
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            if (mRenderSet == nullptr)
            {
                throw std::logic_error("IRenderClass::run() is a deprecated RenderSet-only entry and requires a bound RenderSet. Use an explicit run(...) overload or drawIndirect(...) instead.");
            }
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
                if (mRenderSetVertexBufferBindingEnabled && mSetVertexBuffer == false)
                {
                    passEncoder->setVertexBuffer(RHI::BufferRange(mRenderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                }
                if (mSetIndexBuffer == false)
                {
                    passEncoder->setIndexBuffer(RHI::BufferRange(mRenderSet->getIndexBuffer()), mRenderSet->getIndexFormat());
                }
                mSetVertexBuffer = mSetIndexBuffer = false;
            }
            passEncoder->drawIndexedIndirect(
                mRenderSet->getRenderEntityInfoBuffer(),
                mRenderSet->getMaxEntityCount(),
                sizeof(RenderEntityInfo));
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
                if (mRenderSetVertexBufferBindingEnabled && mSetVertexBuffer == false)
                {
                    passEncoder->setVertexBuffer(RHI::BufferRange(mRenderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                }
                if (mSetIndexBuffer == false)
                {
                    passEncoder->setIndexBuffer(RHI::BufferRange(mRenderSet->getIndexBuffer()), mRenderSet->getIndexFormat());
                }
                mSetVertexBuffer = mSetIndexBuffer = false;
            }
            passEncoder->draw(vertexCount, instanceCount, firstVertex, firstInstance);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(uint indexCount, uint instanceCount, uint firstIndex, int baseVertex, uint firstInstance)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
                if (mRenderSetVertexBufferBindingEnabled && mSetVertexBuffer == false)
                {
                    passEncoder->setVertexBuffer(RHI::BufferRange(mRenderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                }
                if (mSetIndexBuffer == false)
                {
                    passEncoder->setIndexBuffer(RHI::BufferRange(mRenderSet->getIndexBuffer()), mRenderSet->getIndexFormat());
                }
                mSetVertexBuffer = mSetIndexBuffer = false;
            }
            passEncoder->drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::run(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
                if (mRenderSetVertexBufferBindingEnabled && mSetVertexBuffer == false)
                {
                    passEncoder->setVertexBuffer(RHI::BufferRange(mRenderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                }
                if (mSetIndexBuffer == false)
                {
                    passEncoder->setIndexBuffer(RHI::BufferRange(mRenderSet->getIndexBuffer()), mRenderSet->getIndexFormat());
                }
                mSetVertexBuffer = mSetIndexBuffer = false;
            }
            passEncoder->drawIndexedIndirect(indirectBuffer, indirectCommandCount, stride);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::drawIndirect(GVM::RHI::BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride)
    {
        RenderPassTaskDescriptor desp;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
                if (mRenderSetVertexBufferBindingEnabled && mSetVertexBuffer == false)
                {
                    passEncoder->setVertexBuffer(RHI::BufferRange(mRenderSet->getVertexBuffer(), 0, GVM::RHI::WholeSize), 0);
                }

                mSetVertexBuffer = mSetIndexBuffer = false;
            }
            passEncoder->drawIndirect(indirectBuffer, indirectCommandCount, stride);
        };
        return desp;
    }

    RenderPassTaskDescriptor IPixelLocalRenderClass::setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup)
    {
        RenderPassTaskDescriptor desp;
        desp.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            if (mRenderSetBindGroupIndex == slot)
            {
                return;
            }
            bindGroups[slot] = bindgroup;
        };
        return desp;
    }

    RenderPassTaskDescriptor IPixelLocalRenderClass::run()
    {
        RenderPassTaskDescriptor desp;
        desp.phaseRequirement = RenderPassPhaseRequirement::PixelLocalOnly;
        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }
            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
            }
            passEncoder->drawPixels();
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::setVertexBuffer(GVM::RHI::BufferRange buffer, uint32_t slot)
    {
        RenderPassTaskDescriptor desp;

        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            mSetVertexBuffer = true;
            passEncoder->setVertexBuffer(buffer, slot);
        };
        return desp;
    }

    RenderPassTaskDescriptor IRenderClass::setIndexBuffer(GVM::RHI::BufferRange buffer, GVM::RHI::IndexFormat format)
    {
        RenderPassTaskDescriptor desp;

        desp.drawFn = [=, this](GVM::RHI::RenderPassEncoder passEncoder) {
            mSetIndexBuffer = true;
            passEncoder->setIndexBuffer(buffer, format);
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::setBindGroup(uint32_t slot, const GVM::RHI::BindGroup &bindgroup)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [=, this](GVM::RHI::ComputePassEncoder passEncoder) {
            if (mRenderSetBindGroupIndex == slot)
            {
                return;
            }
            bindGroups[slot] = bindgroup;
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::run(uint32_t x, uint32_t y, uint32_t z)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [=, this](GVM::RHI::ComputePassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }

            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
            }
            passEncoder->dispatchWorkgroups(xGE::Math::IntRoundUp(x, this->workGroupX), xGE::Math::IntRoundUp(y, this->workGroupY), xGE::Math::IntRoundUp(z, this->workGroupZ));
        };
        return desp;
    }

    ComputePassTaskDescriptor IComputeClass::run(GVM::RHI::BufferRange indirectBuffer)
    {
        ComputePassTaskDescriptor desp;
        desp.dispatchFn = [=, this](GVM::RHI::ComputePassEncoder passEncoder) {
            passEncoder->setPipeline(pipeline);
            for (auto &[index, bindGroup] : bindGroups)
            {
                passEncoder->setBindGroup(bindGroup, index);
            }

            if (mRenderSet != nullptr)
            {
                passEncoder->setBindGroup(mRenderSet->getBindGroup(), this->mRenderSetBindGroupIndex);
            }
            passEncoder->dispatchWorkgroupsIndirect(indirectBuffer);
        };
        return desp;
    }

} // namespace GVM::Core
