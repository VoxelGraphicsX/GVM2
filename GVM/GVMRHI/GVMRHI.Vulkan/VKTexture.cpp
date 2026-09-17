#include "VKTexture.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKTextureView.hpp"
#include "VKTextureViewCacheUtils.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

#include <stdexcept>
#include <EASTL/string.h>

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

    uint32_t VKTexture::getTotalReferenceCount() const
    {
        return mLifetime.ownerReferences.load(std::memory_order_acquire);
    }

    uint32_t VKTexture::getBindGroupReferenceCount() const
    {
        return mLifetime.bindGroupReferences.load(std::memory_order_acquire);
    }

    uint32_t VKTexture::getCommandReferenceCount() const
    {
        return mLifetime.commandReferences.load(std::memory_order_acquire);
    }

    bool VKTexture::isDestroyRequested() const
    {
        return mLifetime.destroyRequested.load(std::memory_order_acquire);
    }

    bool VKTexture::isReadyForDestroy() const
    {
        return isDestroyRequested() && getTotalReferenceCount() == 0u;
    }

    bool VKTexture::markPendingDestroyQueued()
    {
        bool expected = false;
        return mLifetime.pendingDestroyQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void VKTexture::clearPendingDestroyQueued()
    {
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
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

        eastl::string describeTextureLabel(const TextureDescriptor &descriptor)
        {
            return descriptor.label.empty() ? "unnamed texture" : eastl::string(descriptor.label.c_str());
        }

        const char *describeTextureDimension(TextureDimension dimension)
        {
            switch (dimension)
            {
            case TextureDimension::e1D: return "e1D";
            case TextureDimension::e2D: return "e2D";
            case TextureDimension::e3D: return "e3D";
            default: return "Unknown";
            }
        }

        const char *describeTextureViewDimension(TextureViewDimension dimension)
        {
            switch (dimension)
            {
            case TextureViewDimension::Undefined: return "Undefined";
            case TextureViewDimension::e1D: return "e1D";
            case TextureViewDimension::e2D: return "e2D";
            case TextureViewDimension::e2DArray: return "e2DArray";
            case TextureViewDimension::Cube: return "Cube";
            case TextureViewDimension::CubeArray: return "CubeArray";
            case TextureViewDimension::e3D: return "e3D";
            default: return "Unknown";
            }
        }

        eastl::string describeTextureShape(const TextureDescriptor &descriptor)
        {
            return "dimension=" + eastl::string(describeTextureDimension(descriptor.dimension)) +
                ", size=(" + eastl::to_string(descriptor.size.width) + "x" +
                eastl::to_string(descriptor.size.height) + "x" +
                eastl::to_string(descriptor.size.depth) + "), layers=" +
                eastl::to_string(descriptor.arrayLayerCount) + ", mipLevels=" +
                eastl::to_string(descriptor.mipLevelCount);
        }

        eastl::string describeTextureViewShape(const TextureViewDescriptor &descriptor)
        {
            return "viewDimension=" + eastl::string(describeTextureViewDimension(descriptor.dimension)) +
                ", format=" + eastl::to_string(static_cast<uint32_t>(descriptor.format)) +
                ", baseMipLevel=" + eastl::to_string(descriptor.baseMipLevel) +
                ", mipLevelCount=" + eastl::to_string(descriptor.mipLevelCount) +
                ", baseArrayLayer=" + eastl::to_string(descriptor.baseArrayLayer) +
                ", arrayLayerCount=" + eastl::to_string(descriptor.arrayLayerCount);
        }

        void validatePortableTextureDescriptorOrThrow(const TextureDescriptor &descriptor)
        {
            if (descriptor.dimension == TextureDimension::e1D)
            {
                throw makeInvalidArgument(
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' resolved to TextureDimension::e1D. The current UGLC/GVM Vulkan texture contract only accepts portable 2D/2DArray/3D shapes, and GVM does not author 1D textures. This indicates an upstream descriptor/codegen bug. Actual descriptor: " +
                    describeTextureShape(descriptor) + ".");
            }

            if (descriptor.dimension != TextureDimension::e3D && descriptor.size.depth != 1u)
            {
                throw makeInvalidArgument(
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' uses non-3D depth " + eastl::to_string(descriptor.size.depth) +
                    " for a " + describeTextureDimension(descriptor.dimension) +
                    " texture. Actual descriptor: " + describeTextureShape(descriptor) + ".");
            }

            if (descriptor.dimension == TextureDimension::e3D && descriptor.arrayLayerCount != 1u)
            {
                throw makeInvalidArgument(
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' is 3D but requests arrayLayerCount=" + eastl::to_string(descriptor.arrayLayerCount) +
                    ". The current Vulkan backend does not legalize 3D texture arrays. Actual descriptor: " +
                    describeTextureShape(descriptor) + ".");
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
                    throw makeInvalidArgument(
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment without RenderAttachment|PixelLocalAttachment usage.");
                }

                if ((descriptor.usage & persistentTextureAccess) != 0u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment with sampled/storage/copy usage. Transient pixel-local attachments must not be sampled, stored, or copied outside the render pass.");
                }

                if (descriptor.dimension != TextureDimension::e2D || descriptor.mipLevelCount != 1u || descriptor.arrayLayerCount != 1u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                        "' uses TextureStorageMode::TransientAttachment but is not a single-mip single-layer 2D pixel-local attachment. Actual descriptor: " +
                        describeTextureShape(descriptor) + ".");
                }
            }
        }

        void validateResolvedTextureViewDescriptorOrThrow(const TextureDescriptor &textureDescriptor, const TextureViewDescriptor &viewDescriptor)
        {
            if (viewDescriptor.dimension == TextureViewDimension::Undefined)
            {
                throw makeInvalidArgument(
                    "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                    "' resolved to an undefined texture-view dimension. Texture descriptor: " +
                    describeTextureShape(textureDescriptor) + ". View descriptor: " +
                    describeTextureViewShape(viewDescriptor) + ".");
            }

            if (viewDescriptor.dimension == TextureViewDimension::e1D || textureDescriptor.dimension == TextureDimension::e1D)
            {
                throw makeInvalidArgument(
                    "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                    "' resolved to an unexpected 1D texture/view. GVM does not author 1D textures, so this indicates an upstream descriptor/codegen bug. Texture descriptor: " +
                    describeTextureShape(textureDescriptor) + ". View descriptor: " +
                    describeTextureViewShape(viewDescriptor) + ".");
            }

            const uint32_t totalArrayLayerCount = resolveImageArrayLayers(textureDescriptor);
            switch (viewDescriptor.dimension)
            {
            case TextureViewDimension::e2D:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount != 1u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 2D view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".");
                }
                break;
            case TextureViewDimension::e2DArray:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount <= 1u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 2D-array view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".");
                }
                break;
            case TextureViewDimension::Cube:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount != 6u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a cube view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".");
                }
                break;
            case TextureViewDimension::CubeArray:
                if (textureDescriptor.dimension != TextureDimension::e2D ||
                    viewDescriptor.arrayLayerCount < 6u ||
                    (viewDescriptor.arrayLayerCount % 6u) != 0u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a cube-array view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".");
                }
                break;
            case TextureViewDimension::e3D:
                if (textureDescriptor.dimension != TextureDimension::e3D ||
                    viewDescriptor.baseArrayLayer != 0u ||
                    viewDescriptor.arrayLayerCount != 1u)
                {
                    throw makeInvalidArgument(
                        "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 3D view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".");
                }
                break;
            default:
                throw makeInvalidArgument(
                    "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                    "' encountered unsupported view dimension " + eastl::string(describeTextureViewDimension(viewDescriptor.dimension)) +
                    ". Texture descriptor: " + describeTextureShape(textureDescriptor) +
                    ". View descriptor: " + describeTextureViewShape(viewDescriptor) + ".");
            }

            if (viewDescriptor.baseArrayLayer + viewDescriptor.arrayLayerCount > totalArrayLayerCount)
            {
                throw makeInvalidArgument(
                    "VKTexture::createViewInternal texture '" + describeTextureLabel(textureDescriptor) +
                    "' resolved a view whose array-layer span exceeds the parent texture. Texture descriptor: " +
                    describeTextureShape(textureDescriptor) + ". View descriptor: " +
                    describeTextureViewShape(viewDescriptor) + ".");
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

            throw makeRuntimeError(
                "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                "' requires Vulkan optimal-tiling format feature '" + featureName +
                "', but the selected device does not advertise it for format enum " +
                eastl::to_string(static_cast<uint32_t>(descriptor.format)) + ".");
        }

        void validateTextureFormatSupport(VKDevice *device, const TextureDescriptor &descriptor)
        {
            const vk::FormatProperties formatProperties =
                device->getPhysicalDevice().getFormatProperties(translateTextureFormat(descriptor.format));

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
                throw makeRuntimeError(
                    "VKTexture::init texture '" + describeTextureLabel(descriptor) +
                    "' failed to allocate transient pixel-local image memory.");
            }
        }
    } // namespace

    void VKTexture::init(VKDevice *device, const TextureDescriptor &descriptor)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKTexture::init requires a valid device.");
        }
        if (descriptor.size.width == 0 || descriptor.size.height == 0 || descriptor.size.depth == 0)
        {
            throw makeInvalidArgument("VKTexture::init requires non-zero texture dimensions.");
        }
        if (descriptor.mipLevelCount == 0 || descriptor.arrayLayerCount == 0)
        {
            throw makeInvalidArgument("VKTexture::init requires at least one mip level and one array layer.");
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
                throw makeRuntimeError("VKTexture::init failed to allocate image memory.");
            }
            mImage = nativeImage;
        }
        mLifetime.ownerReferences.store(1u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(false, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mDestroyed = false;
    }

    void VKTexture::initExternalImage(VKDevice *device, const TextureDescriptor &descriptor, vk::Image image)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKTexture::initExternalImage requires a valid device.");
        }
        if (!image)
        {
            throw makeInvalidArgument("VKTexture::initExternalImage requires a valid Vulkan image.");
        }
        if (descriptor.size.width == 0 || descriptor.size.height == 0 || descriptor.size.depth == 0)
        {
            throw makeInvalidArgument("VKTexture::initExternalImage requires non-zero texture dimensions.");
        }
        if (descriptor.mipLevelCount == 0 || descriptor.arrayLayerCount == 0)
        {
            throw makeInvalidArgument("VKTexture::initExternalImage requires at least one mip level and one array layer.");
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
        mLifetime.ownerReferences.store(1u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(false, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
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
        const uint32_t totalArrayLayerCount = resolveImageArrayLayers(mDescriptor);
        if (descriptor.baseMipLevel >= mDescriptor.mipLevelCount)
        {
            throw makeOutOfRange("VKTexture::createView received baseMipLevel outside the texture range.");
        }
        if (descriptor.baseArrayLayer >= totalArrayLayerCount)
        {
            throw makeOutOfRange("VKTexture::createView received baseArrayLayer outside the texture range.");
        }

        const uint32_t resolvedMipLevelCount = descriptor.mipLevelCount == 0
            ? (mDescriptor.mipLevelCount - descriptor.baseMipLevel)
            : descriptor.mipLevelCount;
        const uint32_t resolvedArrayLayerCount = descriptor.arrayLayerCount == 0
            ? (totalArrayLayerCount - descriptor.baseArrayLayer)
            : descriptor.arrayLayerCount;

        if (descriptor.baseMipLevel + resolvedMipLevelCount > mDescriptor.mipLevelCount)
        {
            throw makeOutOfRange("VKTexture::createView requested more mip levels than the texture owns.");
        }
        if (descriptor.baseArrayLayer + resolvedArrayLayerCount > totalArrayLayerCount)
        {
            throw makeOutOfRange("VKTexture::createView requested more array layers than the texture owns.");
        }

        TextureViewDescriptor resolvedDescriptor = descriptor;
        resolvedDescriptor.format = descriptor.format == TextureFormat::Undefined ? mDescriptor.format : descriptor.format;
        resolvedDescriptor.dimension = descriptor.dimension == TextureViewDimension::Undefined
            ? GVM::RHI::Private::getTextureViewDimension(mDescriptor.dimension, resolvedArrayLayerCount)
            : descriptor.dimension;
        resolvedDescriptor.mipLevelCount = resolvedMipLevelCount;
        resolvedDescriptor.arrayLayerCount = resolvedArrayLayerCount;
        validateResolvedTextureViewDescriptorOrThrow(mDescriptor, resolvedDescriptor);

        const TextureViewDescriptor defaultDescriptor = buildDefaultTextureViewDescriptor(mDescriptor);
        const uint64_t descriptorHash = CacheDetail::hashTextureViewDescriptor(resolvedDescriptor);

        ScopedLock lock(mViewCacheMutex);
        if (const CachedTextureViewEntry *cachedView = mCachedViews.findMatching(
                descriptorHash,
                [&](const CachedTextureViewEntry &candidate)
                {
                    return CacheDetail::equalTextureViewDescriptor(candidate.descriptor, resolvedDescriptor);
                }))
        {
            return cachedView->handle;
        }

        auto *viewImpl = new VKTextureView();
        try
        {
            viewImpl->init(*mDevice, *this, resolvedDescriptor);
            TextureView handle = mDevice->getTextureViewPool().alloc(viewImpl);
            mOwnedViews.push_back(handle);
            CachedTextureViewEntry cachedView = {};
            cachedView.descriptor = resolvedDescriptor;
            cachedView.handle = handle;
            mCachedViews.insert(descriptorHash, eastl::move(cachedView));
            if (CacheDetail::equalTextureViewDescriptor(resolvedDescriptor, defaultDescriptor))
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
                mDevice->destroyTextureViewHandle(view);
            }
        }
    }

    void VKTexture::retainBindGroupReference()
    {
        incrementAtomicReference(mLifetime.bindGroupReferences);
        incrementAtomicReference(mLifetime.ownerReferences);
    }

    void VKTexture::releaseBindGroupReference()
    {
        decrementAtomicReference(mLifetime.bindGroupReferences, "VKTexture::releaseBindGroupReference");
        decrementAtomicReference(mLifetime.ownerReferences, "VKTexture::releaseBindGroupReference");
    }

    void VKTexture::retainCommandReference()
    {
        incrementAtomicReference(mLifetime.commandReferences);
        incrementAtomicReference(mLifetime.ownerReferences);
    }

    void VKTexture::releaseCommandReference()
    {
        decrementAtomicReference(mLifetime.commandReferences, "VKTexture::releaseCommandReference");
        decrementAtomicReference(mLifetime.ownerReferences, "VKTexture::releaseCommandReference");
    }

    bool VKTexture::requestUserDestroy()
    {
        bool expected = false;
        if (!mLifetime.destroyRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }
        decrementAtomicReference(mLifetime.ownerReferences, "VKTexture::requestUserDestroy");
        return true;
    }

    void VKTexture::setSteadyStateLayout(vk::ImageLayout layout)
    {
        ScopedLock lock(mCachedStateMutex);
        mSteadyStateLayout = layout;
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
        mLifetime.ownerReferences.store(0u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(true, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mOwnsAllocation = true;
        mDestroyed = true;
        mLogContext.reset();
    }

} // namespace GVM::RHI::Vulkan
