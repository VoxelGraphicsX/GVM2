#pragma once
#include "UGL.Device.h"
#include "UGL.Swapchain.h"
namespace UGL
{

    class AbstractRenderer
    {
    public:
        virtual void init(Device device, Swapchain swapchain) = 0;
        virtual void render() = 0;
        virtual void destroy() = 0;
        void setFrameRingBufferCountOne() {};
        void setFrameRingBufferCountThree() {};
        virtual ~AbstractRenderer() = default;
    };
}
/*
#include "UGL.Device.h"
#include "UGL.Swapchain.h"
#include <type_traits>
namespace UGL
{

class BaseRenderer
{

protected:
    Device createDefaultDevice()
    {
        return {};
    }
    Swapchain getDefaultSwapchain()
    {
        return {};
    }

public:
};


template <typename Renderer>
concept IsRenderer = std::is_base_of_v<BaseRenderer, Renderer>;

template <class Renderer>
    requires(IsRenderer<Renderer>)
void ExecuteRenderer(Renderer &renderer)
{
}
} // namespace UGL
 */
