#include "MBindGroup.hpp"
#include "MBindGroupLayout.hpp"
#include "MBuffer.hpp"
#include "MBindGroupContracts.hpp"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/type_traits.h>
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MSampler.hpp"
#include "MTexture.hpp"
#include "MTextureView.hpp"
#include <EASTL/make_intrusive.h>
#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <stdexcept>
namespace GVM::RHI::Metal
{

    namespace
    {

        /** Declares indirect resources with hazard-tracked render stages or the compute encoder's implicit stage. */
        template <class T>
        void trackUsageImpl(T *encoder, const eastl::vector<eastl::intrusive_ptr<MBindGroup::ResourceUseInfo>> &mAllResourceUseInfos,
            const eastl::vector<MTL::Buffer *> &mNestedArgumentBuffers, MTL::RenderStages stages = 0)
        {
            for (const auto &resourceInfo : mAllResourceUseInfos)
            {
                if (resourceInfo->resources.empty())
                {
                    continue;
                }

                if constexpr (eastl::is_same<T, MTL::RenderCommandEncoder>::value)
                {
                    encoder->useResources(resourceInfo->resources.data(), resourceInfo->resources.size(), resourceInfo->usage, stages);
                }
                else
                {
                    encoder->useResources(resourceInfo->resources.data(), resourceInfo->resources.size(), resourceInfo->usage);
                }
            }

            for (const auto &buffer : mNestedArgumentBuffers)
            {
                if (buffer != nullptr)
                {
                    if constexpr (eastl::is_same<T, MTL::RenderCommandEncoder>::value)
                    {
                        encoder->useResource(buffer, MTL::ResourceUsageRead, stages);
                    }
                    else
                    {
                        encoder->useResource(buffer, MTL::ResourceUsageRead);
                    }
                }
            }
        }
    } // namespace

    MBindGroup::MBindGroup()
    {
    }

    MBindGroup::~MBindGroup()
    {
        if (this->mNativeBuffer != nullptr)
        {
            this->mNativeBuffer->release();
            this->mNativeBuffer = nullptr;
        }
        this->mDevice = nullptr;
        this->mDescriptor = {};
        this->mLabelName = "";

        for (auto &buffer : mNestedArgumentBuffers)
        {
            if (buffer != nullptr)
            {
                buffer->release();
            }
        }
        mNestedArgumentBuffers.clear();
        mAllResourceUseInfos.clear();
    }

