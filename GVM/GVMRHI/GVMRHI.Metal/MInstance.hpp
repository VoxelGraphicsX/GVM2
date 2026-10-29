#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/shared_ptr.h>
#include <Metal/Metal.hpp>
#include "Private/GVMRHIDefines.hpp"
#include "MDefines.hpp"
namespace GVM::RHI::Metal
{

	class MInstance final : public InstanceImpl
	{
	public:
		MInstance();
		void init(const InstanceDescriptor &descriptor = {});
		GraphicsBackend getBackend() const override;
		virtual Device createDevice() override;
		virtual Swapchain createSwapchain(const SwapchainDescriptor &descriptor) override;
		void setLoggingConfig(const LoggingConfig &config) override;
		LoggingConfig getLoggingConfig() const override;
		Logger getLogger() const override;
		const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;
		/// Returns the descriptor used to initialize this Metal instance.
		const InstanceDescriptor &getDescriptor() const;
		virtual void destroy() override;

	private:
		bool mDestroyed = false;
		InstanceDescriptor mDescriptor = {};
		eastl::shared_ptr<Internal::LogContext> mLogContext;
		Logger mLogger;
		MDevice *mDevice = nullptr;
		MSwapchain *mSwapchain = nullptr;
	};

} // namespace GVM::RHI::Metal
