#include "VKArguments.hpp"
#include "VKBuffer.hpp"
#include "VKDevice.hpp"
#include "VKQueue.hpp"
#include "VKEnumUtils.hpp"
#include "VKPipelineLayout.hpp"
#include "VKTextureView.hpp"
#include "VKSampler.hpp"
#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>
#include <EASTL/algorithm.h>
#include <cstring>

namespace GVM::RHI::Vulkan
{
    void VKArguments::init(VKDevice &device, const ArgumentsDescriptor &descriptor)
    {
        auto *layout = dynamic_cast<VKPipelineLayout *>(descriptor.layout.get());
        if (layout == nullptr || &layout->getDevice() != &device)
        {
            throw std::invalid_argument("Vulkan Args require a pipeline layout from the same device.");
        }
        const auto &abi = layout->getDescriptor().arguments;
        Private::validateArgumentSnapshotShape(abi, descriptor);
        mDevice = &device;
        mPool = device.getArgumentResources().getPagePool();
        const uint64_t reserved = sizeof(VKArguments) + descriptor.values.size() +
            descriptor.buffers.size() * sizeof(VKTaskDependencyResolver::BufferRangeSyncRequest) +
            descriptor.resources.size() * (sizeof(VKTaskDependencyResolver::TextureSubresourceSyncRequest) + sizeof(VKSampler *));
        mPool->addSnapshotBytes(reserved);
        mReserved = reserved;
        mLayout = descriptor.layout;
        mBytes.assign(descriptor.values.begin(), descriptor.values.end());
        mBufferUses.reserve(descriptor.buffers.size());
        mTextureUses.reserve(descriptor.resources.size());
        mSamplers.reserve(descriptor.resources.size());
        for (size_t index = 0; index < descriptor.buffers.size(); ++index)
        {
            const auto &range = descriptor.buffers[index];
            auto *buffer = range.buffer.isNull() ? nullptr : dynamic_cast<VKBuffer *>(range.buffer.get());
            if (buffer == nullptr || &buffer->getDevice() != &device || buffer->isDestroyed())
            {
                throw std::invalid_argument("Args require a live buffer allocation from the same device.");
            }
            const auto &field = abi.buffers[index];
            if (buffer->getDeviceAddress() == 0)
            {
                throw std::invalid_argument("Args buffer usage does not match the declared resource type.");
            }
            const auto length = Private::validateArgumentBufferRange(field, buffer->getUsage(), buffer->getStorageSize(), range);
            const uint64_t address = buffer->getDeviceAddress() + range.offset;
            std::memcpy(mBytes.data() + field.addressOffset, &address, sizeof(address));
            std::memcpy(mBytes.data() + field.lengthOffset, &length, sizeof(length));
            if (field.visibility != 0)
            {
                captureBufferUse({buffer, range.offset, length,
                    translateShaderStagesToPipelineStages(field.visibility),
                    field.type == BufferBindingType::Storage ?
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead});
            }
        }
        constexpr vk::PipelineStageFlags2 renderStages =
            vk::PipelineStageFlagBits2::eVertexShader |
            vk::PipelineStageFlagBits2::eTessellationControlShader |
            vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
            vk::PipelineStageFlagBits2::eGeometryShader |
            vk::PipelineStageFlagBits2::eFragmentShader;
        for (size_t index = 0; index < descriptor.resources.size(); ++index)
        {
            const auto &field = abi.resources[index];
            const auto &entry = descriptor.resources[index];
            auto &table = *layout->getArgumentTable();
            uint64_t handle = 0;
            if (field.entry.sampler.type != SamplerBindingType::Undefined)
            {
                const auto *samplerHandle = eastl::get_if<Sampler>(&entry);
                auto *sampler = samplerHandle == nullptr || samplerHandle->isNull() ?
                    nullptr : dynamic_cast<VKSampler *>(samplerHandle->get());
                if (sampler != nullptr && &sampler->getDevice() != &device)
                {
                    throw std::invalid_argument("Args require a sampler from the same device.");
                }
                Private::validateArgumentSampler(field.entry, entry,
                    sampler != nullptr && sampler->getNativeSampler() && !sampler->isDestroyRequested(),
                    sampler != nullptr && sampler->getDescriptor().compare != CompareFunction::Undefined);
                handle = sampler->getArgumentSlots().get(table, field.entry.binding, {sampler->getNativeSampler(), {}, vk::ImageLayout::eUndefined});
                if (field.entry.visibility != 0)
                {
                    if (eastl::find(mSamplers.begin(), mSamplers.end(), sampler) == mSamplers.end())
                    {
                        mSamplers.push_back(sampler);
                        sampler->retainReference();
                    }
                }
            }
            else
            {
                const auto *viewHandle = eastl::get_if<TextureView>(&entry);
                auto *view = viewHandle == nullptr || viewHandle->isNull() ?
                    nullptr : dynamic_cast<VKTextureView *>(viewHandle->get());
                if (view != nullptr && &view->getDevice() != &device)
                {
                    throw std::invalid_argument("Args require a texture view from the same device.");
                }
                Private::validateArgumentTextureView(field.entry, entry,
                    view != nullptr && view->getNativeImageView() && !view->isDestroyRequested() && !view->getTexture()->isDestroyRequested(),
                    view != nullptr ? view->getDescriptor().dimension : TextureViewDimension::Undefined,
                    view != nullptr ? view->getTexture()->getUsage() : TextureUsageFlags(0),
                    view != nullptr ? view->getFormat() : TextureFormat::Undefined);
                const StorageTextureAccess access = field.entry.storageTexture.access;
                const bool storage = access != StorageTextureAccess::Undefined;
                const auto imageLayout = storage ? vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal;
                handle = view->getArgumentSlots().get(table, field.entry.binding, {{}, view->getNativeImageView(), imageLayout});
                if (field.entry.visibility != 0)
                {
                    auto existing = eastl::find_if(mTextureUses.begin(), mTextureUses.end(), [view, imageLayout](const auto &use)
                    {
                        return use.view == view && use.layout == imageLayout;
                    });
                    const auto stages = translateShaderStagesToPipelineStages(field.entry.visibility);
                    if (existing != mTextureUses.end())
                    {
                        existing->stages |= stages;
                        const auto index = static_cast<size_t>(existing - mTextureUses.begin());
                        mTextureUseAccesses[index] = static_cast<StorageTextureAccess>(
                            static_cast<uint32_t>(mTextureUseAccesses[index]) | static_cast<uint32_t>(access));
                    }
                    else
                    {
                        mTextureUses.push_back({view, imageLayout, stages, storage ?
                            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite : vk::AccessFlagBits2::eShaderRead});
                        mTextureUseAccesses.push_back(access);
                        view->retainReference();
                    }
                }
            }
            std::memcpy(mBytes.data() + field.offset, &handle, sizeof(handle));
        }
        for (size_t useIndex = 0u; useIndex < mTextureUses.size(); ++useIndex)
        {
            const auto &use = mTextureUses[useIndex];
            const Private::PassTextureUse passUse = Private::makePassTextureUse(
                *use.view->getTexture(), use.view->getDescriptor(), mTextureUseAccesses[useIndex]);
            if ((use.stages & renderStages) != vk::PipelineStageFlags2{})
            {
                mRenderTextureUses.push_back(passUse);
            }
            if ((use.stages & vk::PipelineStageFlagBits2::eComputeShader) != vk::PipelineStageFlags2{})
            {
                mComputeTextureUses.push_back(passUse);
            }
        }

    }

