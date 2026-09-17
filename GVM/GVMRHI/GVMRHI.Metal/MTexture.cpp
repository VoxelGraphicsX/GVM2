#include "MTexture.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MTextureView.hpp"

#include <GVMRHI/Private/RHIHashXXH64.hpp>

#include <stdexcept>

namespace GVM::RHI::Metal
{
    namespace
    {
        uint32_t resolveImageArrayLayers(const TextureDescriptor &descriptor)
        {
            return descriptor.dimension == TextureDimension::e3D ? 1u : descriptor.arrayLayerCount;
        }

        TextureViewDescriptor buildDefaultTextureViewDescriptor(const TextureDescriptor &descriptor)
        {
            TextureViewDescriptor defaultDescriptor = {};
            defaultDescriptor.format = descriptor.format;
            defaultDescriptor.dimension = Private::getTextureViewDimension(descriptor.dimension, descriptor.arrayLayerCount);
            defaultDescriptor.baseMipLevel = 0u;
            defaultDescriptor.mipLevelCount = descriptor.mipLevelCount;
            defaultDescriptor.baseArrayLayer = 0u;
            defaultDescriptor.arrayLayerCount = resolveImageArrayLayers(descriptor);
            defaultDescriptor.aspect = Private::isDepthFormat(descriptor.format) ? TextureAspect::DepthOnly : TextureAspect::All;
            return defaultDescriptor;
        }

        uint64_t hashTextureViewDescriptor(const TextureViewDescriptor &descriptor)
        {
            GVM::RHI::Detail::XXH64State state;
            state.updateEnum(descriptor.format);
            state.updateEnum(descriptor.dimension);
            state.updatePod(descriptor.baseMipLevel);
            state.updatePod(descriptor.mipLevelCount);
            state.updatePod(descriptor.baseArrayLayer);
            state.updatePod(descriptor.arrayLayerCount);
            state.updatePod(descriptor.aspect);
            return state.digest();
        }

        bool equalTextureViewDescriptor(const TextureViewDescriptor &lhs, const TextureViewDescriptor &rhs)
        {
            return lhs.format == rhs.format &&
                lhs.dimension == rhs.dimension &&
                lhs.baseMipLevel == rhs.baseMipLevel &&
                lhs.mipLevelCount == rhs.mipLevelCount &&
                lhs.baseArrayLayer == rhs.baseArrayLayer &&
                lhs.arrayLayerCount == rhs.arrayLayerCount &&
                lhs.aspect == rhs.aspect;
        }

        /** Returns whether a view descriptor directly aliases the full native texture without reinterpretation or subresource slicing. */
        bool isDefaultTextureViewDescriptor(const TextureDescriptor &textureDescriptor, const TextureViewDescriptor &viewDescriptor)
        {
            return equalTextureViewDescriptor(buildDefaultTextureViewDescriptor(textureDescriptor), viewDescriptor);
        }

        void validateTextureStorageMode(MDevice *device, const TextureDescriptor &descriptor)
        {
            if (descriptor.storageMode != TextureStorageMode::TransientAttachment)
            {
                return;
            }

            if (device == nullptr)
            {
                throw std::invalid_argument("MTexture::init requires a Metal device for TextureStorageMode::TransientAttachment pixel-local textures.");
            }
            constexpr TextureUsageFlags persistentTextureAccess =
                TextureUsage::TextureBinding |
                TextureUsage::StorageBinding |
                TextureUsage::CopySrc |
                TextureUsage::CopyDst;

            if ((descriptor.usage & TextureUsage::RenderAttachment) == 0u ||
                (descriptor.usage & TextureUsage::PixelLocalAttachment) == 0u)
            {
                throw std::invalid_argument("MTexture::init requires TextureStorageMode::TransientAttachment textures to use RenderAttachment|PixelLocalAttachment.");
            }

            if ((descriptor.usage & persistentTextureAccess) != 0u)
            {
                throw std::invalid_argument("MTexture::init does not allow sampled/storage/copy usage on TextureStorageMode::TransientAttachment pixel-local textures.");
            }

            if (descriptor.dimension != TextureDimension::e2D || descriptor.mipLevelCount != 1u || descriptor.arrayLayerCount != 1u)
            {
                throw std::invalid_argument("MTexture::init requires TextureStorageMode::TransientAttachment textures to be single-mip single-layer 2D attachments.");
            }
        }

