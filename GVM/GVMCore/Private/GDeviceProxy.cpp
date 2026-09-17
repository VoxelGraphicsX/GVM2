#include "GDeviceProxy.hpp"
#include <EASTL/make_intrusive.h>
#include <stdexcept>
namespace GVM::Core
{
    DeviceProxyImpl::DeviceProxyImpl(GVM::RHI::Device device)
    {
        mDevice = device;
        mQueueProxy = eastl::make_intrusive<GVM::Core::QueueProxyImpl>(mDevice, mDevice->getMainQueue(), mDevice != nullptr ? mDevice->getLogger() : GVM::RHI::Logger{});
    }

    GVM::RHI::Buffer DeviceProxyImpl::createBuffer(const GVM::RHI::BufferDescriptor &descriptor)
    {
        return mDevice->createBuffer(descriptor);
    }
    GVM::RHI::Texture DeviceProxyImpl::createTexture(const GVM::RHI::TextureDescriptor &descriptor)
    {
        return mDevice->createTexture(descriptor);
    }
    void DeviceProxyImpl::freeBuffer(GVM::RHI::Buffer buffer)
    {
        mDevice->freeBuffer(buffer);
    }
    void DeviceProxyImpl::freeTexture(GVM::RHI::Texture texture)
    {
        mDevice->freeTexture(texture);
    }
    QueueProxy DeviceProxyImpl::graphicsQueue(int index) const
    {
        return mQueueProxy;
    }
    GVM::RHI::Sampler DeviceProxyImpl::createSampler(const GVM::RHI::SamplerDescriptor &descriptor)
    {
        auto sampler = mDevice->createSampler(descriptor);
        return sampler;
    }
    GVM::RHI::GpuTimestampFrameProfiler DeviceProxyImpl::createTimestampFrameProfiler(uint32_t maxScopes, const eastl::string &label) const
    {
        if (mDevice == nullptr)
        {
            throw std::invalid_argument("DeviceProxyImpl::createTimestampFrameProfiler requires a valid RHI device.");
        }
        GVM::RHI::GpuTimestampFrameProfiler profiler;
        profiler.init(mDevice, maxScopes, label);
        return profiler;
    }
    GVM::RHI::GpuPassCounterFrameProfiler DeviceProxyImpl::createPassCounterFrameProfiler(uint32_t maxScopes, const eastl::string &label) const
    {
        if (mDevice == nullptr)
        {
            throw std::invalid_argument("DeviceProxyImpl::createPassCounterFrameProfiler requires a valid RHI device.");
        }
        GVM::RHI::GpuPassCounterFrameProfiler profiler;
        profiler.init(mDevice, maxScopes, label);
        return profiler;
    }
    GVM::RHI::Device DeviceProxyImpl::getNativeDevice() const
    {
        return mDevice;
    }
    DeviceProxy::DeviceProxy(GVM::RHI::Device device)
    {
        mImpl = new DeviceProxyImpl(device);
    }
    eastl::intrusive_ptr<DeviceProxyImpl> DeviceProxy::operator->()
    {
        return mImpl;
    }
    DeviceProxy::~DeviceProxy()
    {
    }
} // namespace GVM::Core
