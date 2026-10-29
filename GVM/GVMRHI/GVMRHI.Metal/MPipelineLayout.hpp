#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MPipelineLayout final : public PipelineLayoutImpl
	{
	public:
		MPipelineLayout();
		void init(MDevice *device, const PipelineLayoutDescriptor &descriptor);
        /** Returns the immutable pipeline layout including the generated Args ABI. */
        const PipelineLayoutDescriptor &getDescriptor() const { return mDescriptor; }
        /** Identifies the owning device for argument snapshot validation. */
        MDevice &getDevice() const { return *mDevice; }

	private:
		MDevice *mDevice = nullptr;
		PipelineLayoutDescriptor mDescriptor;
	};

} // namespace GVM::RHI::Metal