    void MBindGroup::init(MDevice *device, const BindGroupDescriptor &descriptor)
    {
        this->mDevice = device;
        this->mDescriptor = descriptor;
        this->mLabelName = descriptor.label;

        auto layout = eastl::static_pointer_cast<MBindGroupLayout>(mDescriptor.layout);
        const auto &layoutDescriptor = layout->getDescriptor();
        if (mDescriptor.entries.size() != layoutDescriptor.entries.size())
            throw std::invalid_argument("MBindGroup descriptor entry count must match its layout.");
        eastl::sort(mDescriptor.entries.begin(), mDescriptor.entries.end(), [](const BindGroupEntry &left, const BindGroupEntry &right) {
            return left.binding < right.binding;
        });
        for (size_t index = 1u; index < mDescriptor.entries.size(); ++index)
            if (mDescriptor.entries[index - 1u].binding == mDescriptor.entries[index].binding)
                throw std::invalid_argument("MBindGroup descriptor contains duplicate binding numbers.");

        eastl::vector<uint64_t> argumentData;
        mAllResourceUseInfos.resize(layoutDescriptor.entries.size());
        mNestedArgumentBuffers.resize(layoutDescriptor.entries.size(), nullptr);
        for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
        {
            const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
            const auto found = eastl::lower_bound(mDescriptor.entries.begin(), mDescriptor.entries.end(), layoutEntry.binding,
                [](const BindGroupEntry &candidate, uint32_t binding) { return candidate.binding < binding; });
            if (found == mDescriptor.entries.end() || found->binding != layoutEntry.binding)
                throw std::invalid_argument("MBindGroup descriptor is missing a layout binding.");
            const auto descriptorIndex = static_cast<size_t>(found - mDescriptor.entries.begin());
            if (mAllResourceUseInfos[descriptorIndex] != nullptr)
            {
                throw std::invalid_argument("MBindGroupLayout contains duplicate binding numbers.");
            }
            const auto &entry = *found;
            const auto kind = Detail::getBindGroupEntryKind(entry);
            if (kind == Detail::BindGroupEntryKind::Undefined || kind != Detail::getBindGroupLayoutEntryKind(layoutEntry))
                throw std::invalid_argument("MBindGroup descriptor resource kind does not match its layout.");
            const size_t count = Detail::getBindGroupEntryResourceCount(entry, kind);
            if (count == 0u || count > layoutEntry.maxCount)
                throw std::invalid_argument("MBindGroup descriptor resource count is incompatible with layout.maxCount.");
            const auto resourceUsage = layout->getResourceUsages()[layoutIndex];
            auto &resourceUse = mAllResourceUseInfos[descriptorIndex];
            resourceUse = eastl::make_intrusive<ResourceUseInfo>();
            resourceUse->usage = resourceUsage;
            if (entry.sampler.empty() == false)
            {
                for (const auto &sampler : entry.sampler)
                {
                    if (sampler.isNull() || sampler.get() == nullptr)
                    {
                        throw std::invalid_argument("MBindGroup::init received a null sampler in a bind group entry.");
                    }
                    argumentData.emplace_back(static_cast<MSampler *>(sampler.get())->getNativeSampler()->gpuResourceID()._impl);
                }
            }
            else if (entry.textureView.empty() == false)
            {
                if (entry.textureView.size() > 1)
                {
                    eastl::vector<uint64_t> nestedArgumentData;
                    MTL::Buffer *nestedArgumentBuffer = nullptr;
                    for (const auto &textureView : entry.textureView)
                    {
                        if (textureView.isNull() || textureView.get() == nullptr)
                        {
                            throw std::invalid_argument("MBindGroup::init received a null texture view in a bind group entry.");
                        }
                        auto nativeTexture = static_cast<MTextureView *>(textureView.get())->getNativeTextureView();
                        nestedArgumentData.emplace_back(nativeTexture->gpuResourceID()._impl);
                        resourceUse->resources.emplace_back(nativeTexture);
                    }
                    nestedArgumentBuffer = mDevice->getNativeDevice()->newBuffer(nestedArgumentData.data(), nestedArgumentData.size() * sizeof(nestedArgumentData[0]), MTL::ResourceStorageModeShared);
                    if (nestedArgumentBuffer == nullptr)
                    {
                        throw std::runtime_error("MBindGroup::init failed to allocate a nested Metal argument buffer.");
                    }
                    nestedArgumentBuffer->setLabel(NS::String::string((descriptor.label + "__binding__" + eastl::to_string(layoutEntry.binding)).c_str(), NS::UTF8StringEncoding));
                    argumentData.emplace_back(nestedArgumentBuffer->gpuAddress());
                    mNestedArgumentBuffers[layoutIndex] = nestedArgumentBuffer;
                }
                else
                {
                    for (const auto &textureView : entry.textureView)
                    {
                        if (textureView.isNull() || textureView.get() == nullptr)
                        {
                            throw std::invalid_argument("MBindGroup::init received a null texture view in a bind group entry.");
                        }
                        auto nativeTexture = static_cast<MTextureView *>(textureView.get())->getNativeTextureView();
                        argumentData.emplace_back(nativeTexture->gpuResourceID()._impl);
                        resourceUse->resources.emplace_back(nativeTexture);
                    }
                }
            }
            else if (entry.buffer.empty() == false)
            {
                for (const auto &buffer : entry.buffer)
                {
                    if (buffer.buffer.isNull() || buffer.buffer.get() == nullptr)
                    {
                        throw std::invalid_argument("MBindGroup::init received a null buffer in a bind group entry.");
                    }
                    auto *bufferImpl = static_cast<MBuffer *>(buffer.buffer.get());
                    Private::validateBindGroupBufferRange(bufferImpl->getStorageSize(), buffer);
                    auto nativeBuffer = bufferImpl->getNativeBuffer();
                    argumentData.emplace_back(nativeBuffer->gpuAddress() + buffer.offset);
                    resourceUse->resources.emplace_back(nativeBuffer);
                }
            }
            else
            {
                throw std::runtime_error("Unsupported bind group binding type");
            }
        }
        for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
        {
            const auto &layoutEntry = layoutDescriptor.entries[layoutIndex];
            const bool renderVisible = (layoutEntry.visibility & Private::RenderPassShaderStages) != 0;
            const bool computeVisible = (layoutEntry.visibility & Private::ComputePassShaderStages) != 0;
            if (!renderVisible && !computeVisible)
            {
                continue;
            }
            const auto found = eastl::lower_bound(mDescriptor.entries.begin(), mDescriptor.entries.end(), layoutEntry.binding,
                [](const BindGroupEntry &candidate, uint32_t binding) { return candidate.binding < binding; });
            if (found == mDescriptor.entries.end() || found->binding != layoutEntry.binding || found->textureView.empty())
            {
                continue;
            }
            for (const auto &viewHandle : found->textureView)
            {
                auto *view = static_cast<MTextureView *>(viewHandle.get());
                auto *texture = view != nullptr ? view->getParentTexture() : nullptr;
                if (texture == nullptr)
                {
                    continue;
                }
                const Private::PassTextureUse use = Private::makePassTextureUse(
                    *texture, view->getDescriptor(), layoutEntry.storageTexture.access);
                if (renderVisible)
                {
                    mRenderTextureUses.push_back(use);
                }
                if (computeVisible)
                {
                    mComputeTextureUses.push_back(use);
                }
            }
        }
        this->mNativeBuffer = mDevice->getNativeDevice()->newBuffer(argumentData.data(), argumentData.size() * sizeof(argumentData[0]), MTL::ResourceStorageModeShared);
        if (this->mNativeBuffer == nullptr)
        {
            throw std::runtime_error("MBindGroup::init failed to allocate the root Metal argument buffer.");
        }
        this->mNativeBuffer->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
    }

    MTL::Buffer *MBindGroup::getNativeBuffer() const
    {
        return this->mNativeBuffer;
    }

    const BindGroupDescriptor &MBindGroup::getDescriptor() const
    {
        return this->mDescriptor;
    }


    void MBindGroup::trackUsage(MTL::RenderCommandEncoder *encoder, MTL::RenderStages stages)
    {
        if (stages != 0)
        {
            trackUsageImpl(encoder, mAllResourceUseInfos, mNestedArgumentBuffers, stages);
        }
    }

    void MBindGroup::trackUsage(MTL::ComputeCommandEncoder *encoder)
    {
        trackUsageImpl(encoder, mAllResourceUseInfos, mNestedArgumentBuffers);
    }


} // namespace GVM::RHI::Metal
