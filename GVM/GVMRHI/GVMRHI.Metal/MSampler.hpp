#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MSampler final : public SamplerImpl
	{
	public:
		MSampler();
		~MSampler();
		void init(MDevice *device, const SamplerDescriptor &descriptor);
		MTL::SamplerState *getNativeSampler() const;
		/** Returns the device that owns this sampler. */
		MDevice &getDevice() const { return *mDevice; }
		/** Returns the native resource identifier cached when this sampler was created. */
		uint64_t getResourceId() const { return mResourceId; }
		/** Returns immutable sampler properties captured at creation. */
		const SamplerDescriptor &getDescriptor() const { return mDescriptor; }

	private:
		MDevice *mDevice = nullptr;
		MTL::SamplerState *mNativeSampler = nullptr;
		uint64_t mResourceId = 0;
		SamplerDescriptor mDescriptor{};
	};

} // namespace GVM::RHI::Metal
