#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include "MDefines.hpp"
#include <EASTL/unordered_map.h>
namespace GVM::RHI::Metal
{

	class MShaderModule final : public ShaderModuleImpl
	{
	public:
		MShaderModule();
		void init(MDevice *device, const ShaderModuleDescriptor &descriptor);
		MTL::Function *makeLibrary(const eastl::string &functionName);
		~MShaderModule();

	private:
		MDevice *mDevice = nullptr;
		MTL::Library *mNativeLibrary = nullptr;
		eastl::unordered_map<eastl::string, MTL::Function *> mFunctions;
	};

} // namespace GVM::RHI::Metal
