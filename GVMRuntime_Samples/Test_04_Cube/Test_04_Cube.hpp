#include "CheckerBoardBackground.hpp"
#include "Composite.hpp"
#include "Cube.hpp"
#include "CubeData.hpp"
#include "SimpleCamera.hpp"
#include <vector>

class MyRenderer : public UGL::AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    RenderClass<CubeDraw> cube;
    ComputeClass<CheckerBoardBackground> checkerBoardBackground;
    Buffer<CubeVertexInput, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<Camera, BufferUsage<Uniform, CopyDst>> cameraBuffer;
    SimpleCamera cameraController;
    Camera camera;

    Texture<UGL::TextureFormat::BGRA8Unorm, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> gBufferAlbedo;
    Texture<UGL::TextureFormat::Depth32Float, TextureUsage<RenderAttachment, TextureBinding>, TextureDimension::e2D> depthBuffer;

    Texture<UGL::TextureFormat::RGBA8Unorm, TextureUsage<StorageBinding, TextureBinding, CopyDst>, TextureDimension::e2D> albedoTexture;
    Texture<UGL::TextureFormat::R32Float, TextureUsage<StorageBinding, TextureBinding, CopyDst>, TextureDimension::e2D> depthTexture;

    ComputeClass<Composite> composite;

    [[Export]] int MouseLeftClick = 0;
    [[Export]] int mouseMoveX = 0;
    [[Export]] int mouseMoveY = 0;
    [[Export]] int cameraMove = 0;
    [[Export]] double durationTicks = 0;

    int lastMouseMoveX = -1;
    int lastMouseMoveY = -1;

    int width = 1280 / 2;
    int height = 960 / 2;

public:
    void init(Device device, Swapchain swapchain)
    {
        this->device = device;
        this->swapchain = swapchain;
        cameraController.create(CameraType::FPS);
        gBufferAlbedo = device->createTexture("GBufferAlbedo", width, height, 1);
        depthBuffer = device->createTexture("DepthBuffer", width, height, 1);
        cameraBuffer = device->createBuffer("CameraBuffer", 1);

        albedoTexture = device->createTexture("AlbedoTexture", width, height, 1);
        depthTexture = device->createTexture("DepthTexture", width, height, 1);

        camera.proj = PerspectiveLH(45 * 3.14 / 180., float(width) / height, 0.1, 1000000.0);
        camera.projInv = inverse(camera.proj);
        camera.view = identity<float4x4>();

        BindGroup<CameraBindGroup> camBindGroup = device->createBindGroup<CameraBindGroup>(cameraBuffer);

        BindGroup<CheckBoardGBufferBindGroup> checkerBoardGBufferBindGroup = device->createBindGroup<CheckBoardGBufferBindGroup>(albedoTexture->createView(), depthTexture->createView());
        cube = device->createRenderClass<CubeDraw>(camBindGroup);
        checkerBoardBackground = device->createComputeClass<CheckerBoardBackground>(camBindGroup, checkerBoardGBufferBindGroup);

        BindGroup<GBufferBindGroup> mainGBufferBindGroup = device->createBindGroup<GBufferBindGroup>(gBufferAlbedo->createView(), depthBuffer->createView());

        composite = device->createComputeClass<Composite>(camBindGroup, checkerBoardGBufferBindGroup, mainGBufferBindGroup);

        vertexBuffer = device->createBuffer("VertexBuffer", cubeData.size());
        // indexBuffer = device->createBuffer("IndexBuffer",  3);

        std::vector<Camera> cameraA = {camera};

        device->graphicsQueue(0)->writeBuffer(BufferRange(cameraBuffer), cameraA.data(), sizeof(Camera));
        device->graphicsQueue(0)->writeBuffer(BufferRange(vertexBuffer), cubeData.data(), sizeof(float) * cubeData.size());
        // device->graphicsQueue(0)->writeBuffer(indexBuffer, 0, std::vector<uint32_t>{0, 1, 2}.data(), sizeof(uint32_t)
        // * 3);
    }
    void render()
    {

        if (lastMouseMoveX == -1)
        {
            lastMouseMoveX = mouseMoveX;
            lastMouseMoveY = mouseMoveY;
        }
        else if (MouseLeftClick > 0)
        {
            cameraController.processMouseMovement(mouseMoveX - lastMouseMoveX, mouseMoveY - lastMouseMoveY, true);
        }
        lastMouseMoveX = mouseMoveX;
        lastMouseMoveY = mouseMoveY;
        cameraController.processKeyboard(this->cameraMove, durationTicks);
        // printf("cameraMove:%lf", durationTicks * .00001f);
        cameraController.update();
        // printf("\n camera pos: %f, %f, %f", cameraController.Pos.x, cameraController.Pos.y, cameraController.Pos.z);
        //  this->cameraMove = 0;
        camera.view = (cameraController.getViewMatrix());
        camera.viewInv = inverse(camera.view);
        //  camera.proj = cameraController.GetProjectionMatrix();
        std::vector<Camera> cameraA = {camera};
        device->graphicsQueue(0)->writeBuffer(BufferRange(cameraBuffer), cameraA.data(), sizeof(Camera));

        CubeFrameBuffer triangleFB;
        {
            triangleFB.color = gBufferAlbedo->createView();
            triangleFB.color.loadOp = LoadOp::Clear;
            triangleFB.color.clearValue = {0, 0, 0.2, 0};
            triangleFB.color.storeOp = StoreOp::Store;
            triangleFB.depthStencil = depthBuffer->createView();
            triangleFB.depthStencil.depthLoadOp = LoadOp::Clear;
            triangleFB.depthStencil.depthStoreOp = StoreOp::Store;
            triangleFB.depthStencil.depthClearValue = .0f;
        }

        device->graphicsQueue(0)->computePass("CheckerBoardPass", checkerBoardBackground(albedoTexture->getWidth(), albedoTexture->getHeight(), 1))->renderPass("MainRenderPass", triangleFB, cube->setVertexBuffer(vertexBuffer), cube(36, 1, 0, 0))->computePass("CompositePass", composite(albedoTexture->getWidth(), albedoTexture->getHeight(), 1));
        auto nextTextureStatus = this->swapchain->queryNextTexture();
        device->graphicsQueue(0)->renderToSwapchain(nextTextureStatus, albedoTexture)->submit();

        this->swapchain->present();
    }
};
