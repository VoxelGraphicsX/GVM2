#include "MArguments.hpp"
#include "MBuffer.hpp"
#include "MDevice.hpp"
#include "MPipelineLayout.hpp"
#include "MTexture.hpp"
#include "MTextureView.hpp"
#include "MSampler.hpp"
#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>
#include <EASTL/algorithm.h>
#include <cstring>
#include <stdexcept>

namespace GVM::RHI::Metal
{
    void MArguments::init(MDevice &device, const ArgumentsDescriptor &descriptor)
    {
        auto *layout = dynamic_cast<MPipelineLayout *>(descriptor.layout.get());
        if (layout == nullptr || &layout->getDevice() != &device)
        {
            throw std::invalid_argument("Metal Args require a pipeline layout from the same device.");
        }
        const auto &abi = layout->getDescriptor().arguments;
        Private::validateArgumentSnapshotShape(abi, descriptor);
        mPool = device.getArgumentPagePool();
        const uint64_t reserved = sizeof(MArguments) + descriptor.values.size() +
            (descriptor.buffers.size() + descriptor.resources.size()) * sizeof(MArgumentResourceUse) +
            descriptor.resources.size() * sizeof(MArgumentTextureUse);
        mPool->addSnapshotBytes(reserved);
        mReserved = reserved;
        mLayout = descriptor.layout;
        mBytes.assign(descriptor.values.begin(), descriptor.values.end());
        mResources.reserve(descriptor.buffers.size() + descriptor.resources.size());
        for (size_t index = 0; index < descriptor.buffers.size(); ++index)
        {
            const auto &range = descriptor.buffers[index];
            auto *buffer = range.buffer.isNull() ? nullptr : dynamic_cast<MBuffer *>(range.buffer.get());
            if (buffer == nullptr || &buffer->getDevice() != &device || buffer->getNativeBuffer() == nullptr)
            {
                throw std::invalid_argument("Args require a live buffer allocation from the same device.");
            }
            const auto &field = abi.buffers[index];
            const uint64_t length = Private::validateArgumentBufferRange(field, buffer->getUsage(), buffer->getStorageSize(), range);
            auto &native = *buffer->getNativeBuffer();
            const uint64_t address = native.gpuAddress() + range.offset;
            std::memcpy(mBytes.data() + field.addressOffset, &address, sizeof(address));
            std::memcpy(mBytes.data() + field.lengthOffset, &length, sizeof(length));
            if (field.visibility != 0)
            {
                retainResource(native, &native, field.type == BufferBindingType::Storage ?
                    MTL::ResourceUsageRead | MTL::ResourceUsageWrite : MTL::ResourceUsageRead, field.visibility);
            }
        }
        for (size_t index = 0; index < descriptor.resources.size(); ++index)
        {
            const auto &field = abi.resources[index];
            const auto &entry = descriptor.resources[index];
            uint64_t handle = 0;
            if (field.entry.sampler.type != SamplerBindingType::Undefined)
            {
                const auto *samplerHandle = eastl::get_if<Sampler>(&entry);
                auto *sampler = samplerHandle == nullptr || samplerHandle->isNull() ?
                    nullptr : dynamic_cast<MSampler *>(samplerHandle->get());
                if (sampler != nullptr && &sampler->getDevice() != &device)
                {
                    throw std::invalid_argument("Args require a sampler from the same device.");
                }
                Private::validateArgumentSampler(field.entry, entry, sampler != nullptr && sampler->getNativeSampler() != nullptr,
                    sampler != nullptr && sampler->getDescriptor().compare != CompareFunction::Undefined);
                handle = sampler->getResourceId();
                if (field.entry.visibility != 0)
                {
                    retainResource(*sampler->getNativeSampler(), nullptr, 0, field.entry.visibility);
                }
            }
            else
            {
                const auto *viewHandle = eastl::get_if<TextureView>(&entry);
                auto *view = viewHandle == nullptr || viewHandle->isNull() ?
                    nullptr : dynamic_cast<MTextureView *>(viewHandle->get());
                auto *parentTexture = view != nullptr ? view->getParentTexture() : nullptr;
                const bool viewLive = view != nullptr && view->getNativeTextureView() != nullptr &&
                    parentTexture != nullptr && parentTexture->getNativeTexture() != nullptr;
                if (view != nullptr && parentTexture != nullptr && &parentTexture->getDevice() != &device)
                {
                    throw std::invalid_argument("Args require a texture view from the same device.");
                }
                Private::validateArgumentTextureView(field.entry, entry, viewLive,
                    view != nullptr ? view->getDimension() : TextureViewDimension::Undefined,
                    parentTexture != nullptr ? parentTexture->getUsage() : TextureUsageFlags(0),
                    view != nullptr ? view->getFormat() : TextureFormat::Undefined);
                const bool storage = field.entry.storageTexture.access != StorageTextureAccess::Undefined;
                handle = view->getResourceId();
                if (field.entry.visibility != 0)
                {
                    auto *native = view->getNativeTextureView();
                    retainResource(*native, native, storage ? MTL::ResourceUsageRead | MTL::ResourceUsageWrite : MTL::ResourceUsageRead,
                        field.entry.visibility);
                    const auto existingUse = eastl::find_if(mTextureUses.begin(), mTextureUses.end(),
                        [view, access = field.entry.storageTexture.access](const MArgumentTextureUse &use)
                        {
                            return use.view.get() == view && use.access == access;
                        });
                    if (existingUse == mTextureUses.end())
                    {
                        mTextureUses.push_back({*viewHandle, field.entry.storageTexture.access, field.entry.visibility});
                    }
                    else
                    {
                        existingUse->visibility |= field.entry.visibility;
                    }
                }
            }
            std::memcpy(mBytes.data() + field.offset, &handle, sizeof(handle));
        }
        for (const auto &use : mTextureUses)
        {
            const bool renderVisible = (use.visibility & Private::RenderPassShaderStages) != 0;
            const bool computeVisible = (use.visibility & Private::ComputePassShaderStages) != 0;
            if (!renderVisible && !computeVisible)
            {
                continue;
            }
            auto *view = static_cast<MTextureView *>(use.view.get());
            auto *parentTexture = view != nullptr ? view->getParentTexture() : nullptr;
            if (parentTexture == nullptr)
            {
                continue;
            }
            const Private::PassTextureUse passUse = Private::makePassTextureUse(
                *parentTexture, view->getDescriptor(), use.access);
            if (renderVisible)
            {
                mRenderTextureUses.push_back(passUse);
            }
            if (computeVisible)
            {
                mComputeTextureUses.push_back(passUse);
            }
        }
    }

