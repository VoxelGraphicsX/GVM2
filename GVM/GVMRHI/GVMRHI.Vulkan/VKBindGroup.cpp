#include "VKBindGroup.hpp"

#include "VKBindingUtils.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKBuffer.hpp"
#include "VKDescriptorManager.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKSampler.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <cstdint>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr eastl::string_view BindGroupLogCategory = "gvmrhi.vulkan.bind_group";
    } // namespace

    vk::DescriptorSet VKBindGroup::getNativeDescriptorSet() const
    {
        return mDescriptorSet;
    }

    const BindGroupLayout &VKBindGroup::getLayoutHandle() const
    {
        return mLayout;
    }

    const eastl::vector<VKBindGroup::ResolvedBinding> &VKBindGroup::getResolvedBindings() const
    {
        return mResolvedBindings;
    }

    namespace
    {
        /** Returns true when a bind-group layout entry declares a storage texture binding. */
        bool isStorageTextureBinding(const BindGroupLayoutEntry &entry)
        {
            return entry.storageTexture.access != StorageTextureAccess::Undefined;
        }

        /** Returns a stable label for diagnostics that involve a Vulkan texture binding. */
        eastl::string describeTextureLabel(const VKTexture *texture)
        {
            if (texture == nullptr)
            {
                return "<null>";
            }
            return texture->getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(texture->getLabelName().c_str());
        }

        /** Rejects bind groups that bind the same texture as both sampled and storage resources. */
        void validateTextureRoleConflicts(const eastl::string &bindGroupLabel, const eastl::vector<VKBindGroup::ResolvedBinding> &bindings)
        {
            for (size_t sampledBindingIndex = 0u; sampledBindingIndex < bindings.size(); ++sampledBindingIndex)
            {
                const VKBindGroup::ResolvedBinding &sampledBinding = bindings[sampledBindingIndex];
                if (isStorageTextureBinding(sampledBinding.layoutEntry) || sampledBinding.descriptorEntry.textureView.empty())
                {
                    continue;
                }

                for (const TextureView &sampledViewHandle : sampledBinding.descriptorEntry.textureView)
                {
                    auto *sampledView = static_cast<VKTextureView *>(sampledViewHandle.get());
                    const VKTexture *sampledTexture = sampledView != nullptr ? sampledView->getTexture() : nullptr;
                    if (sampledTexture == nullptr)
                    {
                        continue;
                    }

                    for (size_t storageBindingIndex = 0u; storageBindingIndex < bindings.size(); ++storageBindingIndex)
                    {
                        const VKBindGroup::ResolvedBinding &storageBinding = bindings[storageBindingIndex];
                        if (!isStorageTextureBinding(storageBinding.layoutEntry) || storageBinding.descriptorEntry.textureView.empty())
                        {
                            continue;
                        }

                        for (const TextureView &storageViewHandle : storageBinding.descriptorEntry.textureView)
                        {
                            auto *storageView = static_cast<VKTextureView *>(storageViewHandle.get());
                            const VKTexture *storageTexture = storageView != nullptr ? storageView->getTexture() : nullptr;
                            if (sampledTexture != storageTexture)
                            {
                                continue;
                            }

                            throw makeInvalidArgument(
                                "VKBindGroup::init bind group '" + bindGroupLabel +
                                "' binds texture '" + describeTextureLabel(sampledTexture) +
                                "' as both sampled and storage resources in the same descriptor set. Sampled binding " +
                                eastl::to_string(sampledBinding.layoutEntry.binding) +
                                ", storage binding " + eastl::to_string(storageBinding.layoutEntry.binding) +
                                ". Split the sampled and RW texture views into different bind groups and dispatch them in ordered phases.");
                        }
                    }
                }
            }
        }

        template <typename Fn>
        void forEachBindGroupDescriptorResource(const BindGroupDescriptor &descriptor, Fn &&visitor)
        {
            for (const BindGroupEntry &entry : descriptor.entries)
            {
                for (const BufferRange &bufferRange : entry.buffer)
                {
                    visitor(bufferRange.buffer, nullptr, Sampler{});
                }
                for (const TextureView &viewHandle : entry.textureView)
                {
                    auto *view = static_cast<VKTextureView *>(viewHandle.get());
                    visitor(Buffer{}, view != nullptr ? view->getTexture() : nullptr, Sampler{});
                }
                for (const Sampler &samplerHandle : entry.sampler)
                {
                    visitor(Buffer{}, nullptr, samplerHandle);
                }
            }
        }

        struct BindGroupResourceCounts
        {
            size_t entryCount = 0u;
            size_t bufferRangeCount = 0u;
            size_t textureViewCount = 0u;
            size_t samplerCount = 0u;
        };

        [[nodiscard]]
        BindGroupResourceCounts countBindGroupResources(const BindGroupDescriptor &descriptor)
        {
            BindGroupResourceCounts counts = {};
            counts.entryCount = descriptor.entries.size();
            for (const BindGroupEntry &entry : descriptor.entries)
            {
                counts.bufferRangeCount += entry.buffer.size();
                counts.textureViewCount += entry.textureView.size();
                counts.samplerCount += entry.sampler.size();
            }
            return counts;
        }

    } // namespace

    VKBindGroup::~VKBindGroup()
    {
        if (mDevice != nullptr)
        {
            mDevice->noteBindGroupDestroyed();
        }
        if (mResourceReferencesRegistered && mDevice != nullptr)
        {
            const BindGroupResourceCounts resourceCounts = countBindGroupResources(mDescriptor);
            eastl::vector<VKBuffer *> buffersToRelease;
            eastl::vector<VKTexture *> texturesToRelease;
            eastl::vector<VKSampler *> samplersToRelease;
            buffersToRelease.reserve(resourceCounts.bufferRangeCount);
            texturesToRelease.reserve(resourceCounts.textureViewCount);
            samplersToRelease.reserve(resourceCounts.samplerCount);
            forEachBindGroupDescriptorResource(mDescriptor, [this, &buffersToRelease, &texturesToRelease, &samplersToRelease](Buffer buffer, VKTexture *texture, Sampler sampler)
            {
                if (!buffer.isNull())
                {
                    buffersToRelease.push_back(static_cast<VKBuffer *>(buffer.get()));
                }
                if (texture != nullptr)
                {
                    texturesToRelease.push_back(texture);
                }
                if (!sampler.isNull())
                {
                    samplersToRelease.push_back(static_cast<VKSampler *>(sampler.get()));
                }
            });
            mResourceReferencesRegistered = false;
            GVMLogDebug(
                this, BindGroupLogCategory,
                "event=bind_group_destroy_enqueue_resource_releases label={} descriptor_set_ptr={} buffer_refs={} texture_refs={} sampler_refs={}",
                safeLogLabel(mLabelName),
                reinterpret_cast<void *>(static_cast<VkDescriptorSet>(mDescriptorSet)),
                buffersToRelease.size(),
                texturesToRelease.size(),
                samplersToRelease.size());
            mDevice->enqueueBindGroupReferenceReleases(
                eastl::move(buffersToRelease),
                eastl::move(texturesToRelease),
                eastl::move(samplersToRelease));
        }

        if (mDevice != nullptr && mDescriptorPool && mDescriptorSet)
        {
            GVMLogDebug(
                this, BindGroupLogCategory,
                "event=bind_group_destroy_free_descriptor_set label={} descriptor_pool_ptr={} descriptor_set_ptr={}",
                safeLogLabel(mLabelName),
                reinterpret_cast<void *>(static_cast<VkDescriptorPool>(mDescriptorPool)),
                reinterpret_cast<void *>(static_cast<VkDescriptorSet>(mDescriptorSet)));
            mDevice->freeDescriptorSet(mDescriptorPool, mDescriptorSet);
        }
    }

    const eastl::shared_ptr<Internal::LogContext> &VKBindGroup::getLogContext() const
    {
        return mLogContext;
    }

    Logger VKBindGroup::getLogger() const
    {
        return mLogger;
    }

    void VKBindGroup::init(VKDevice *device, const BindGroupDescriptor &descriptor)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKBindGroup::init requires a valid device.");
        }
        if (descriptor.layout == nullptr)
        {
            throw makeInvalidArgument("VKBindGroup::init requires a valid bind group layout.");
        }

        mLogger = device->getLogger();
        mLogContext = device->getLogContext();
        mDevice = device;
        mDescriptor = descriptor;
        eastl::sort(mDescriptor.entries.begin(), mDescriptor.entries.end(), [](const BindGroupEntry &lhs, const BindGroupEntry &rhs)
        {
            return lhs.binding < rhs.binding;
        });
        mLayout = descriptor.layout;
        mLabelName = descriptor.label;

        auto *layoutImpl = static_cast<VKBindGroupLayout *>(mLayout.get());
        const BindGroupLayoutDescriptor &layoutDescriptor = layoutImpl->getDescriptor();
        const eastl::vector<size_t> resolvedIndices = Detail::resolveBindGroupEntryIndices(layoutDescriptor, mDescriptor.entries);

        mResolvedBindings.clear();
        mResolvedBindings.reserve(layoutDescriptor.entries.size());
        for (size_t layoutIndex = 0; layoutIndex < layoutDescriptor.entries.size(); ++layoutIndex)
        {
            ResolvedBinding resolved = {};
            resolved.layoutEntry = layoutDescriptor.entries[layoutIndex];
            resolved.descriptorEntry = mDescriptor.entries[resolvedIndices[layoutIndex]];
            mResolvedBindings.push_back(resolved);
        }
        validateTextureRoleConflicts(mLabelName.empty() ? eastl::string("<unnamed>") : eastl::string(mLabelName.c_str()), mResolvedBindings);

        mDescriptorSet = mDevice->allocateDescriptorSet(
            layoutImpl->getNativeDescriptorSetLayout(),
            layoutImpl->getPoolSizesPerSet(),
            mDescriptorPool);
        GVMLogDebug(
            this, BindGroupLogCategory,
            "event=bind_group_init_descriptor_set_allocated label={} descriptor_pool_ptr={} descriptor_set_ptr={}",
            safeLogLabel(mLabelName),
            reinterpret_cast<void *>(static_cast<VkDescriptorPool>(mDescriptorPool)),
                reinterpret_cast<void *>(static_cast<VkDescriptorSet>(mDescriptorSet)));
        const VKDescriptorManager::BindGroupWriteResult defaultWrite =
            VKDescriptorManager::writeBindGroupDescriptorSet(mDevice, mDescriptorSet, mResolvedBindings);

        forEachBindGroupDescriptorResource(mDescriptor, [this](Buffer buffer, VKTexture *texture, Sampler sampler)
        {
            if (!buffer.isNull())
            {
                mDevice->retainBindGroupBufferReference(buffer);
            }
            if (texture != nullptr)
            {
                mDevice->retainBindGroupTextureReference(texture);
            }
            if (!sampler.isNull())
            {
                mDevice->retainBindGroupSamplerReference(sampler);
            }
        });
        mResourceReferencesRegistered = true;
        GVMLogDebug(
            this, BindGroupLogCategory,
            "event=bind_group_init_end label={} descriptor_set_ptr={} resolved_bindings={} writes={}",
            safeLogLabel(mLabelName),
            reinterpret_cast<void *>(static_cast<VkDescriptorSet>(mDescriptorSet)),
            mResolvedBindings.size(),
            defaultWrite.writeCount);
    }
} // namespace GVM::RHI::Vulkan
