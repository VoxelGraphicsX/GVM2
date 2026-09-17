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
		void init(MTexture *parentTexture, MTL::Texture *nativeTextureView, const eastl::string &labelName);
		/// Returns the texture format exposed by this Metal texture view.
		TextureFormat getFormat() const override;
		/// Returns the width exposed by this Metal texture view.
		uint32_t getWidth() const override;
		/// Returns the height exposed by this Metal texture view.
		uint32_t getHeight() const override;
		virtual void destroy() override;
		MTL::Texture *getNativeTextureView() const;
		MTexture *getParentTexture() const;

	private:
		MTL::Texture *mNativeTextureView = nullptr;
		MTexture *mParentTexture = nullptr;
	};

} // namespace GVM::RHI::Metal
