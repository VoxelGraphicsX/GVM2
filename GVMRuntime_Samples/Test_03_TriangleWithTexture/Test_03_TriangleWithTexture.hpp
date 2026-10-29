#include "TriangleHeader1.hpp"

#include <vector>
class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    RenderClass<Triangle> triangle;
    Buffer<VertexInput, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint32_t, BufferUsage<Index, CopyDst>> indexBuffer;
    Texture<UGL::TextureFormat::BGRA8Unorm, TextureUsage<TextureBinding, StorageBinding, CopyDst>, TextureDimension::e2D> texture0;
    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> presentTexture;
    Sampler sampler;

public:
    void init(Device device, Swapchain swapchain)
    {
        this->device = device;
        this->swapchain = swapchain;
        texture0 = device->createTexture("Texture", 1920, 1080, 1);
        sampler = device->createSampler({.label = "Sampler", .addressModeU = AddressMode::ClampToEdge, .addressModeV = AddressMode::ClampToEdge, .addressModeW = AddressMode::ClampToEdge, .magFilter = FilterMode::Linear, .minFilter = FilterMode::Linear, .mipmapFilter = MipmapFilterMode::Linear, .lodMinClamp = 0, .lodMaxClamp = 12, .maxAnisotropy = 16});
        auto textureView = texture0->createView();
        BindGroup<TriangleBindGroup> bindGroup = device->createBindGroup<TriangleBindGroup>(textureView, sampler);
        triangle = device->createRenderClass<Triangle>(bindGroup);
        auto initialSwapchainTexture = this->swapchain->queryNextTexture();
        presentTexture = device->createTexture("PresentTexture", initialSwapchainTexture.texture->getWidth(), initialSwapchainTexture.texture->getHeight(), 1);

        std::vector<float> vertexData = {0.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 1.0, -1.0, 0.0, 1.0, 0.3, 1.0, 0.3, 1.0, -1.0, -1.0, 0.0, 1.0, 0.3, 0.3, 1.0, 1.0};
        std::vector<uint32_t> indexData = {0, 1, 2};
        auto graphicsQueue = device->graphicsQueue(0);

        vertexBuffer = device->createBuffer("VertexBuffer", vertexData.size());
        indexBuffer = device->createBuffer("IndexBuffer", 3);

        graphicsQueue->writeBuffer(BufferRange(vertexBuffer, 0), vertexData.data(), sizeof(float) * vertexData.size());
        graphicsQueue->writeBuffer(BufferRange(indexBuffer, 0), indexData.data(), sizeof(uint32_t) * indexData.size());
    }
    /// Renders one textured triangle frame and presents it through the current swapchain.
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
