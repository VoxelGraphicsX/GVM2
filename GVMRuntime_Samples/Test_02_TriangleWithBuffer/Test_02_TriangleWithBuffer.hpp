#include "TriangleHeader1.hpp"

#include <vector>
class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    RenderClass<Triangle> triangle;
    Buffer<VertexInput, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint32_t, BufferUsage<Index, CopyDst>> indexBuffer;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> presentTexture;

public:
    void init(Device device, Swapchain swapchain)
    {
        this->device = device;
        this->swapchain = swapchain;
        triangle = device->createRenderClass<Triangle>();
        auto initialSwapchainTexture = this->swapchain->queryNextTexture();
        presentTexture = device->createTexture("PresentTexture", initialSwapchainTexture.texture->getWidth(), initialSwapchainTexture.texture->getHeight(), 1);

        std::vector<float> vertexData = {0.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 1.0, -1.0, 0.0, 1.0, 0.3, 1.0, 0.3, 1.0, -1.0, -1.0, 0.0, 1.0, 0.3, 0.3, 1.0, 1.0};
        std::vector<uint32_t> indexData = {0, 1, 2};
        auto graphicsQueue = device->graphicsQueue(0);

        vertexBuffer = device->createBuffer("VertexBuffer", sizeof(float) * vertexData.size());
        indexBuffer = device->createBuffer("IndexBuffer", sizeof(uint32_t) * 3);

        graphicsQueue->writeBuffer(BufferRange(vertexBuffer), vertexData.data(), sizeof(float) * vertexData.size());
        graphicsQueue->writeBuffer(BufferRange(indexBuffer), indexData.data(), sizeof(uint32_t) * indexData.size());
    }
    /// Renders one indexed triangle frame and presents it through the current swapchain.
    void render()
    {

        auto nextTextureStatus = this->swapchain->queryNextTexture();
        auto graphicsQueue = device->graphicsQueue(0);
        TriangleFrameBuffer triangleFB;
        triangleFB.color = presentTexture->createView();
        triangleFB.color.loadOp = LoadOp::Clear;
        triangleFB.color.clearValue = {0, 0, 0.2, 1};
        triangleFB.color.storeOp = StoreOp::Store;

        graphicsQueue->renderPass("MainRenderPass", triangleFB, triangle->setVertexBuffer(vertexBuffer), triangle->setIndexBuffer(indexBuffer), triangle(3, 1, 0, 0))
            ->renderToSwapchain(nextTextureStatus, presentTexture, UGL::RenderToSwapchainDescriptor{}, RenderImGui())
            ->submit();

        this->swapchain->present();
    }

    void destroy()
    {
    }
};