    void VKArguments::captureBufferUse(VKTaskDependencyResolver::BufferRangeSyncRequest use)
    {
        const auto position = eastl::find_if(mBufferUses.begin(), mBufferUses.end(), [buffer = use.buffer](const auto &existing)
        {
            return existing.buffer == buffer;
        });
        const bool firstUse = position == mBufferUses.end();
        mBufferUses.insert(position, use);
        if (firstUse)
        {
            use.buffer->retainReference();
        }
    }

    VKArguments::~VKArguments()
    {
        // Each contiguous buffer group owns one native allocation reference.
        VKBuffer *previousBuffer = nullptr;
        for (const auto &use : mBufferUses)
        {
            if (use.buffer != previousBuffer)
            {
                previousBuffer = use.buffer;
                use.buffer->releaseReference();
            }
        }
        for (const auto &use : mTextureUses)
        {
            use.view->releaseReference();
        }
        for (auto *sampler : mSamplers)
        {
            sampler->releaseReference();
        }
        if (mReserved != 0)
        {
            mPool->removeSnapshotBytes(mReserved);
        }
    }

    vk::DescriptorSet VKArguments::getResourceTable() const
    {
        const auto *table = static_cast<const VKPipelineLayout *>(mLayout.get())->getArgumentTable();
        return table != nullptr ? table->getSet() : vk::DescriptorSet{};
    }