        MTL::StorageMode translateTextureStorageModeToMTL(TextureStorageMode storageMode)
        {
            switch (storageMode)
            {
            case TextureStorageMode::Persistent: return MTL::StorageMode::StorageModePrivate;
            case TextureStorageMode::TransientAttachment: return MTL::StorageMode::StorageModeMemoryless;
            default: throw std::invalid_argument("MTexture::init received an unsupported TextureStorageMode.");
            }
        }
    } // namespace

    MTexture::MTexture()
    {
    }

    void MTexture::init(MDevice *device, const TextureDescriptor &descriptor)
    {
        validateTextureStorageMode(device, descriptor);

        this->mDevice = device;
        this->mOwnsNativeTexture = true;
        auto textureDescriptor = MTL::TextureDescriptor::alloc()->init();
        textureDescriptor->autorelease();
        textureDescriptor->setTextureType(translateTextureDimensionToMTL(descriptor.dimension, descriptor.arrayLayerCount));
        textureDescriptor->setPixelFormat(translateTextureFormatToMTL(descriptor.format));
        textureDescriptor->setWidth(descriptor.size.width);
        textureDescriptor->setHeight(descriptor.size.height);
        textureDescriptor->setDepth(descriptor.size.depth);
        textureDescriptor->setArrayLength(descriptor.arrayLayerCount);
        textureDescriptor->setMipmapLevelCount(descriptor.mipLevelCount);
        // textureDescriptor->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
        //  textureDescriptor->setSampleCount(descriptor.sampleCount);
        textureDescriptor->setStorageMode(translateTextureStorageModeToMTL(descriptor.storageMode));
        textureDescriptor->setUsage(translateTextureUsageToMTL(descriptor.usage));
        this->mNativeTexture = device->getNativeDevice()->newTexture(textureDescriptor);
        if (this->mNativeTexture == nullptr)
        {
            if (descriptor.storageMode == TextureStorageMode::TransientAttachment)
            {
                throw std::runtime_error("MTexture::init failed to create a transient pixel-local attachment texture. Check Metal memoryless attachment support, format, and usage flags.");
            }
            throw std::runtime_error("MTexture::init failed to create Metal texture.");
        }
        this->mNativeTexture->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));

        this->mLabelName = descriptor.label;
        this->mDescriptor = descriptor;
        this->mTextureViews.clear();
        this->mCachedViews.clear();

        const TextureViewDescriptor defaultDescriptor = buildDefaultTextureViewDescriptor(mDescriptor);
        MTL::Texture *defaultNativeTextureView = nullptr;
        if (mDescriptor.storageMode == TextureStorageMode::TransientAttachment)
        {
            defaultNativeTextureView = this->mNativeTexture;
            defaultNativeTextureView->retain();
        }
        else
        {
            defaultNativeTextureView = this->mNativeTexture->newTextureView(
                translateTextureFormatToMTL(defaultDescriptor.format),
                translateTextureViewDimensionToMTL(defaultDescriptor.dimension),
                NS::Range(defaultDescriptor.baseMipLevel, defaultDescriptor.mipLevelCount),
                NS::Range(defaultDescriptor.baseArrayLayer, defaultDescriptor.arrayLayerCount));
        }
        if (defaultNativeTextureView == nullptr)
        {
            throw std::runtime_error("MTexture::init failed to create the default Metal texture view.");
        }

        this->mDefaultView = new MTextureView();
        this->mDefaultView->init(this, defaultNativeTextureView, this->mLabelName);
        this->mDefaultViewHandle = this->mDevice->mTextureViewPool.alloc(mDefaultView);
        this->mTextureViews.push_back(this->mDefaultViewHandle);
        this->mCachedViews.push_back({
            .hash = hashTextureViewDescriptor(defaultDescriptor),
            .descriptor = defaultDescriptor,
            .handle = this->mDefaultViewHandle,
        });

        initTextureCopyData();
        // textureDescriptor->release();
    }

    void MTexture::initWithNativeTexture(MDevice *device, MTL::Texture *nativeTexture, const eastl::string &labelName)
    {
        this->mDevice = device;
        this->mNativeTexture = nativeTexture;
        this->mLabelName = labelName;
        this->mOwnsNativeTexture = false;

        GVM::RHI::TextureDescriptor descriptor = {};
        descriptor.size.width = nativeTexture->width();
        descriptor.size.height = nativeTexture->height();
        descriptor.size.depth = nativeTexture->depth();
        descriptor.arrayLayerCount = nativeTexture->arrayLength();
        descriptor.mipLevelCount = nativeTexture->mipmapLevelCount();
        descriptor.format = translateMTLTextureFormatToGVM(nativeTexture->pixelFormat());
        // descriptor.usage = translateTextureUsageToGVM(nativeTexture->usage());
        this->mDescriptor = descriptor;
        this->mTextureViews.clear();
        this->mCachedViews.clear();

        const TextureViewDescriptor defaultDescriptor = buildDefaultTextureViewDescriptor(mDescriptor);
        auto *defaultNativeTextureView = this->mNativeTexture->newTextureView(
            translateTextureFormatToMTL(defaultDescriptor.format),
            translateTextureViewDimensionToMTL(defaultDescriptor.dimension),
            NS::Range(defaultDescriptor.baseMipLevel, defaultDescriptor.mipLevelCount),
            NS::Range(defaultDescriptor.baseArrayLayer, defaultDescriptor.arrayLayerCount));
        if (defaultNativeTextureView == nullptr)
        {
            throw std::runtime_error("MTexture::initWithNativeTexture failed to create the default Metal texture view.");
        }

        this->mDefaultView = new MTextureView();
        this->mDefaultView->init(this, defaultNativeTextureView, this->mLabelName + "__DefaultView__");
        this->mDefaultViewHandle = this->mDevice->mTextureViewPool.alloc(mDefaultView);
        this->mTextureViews.push_back(this->mDefaultViewHandle);
        this->mCachedViews.push_back({
            .hash = hashTextureViewDescriptor(defaultDescriptor),
            .descriptor = defaultDescriptor,
            .handle = this->mDefaultViewHandle,
        });

        initTextureCopyData();
    }

    MTL::Texture *MTexture::getNativeTexture() const
    {
        return this->mNativeTexture;
    }

    TextureView MTexture::createView(const TextureViewDescriptor &descriptor)
    {
        const uint32_t totalArrayLayerCount = resolveImageArrayLayers(mDescriptor);
        if (descriptor.baseMipLevel >= mDescriptor.mipLevelCount)
        {
            throw std::out_of_range("MTexture::createView received baseMipLevel outside the texture range.");
        }
        if (descriptor.baseArrayLayer >= totalArrayLayerCount)
        {
            throw std::out_of_range("MTexture::createView received baseArrayLayer outside the texture range.");
        }

        const uint32_t resolvedMipLevelCount = descriptor.mipLevelCount == 0
            ? (mDescriptor.mipLevelCount - descriptor.baseMipLevel)
            : descriptor.mipLevelCount;
        const uint32_t resolvedArrayLayerCount = descriptor.arrayLayerCount == 0
            ? (totalArrayLayerCount - descriptor.baseArrayLayer)
            : descriptor.arrayLayerCount;

        if (descriptor.baseMipLevel + resolvedMipLevelCount > mDescriptor.mipLevelCount)
        {
            throw std::out_of_range("MTexture::createView requested more mip levels than the texture owns.");
        }
        if (descriptor.baseArrayLayer + resolvedArrayLayerCount > totalArrayLayerCount)
        {
            throw std::out_of_range("MTexture::createView requested more array layers than the texture owns.");
        }

        TextureViewDescriptor trueDescriptor = descriptor;
        trueDescriptor.format = descriptor.format == TextureFormat::Undefined ? mDescriptor.format : descriptor.format;
        trueDescriptor.dimension = descriptor.dimension == TextureViewDimension::Undefined
            ? Private::getTextureViewDimension(mDescriptor.dimension, resolvedArrayLayerCount)
            : descriptor.dimension;
        trueDescriptor.mipLevelCount = resolvedMipLevelCount;
        trueDescriptor.arrayLayerCount = resolvedArrayLayerCount;
        trueDescriptor.aspect = Private::isDepthFormat(trueDescriptor.format) ? TextureAspect::DepthOnly : TextureAspect::All;
        const uint64_t descriptorHash = hashTextureViewDescriptor(trueDescriptor);

        std::scoped_lock lock(mViewCacheMutex);
        for (const CachedTextureViewEntry &cachedView : mCachedViews)
        {
            if (cachedView.hash == descriptorHash &&
                equalTextureViewDescriptor(cachedView.descriptor, trueDescriptor))
            {
                return cachedView.handle;
            }
        }

        if (mDescriptor.storageMode == TextureStorageMode::TransientAttachment)
        {
            if (isDefaultTextureViewDescriptor(mDescriptor, trueDescriptor))
            {
                return mDefaultViewHandle;
            }
            throw std::runtime_error("MTexture::createView does not support reinterpretation or subresource views for Metal memoryless pixel-local attachments.");
        }

        auto nativeTextureView = this->mNativeTexture->newTextureView(
            translateTextureFormatToMTL(trueDescriptor.format),
            translateTextureViewDimensionToMTL(trueDescriptor.dimension),
            NS::Range(trueDescriptor.baseMipLevel, trueDescriptor.mipLevelCount),
            NS::Range(trueDescriptor.baseArrayLayer, trueDescriptor.arrayLayerCount));
        if (nativeTextureView == nullptr)
        {
            throw std::runtime_error("MTexture::createView failed to create a Metal texture view.");
        }
        MTextureView *textureView = new MTextureView();
        textureView->init(this, nativeTextureView, trueDescriptor.label);
        auto textureViewHandle = this->mDevice->mTextureViewPool.alloc(textureView);
        mTextureViews.emplace_back(textureViewHandle);
        mCachedViews.push_back({
            .hash = descriptorHash,
            .descriptor = trueDescriptor,
            .handle = textureViewHandle,
        });
        return textureViewHandle;
    }

    TextureView MTexture::createView()
    {
        return mDefaultViewHandle;
    }

    uint32_t MTexture::getWidth() const
    {
        return this->mDescriptor.size.width;
    }

    uint32_t MTexture::getHeight() const
    {
        return this->mDescriptor.size.height;
    }

    uint32_t MTexture::getDepth() const
    {
        return this->mDescriptor.size.depth;
    }

    uint32_t MTexture::getMipLevelCount() const
    {
        return this->mDescriptor.mipLevelCount;
    }

    uint32_t MTexture::getArrayLayerCount() const
    {
        return this->mDescriptor.arrayLayerCount;
    }

    TextureFormat MTexture::getFormat() const
    {
        return this->mDescriptor.format;
    }

    TextureUsageFlags MTexture::getUsage() const
    {
        return this->mDescriptor.usage;
    }

    uint64_t MTexture::getBytesPerRow() const
    {
        return mBytesPerRow;
    }

    uint64_t MTexture::getBytesPerImage() const
    {
        return mBytesPerImage;
    }

    void MTexture::destroy()
    {
        eastl::vector<TextureView> textureViews;
        {
            std::scoped_lock lock(mViewCacheMutex);
            textureViews = eastl::move(mTextureViews);
            mCachedViews.clear();
            this->mDefaultViewHandle.reset();
        }
        this->mDefaultView = nullptr;
        for (auto &view : textureViews)
        {
            if (view.isNull())
            {
                continue;
            }
            view->destroy();
            if (this->mDevice != nullptr)
            {
                this->mDevice->mTextureViewPool.freeByHandle(view);
            }
        }
        if (mNativeTexture)
        {
            if (mOwnsNativeTexture)
            {
                mNativeTexture->release();
            }
            mNativeTexture = nullptr;
        }
        mOwnsNativeTexture = false;
        mDevice = nullptr;
    }

    void MTexture::initTextureCopyData()
    {
        uint64_t blocksPerRow = (getWidth() + (Private::getTextureBlockWidth(getFormat()) - 1)) / Private::getTextureBlockWidth(getFormat());
        uint64_t bytesPerBlock = Private::getTextureBytesPerBlock(getFormat());
        this->mBytesPerRow = blocksPerRow * bytesPerBlock;
        this->mBytesPerImage = this->mBytesPerRow * ((getHeight() + (Private::getTextureBlockHeight(getFormat()) - 1)) / Private::getTextureBlockHeight(getFormat()));
    }

} // namespace GVM::RHI::Metal
