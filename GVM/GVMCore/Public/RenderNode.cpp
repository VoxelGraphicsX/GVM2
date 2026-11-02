#include "RenderNode.hpp"
#include "../Private/GraphBufferAllocation.hpp"
#include <stdexcept>

namespace GVM::Core
{
    RenderNode::RenderNode(eastl::shared_ptr<Private::GraphDevice> device, uint32_t maximumPasses)
        : mDevice(eastl::move(device)), mMaximumPasses(maximumPasses)
    {
        if (!mDevice)
        {
            throw std::logic_error("A RenderNode requires a TaskRuntime constructed with a device.");
        }
    }
    RenderNode::~RenderNode() = default;
    Private::RenderNodeFragment RenderNode::finish()
    {
        return eastl::move(mFragment);
    }
    void RenderNode::append(Private::GraphPass pass)
    {
        if (mFragment.passes.size() >= mMaximumPasses)
        {
            throw std::length_error("RenderNode exceeds the runtime pass budget.");
        }
        for (const auto &access : pass.resources.accesses)
        {
            pass.resources.complete = pass.resources.complete && access.resource != nullptr;
        }
        mFragment.passes.push_back(eastl::move(pass));
    }
    GraphBuffer RenderNode::createBuffer(const RHI::BufferDescriptor &descriptor)
    {
        if (descriptor.size == 0)
        {
            throw std::invalid_argument("Graph buffer allocations must be nonempty.");
        }
        GraphBuffer result(eastl::make_shared<Private::GraphBufferAllocation>(mDevice, descriptor), 0, descriptor.size);
        mFragment.buffers.push_back(result);
        return result;
    }
    void RenderNode::computePass(const eastl::string &label, eastl::vector<ComputePassTaskDescriptor> tasks)
    {
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Compute;
        pass.resources.complete = true;
        for (const auto &task : tasks)
        {
            if (!task.dispatchFn)
            {
                throw std::invalid_argument("Compute pass contains an empty dispatch.");
            }
            pass.resources.complete = pass.resources.complete && task.resources.complete;
            pass.resources.accesses.insert(pass.resources.accesses.end(), task.resources.accesses.begin(), task.resources.accesses.end());
        }
        pass.record = [label, tasks = eastl::move(tasks)](RHI::CommandEncoder encoder)
        {
            auto compute = encoder->beginComputePass({.label = label});
            try
            {
                for (const auto &task : tasks)
                {
                    task.dispatchFn(compute);
                }
            }
            catch (...)
            {
                compute->end();
                throw;
            }
            compute->end();
        };
        append(eastl::move(pass));
    }
    void RenderNode::renderPass(RHI::RenderPassDescriptor descriptor, eastl::vector<RenderPassTaskDescriptor> tasks)
    {
        if (descriptor.pixelLocal.enabled)
        {
            throw std::invalid_argument("Graph render passes currently require ordinary framebuffer attachments.");
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Render;
        pass.resources.complete = true;
        for (const auto &attachment : descriptor.colorAttachments)
        {
            if (!attachment.view.isNull())
            {
                pass.resources.accesses.push_back(Private::PassResourceAccess::texture(attachment.view, true));
            }
        }
        if (!descriptor.depthStencilAttachment.view.isNull())
        {
            pass.resources.accesses.push_back(Private::PassResourceAccess::texture(descriptor.depthStencilAttachment.view, true));
        }
        for (const auto &task : tasks)
        {
            if (!task.drawFn || task.phaseRequirement == RenderPassPhaseRequirement::PixelLocalOnly)
            {
                throw std::invalid_argument("Graph render passes require ordinary draw tasks.");
            }
            pass.resources.complete = pass.resources.complete && task.resources.complete;
            pass.resources.accesses.insert(pass.resources.accesses.end(), task.resources.accesses.begin(), task.resources.accesses.end());
        }
        pass.record = [descriptor = eastl::move(descriptor), tasks = eastl::move(tasks)](RHI::CommandEncoder encoder)
        {
            auto render = encoder->beginRenderPass(descriptor);
            try
            {
                for (const auto &task : tasks)
                {
                    task.drawFn(render);
                }
            }
            catch (...)
            {
                render->end();
                throw;
            }
            render->end();
        };
        append(eastl::move(pass));
    }
    void RenderNode::copyBuffer(GraphBuffer source, GraphBuffer destination)
    {
        if (source.size() != destination.size() || source.size() == 0)
        {
            throw std::invalid_argument("Graph copies require equal nonempty buffer ranges.");
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.resources = {{Private::PassResourceAccess::buffer(source.range(), false),
            Private::PassResourceAccess::buffer(destination.range(), true)}, true};
        pass.record = [source = eastl::move(source), destination = eastl::move(destination)](RHI::CommandEncoder encoder)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphCopy"});
            try
            {
                copy->copyBufferToBuffer(source.range(), destination.range());
            }
            catch (...)
            {
                copy->end();
                throw;
            }
            copy->end();
        };
        append(eastl::move(pass));
    }
    void RenderNode::fillBuffer(GraphBuffer target, uint8_t value)
    {
        const auto range = target.range();
        if ((range.offset % 4) != 0 || (range.size % 4) != 0)
        {
            throw std::invalid_argument("Graph fills require four-byte aligned ranges.");
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.resources = {{Private::PassResourceAccess::buffer(range, true)}, true};
        pass.record = [target = eastl::move(target), value](RHI::CommandEncoder encoder)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphFill"});
            try
            {
                copy->fillBuffer(target.range(), value);
            }
            catch (...)
            {
                copy->end();
                throw;
            }
            copy->end();
        };
        append(eastl::move(pass));
    }
    void RenderNode::upload(GraphBuffer destination, const void *data, uint64_t size)
    {
        if (size != destination.size() || size == 0 || data == nullptr)
        {
            throw std::invalid_argument("Graph upload bytes must match the destination range.");
        }
        auto staging = createBuffer({.label = "GraphUpload", .usage = RHI::BufferUsage::MapWrite | RHI::BufferUsage::CopySrc, .size = size});
        staging.mAllocation->write(data, size);
        copyBuffer(eastl::move(staging), eastl::move(destination));
    }
    GraphBuffer RenderNode::prepareReadback(GraphBuffer source)
    {
        auto staging = createBuffer({.label = "GraphReadback", .usage = RHI::BufferUsage::MapRead | RHI::BufferUsage::CopyDst, .size = source.size()});
        copyBuffer(eastl::move(source), staging);
        return staging;
    }
    void RenderNode::resolveAfterGpu(eastl::function<eastl::any()> resolver)
    {
        if (mFragment.resolve)
        {
            throw std::logic_error("One node invocation may publish only one readback result.");
        }
        mFragment.resolve = eastl::move(resolver);
    }
}
