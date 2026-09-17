#include <GVMRHI/GVMRHI.hpp>
#include "SampleQueueOverlay.hpp"
#include <SDL.h>
#include <iostream>

const char *g_shaderCode = R"D4LIN4R(
#include <metal_stdlib>

using namespace metal;

struct VertexOutput
{
  float4 position [[position]];
  float4 color;
};
struct VertexInput
{
  float4 position [[attribute(0)]];
  float4 color [[attribute(1)]];
};
vertex VertexOutput render_vertex(uint vid [[vertex_id]], VertexInput vInput [[stage_in]])
{
  VertexOutput vertexOut;
  //Clockwise winding order
  vertexOut.position = vInput.position;
  vertexOut.color = vInput.color;
  return vertexOut;
}

fragment float4 render_fragment(VertexOutput vertexIn [[stage_in]])
{
  return vertexIn.color;
}
)D4LIN4R";

int main()
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("SDL Metal", -1, -1, 640, 480, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    GVM::RHI::Instance instance = GVM::RHI::createInstance({
        .diagnosticsOverlay = {.enabled = GVM::RHI::True},
    });
    GVM::RHI::Device device = instance->createDevice();
    GVM::RHI::Swapchain swapchain = instance->createSwapchain({.A = SDL_RenderGetMetalLayer(renderer)});
    GVM::RHI::Buffer buffer = device->createBuffer({.size = 1024, .usage = GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite, .label = "Buffer"});
    GVM::RHI::Texture texture = device->createTexture({.size = {640, 480, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm, .usage = GVM::RHI::TextureUsage::RenderAttachment, .label = "Texture"});
    auto mainQueue = device->getMainQueue();
    GVM::Core::QueueProxy queue = new GVM::Core::QueueProxyImpl(device, mainQueue);
    GVM::RHI::Color color = {.r = 0, .g = 0, .b = 0, .a = 1};
    auto triangleShader = device->createShaderModule({.code = g_shaderCode});
    GVM::RHI::RenderPipeline triangleRenderPipeline = device->createRenderPipeline({
        .label = "Triangle",
        .vertex = {.module = triangleShader, .entryPoint = "render_vertex", .buffers = {{.arrayStride = sizeof(float) * 8, .stepMode = GVM::RHI::VertexStepMode::Vertex, .attributes = {{.format = GVM::RHI::VertexFormat::Float32x4, .offset = 0, .shaderLocation = 0}, {.format = GVM::RHI::VertexFormat::Float32x4, .offset = sizeof(float) * 4, .shaderLocation = 1}}}}},
        .fragment = {.module = triangleShader, .entryPoint = "render_fragment", .targets = {{.format = GVM::RHI::TextureFormat::BGRA8Unorm}}},
    });

    eastl::vector<float> vertexData = {0.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 1.0, -1.0, 0.0, 1.0, 0.3, 1.0, 0.3, 1.0, -1.0, -1.0, 0.0, 1.0, 0.3, 0.3, 1.0, 1.0};

    auto vertexBuffer = device->createBuffer({.size = sizeof(float) * vertexData.size(), .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite, .label = "VertexBuffer"});
    auto indexBuffer = device->createBuffer({.size = sizeof(uint32_t) * 3, .usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite, .label = "IndexBuffer"});

    vertexBuffer.get()->map();
    auto vertexBufferPointer = vertexBuffer.get()->getMappedRange(0, sizeof(float) * vertexData.size());
    memcpy(vertexBufferPointer, vertexData.data(), sizeof(float) * vertexData.size());
    vertexBuffer.get()->unmap();

    eastl::vector<uint32_t> indexData = {0, 1, 2};
    indexBuffer.get()->map();
    auto indexBufferPointer = indexBuffer.get()->getMappedRange(0, sizeof(uint32_t) * 3);
    memcpy(indexBufferPointer, indexData.data(), sizeof(uint32_t) * 3);
    indexBuffer.get()->unmap();

    bool quit = false;
    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0)
        {
            switch (e.type)
            {
            case SDL_QUIT:
                quit = true;
                break;
            }
        }

        {
            auto nextTextureStatus = swapchain->queryNextTexture();

            // color.r = (color.r > 1.0) ? 0 : color.r + 0.01;

            GVM::RHI::RenderPassDescriptor passDescriptor = {.colorAttachments = {GVM::RHI::RenderPassColorAttachment{.view = nextTextureStatus.texture.get()->createView(), .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store, .clearValue = color}}};
            queue->renderPass(
                     "SampleTriangleWithBufferPass",
                     GVM::RHI::Samples::SampleFramebuffer{.descriptor = passDescriptor},
                     GVM::Core::RenderPassTaskDescriptor{
                         .drawFn = [&](GVM::RHI::RenderPassEncoder renderPassEncoder) {
                             renderPassEncoder->setPipeline(triangleRenderPipeline);
                             renderPassEncoder->setVertexBuffer(GVM::RHI::BufferRange(vertexBuffer), 0);
                             renderPassEncoder->setIndexBuffer(GVM::RHI::BufferRange(indexBuffer), GVM::RHI::IndexFormat::Uint32);
                             renderPassEncoder->drawIndexed(3, 1, 0, 0, 0);
                         },
                     })
                ->submit();

            swapchain->present();
        }
    }
    device->freeBuffer(vertexBuffer);
    device->freeBuffer(indexBuffer);

    device->freeBuffer(buffer);
    device->freeTexture(texture);

    instance->destroy();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
