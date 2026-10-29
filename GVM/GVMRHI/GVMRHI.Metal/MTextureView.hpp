#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MTextureView final : public TextureViewImpl
	{
	public:
		MTextureView();
		/** Takes ownership of a retained native view and caches its effective format, dimension, and resource identifier. */
		void init(MTexture &parentTexture, MTL::Texture *nativeTextureView, const TextureViewDescriptor &descriptor);
		/// Returns the texture format exposed by this Metal texture view.
		TextureFormat getFormat() const override;
		/// Returns the width exposed by this Metal texture view.
		uint32_t getWidth() const override;
		/// Returns the height exposed by this Metal texture view.
		uint32_t getHeight() const override;
		virtual void destroy() override;
		MTL::Texture *getNativeTextureView() const;
		MTexture *getParentTexture() const;
		/** Returns the native resource identifier cached when this view was created. */
		uint64_t getResourceId() const { return mResourceId; }
		/** Returns the immutable dimension of this view rather than its parent allocation. */
		TextureViewDimension getDimension() const { return mDimension; }
		/** Returns the descriptor this view was created with, including its subresource range. */
		const TextureViewDescriptor &getDescriptor() const { return mDescriptor; }

	private:
		MTL::Texture *mNativeTextureView = nullptr;
		MTexture *mParentTexture = nullptr;
		uint64_t mResourceId = 0;
		TextureFormat mFormat = TextureFormat::Undefined;
		TextureViewDimension mDimension = TextureViewDimension::Undefined;
		TextureViewDescriptor mDescriptor = {};
	};

} // namespace GVM::RHI::Metal
