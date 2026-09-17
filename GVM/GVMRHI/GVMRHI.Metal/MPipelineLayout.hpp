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

	private:
		MDevice *mDevice = nullptr;
		PipelineLayoutDescriptor mDescriptor;
	};

} // namespace GVM::RHI::Metal
