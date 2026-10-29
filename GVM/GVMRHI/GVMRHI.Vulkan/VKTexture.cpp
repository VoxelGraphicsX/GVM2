#include "VKTexture.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKTextureView.hpp"
#include <GVMRHI/Private/GEnumUtils.hpp>

#include <stdexcept>
#include <EASTL/string.h>
#include <EASTL/atomic.h>

namespace GVM::RHI::Vulkan
{
    vk::Image VKTexture::getNativeImage() const
    {
        return mImage;
    }

    TextureUsageFlags VKTexture::getUsage() const
    {
        return mDescriptor.usage;
    }

    TextureDimension VKTexture::getDimension() const
    {
        return mDescriptor.dimension;
    }

    vk::ImageLayout VKTexture::getSteadyStateLayout() const
    {
        ScopedLock lock(mCachedStateMutex);
        return mSteadyStateLayout;
    }

    bool VKTexture::hasSteadyStateLayout() const
    {
        ScopedLock lock(mCachedStateMutex);
        return mSteadyStateLayout != vk::ImageLayout::eUndefined;
    }

    vk::ImageSubresourceRange VKTexture::buildWholeTextureRange(TextureAspectFlags aspectMask) const
    {
        return buildSubresourceRange(
            aspectMask,
            0u,
            mDescriptor.mipLevelCount,
            0u,
            getCachedArrayLayerCount());
    }

    uint32_t VKTexture::getCachedArrayLayerCount() const
    {
        return mCachedArrayLayerCount;
    }

    bool VKTexture::is3D() const
    {
        return mDescriptor.dimension == TextureDimension::e3D;
    }

    const TextureDescriptor &VKTexture::getDescriptor() const
    {
        return mDescriptor;
    }

    namespace
    {
        constexpr eastl::string_view TextureLogCategory = "gvmrhi.vulkan.texture";

        vk::ImageLayout resolveSteadyStateLayout(TextureUsageFlags usage, TextureFormat format)
        {
            const bool hasTextureBinding = (usage & TextureUsage::TextureBinding) != 0u;
            const bool hasStorageBinding = (usage & TextureUsage::StorageBinding) != 0u;

            if ((usage & TextureUsage::RenderAttachment) != 0u)
            {
                return isDepthStencilFormat(format)
                    ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                    : vk::ImageLayout::eColorAttachmentOptimal;
            }
            if (hasTextureBinding && hasStorageBinding)
            {
                // Mixed sampled+storage textures routinely ping-pong between
                // GENERAL and SHADER_READ_ONLY_OPTIMAL during steady-state
                // screen-space workloads. Leaving them in their last known
                // layout avoids paying a backend-invented restore at encoder
                // finalization; later passes still issue explicit subresource
                // transitions based on actual usage.
                return vk::ImageLayout::eUndefined;
            }
            if (hasTextureBinding)
            {
                return vk::ImageLayout::eShaderReadOnlyOptimal;
            }
            if (hasStorageBinding)
            {
                // Storage-only textures do not have a portable, stage-agnostic idle
                // layout. Keep their last known usage state instead of forcing an
                // imprecise GENERAL restore at command-buffer finalization.
                return vk::ImageLayout::eUndefined;
            }
            if ((usage & TextureUsage::CopySrc) != 0u)
            {
                return vk::ImageLayout::eTransferSrcOptimal;
            }
            if ((usage & TextureUsage::CopyDst) != 0u)
            {
                return vk::ImageLayout::eTransferDstOptimal;
            }
            return vk::ImageLayout::eUndefined;
        }

        uint32_t resolveImageArrayLayers(const TextureDescriptor &descriptor)
        {
            return descriptor.dimension == TextureDimension::e3D ? 1u : descriptor.arrayLayerCount;
        }

        TextureViewDescriptor buildDefaultTextureViewDescriptor(const TextureDescriptor &descriptor)
        {
            TextureViewDescriptor defaultDescriptor = {};
            defaultDescriptor.format = descriptor.format;
            defaultDescriptor.dimension = GVM::RHI::Private::getTextureViewDimension(descriptor.dimension, descriptor.arrayLayerCount);
            defaultDescriptor.baseMipLevel = 0u;
            defaultDescriptor.mipLevelCount = descriptor.mipLevelCount;
            defaultDescriptor.baseArrayLayer = 0u;
            defaultDescriptor.arrayLayerCount = resolveImageArrayLayers(descriptor);
            defaultDescriptor.aspect = TextureAspect::All;
            return defaultDescriptor;
        }

