#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI
{
    Queue DeviceImpl::getQueue(const QueueDescriptor &descriptor)
    {
        switch (descriptor.prefer)
        {
        case QueuePreference::Graphics:
        case QueuePreference::Compute:
        case QueuePreference::Transfer:
            return getMainQueue();
        default:
            throw std::invalid_argument("Device::getQueue requires a valid queue preference.");
        }
    }

    PassCounterQuerySupport DeviceImpl::getPassCounterQuerySupport() const
    {
        PassCounterQuerySupport support = {};
        support.supported = False;
        support.backendName = "unknown";
        support.unsupportedReason = "Pass counter queries are not implemented by this device.";
        return support;
    }

    DeviceImpl::~DeviceImpl() = default;
}
