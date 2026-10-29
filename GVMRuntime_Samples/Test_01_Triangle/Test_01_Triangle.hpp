#ifndef TEST_01_TRIANGLE_HPP
#define TEST_01_TRIANGLE_HPP

#include "TriangleHeader1.hpp"

class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    RenderClass<Triangle> triangle;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> presentTexture;

public:
    void init(Device device, Swapchain swapchain)
    {
        this->device = device;
        this->swapchain = swapchain;
        triangle = device->createRenderClass<Triangle>();
        auto initialSwapchainTexture = this->swapchain->queryNextTexture();
        presentTexture = device->createTexture("PresentTexture", initialSwapchainTexture.texture->getWidth(), initialSwapchainTexture.texture->getHeight(), 1);
    }
    /// Renders one triangle frame and presents it through the current swapchain.
    void render()
    {

        auto nextTextureStatus = this->swapchain->queryNextTexture();
        TriangleFrameBuffer triangleFB;
        triangleFB.color = presentTexture->createView();
        triangleFB.color.loadOp = LoadOp::Clear;
        triangleFB.color.clearValue = {0, 0, 0.2, 1};
        triangleFB.color.storeOp = StoreOp::Store;
        device->graphicsQueue(0)->renderPass("MainRenderPass", triangleFB, triangle(3, 1, 0, 0))
            ->renderToSwapchain(nextTextureStatus, presentTexture, UGL::RenderToSwapchainDescriptor{}, RenderImGui())
            ->submit();

        this->swapchain->present();
    }
    void destroy()
    {
    }
};

#endif // TEST_01_TRIANGLE_HPP