        using Private::describeTextureDimension;
        using Private::describeTextureLabel;
        using Private::describeTextureShape;

        void validatePortableTextureDescriptorOrThrow(const TextureDescriptor &descriptor)
        {
            if (descriptor.dimension == TextureDimension::e1D)
            {
                throw std::invalid_argument((
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' resolved to TextureDimension::e1D. The current UGLC/GVM Vulkan texture contract only accepts portable 2D/2DArray/3D shapes, and GVM does not author 1D textures. This indicates an upstream descriptor/codegen bug. Actual descriptor: " +
                    describeTextureShape(descriptor) + ".").c_str());
            }

            if (descriptor.dimension != TextureDimension::e3D && descriptor.size.depth != 1u)
            {
                throw std::invalid_argument((
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' uses non-3D depth " + eastl::to_string(descriptor.size.depth) +
                    " for a " + describeTextureDimension(descriptor.dimension) +
                    " texture. Actual descriptor: " + describeTextureShape(descriptor) + ".").c_str());
            }

            if (descriptor.dimension == TextureDimension::e3D && descriptor.arrayLayerCount != 1u)
            {
                throw std::invalid_argument((
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' is 3D but requests arrayLayerCount=" + eastl::to_string(descriptor.arrayLayerCount) +
                    ". The current Vulkan backend does not legalize 3D texture arrays. Actual descriptor: " +
                    describeTextureShape(descriptor) + ".").c_str());
            }

            if (descriptor.storageMode == TextureStorageMode::TransientAttachment)
            {
                constexpr TextureUsageFlags persistentTextureAccess =
                    TextureUsage::TextureBinding |
                    TextureUsage::StorageBinding |
                    TextureUsage::CopySrc |
                    TextureUsage::CopyDst;

                if ((descriptor.usage & TextureUsage::RenderAttachment) == 0u ||
                    (descriptor.usage & TextureUsage::PixelLocalAttachment) == 0u)
                {
                    throw std::invalid_argument((
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment without RenderAttachment|PixelLocalAttachment usage.").c_str());
                }

                if ((descriptor.usage & persistentTextureAccess) != 0u)
                {
                    throw std::invalid_argument((
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment with sampled/storage/copy usage. Transient pixel-local attachments must not be sampled, stored, or copied outside the render pass.").c_str());
                }

                if (descriptor.dimension != TextureDimension::e2D || descriptor.mipLevelCount != 1u || descriptor.arrayLayerCount != 1u)
                {
                    throw std::invalid_argument((
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment but is not a single-mip single-layer 2D pixel-local attachment. Actual descriptor: " +
                        describeTextureShape(descriptor) + ".").c_str());
                }
            }
        }

        void requireOptimalTilingFeature(
            const vk::FormatProperties &formatProperties,
            vk::FormatFeatureFlags requiredFeatures,
            const char *featureName,
            const TextureDescriptor &descriptor)
        {
            if ((formatProperties.optimalTilingFeatures & requiredFeatures) == requiredFeatures)
            {
                return;
            }

            throw std::runtime_error((
                "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                "' requires Vulkan optimal-tiling format feature '" + featureName +
                "', but the selected device does not advertise it for format enum " +
                eastl::to_string(static_cast<uint32_t>(descriptor.format)) + ".").c_str());
        }

        void validateTextureFormatSupport(VKDevice *device, const TextureDescriptor &descriptor)
        {
            const vk::FormatProperties &formatProperties = device->getCapabilities().getTextureFormatProperties(descriptor.format);

            if ((descriptor.usage & TextureUsage::TextureBinding) != 0u)
            {
                requireOptimalTilingFeature(
                    formatProperties,
                    vk::FormatFeatureFlagBits::eSampledImage,
                    "sampled image",
                    descriptor);
            }
            if ((descriptor.usage & TextureUsage::StorageBinding) != 0u)
            {
                requireOptimalTilingFeature(
                    formatProperties,
                    vk::FormatFeatureFlagBits::eStorageImage,
                    "storage image",
                    descriptor);
            }
            if ((descriptor.usage & TextureUsage::CopySrc) != 0u)
            {
                requireOptimalTilingFeature(
                    formatProperties,
                    vk::FormatFeatureFlagBits::eTransferSrc,
                    "transfer source",
                    descriptor);
            }
            if ((descriptor.usage & TextureUsage::CopyDst) != 0u)
            {
                requireOptimalTilingFeature(
                    formatProperties,
                    vk::FormatFeatureFlagBits::eTransferDst,
                    "transfer destination",
                    descriptor);
            }
            if ((descriptor.usage & TextureUsage::RenderAttachment) != 0u)
            {
                const bool depthStencil = isDepthStencilFormat(descriptor.format);
                requireOptimalTilingFeature(
                    formatProperties,
                    depthStencil
                        ? vk::FormatFeatureFlags(vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                        : vk::FormatFeatureFlags(vk::FormatFeatureFlagBits::eColorAttachment),
                    depthStencil ? "depth-stencil attachment" : "color attachment",
                    descriptor);
            }
        }

        /// Creates a Vulkan transient pixel-local image using device-local memory while preferring lazily allocated memory when the driver exposes it for the exact image usage.
        void createTransientAttachmentImage(VKDevice &device, const TextureDescriptor &descriptor, const VkImageCreateInfo &imageCreateInfo, vk::Image &outImage, VmaAllocation &outAllocation, VmaAllocationInfo &outAllocationInfo)
        {
            VmaAllocationCreateInfo allocationCreateInfo = {};
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

            const VkResult createResult = vmaCreateImage(
                device.getAllocator(),
                &imageCreateInfo,
                &allocationCreateInfo,
                reinterpret_cast<VkImage *>(&outImage),
                &outAllocation,
                &outAllocationInfo);
            if (createResult != VK_SUCCESS)
            {
                throw std::runtime_error((
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' failed to allocate transient pixel-local image memory.").c_str());
            }
        }
    } // namespace

    void VKTexture::init(VKDevice *device, const TextureDescriptor &descriptor)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("VKTexture::init requires a valid device.");
        }
        if (descriptor.size.width == 0 || descriptor.size.height == 0 || descriptor.size.depth == 0)
        {
            throw std::invalid_argument("VKTexture::init requires non-zero texture dimensions.");
        }
        if (descriptor.mipLevelCount == 0 || descriptor.arrayLayerCount == 0)
        {
            throw std::invalid_argument("VKTexture::init requires at least one mip level and one array layer.");
        }
        validatePortableTextureDescriptorOrThrow(descriptor);
        validateTextureFormatSupport(device, descriptor);

        mDevice = device;
        mLogContext = device->getLogContext();
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mSteadyStateLayout = resolveSteadyStateLayout(descriptor.usage, descriptor.format);
        mCachedArrayLayerCount = resolveImageArrayLayers(descriptor);
        mDevice->getPresentManager().unregisterDirectPresentTexture(this);
        mOwnsAllocation = true;

        vk::ImageCreateFlags createFlags{};
        if (descriptor.dimension == TextureDimension::e2D && descriptor.arrayLayerCount >= 6 && (descriptor.arrayLayerCount % 6u) == 0u)
        {
            createFlags |= vk::ImageCreateFlagBits::eCubeCompatible;
        }

        vk::ImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.flags = createFlags;
        imageCreateInfo.imageType = translateTextureDimension(descriptor.dimension);
        imageCreateInfo.format = translateTextureFormat(descriptor.format);
        imageCreateInfo.extent = vk::Extent3D{
            descriptor.size.width,
            descriptor.dimension == TextureDimension::e1D ? 1u : descriptor.size.height,
            descriptor.dimension == TextureDimension::e3D ? descriptor.size.depth : 1u};
        imageCreateInfo.mipLevels = descriptor.mipLevelCount;
        imageCreateInfo.arrayLayers = resolveImageArrayLayers(descriptor);
        imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
        imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
        imageCreateInfo.usage = translateTextureUsage(descriptor.usage, descriptor.format);
        if (descriptor.storageMode == TextureStorageMode::TransientAttachment)
        {
            imageCreateInfo.usage |= vk::ImageUsageFlagBits::eTransientAttachment;
        }
        imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
        imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;

        const VkImageCreateInfo nativeImageCreateInfo = static_cast<VkImageCreateInfo>(imageCreateInfo);
        VkImage nativeImage = VK_NULL_HANDLE;
        if (descriptor.storageMode == TextureStorageMode::TransientAttachment)
        {
            createTransientAttachmentImage(*mDevice, descriptor, nativeImageCreateInfo, mImage, mAllocation, mAllocationInfo);
        }
        else
        {
            VmaAllocationCreateInfo allocationCreateInfo = {};
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VkResult createResult = vmaCreateImage(
                mDevice->getAllocator(),
                &nativeImageCreateInfo,
                &allocationCreateInfo,
                &nativeImage,
                &mAllocation,
                &mAllocationInfo);
            if (createResult != VK_SUCCESS)
            {
                throw std::runtime_error("VKTexture::init failed to allocate image memory.");
            }
            mImage = nativeImage;
            mDevice->setDebugObjectName(vk::ObjectType::eImage, reinterpret_cast<uint64_t>(static_cast<VkImage>(nativeImage)), mLabelName.c_str());
        }
        mDestroyed = false;
    }

    void VKTexture::initExternalImage(VKDevice *device, const TextureDescriptor &descriptor, vk::Image image)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("VKTexture::initExternalImage requires a valid device.");
        }
        if (!image)
        {
            throw std::invalid_argument("VKTexture::initExternalImage requires a valid Vulkan image.");
        }
        if (descriptor.size.width == 0 || descriptor.size.height == 0 || descriptor.size.depth == 0)
        {
            throw std::invalid_argument("VKTexture::initExternalImage requires non-zero texture dimensions.");
        }
        if (descriptor.mipLevelCount == 0 || descriptor.arrayLayerCount == 0)
        {
            throw std::invalid_argument("VKTexture::initExternalImage requires at least one mip level and one array layer.");
        }

        validatePortableTextureDescriptorOrThrow(descriptor);

        mDevice = device;
        mLogContext = device->getLogContext();
        mDescriptor = descriptor;
        mLabelName = descriptor.label;
        mImage = image;
        mAllocation = nullptr;
        mAllocationInfo = {};
        mDedicatedMemory = nullptr;
        mSteadyStateLayout = resolveSteadyStateLayout(descriptor.usage, descriptor.format);
        mCachedArrayLayerCount = resolveImageArrayLayers(descriptor);
        mDevice->getPresentManager().unregisterDirectPresentTexture(this);
        mOwnsAllocation = false;
        mDestroyed = false;
    }

    const eastl::shared_ptr<Internal::LogContext> &VKTexture::getLogContext() const
    {
        return mLogContext;
    }

    TextureView VKTexture::createView(const TextureViewDescriptor &descriptor)
    {
        return createViewInternal(descriptor);
    }

    TextureView VKTexture::createView()
    {
        {
            ScopedLock lock(mViewCacheMutex);
            if (!mDefaultViewHandle.isNull())
            {
                return mDefaultViewHandle;
            }
        }
        return createViewInternal(buildDefaultTextureViewDescriptor(mDescriptor));
    }

    TextureView VKTexture::createViewInternal(const TextureViewDescriptor &descriptor)
    {
        const TextureViewDescriptor resolvedDescriptor = Private::resolveTextureViewDescriptor(mDescriptor, descriptor);
        const TextureViewDescriptor defaultDescriptor = buildDefaultTextureViewDescriptor(mDescriptor);

        ScopedLock lock(mViewCacheMutex);
        if (const TextureView cachedView = mCachedViews.find(resolvedDescriptor); !cachedView.isNull())
        {
            return cachedView;
        }

        auto *viewImpl = new VKTextureView();
        try
        {
            viewImpl->init(*mDevice, *this, resolvedDescriptor);
            TextureView handle = mDevice->getResourceRegistry().getTextureViewPool().alloc(viewImpl);
            mOwnedViews.push_back(handle);
            mCachedViews.insert(resolvedDescriptor, handle);
            if (Private::equalTextureViewDescriptors(resolvedDescriptor, defaultDescriptor))
            {
                mDefaultViewHandle = handle;
            }
            return handle;
        }
        catch (...)
        {
            delete viewImpl;
            throw;
        }
    }

    uint32_t VKTexture::getWidth() const
    {
        return mDescriptor.size.width;
    }

    uint32_t VKTexture::getHeight() const
    {
        return mDescriptor.size.height;
    }

    uint32_t VKTexture::getDepth() const
    {
        return mDescriptor.size.depth;
    }

    uint32_t VKTexture::getMipLevelCount() const
    {
        return mDescriptor.mipLevelCount;
    }

    uint32_t VKTexture::getArrayLayerCount() const
    {
        return mDescriptor.arrayLayerCount;
    }

    TextureFormat VKTexture::getFormat() const
    {
        return mDescriptor.format;
    }

    vk::ImageSubresourceRange VKTexture::buildSubresourceRange(
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount) const
    {
        vk::ImageSubresourceRange range = {};
        range.aspectMask = resolveTextureAspect(mDescriptor.format, aspectMask);
        range.baseMipLevel = baseMipLevel;
        range.levelCount = mipLevelCount;
        range.baseArrayLayer = is3D() ? 0u : baseArrayLayer;
        range.layerCount = is3D() ? 1u : arrayLayerCount;
        return range;
    }

    bool VKTexture::isInSteadyStateLayout() const
    {
        const vk::ImageLayout steadyStateLayout = getSteadyStateLayout();
        if (steadyStateLayout == vk::ImageLayout::eUndefined)
        {
            return true;
        }

        if (mDevice == nullptr)
        {
            return false;
        }

        VKQueue *queue = mDevice->getMainQueueImpl();
        if (queue == nullptr)
        {
            return false;
        }

        return queue->getResourceStateDB().isTextureInSteadyStateLayout(*this);
    }

    void VKTexture::destroyOwnedViews()
    {
        eastl::vector<TextureView> ownedViews;
        {
            ScopedLock lock(mViewCacheMutex);
            ownedViews = eastl::move(mOwnedViews);
            mCachedViews.clear();
            mDefaultViewHandle.reset();
        }

        for (TextureView view : ownedViews)
        {
            if (!view.isNull())
            {
                mDevice->getResourceRegistry().destroyTextureViewHandle(view);
            }
        }
    }

    void VKTexture::setSteadyStateLayout(vk::ImageLayout layout)
    {
        ScopedLock lock(mCachedStateMutex);
        mSteadyStateLayout = layout;
    }

    void VKTexture::setHandle(Texture handle)
    {
        mHandle = handle;
    }

    Texture VKTexture::getHandle() const
    {
        return mHandle;
    }

    void VKTexture::retainReference()
    {
        mLifetime.retain();
    }

    void VKTexture::releaseReference()
    {
        if (mLifetime.release("VKTexture::releaseReference"))
        {
            mDevice->getResourceRegistry().enqueueRetiredTexture(mHandle);
        }
    }

    VKUserReleaseResult VKTexture::releaseUserReference()
    {
        return mLifetime.releaseUser();
    }

    bool VKTexture::isDestroyRequested() const
    {
        return mLifetime.isUserReleased();
    }

    bool VKTexture::isDestroyable() const
    {
        return mLifetime.isDestroyable();
    }

    uint32_t VKTexture::getReferenceCount() const
    {
        return mLifetime.getReferenceCount();
    }

    void VKTexture::destroy()
    {
        if (mDestroyed)
        {
            return;
        }

        destroyOwnedViews();
        if (mDevice != nullptr)
        {
            mDevice->getPresentManager().unregisterDirectPresentTexture(this);
        }
        if (mOwnsAllocation && mImage && mAllocation != nullptr)
        {
            vmaDestroyImage(mDevice->getAllocator(), static_cast<VkImage>(mImage), mAllocation);
        }
        else if (mOwnsAllocation && mImage)
        {
            mDevice->getNativeDevice().destroyImage(mImage);
            if (mDedicatedMemory)
            {
                mDevice->getNativeDevice().freeMemory(mDedicatedMemory);
            }
        }

        mImage = nullptr;
        mAllocation = nullptr;
        mAllocationInfo = {};
        mDedicatedMemory = nullptr;
        mOwnsAllocation = true;
        mDestroyed = true;
        mLogContext.reset();
    }

} // namespace GVM::RHI::Vulkan
