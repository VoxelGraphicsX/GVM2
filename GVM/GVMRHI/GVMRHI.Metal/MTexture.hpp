#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/TextureViewResolution.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"

#include <mutex>

namespace GVM::RHI::Metal
{

	class MTexture final : public TextureImpl
	{
	public:
		MTexture();
		void init(MDevice *device, const TextureDescriptor &descriptor);
		void initWithNativeTexture(MDevice *device, MTL::Texture *nativeTexture, TextureUsageFlags usage, const eastl::string &labelName);
		MTL::Texture *getNativeTexture() const;
		/** Returns the device that owns this immutable texture allocation. */
		MDevice &getDevice() const { return *mDevice; }
		virtual TextureView createView(const TextureViewDescriptor &descriptor) override;
		virtual TextureView createView() override;
		[[nodiscard]]
		virtual uint32_t getWidth() const override;
		[[nodiscard]]
		virtual uint32_t getHeight() const override;
		[[nodiscard]]
		virtual uint32_t getDepth() const override;
		[[nodiscard]]
		virtual uint32_t getMipLevelCount() const override;
		[[nodiscard]]
		virtual uint32_t getArrayLayerCount() const override;
		[[nodiscard]]
		virtual TextureFormat getFormat() const override;
		[[nodiscard]]
		TextureUsageFlags getUsage() const;

		[[nodiscard]]
		uint64_t getBytesPerRow() const;
		[[nodiscard]]
		uint64_t getBytesPerImage() const;

		virtual void destroy() override;

	private:
		MDevice *mDevice = nullptr;
		MTL::Texture *mNativeTexture = nullptr;
		MTextureView *mDefaultView = nullptr;
		TextureView mDefaultViewHandle;
		std::mutex mViewCacheMutex;
		eastl::vector<TextureView> mTextureViews;
		Private::TextureViewCache mCachedViews;
		TextureDescriptor mDescriptor;
		bool mOwnsNativeTexture = false;

		uint64_t mBytesPerRow = 0;
		uint64_t mBytesPerImage = 0;

		void initTextureCopyData();
	};

} // namespace GVM::RHI::Metal
