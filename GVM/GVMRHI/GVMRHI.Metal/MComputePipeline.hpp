#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MComputePipeline final : public ComputePipelineImpl
	{
	public:
		MComputePipeline();
		~MComputePipeline();
		void init(MDevice *device, const ComputePipelineDescriptor &descriptor);
		MTL::ComputePipelineState *getNativePipelineState() const;
		MTL::Size getLocalThreadGroupSize() const;

	private:
		MDevice *mDevice = nullptr;
		ComputePipelineDescriptor mDescriptor;
		MTL::ComputePipelineState *mNativePipelineState = nullptr;
		MTL::Size mLocalThreadGroupSize;
	};

} // namespace GVM::RHI::Metal
