#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MBindGroupLayout final : public BindGroupLayoutImpl
	{
	public:
		MBindGroupLayout();
		~MBindGroupLayout();
		void init(MDevice *device, const BindGroupLayoutDescriptor &descriptor);
		const BindGroupLayoutDescriptor &getDescriptor() const;
		const eastl::vector<MTL::ResourceUsage> &getResourceUsages() const;

	private:
		MDevice *mDevice = nullptr;
		BindGroupLayoutDescriptor mDescriptor;
		eastl::vector<MTL::ResourceUsage> mResourceUsages;
	};

} // namespace GVM::RHI::Metal
