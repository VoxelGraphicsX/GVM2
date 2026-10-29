#pragma once
#include "UGL.Queue.h"
#include "UGL.RenderSet.h"
#include "UGL.Resources.h"
#include "UGL.Shaders.h"
namespace UGL
{

    /*     template <typename T, typename... Args>
        concept Constructible = requires(Args... args) {
            { T(args...) } -> std::same_as<T>;
        }; */
    template <typename T, typename... Args>
    concept HasCreateFunction = requires(T t, Args... args) { t.create(args...); };
    class DeviceImpl
    {
    public:
        Private::BufferProxy createBuffer(string label, uint64_t elementCount)
        {
            return {};
        }
        Private::TextureProxy createTexture(string label, uint32_t width, uint32_t height, uint32_t depth = 1, uint32_t mipLevelCount = 1, uint32_t layerCount = 1)
        {
            return {};
        }
        /** Releases a DSL buffer through the generated host device after future API calls no longer need it. */
        template <class T, class Usages>
        void freeBuffer(Buffer<T, Usages> buffer)
        {
        }
        /** Releases a DSL texture through the generated host device after future API calls no longer need it. */
        template <class T, class Usages, class Dimension>
        void freeTexture(Texture<T, Usages, Dimension> texture)
        {
        }
        Sampler createSampler(const SamplerDescriptor &descriptor)
        {
            return {};
        }
        GpuTimestampFrameProfiler createTimestampFrameProfiler(uint32_t maxScopes, string label = "GpuTimestampFrameProfiler")
        {
            return {};
        }
        GpuPassCounterFrameProfiler createPassCounterFrameProfiler(uint32_t maxScopes, string label = "GpuPassCounterFrameProfiler")
        {
            return {};
        }
        DeviceImpl *getNativeDevice()
        {
            return this;
        }
        PassCounterQuerySupport getPassCounterQuerySupport() const
        {
            return {};
        }
        Queue graphicsQueue(int index) const
        {
            return {};
        }

        template <typename T, typename... Args>
            requires(std::is_base_of_v<IBindGroup, T> && HasCreateFunction<T, Args...>)
        BindGroup<T> createBindGroup(Args &&...args)
        {
            BindGroup<T> t;
            t->create(std::forward<Args>(args)...);
            return t;
        }

        template <typename T>
            requires(std::is_base_of_v<IRenderSet, T>)
        RenderSet<T> createRenderSet()
        {
            RenderSet<T> t1;
            return t1;
        }

        /* template <typename T>
            requires(std::is_base_of_v<IBindGroup, T>)
        BindGroup<T> createBindGroup(const T &t)
        {
            BindGroup<T> t1;
            return t1;
        } */
        template <typename T, typename... Args>
            requires(std::is_base_of_v<IComputeClass, T>)
        ComputeClass<T> createComputeClass(Args &&...args)
        {
            ComputeClass<T> t;
            t->create(std::forward<Args>(args)...);
            return t;
        }

        template <typename T, typename... Args>
            requires(std::is_base_of_v<IRenderClass, T> || std::is_base_of_v<IPixelLocalRenderClass, T>)
        RenderClass<T> createRenderClass(Args &&...args)
        {
            RenderClass<T> t;
            t->create(std::forward<Args>(args)...);
            return t;
        }
    };
    class Device
    {

        DeviceImpl *impl;

    public:
        DeviceImpl *operator->()
        {
            return impl;
        }
    };
} // namespace UGL
