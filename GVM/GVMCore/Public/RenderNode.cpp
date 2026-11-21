#include "RenderNode.hpp"
#include "../Private/GraphDevice.hpp"
#include "../Private/GraphResourceAllocation.hpp"
#include "../Private/PassWeightProfiler.hpp"
#include <GVMRHI/Private/GEnumUtils.hpp>
#include <GVMRHI/Private/TextureCopyFootprint.hpp>
#include <EASTL/algorithm.h>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace GVM::Core
{
    namespace
    {
        uint16_t estimateTransferWeight(uint64_t bytes) noexcept
        {
            if (bytes < (64ull << 10))
            {
                return 2u;
            }
            if (bytes <= (1ull << 20))
            {
                return Private::kDefaultPassWeight;
            }
            return 16u;
        }

        uint16_t estimateRenderWeight(uint32_t drawCount) noexcept
        {
            const uint32_t extra = drawCount > 1u ? eastl::min<uint32_t>((drawCount - 1u) * 2u, 24u) : 0u;
            return static_cast<uint16_t>(Private::kDefaultPassWeight + extra);
        }

        bool hasResourceHazard(const Private::PassResourceUsage &lhs, const Private::PassResourceUsage &rhs) noexcept
        {
            for (const auto &a : lhs.accesses)
            {
                for (const auto &b : rhs.accesses)
                {
                    if ((a.writes || b.writes) && a.conflicts(b))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /// Captures mip-adjusted spatial limits and 3D-vs-layer semantics for one texture transfer endpoint.
        struct ResolvedTextureCopyBounds
        {
            uint32_t mipWidth = 0;
            uint32_t mipHeight = 0;
            uint32_t depthLimit = 0;
            bool is3D = false;
        };

        /// Validates a texture transfer endpoint and returns the remaining copyable bounds at its mip level.
        ResolvedTextureCopyBounds resolveTextureCopyBounds(const char *context, const RHI::ImageCopyTexture &endpoint)
        {
            if (endpoint.texture.isNull() || endpoint.texture.get() == nullptr)
            {
                throw std::invalid_argument((eastl::string(context) + " requires a live texture.").c_str());
            }
            if (endpoint.mipLevel >= endpoint.texture->getMipLevelCount())
            {
                throw std::out_of_range((eastl::string(context) + " mip level exceeds the texture mip count.").c_str());
            }
            const auto dimension = endpoint.texture->getDimension();
            const uint32_t mipWidth = eastl::max(1u, endpoint.texture->getWidth() >> endpoint.mipLevel);
            const uint32_t mipHeight = dimension == RHI::TextureDimension::e1D
                ? 1u
                : eastl::max(1u, endpoint.texture->getHeight() >> endpoint.mipLevel);
            const bool is3D = dimension == RHI::TextureDimension::e3D;
            const uint32_t depthLimit = is3D
                ? eastl::max(1u, endpoint.texture->getDepth() >> endpoint.mipLevel)
                : endpoint.texture->getArrayLayerCount();
            if (endpoint.origin.x > mipWidth || endpoint.origin.y > mipHeight || endpoint.origin.z > depthLimit)
            {
                throw std::out_of_range((eastl::string(context) + " origin exceeds the texture subresource bounds.").c_str());
            }
            return {
                .mipWidth = mipWidth,
                .mipHeight = mipHeight,
                .depthLimit = depthLimit,
                .is3D = is3D,
            };
        }

        /// Resolves default zero copy extents to remaining subresource bounds and validates explicit extents.
        RHI::Extent3D resolveTextureCopyExtent(
            const char *context,
            const RHI::ImageCopyTexture &endpoint,
            const ResolvedTextureCopyBounds &bounds,
            const RHI::Extent3D &requested)
        {
            const RHI::Extent3D remaining = {
                .width = bounds.mipWidth - endpoint.origin.x,
                .height = bounds.mipHeight - endpoint.origin.y,
                .depth = bounds.depthLimit - endpoint.origin.z,
            };
            RHI::Extent3D resolved = requested;
            if (resolved.width == 0u && resolved.height == 0u && resolved.depth == 0u)
            {
                resolved = remaining;
            }
            if (resolved.width == 0u || resolved.height == 0u || resolved.depth == 0u)
            {
                throw std::invalid_argument((eastl::string(context) + " requires a non-empty copy extent.").c_str());
            }
            if (resolved.width > remaining.width || resolved.height > remaining.height || resolved.depth > remaining.depth)
            {
                throw std::out_of_range((eastl::string(context) + " extent exceeds the texture subresource bounds.").c_str());
            }
            return resolved;
        }

        /// Estimates transfer byte volume for GPU texture-to-texture scheduling without requiring a host-addressable block size.
        uint64_t estimateTextureCopyBytes(RHI::TextureFormat format, const RHI::Extent3D &extent) noexcept
        {
            const uint64_t blockWidth = eastl::max<uint64_t>(1u, RHI::Private::getTextureBlockWidth(format));
            const uint64_t blockHeight = eastl::max<uint64_t>(1u, RHI::Private::getTextureBlockHeight(format));
            const uint64_t blocksX = (static_cast<uint64_t>(extent.width) + blockWidth - 1u) / blockWidth;
            const uint64_t blocksY = (static_cast<uint64_t>(extent.height) + blockHeight - 1u) / blockHeight;
            uint64_t bytesPerBlock = RHI::Private::tryGetTextureBytesPerBlock(format);
            if (bytesPerBlock == 0u)
            {
                bytesPerBlock = format == RHI::TextureFormat::Stencil8 ? 1u : 4u;
            }
            const uint64_t blockCount = blocksX * blocksY * static_cast<uint64_t>(extent.depth);
            if (blockCount > ~uint64_t(0) / bytesPerBlock)
            {
                return ~uint64_t(0);
            }
            return blockCount * bytesPerBlock;
        }

        /// Returns true when the texture copy region satisfies the supplied queue image transfer granularity.
        bool satisfiesQueueTransferGranularity(
            const RHI::ImageCopyTexture &copy,
            const ResolvedTextureCopyBounds &bounds,
            const RHI::Extent3D &resolvedExtent,
            const RHI::Extent3D &granularity) noexcept
        {
            const RHI::TextureFormat format = copy.texture->getFormat();
            const uint64_t blockWidth = RHI::Private::getTextureBlockWidth(format);
            const uint64_t blockHeight = RHI::Private::getTextureBlockHeight(format);
            const uint64_t alignment[3] = {
                static_cast<uint64_t>(granularity.width) * blockWidth,
                static_cast<uint64_t>(granularity.height) * blockHeight,
                static_cast<uint64_t>(granularity.depth),
            };
            const uint64_t origin[3] = {
                copy.origin.x,
                copy.texture->getDimension() == RHI::TextureDimension::e1D ? 0u : copy.origin.y,
                bounds.is3D ? copy.origin.z : 0u,
            };
            const uint64_t size[3] = {
                resolvedExtent.width,
                copy.texture->getDimension() == RHI::TextureDimension::e1D ? 1u : resolvedExtent.height,
                bounds.is3D ? resolvedExtent.depth : 1u,
            };
            const uint64_t mipSize[3] = {
                bounds.mipWidth,
                bounds.mipHeight,
                bounds.is3D ? bounds.depthLimit : 1u,
            };
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const bool wholeMip = (origin[axis] == 0u && size[axis] == mipSize[axis]);
                if (alignment[axis] == 0u)
                {
                    if (!wholeMip)
                    {
                        return false;
                    }
                }
                else if (origin[axis] % alignment[axis] != 0u ||
                    (size[axis] % alignment[axis] != 0u && origin[axis] + size[axis] != mipSize[axis]))
                {
                    return false;
                }
            }
            return true;
        }

        /// Returns true when a pass descriptor already carries caller-specified timestamp query writes.
        bool hasCallerTimestampWrites(const RHI::PassTimestampWrites &writes) noexcept
        {
            return writes.querySet != nullptr ||
                RHI::isTimestampQueryIndexEnabled(writes.beginningOfPassWriteIndex) ||
                RHI::isTimestampQueryIndexEnabled(writes.endOfPassWriteIndex);
        }
    } // namespace

    RenderNode::RenderNode(eastl::shared_ptr<Private::GraphDevice> device, uint32_t maximumPasses,
        std::atomic<uint32_t> *sharedPassCount)
        : mDevice(eastl::move(device)), mMaximumPasses(maximumPasses), mSharedPassCount(sharedPassCount)
    {
        if (!mDevice)
        {
            throw std::logic_error("A RenderNode requires a TaskRuntime constructed with a device.");
        }
    }
    RenderNode::~RenderNode()
    {
        if (!mCommitted && mSharedPassCount != nullptr && !mFragment.passes.empty())
        {
            mSharedPassCount->fetch_sub(static_cast<uint32_t>(mFragment.passes.size()), std::memory_order_relaxed);
        }
    }
    std::unique_lock<std::mutex> RenderNode::lockDeviceResources()
    {
        return mDevice->lockResources();
    }
    RHI::DeviceImpl &RenderNode::nativeDevice() const
    {
        return mDevice->device();
    }
    uint16_t RenderNode::estimateComputeWeight(uint64_t x, uint64_t y, uint64_t z) noexcept
    {
        if (x == 0ull || y == 0ull || z == 0ull)
        {
            return 2u;
        }
        constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
        const uint64_t xy = x > kMax / y ? kMax : x * y;
        const uint64_t threads = xy > kMax / z ? kMax : xy * z;
        if (threads < 4096ull)
        {
            return 2u;
        }
        if (threads <= 262144ull)
        {
            return Private::kDefaultPassWeight;
        }
        return 16u;
    }
    Private::RenderNodeFragment RenderNode::finish()
    {
        if (mHasUnattachedExternalWaits)
        {
            Private::GraphPass waitPass;
            waitPass.kind = Private::GraphPassKind::Transfer;
            waitPass.weight = 1u;
            waitPass.resources.complete = true;
            waitPass.record = [](RHI::CommandEncoder, const RHI::PassTimestampWrites *)
            {
            };
            append(eastl::move(waitPass));
        }
        mFragment.externalWaits = mPendingExternalWaits;
        mCommitted = true;
        return eastl::move(mFragment);
    }
    void RenderNode::append(Private::GraphPass pass)
    {
        if (mSharedPassCount != nullptr)
        {
            uint32_t current = mSharedPassCount->load(std::memory_order_relaxed);
            for (;;)
            {
                if (current >= mMaximumPasses)
                {
                    throw std::length_error("GPU graph exceeds the execution pass budget.");
                }
                if (mSharedPassCount->compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
                {
                    break;
                }
            }
        }
        else if (mFragment.passes.size() >= mMaximumPasses)
        {
            throw std::length_error("RenderNode exceeds the runtime pass budget.");
        }
        for (const auto &access : pass.resources.accesses)
        {
            pass.resources.complete = pass.resources.complete && access.resource != nullptr;
        }
        if (!mPendingExternalWaits.empty())
        {
            for (const auto &wait : mPendingExternalWaits)
            {
                if (eastl::find(pass.externalWaits.begin(), pass.externalWaits.end(), wait) == pass.externalWaits.end())
                {
                    pass.externalWaits.push_back(wait);
                }
            }
            mHasUnattachedExternalWaits = false;
        }
        try
        {
            mFragment.passes.push_back(eastl::move(pass));
        }
        catch (...)
        {
            if (mSharedPassCount != nullptr)
            {
                mSharedPassCount->fetch_sub(1, std::memory_order_relaxed);
            }
            throw;
        }
    }
    RHI::Buffer RenderNode::createBuffer(const RHI::BufferDescriptor &descriptor)
    {
        return allocateBuffer(descriptor)->buffer();
    }
    RHI::Texture RenderNode::createTexture(const RHI::TextureDescriptor &descriptor)
    {
        auto allocation = eastl::make_shared<Private::GraphResourceAllocation>(mDevice, descriptor);
        mFragment.allocations.push_back(allocation);
        return allocation->texture();
    }
    RHI::TextureView RenderNode::view(RHI::Texture texture)
    {
        if (texture.isNull())
        {
            throw std::invalid_argument("Graph texture view requires a live texture.");
        }
        auto lock = mDevice->lockResources();
        return texture->createView();
    }
    eastl::shared_ptr<Private::GraphResourceAllocation> RenderNode::allocateBuffer(const RHI::BufferDescriptor &descriptor)
    {
        if (descriptor.size == 0)
        {
            throw std::invalid_argument("Graph buffer allocations must be nonempty.");
        }
        auto allocation = eastl::make_shared<Private::GraphResourceAllocation>(mDevice, descriptor);
        mFragment.allocations.push_back(allocation);
        return allocation;
    }
    void RenderNode::waitFor(RHI::Submission submission)
    {
        if (!submission.isValid())
        {
            throw std::invalid_argument("RenderNode::waitFor requires a valid Submission.");
        }
        if (eastl::find(mPendingExternalWaits.begin(), mPendingExternalWaits.end(), submission) == mPendingExternalWaits.end())
        {
            mPendingExternalWaits.push_back(eastl::move(submission));
        }
        mHasUnattachedExternalWaits = true;
    }
    void RenderNode::computePass(const eastl::string &label, eastl::vector<ComputePassTaskDescriptor> tasks)
    {
        bool allComplete = !tasks.empty();
        for (const auto &task : tasks)
        {
            if (!task.dispatchFn)
            {
                throw std::invalid_argument("Compute pass contains an empty dispatch.");
            }
            allComplete = allComplete && task.resources.complete;
            for (const auto &access : task.resources.accesses)
            {
                allComplete = allComplete && access.resource != nullptr;
            }
        }
        auto emitGroup = [&](eastl::vector<ComputePassTaskDescriptor> groupTasks, Private::PassResourceUsage groupResources,
                             uint16_t groupWeight, uint32_t groupOrdinal, uint32_t totalGroups)
        {
            Private::GraphPass pass;
            pass.kind = Private::GraphPassKind::Compute;
            pass.weight = groupWeight;
            pass.resources = eastl::move(groupResources);
            if (!label.empty())
            {
                if (totalGroups > 1u)
                {
                    char suffix[24]{};
                    std::snprintf(suffix, sizeof(suffix), "#%u", groupOrdinal);
                    pass.profileLabel = label + suffix;
                }
                else
                {
                    pass.profileLabel = label;
                }
                pass.profileKey = Private::PassWeightProfiler::hashProfileKey(Private::GraphPassKind::Compute, pass.profileLabel);
            }
            if (groupTasks.size() == 1u)
            {
                pass.record = [label, task = eastl::move(groupTasks.front())](
                                  RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *timestamps)
                {
                    RHI::ComputePassDescriptor passDesc = {.label = label};
                    if (timestamps != nullptr)
                    {
                        passDesc.timestampWrites = *timestamps;
                    }
                    auto compute = encoder->beginComputePass(passDesc);
                    try
                    {
                        task.dispatchFn(compute);
                    }
                    catch (...)
                    {
                        compute->end();
                        throw;
                    }
                    compute->end();
                };
            }
            else
            {
                pass.record = [label, groupTasks = eastl::move(groupTasks)](
                                  RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *timestamps)
                {
                    RHI::ComputePassDescriptor passDesc = {.label = label};
                    if (timestamps != nullptr)
                    {
                        passDesc.timestampWrites = *timestamps;
                    }
                    auto compute = encoder->beginComputePass(passDesc);
                    try
                    {
                        for (const auto &task : groupTasks)
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
            }
            append(eastl::move(pass));
        };

        if (allComplete && tasks.size() > 1u)
        {
            struct FusedComputeGroup
            {
                eastl::vector<ComputePassTaskDescriptor> tasks;
                Private::PassResourceUsage resources;
                uint16_t weight = 0u;
            };
            eastl::vector<FusedComputeGroup> groups;
            groups.reserve(tasks.size());
            for (auto &task : tasks)
            {
                const uint16_t taskWeight = eastl::max<uint16_t>(1u, task.weight);
                bool canFuseWithBack = !groups.empty() && hasResourceHazard(groups.back().resources, task.resources);
                if (canFuseWithBack)
                {
                    for (size_t earlier = 0; earlier + 1u < groups.size(); ++earlier)
                    {
                        if (hasResourceHazard(groups[earlier].resources, task.resources))
                        {
                            canFuseWithBack = false;
                            break;
                        }
                    }
                }
                if (canFuseWithBack)
                {
                    auto &back = groups.back();
                    back.resources.accesses.insert(
                        back.resources.accesses.end(), task.resources.accesses.begin(), task.resources.accesses.end());
                    back.weight = static_cast<uint16_t>(eastl::min<uint32_t>(64u, uint32_t(back.weight) + taskWeight));
                    back.tasks.push_back(eastl::move(task));
                }
                else
                {
                    FusedComputeGroup nextGroup;
                    nextGroup.resources = task.resources;
                    nextGroup.resources.complete = true;
                    nextGroup.weight = taskWeight;
                    nextGroup.tasks.push_back(eastl::move(task));
                    groups.push_back(eastl::move(nextGroup));
                }
            }
            const uint32_t totalGroups = static_cast<uint32_t>(groups.size());
            for (uint32_t idx = 0u; idx < totalGroups; ++idx)
            {
                auto &group = groups[idx];
                emitGroup(eastl::move(group.tasks), eastl::move(group.resources), group.weight, idx, totalGroups);
            }
            return;
        }

        Private::PassResourceUsage combinedResources;
        combinedResources.complete = !tasks.empty();
        uint32_t totalWeight = tasks.empty() ? 2u : 0u;
        for (const auto &task : tasks)
        {
            combinedResources.complete = combinedResources.complete && task.resources.complete;
            combinedResources.accesses.insert(
                combinedResources.accesses.end(), task.resources.accesses.begin(), task.resources.accesses.end());
            totalWeight = eastl::min<uint32_t>(64u, totalWeight + eastl::max<uint16_t>(1u, task.weight));
        }
        if (tasks.empty())
        {
            combinedResources.complete = true;
        }
        emitGroup(eastl::move(tasks), eastl::move(combinedResources), static_cast<uint16_t>(totalWeight), 0u, 1u);
    }
    void RenderNode::collectRenderPhaseResources(Private::PassResourceUsage &usage, uint32_t &drawCount, const RenderPassTaskDescriptor &task)
    {
        if (!task.drawFn)
        {
            throw std::invalid_argument("Graph render passes require valid draw tasks.");
        }
        ++drawCount;
        usage.complete = usage.complete && task.resources.complete;
        usage.accesses.insert(usage.accesses.end(), task.resources.accesses.begin(), task.resources.accesses.end());
    }
    void RenderNode::collectRenderPhaseResources(Private::PassResourceUsage &usage, uint32_t &drawCount, const PixelLocalPassTaskDescriptor &tasks)
    {
        for (const auto &subpass : tasks.pixelLocalPasses)
        {
            for (const auto &task : subpass)
            {
                collectRenderPhaseResources(usage, drawCount, task);
            }
        }
    }
    void RenderNode::renderPassWithEncoder(RHI::RenderPassDescriptor descriptor, Private::PassResourceUsage taskResources,
        uint32_t drawCount, eastl::function<void(RHI::RenderPassEncoder)> encodeTasks)
    {
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Render;
        pass.weight = estimateRenderWeight(drawCount);
        const bool explicitTimestamps = hasCallerTimestampWrites(descriptor.timestampWrites);
        pass.hasExplicitTimestampWrites = explicitTimestamps;
        if (!descriptor.label.empty())
        {
            pass.profileLabel = descriptor.label;
            pass.profileKey = Private::PassWeightProfiler::hashProfileKey(Private::GraphPassKind::Render, pass.profileLabel);
        }
        pass.resources.complete = taskResources.complete;
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
        pass.resources.accesses.insert(pass.resources.accesses.end(),
            taskResources.accesses.begin(), taskResources.accesses.end());
        pass.record = [descriptor = eastl::move(descriptor), encodeTasks = eastl::move(encodeTasks), explicitTimestamps](
                          RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *timestamps)
        {
            RHI::RenderPassEncoder render;
            if (timestamps != nullptr && !explicitTimestamps)
            {
                RHI::RenderPassDescriptor timedDescriptor = descriptor;
                timedDescriptor.timestampWrites = *timestamps;
                render = encoder->beginRenderPass(timedDescriptor);
            }
            else
            {
                render = encoder->beginRenderPass(descriptor);
            }
            try
            {
                auto target = descriptor.depthStencilAttachment.view;
                if (!descriptor.colorAttachments.empty())
                {
                    target = descriptor.colorAttachments.front().view;
                }
                if (!target.isNull())
                {
                    render->setViewport(0.0f, 0.0f, float(target->getWidth()), float(target->getHeight()), 0.0f, 1.0f);
                    render->setScissorRect(0, 0, target->getWidth(), target->getHeight());
                }
                encodeTasks(render);
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
    void RenderNode::renderPass(RHI::RenderPassDescriptor descriptor, eastl::vector<RenderPassTaskDescriptor> tasks)
    {
        if (descriptor.pixelLocal.enabled)
        {
            throw std::invalid_argument("Graph render passes currently require ordinary framebuffer attachments.");
        }
        Private::PassResourceUsage taskResources;
        taskResources.complete = true;
        uint32_t drawCount = 0u;
        for (const auto &task : tasks)
        {
            if (!task.drawFn || task.phaseRequirement == RenderPassPhaseRequirement::PixelLocalOnly)
            {
                throw std::invalid_argument("Graph render passes require ordinary draw tasks.");
            }
            collectRenderPhaseResources(taskResources, drawCount, task);
        }
        renderPassWithEncoder(eastl::move(descriptor), eastl::move(taskResources), drawCount,
            [tasks = eastl::move(tasks)](RHI::RenderPassEncoder render)
            {
                for (const auto &task : tasks)
                {
                    task.drawFn(render);
                }
            });
    }
    void RenderNode::renderToSwapchainImpl(const RHI::SwapchainQueryResult &swapchainResult, RHI::Texture sourceTexture,
        const RHI::RenderToSwapchainDescriptor &descriptor, eastl::vector<RenderPassTaskDescriptor> tasks)
    {
        if (swapchainResult.status != RHI::SwapchainNextTextureQueryStatus::Success || swapchainResult.texture.isNull())
        {
            throw std::invalid_argument("RenderNode::renderToSwapchain requires a successful SwapchainQueryResult with a valid swapchain texture.");
        }
        if (sourceTexture.isNull())
        {
            throw std::invalid_argument("RenderNode::renderToSwapchain requires a valid source texture.");
        }
        Private::validateOrdinaryRenderPassTaskRange(tasks, "RenderNode::renderToSwapchain");
        RHI::TextureView swapchainView;
        {
            auto lock = mDevice->lockResources();
            swapchainView = swapchainResult.texture->createView();
        }
        RHI::RenderPassDescriptor passDescriptor = {};
        passDescriptor.label = "RenderToSwapchain";
        passDescriptor.colorAttachments.push_back({
            .view = swapchainView,
            .loadOp = RHI::LoadOp::Clear,
            .storeOp = RHI::StoreOp::Store,
            .clearValue = {0.0, 0.0, 0.0, 1.0},
        });
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Render;
        pass.profileLabel = passDescriptor.label;
        pass.profileKey = Private::PassWeightProfiler::hashProfileKey(Private::GraphPassKind::Render, pass.profileLabel);
        pass.requiresMainQueue = true;
        pass.resources.complete = true;
        pass.resources.accesses.push_back(Private::PassResourceAccess::textureRange(sourceTexture.get(), false));
        pass.resources.accesses.push_back(Private::PassResourceAccess::textureRange(swapchainResult.texture.get(), true));
        uint32_t drawCount = 1u;
        for (const auto &task : tasks)
        {
            collectRenderPhaseResources(pass.resources, drawCount, task);
        }
        pass.weight = estimateRenderWeight(drawCount);
        pass.record = [passDescriptor = eastl::move(passDescriptor), sourceTexture = eastl::move(sourceTexture),
            descriptor, tasks = eastl::move(tasks)](
                          RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *timestamps)
        {
            RHI::RenderPassEncoder render;
            if (timestamps != nullptr)
            {
                RHI::RenderPassDescriptor timedDescriptor = passDescriptor;
                timedDescriptor.timestampWrites = *timestamps;
                render = encoder->beginRenderPass(timedDescriptor);
            }
            else
            {
                render = encoder->beginRenderPass(passDescriptor);
            }
            try
            {
                render->drawFullscreenTexture(sourceTexture, descriptor);
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
    void RenderNode::copyBuffer(RHI::BufferRange source, RHI::BufferRange destination)
    {
        source = RHI::BufferRange(source.buffer, source.offset, source.size);
        destination = RHI::BufferRange(destination.buffer, destination.offset, destination.size);
        if (source.size != destination.size || source.size == 0)
        {
            throw std::invalid_argument("Graph copies require equal nonempty buffer ranges.");
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.weight = estimateTransferWeight(source.size);
        pass.resources = {{Private::PassResourceAccess::buffer(source, false),
            Private::PassResourceAccess::buffer(destination, true)}, true};
        pass.record = [source = eastl::move(source), destination = eastl::move(destination)](
                          RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphCopy"});
            try
            {
                copy->copyBufferToBuffer(source, destination);
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
    void RenderNode::fillBuffer(RHI::BufferRange target, uint8_t value)
    {
        target = RHI::BufferRange(target.buffer, target.offset, target.size);
        const auto range = target;
        if ((range.offset % 4) != 0 || (range.size % 4) != 0)
        {
            throw std::invalid_argument("Graph fills require four-byte aligned ranges.");
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.weight = estimateTransferWeight(range.size);
        pass.resources = {{Private::PassResourceAccess::buffer(range, true)}, true};
        pass.record = [target = eastl::move(target), value](
                          RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphFill"});
            try
            {
                copy->fillBuffer(target, value);
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
    void RenderNode::writeTexture(RHI::ImageCopyTexture destination, const void *data, uint64_t dataSize,
        RHI::TextureDataLayout dataLayout, RHI::Extent3D writeSize)
    {
        const auto bounds = resolveTextureCopyBounds("RenderNode::writeTexture", destination);
        const auto resolvedExtent = resolveTextureCopyExtent("RenderNode::writeTexture", destination, bounds, writeSize);
        if (data == nullptr)
        {
            throw std::invalid_argument("RenderNode::writeTexture requires non-null source data.");
        }
        const auto footprint = RHI::Private::computeTextureCopyFootprint(
            "RenderNode::writeTexture", destination.texture->getFormat(), dataLayout, resolvedExtent, 1u);
        if (footprint.minimumDataBytes > dataSize)
        {
            throw std::invalid_argument("RenderNode::writeTexture dataSize is smaller than the requested texture region.");
        }
        const uint64_t tightSliceBytes = footprint.tightRowBytes * footprint.blocksY;
        const uint64_t stagingBytes = tightSliceBytes * resolvedExtent.depth;
        auto staging = allocateBuffer({
            .label = "GraphTextureUpload",
            .usage = RHI::BufferUsage::MapWrite | RHI::BufferUsage::CopySrc,
            .size = stagingBytes,
        });
        const auto *sourceBytes = static_cast<const uint8_t *>(data) + dataLayout.offset;
        if (footprint.bytesPerRow == footprint.tightRowBytes && footprint.rowsPerImage == footprint.blocksY)
        {
            staging->write(sourceBytes, stagingBytes);
        }
        else
        {
            auto lock = mDevice->lockResources();
            auto stagingBuffer = staging->buffer();
            stagingBuffer->map();
            try
            {
                auto *mapped = static_cast<uint8_t *>(stagingBuffer->getMappedRange(0, stagingBytes));
                for (uint32_t slice = 0; slice < resolvedExtent.depth; ++slice)
                {
                    const uint8_t *sliceSource = sourceBytes + slice * footprint.bytesPerImage;
                    uint8_t *sliceDst = mapped + slice * tightSliceBytes;
                    for (uint32_t row = 0; row < footprint.blocksY; ++row)
                    {
                        std::memcpy(
                            sliceDst + row * footprint.tightRowBytes,
                            sliceSource + row * footprint.bytesPerRow,
                            footprint.tightRowBytes);
                    }
                }
            }
            catch (...)
            {
                stagingBuffer->unmap();
                throw;
            }
            stagingBuffer->unmap();
        }
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.weight = estimateTransferWeight(stagingBytes);
        const uint32_t transferQueue = mDevice->candidates().transfer;
        if (transferQueue != 0u)
        {
            const RHI::Extent3D granularity = mDevice->queue(transferQueue)->getImageTransferGranularity();
            if (RHI::Private::isDepthStencilFormat(destination.texture->getFormat()) ||
                !satisfiesQueueTransferGranularity(destination, bounds, resolvedExtent, granularity))
            {
                pass.requiresMainQueue = true;
            }
        }
        const uint32_t baseLayer = bounds.is3D ? 0u : destination.origin.z;
        const uint32_t layerCount = bounds.is3D ? 1u : resolvedExtent.depth;
        pass.resources = {{
            Private::PassResourceAccess::buffer(staging->buffer(), false),
            Private::PassResourceAccess::textureRange(
                destination.texture.get(), true, destination.mipLevel, 1u, baseLayer, layerCount, destination.aspect),
        }, true};
        auto stagingBuffer = staging->buffer();
        pass.record = [stagingBuffer = eastl::move(stagingBuffer), destination = eastl::move(destination),
            resolvedExtent](RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphTextureUpload"});
            try
            {
                copy->copyBufferToTexture({.layout = {}, .buffer = stagingBuffer}, destination, resolvedExtent);
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
    void RenderNode::writeTexture(RHI::Texture destination, const void *data, uint64_t dataSize,
        uint32_t mipLevel, uint32_t arrayLayer)
    {
        if (destination.isNull() || destination.get() == nullptr)
        {
            throw std::invalid_argument("RenderNode::writeTexture requires a live destination texture.");
        }
        if (mipLevel >= destination->getMipLevelCount())
        {
            throw std::out_of_range("RenderNode::writeTexture mip level exceeds the texture mip count.");
        }
        if (arrayLayer >= destination->getArrayLayerCount())
        {
            throw std::out_of_range("RenderNode::writeTexture array layer exceeds the texture layer count.");
        }
        const auto dimension = destination->getDimension();
        const RHI::Extent3D extent = {
            .width = eastl::max(1u, destination->getWidth() >> mipLevel),
            .height = dimension == RHI::TextureDimension::e1D ? 1u : eastl::max(1u, destination->getHeight() >> mipLevel),
            .depth = dimension == RHI::TextureDimension::e3D ? eastl::max(1u, destination->getDepth() >> mipLevel) : 1u,
        };
        const RHI::ImageCopyTexture target = {
            .texture = eastl::move(destination),
            .mipLevel = mipLevel,
            .origin = {.x = 0u, .y = 0u, .z = dimension == RHI::TextureDimension::e3D ? 0u : arrayLayer},
        };
        writeTexture(target, data, dataSize, {}, extent);
    }
    void RenderNode::copyTexture(RHI::ImageCopyTexture source, RHI::ImageCopyTexture destination, RHI::Extent3D copySize)
    {
        const auto srcBounds = resolveTextureCopyBounds("RenderNode::copyTexture source", source);
        const auto dstBounds = resolveTextureCopyBounds("RenderNode::copyTexture destination", destination);
        RHI::Extent3D resolvedExtent = copySize;
        if (resolvedExtent.width == 0u && resolvedExtent.height == 0u && resolvedExtent.depth == 0u)
        {
            resolvedExtent = {
                .width = eastl::min(srcBounds.mipWidth - source.origin.x, dstBounds.mipWidth - destination.origin.x),
                .height = eastl::min(srcBounds.mipHeight - source.origin.y, dstBounds.mipHeight - destination.origin.y),
                .depth = eastl::min(srcBounds.depthLimit - source.origin.z, dstBounds.depthLimit - destination.origin.z),
            };
        }
        resolvedExtent = resolveTextureCopyExtent("RenderNode::copyTexture source", source, srcBounds, resolvedExtent);
        resolvedExtent = resolveTextureCopyExtent("RenderNode::copyTexture destination", destination, dstBounds, resolvedExtent);
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.weight = estimateTransferWeight(estimateTextureCopyBytes(source.texture->getFormat(), resolvedExtent));
        const uint32_t transferQueue = mDevice->candidates().transfer;
        if (transferQueue != 0u)
        {
            const RHI::Extent3D granularity = mDevice->queue(transferQueue)->getImageTransferGranularity();
            if (RHI::Private::isDepthStencilFormat(source.texture->getFormat()) ||
                RHI::Private::isDepthStencilFormat(destination.texture->getFormat()) ||
                !satisfiesQueueTransferGranularity(source, srcBounds, resolvedExtent, granularity) ||
                !satisfiesQueueTransferGranularity(destination, dstBounds, resolvedExtent, granularity))
            {
                pass.requiresMainQueue = true;
            }
        }
        const uint32_t srcBaseLayer = srcBounds.is3D ? 0u : source.origin.z;
        const uint32_t srcLayerCount = srcBounds.is3D ? 1u : resolvedExtent.depth;
        const uint32_t dstBaseLayer = dstBounds.is3D ? 0u : destination.origin.z;
        const uint32_t dstLayerCount = dstBounds.is3D ? 1u : resolvedExtent.depth;
        pass.resources = {{
            Private::PassResourceAccess::textureRange(
                source.texture.get(), false, source.mipLevel, 1u, srcBaseLayer, srcLayerCount, source.aspect),
            Private::PassResourceAccess::textureRange(
                destination.texture.get(), true, destination.mipLevel, 1u, dstBaseLayer, dstLayerCount, destination.aspect),
        }, true};
        pass.record = [source = eastl::move(source), destination = eastl::move(destination), resolvedExtent](
                          RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphTextureCopy"});
            try
            {
                copy->copyTextureToTexture(source, destination, resolvedExtent);
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
    void RenderNode::copyTexture(RHI::Texture source, RHI::Texture destination)
    {
        copyTexture(RHI::ImageCopyTexture{.texture = eastl::move(source)}, RHI::ImageCopyTexture{.texture = eastl::move(destination)}, {});
    }
    void RenderNode::upload(RHI::BufferRange destination, const void *data, uint64_t size)
    {
        if (size != destination.size || size == 0 || data == nullptr)
        {
            throw std::invalid_argument("Graph upload bytes must match the destination range.");
        }
        auto staging = allocateBuffer({.label = "GraphUpload", .usage = RHI::BufferUsage::MapWrite | RHI::BufferUsage::CopySrc, .size = size});
        staging->write(data, size);
        copyBuffer(staging->buffer(), destination);
    }
    eastl::shared_ptr<Private::GraphResourceAllocation> RenderNode::prepareReadback(RHI::BufferRange source)
    {
        source = RHI::BufferRange(source.buffer, source.offset, source.size);
        auto staging = allocateBuffer({.label = "GraphReadback", .usage = RHI::BufferUsage::MapRead | RHI::BufferUsage::CopyDst, .size = source.size});
        copyBuffer(source, staging->buffer());
        return staging;
    }
    TextureReadbackStaging RenderNode::prepareTextureReadback(const TextureReadbackDescriptor &source)
    {
        const auto bounds = resolveTextureCopyBounds("RenderNode::prepareTextureReadback", source.source);
        const auto resolvedExtent = resolveTextureCopyExtent("RenderNode::prepareTextureReadback", source.source, bounds, source.readSize);
        const auto footprint = RHI::Private::computeTextureCopyFootprint(
            "RenderNode::prepareTextureReadback", source.source.texture->getFormat(), {}, resolvedExtent, 1u);
        if (footprint.stagingBytes == 0u)
        {
            throw std::invalid_argument("Graph texture readback requires a non-empty byte size.");
        }
        auto staging = allocateBuffer({
            .label = "GraphTextureReadback",
            .usage = RHI::BufferUsage::MapRead | RHI::BufferUsage::CopyDst,
            .size = footprint.stagingBytes,
        });
        Private::GraphPass pass;
        pass.kind = Private::GraphPassKind::Transfer;
        pass.weight = estimateTransferWeight(footprint.stagingBytes);
        const uint32_t transferQueue = mDevice->candidates().transfer;
        if (transferQueue != 0u)
        {
            const RHI::Extent3D granularity = mDevice->queue(transferQueue)->getImageTransferGranularity();
            if (RHI::Private::isDepthStencilFormat(source.source.texture->getFormat()) ||
                !satisfiesQueueTransferGranularity(source.source, bounds, resolvedExtent, granularity))
            {
                pass.requiresMainQueue = true;
            }
        }
        const uint32_t baseLayer = bounds.is3D ? 0u : source.source.origin.z;
        const uint32_t layerCount = bounds.is3D ? 1u : resolvedExtent.depth;
        pass.resources = {{
            Private::PassResourceAccess::textureRange(
                source.source.texture.get(), false, source.source.mipLevel, 1u, baseLayer, layerCount, source.source.aspect),
            Private::PassResourceAccess::buffer(staging->buffer(), true),
        }, true};
        auto stagingBuffer = staging->buffer();
        auto copySource = source.source;
        pass.record = [copySource = eastl::move(copySource), stagingBuffer = eastl::move(stagingBuffer),
            resolvedExtent](RHI::CommandEncoder encoder, const RHI::PassTimestampWrites *)
        {
            auto copy = encoder->beginBlitPass({.label = "GraphTextureReadback"});
            try
            {
                copy->copyTextureToBuffer(copySource, {.layout = {}, .buffer = stagingBuffer}, resolvedExtent);
            }
            catch (...)
            {
                copy->end();
                throw;
            }
            copy->end();
        };
        append(eastl::move(pass));
        return {
            .allocation = eastl::move(staging),
            .byteCount = footprint.stagingBytes,
        };
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
