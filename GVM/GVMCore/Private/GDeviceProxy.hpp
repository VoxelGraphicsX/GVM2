#pragma once
#include "GQueue.hpp"
#include "GShader.hpp"
#include <GVMRHI/GVMRHI.hpp>
namespace GVM::Core
{

    class DeviceProxyImpl final : public GVM::RHI::RefCountedObject
    {
        GVM::RHI::Device mDevice;
        friend class DeviceProxy;

        eastl::intrusive_ptr<GVM::Core::QueueProxyImpl> mQueueProxy = nullptr;

    public:
        DeviceProxyImpl(GVM::RHI::Device device);
        GVM::RHI::Buffer createBuffer(const GVM::RHI::BufferDescriptor &descriptor);
        GVM::RHI::Texture createTexture(const GVM::RHI::TextureDescriptor &descriptor);
        /** Retires a buffer through the wrapped RHI device once generated host code is done using it. */
        void freeBuffer(GVM::RHI::Buffer buffer);
        /** Retires a texture through the wrapped RHI device once generated host code is done using it. */
        void freeTexture(GVM::RHI::Texture texture);

        // template <typename T>
        // eastl::intrusive_ptr<T> createBindGroup(const T &t)
        template <typename T, typename... Args>
        eastl::intrusive_ptr<T> createBindGroup(Args &&...args)
        {
            eastl::intrusive_ptr<T> t1 = new T();
            t1->mDevice = mDevice;
            t1->create(std::forward<Args>(args)...);
            // t1->create();
            return t1;
        }
        template <typename T, typename... Args>
        eastl::intrusive_ptr<T> createRenderClass(Args &&...args)
        {
            eastl::intrusive_ptr<T> t = new T();
            t->mDevice = mDevice;
            t->create(std::forward<Args>(args)...);
            return t;
        }

        template <typename T, typename... Args>
        eastl::intrusive_ptr<T> createComputeClass(Args &&...args)
        {
            eastl::intrusive_ptr<T> t = new T();
            t->mDevice = mDevice;
            t->create(std::forward<Args>(args)...);
            return t;
        }

        template <typename T>
        eastl::intrusive_ptr<T> createRenderSet()
        {
            eastl::intrusive_ptr<T> t = new T();
            t->create(mDevice);
            return t;
        }

        QueueProxy graphicsQueue(int index) const;
        QueueProxy computeQueue(int index) const;
        GVM::RHI::Sampler createSampler(const GVM::RHI::SamplerDescriptor &descriptor);
        GVM::RHI::GpuTimestampFrameProfiler createTimestampFrameProfiler(uint32_t maxScopes, const eastl::string &label = "GpuTimestampFrameProfiler") const;
        GVM::RHI::GpuPassCounterFrameProfiler createPassCounterFrameProfiler(uint32_t maxScopes, const eastl::string &label = "GpuPassCounterFrameProfiler") const;
        GVM::RHI::Device getNativeDevice() const;
    };

    class DeviceProxy final
    {
        eastl::intrusive_ptr<DeviceProxyImpl> mImpl;

    public:
        DeviceProxy() = default;
        DeviceProxy(GVM::RHI::Device device);
        eastl::intrusive_ptr<DeviceProxyImpl> operator->();
        ~DeviceProxy();
    };

} // namespace GVM::Core
