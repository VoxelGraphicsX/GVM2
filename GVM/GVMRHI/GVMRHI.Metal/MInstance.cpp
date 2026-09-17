#include "MInstance.hpp"
#include "MDevice.hpp"
#include "MSwapchain.hpp"

#include "Private/LoggingCore.hpp"
#include "Private/PublicLoggerAdapter.hpp"

#include <stdexcept>
namespace GVM::RHI::Metal
{

    MInstance::MInstance() {}

    void MInstance::init(const InstanceDescriptor &descriptor)
    {
        if (mLogContext)
        {
            return;
        }

        mDescriptor = descriptor;
        mLogContext = eastl::make_shared<Internal::LogContext>(GraphicsBackend::Metal);
        mLogger = Internal::createPublicLogger(mLogContext);
        if (descriptor.hasLoggingConfig != False)
        {
            mLogContext->configure(descriptor.logging);
        }
    }

    GraphicsBackend MInstance::getBackend() const
    {
        return GraphicsBackend::Metal;
    }

    Device MInstance::createDevice()
    {
        if (mDestroyed)
        {
            throw std::runtime_error("MInstance::createDevice was called after the instance was destroyed.");
        }
        if (!mLogContext)
        {
            init();
        }
        if (this->mDevice == nullptr)
        {
            auto *device = new MDevice();
            try
            {
                device->init(this);
                this->mDevice = device;
            }
            catch (...)
            {
                delete device;
                throw;
            }
        }
        return this->mDevice;
    }

    Swapchain MInstance::createSwapchain(const SwapchainDescriptor &descriptor)
    {
        if (mDestroyed)
        {
            throw std::runtime_error("MInstance::createSwapchain was called after the instance was destroyed.");
        }
        if (this->mSwapchain == nullptr)
        {
            auto *device = static_cast<MDevice *>(createDevice());
            auto *swapchain = new MSwapchain();
            try
            {
                swapchain->init(device, descriptor);
                this->mSwapchain = swapchain;
            }
            catch (...)
            {
                delete swapchain;
                throw;
            }
        }
        return this->mSwapchain;
    }

    void MInstance::setLoggingConfig(const LoggingConfig &config)
    {
        if (mDestroyed)
        {
            throw std::runtime_error("MInstance::setLoggingConfig was called after the instance was destroyed.");
        }
        if (!mLogContext)
        {
            init();
        }
        mLogContext->configure(config);
        mDescriptor.hasLoggingConfig = True;
        mDescriptor.logging = config;
    }

    LoggingConfig MInstance::getLoggingConfig() const
    {
        return mLogContext ? mLogContext->getConfig() : LoggingConfig{};
    }

    Logger MInstance::getLogger() const
    {
        return mLogger;
    }

    const eastl::shared_ptr<Internal::LogContext> &MInstance::getLogContext() const
    {
        return mLogContext;
    }

    const InstanceDescriptor &MInstance::getDescriptor() const
    {
        return mDescriptor;
    }

    void MInstance::destroy()
    {
        if (mDestroyed)
        {
            return;
        }

        if (this->mSwapchain != nullptr)
        {
            this->mSwapchain->destroy();
        }
        if (this->mDevice != nullptr)
        {
            this->mDevice->destroy();
        }
        delete this->mSwapchain;
        delete this->mDevice;
        this->mDevice = nullptr;
        this->mSwapchain = nullptr;
        GVM::RHI::flushLogger(mLogger);
        mLogger = nullptr;
        mLogContext.reset();
        mDestroyed = true;
    }

} // namespace GVM::RHI::Metal

namespace GVM::RHI
{
    Instance createInstance(const InstanceDescriptor &descriptor)
    {
        auto *instance = new Metal::MInstance();
        try
        {
            instance->init(descriptor);
            return instance;
        }
        catch (...)
        {
            delete instance;
            throw;
        }
    }
    void destroyInstance(Instance instance)
    {
        instance->destroy();
        delete instance;
    }
}