    MArguments::~MArguments()
    {
        if (mReserved != 0)
        {
            mPool->removeSnapshotBytes(mReserved);
        }
    }

    void MArguments::retainResource(NS::Object &allocation, MTL::Resource *resource, MTL::ResourceUsage usage, ShaderStageFlags visibility)
    {
        MTL::RenderStages stages = 0;
        if (visibility & ShaderStage::Vertex)
        {
            stages |= MTL::RenderStageVertex;
        }
        if (visibility & ShaderStage::Fragment)
        {
            stages |= MTL::RenderStageFragment;
        }
        for (auto &existing : mResources)
        {
            if (existing.allocation.get() == &allocation)
            {
                existing.usage |= usage;
                existing.stages |= stages;
                existing.compute |= (visibility & ShaderStage::Compute) != 0;
                return;
            }
        }
        mResources.push_back({NS::RetainPtr(&allocation), resource, usage, stages, (visibility & ShaderStage::Compute) != 0});
    }

    void MArguments::trackUsage(MTL::RenderCommandEncoder &encoder) const
    {
        for (const auto &use : mResources)
        {
            if (use.resource != nullptr && use.stages != 0)
            {
                encoder.useResource(use.resource, use.usage, use.stages);
            }
        }
    }

    void MArguments::trackUsage(MTL::ComputeCommandEncoder &encoder) const
    {
        for (const auto &use : mResources)
        {
            if (use.resource != nullptr && use.compute)
            {
                encoder.useResource(use.resource, use.usage);
            }
        }
    }

    MArgumentPage MArgumentPagePool::createPage(uint64_t size)
    {
        auto buffer = NS::TransferPtr(mDevice.getNativeDevice()->newBuffer(size, MTL::ResourceStorageModeShared));
        if (!buffer)
        {
            throw std::runtime_error("Metal argument page allocation failed.");
        }
        return {eastl::move(buffer), size};
    }

    void MArgumentPagePool::destroyPage(MArgumentPage &page)
    {
        page.buffer.reset();
        page.size = 0;
    }

    MArgumentStorage::MArgumentStorage(eastl::intrusive_ptr<MArgumentPagePool> pool) : mPool(eastl::move(pool)) {}
    MArgumentStorage::~MArgumentStorage() { complete(); }

    MArgumentAllocation MArgumentStorage::allocate(MDevice &device, Arguments arguments)
    {
        auto *snapshot = dynamic_cast<MArguments *>(arguments.get());
        if (snapshot == nullptr || &static_cast<MPipelineLayout *>(snapshot->getLayout().get())->getDevice() != &device)
        {
            throw std::invalid_argument("Metal encoders require an Args snapshot from the same device.");
        }
        const auto &bytes = snapshot->getBytes();
        if (bytes.empty())
        {
            mArguments.push_back(arguments);
            return {nullptr, 0, eastl::move(arguments)};
        }
        uint64_t offset = (mCursor + 255u) & ~uint64_t(255u);
        if (mPages.empty() || offset > mPages.back().size || bytes.size() > mPages.back().size - offset)
        {
            MArgumentPage page = mPool->acquire(bytes.size());
            try { mPages.push_back(eastl::move(page)); }
            catch (...)
            {
                mPool->release(eastl::span<MArgumentPage>(&page, 1), false);
                throw;
            }
            mReserved += mPages.back().size;
            offset = 0;
        }
        mArguments.push_back(arguments);
        auto *page = mPages.back().buffer.get();
        std::memcpy(static_cast<uint8_t *>(page->contents()) + offset, bytes.data(), bytes.size());
        mCursor = offset + bytes.size();
        return {page, offset, eastl::move(arguments)};
    }

    void MArgumentStorage::markSubmitted()
    {
        if (mSubmitted)
        {
            return;
        }
        mSubmitted = true;
        mPool->markSubmitted(mReserved);
    }

    void MArgumentStorage::complete()
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
