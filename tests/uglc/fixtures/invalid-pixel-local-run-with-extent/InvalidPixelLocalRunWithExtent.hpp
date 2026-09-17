#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_RUN_WITH_EXTENT_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_RUN_WITH_EXTENT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidPixelLocalRunExtentFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        color;
};

class InvalidPixelLocalRunExtentPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidPixelLocalRunExtentFrame pixel(InvalidPixelLocalRunExtentFrame)
    {
        InvalidPixelLocalRunExtentFrame outputValue;
        outputValue.color = half4(1.0f, 1.0f, 1.0f, 1.0f);
        return outputValue;
    }
};

class InvalidPixelLocalRunWithExtentRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue queue;
    InvalidPixelLocalRunExtentFrame frame;
    RenderClass<InvalidPixelLocalRunExtentPass> lightingPass;
    uint width = 640u;
    uint height = 480u;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        queue = device->graphicsQueue(0);
        lightingPass = device->createRenderClass<InvalidPixelLocalRunExtentPass>();
    }

    void render() override
    {
        queue->renderPass("InvalidPixelLocalRunWithExtent",
                          frame,
                          pixelLocalPass(lightingPass(width, height)));
    }

    void destroy() override
    {
    }
};

#endif