    VKArgumentPage VKArgumentPagePool::createPage(uint64_t size)
    {
        VKArgumentPage page;
        page.size = size;
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = page.size;
        bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocation{};
        if (vmaCreateBuffer(mDevice.getAllocator(), &bufferInfo, &allocationInfo, &page.buffer, &page.allocation, &allocation) != VK_SUCCESS)
        {
            throw std::runtime_error("Vulkan argument page allocation failed.");
        }
        page.data = allocation.pMappedData;
        page.address = mDevice.getNativeDevice().getBufferAddress(vk::BufferDeviceAddressInfo{vk::Buffer(page.buffer)});
        VkMemoryPropertyFlags properties = 0;
        vmaGetAllocationMemoryProperties(mDevice.getAllocator(), page.allocation, &properties);
        page.coherent = (properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        return page;
    }

    void VKArgumentPagePool::destroyPage(VKArgumentPage &page)
    {
        if (page.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(mDevice.getAllocator(), page.buffer, page.allocation);
            page = {};
        }
    }

    bool VKArgumentPagePool::waitForSubmittedWork()
    {
        VKQueue *queue = mDevice.getMainQueueImpl();
        return queue != nullptr && queue->waitForOldestPendingSubmission();
    }

    VKArgumentStorage::VKArgumentStorage(VKDevice &device) : mDevice(device), mPool(device.getArgumentResources().getPagePool()) {}
    VKArgumentStorage::~VKArgumentStorage() { complete(); }

    VKArgumentAllocation VKArgumentStorage::allocate(Arguments arguments)
    {
        const auto &bytes = static_cast<VKArguments *>(arguments.get())->getBytes();
        if (bytes.empty())
        {
            auto *snapshot = static_cast<VKArguments *>(arguments.get());
            mArguments.push_back(eastl::move(arguments));
            return {0, snapshot};
        }
        uint64_t offset = (mCursor + 255u) & ~uint64_t(255u);
        if (mPages.empty() || offset > mPages.back().size || bytes.size() > mPages.back().size - offset)
        {
            VKArgumentPage page = mPool->acquire(bytes.size());
            try { mPages.push_back(page); }
            catch (...)
            {
                mPool->release(eastl::span<VKArgumentPage>(&page, 1), false);
                throw;
            }
            mReserved += page.size;
            offset = 0;
        }
        const auto &page = mPages.back();
        std::memcpy(static_cast<uint8_t *>(page.data) + offset, bytes.data(), bytes.size());
        if (!page.coherent && vmaFlushAllocation(mDevice.getAllocator(), page.allocation, offset, bytes.size()) != VK_SUCCESS)
        {
            throw std::runtime_error("Vulkan argument page flush failed.");
        }
        mArguments.push_back(eastl::move(arguments));
        mCursor = offset + bytes.size();
        return {page.address + offset, static_cast<VKArguments *>(mArguments.back().get())};
    }

    void VKArgumentStorage::markSubmitted()
    {
        if (mSubmitted)
        {
            return;
        }
        mSubmitted = true;
        mPool->markSubmitted(mReserved);
    }

    void VKArgumentStorage::complete()
    {
        mArguments.clear();
        if (!mPages.empty())
        {
            mPool->release(mPages, mSubmitted);
            mPages.clear();
        }
        mReserved = 0;
        mCursor = 0;
    }
}
