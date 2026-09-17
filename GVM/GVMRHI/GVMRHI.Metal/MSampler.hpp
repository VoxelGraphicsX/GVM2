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

	private:
		MDevice *mDevice = nullptr;
		MTL::SamplerState *mNativeSampler = nullptr;
	};

} // namespace GVM::RHI::Metal
