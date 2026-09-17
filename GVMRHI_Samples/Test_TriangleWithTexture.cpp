#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include "SampleQueueOverlay.hpp"
#include <SDL.h>
#include <cstdint>
#include <iostream>

const char *g_shaderCode = R"D4LIN4R(
#include <metal_stdlib>

using namespace metal;

struct VertexOutput
{
  float4 position [[position]];
  float2 uv;
};
struct VertexInput
{
  float4 position [[attribute(0)]];
  float4 color [[attribute(1)]];
  float4 uv [[attribute(2)]];
};

struct MyArgumentBuffer {
    texture2d<float> myTexture [[id(0)]];
    device uint8_t* myBuffer [[id(1)]];
    sampler mySampler [[id(2)]];
};

vertex VertexOutput render_vertex(uint vid [[vertex_id]], VertexInput vInput [[stage_in]])
{
  VertexOutput vertexOut;
  //Clockwise winding order
  vertexOut.position = vInput.position;
  vertexOut.uv = vInput.uv.xy;
  return vertexOut;
}

fragment float4 render_fragment(VertexOutput vertexIn [[stage_in]], const constant MyArgumentBuffer& myArgumentBuffer [[buffer(1)]])
{
  //return float4(float3(myArgumentBuffer.myBuffer[1000]/255), 1.0);
  return myArgumentBuffer.myTexture.sample(myArgumentBuffer.mySampler, vertexIn.uv);
}
)D4LIN4R";

static uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8u) |
           (static_cast<uint32_t>(b) << 16u) |
           (static_cast<uint32_t>(a) << 24u);
}

static eastl::vector<uint32_t> makeCheckerTexture(uint32_t width, uint32_t height)
{
    eastl::vector<uint32_t> pixels(width * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const bool checker = (((x / 32u) ^ (y / 32u)) & 1u) != 0u;
            const uint8_t edge = static_cast<uint8_t>(((x ^ y) & 0x1fu) * 3u);
            pixels[y * width + x] = checker
                                         ? packRgba(235u, static_cast<uint8_t>(142u + edge), 64u, 255u)
                                         : packRgba(38u, 92u, static_cast<uint8_t>(158u + edge), 255u);
        }
    }
    return pixels;
}

int main()
{
    static constexpr int screen_width = 640;
    static constexpr int screen_height = 480;
    static constexpr uint32_t texture_width = 512;
    static constexpr uint32_t texture_height = 512;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("SDL Metal", -1, -1, screen_width, screen_height, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    GVM::RHI::Instance instance = GVM::RHI::createInstance({
        .diagnosticsOverlay = {.enabled = GVM::RHI::True},
    });
    GVM::RHI::Device device = instance->createDevice();
    GVM::RHI::Swapchain swapchain = instance->createSwapchain({.A = SDL_RenderGetMetalLayer(renderer)});

    eastl::vector<uint32_t> texturePixels = makeCheckerTexture(texture_width, texture_height);
    const uint64_t textureBytes = texturePixels.size() * sizeof(texturePixels[0]);

    GVM::RHI::Texture texture = device->createTexture({.label = "TextureQuad_Texture", .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst, .size = {texture_width, texture_height, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm, .mipLevelCount = 1});
    GVM::RHI::Buffer buffer = device->createBuffer({.label = "TextureQuad_Buffer", .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst, .size = textureBytes});

    auto mainQueue = device->getMainQueue();
    GVM::Core::QueueProxy queue = new GVM::Core::QueueProxyImpl(device, mainQueue);
    GVM::RHI::Color color = {.r = 0, .g = 0, .b = 0, .a = 1};

    eastl::vector<float> vertexData = {-1.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, -1.0, 0.0, 1.0, 0.3, 1.0, 0.3, 1.0, 1.0, 0.0, 0.0, 0.0, -1.0, -1.0, 0.0, 1.0, 0.3, 0.3, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 1.0, 1.0, 0.0, 0.0};

    eastl::vector<uint32_t> indexData = {0, 3, 1, 0, 1, 2};

    auto vertexBuffer = device->createBuffer({.label = "VertexBuffer", .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst, .size = sizeof(float) * vertexData.size()});
    auto indexBuffer = device->createBuffer({.label = "IndexBuffer", .usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = sizeof(uint32_t) * indexData.size()});

    mainQueue->writeBuffer(GVM::RHI::BufferRange(vertexBuffer), vertexData.data(), sizeof(float) * vertexData.size());
    mainQueue->writeBuffer(GVM::RHI::BufferRange(indexBuffer), indexData.data(), sizeof(uint32_t) * indexData.size());
    mainQueue->writeBuffer(GVM::RHI::BufferRange(buffer), texturePixels.data(), textureBytes);
    mainQueue->writeTexture(
        {.texture = texture, .mipLevel = 0, .origin = {0, 0, 0}, .aspect = GVM::RHI::TextureAspect::All},
        texturePixels.data(),
        textureBytes,
        {.bytesPerRow = texture_width * sizeof(texturePixels[0]), .rowsPerImage = texture_height},
        {texture_width, texture_height, 1});

    auto sampler = device->createSampler({.label = "Sampler", .addressModeU = GVM::RHI::AddressMode::ClampToEdge, .addressModeV = GVM::RHI::AddressMode::ClampToEdge, .addressModeW = GVM::RHI::AddressMode::ClampToEdge, .magFilter = GVM::RHI::FilterMode::Linear, .minFilter = GVM::RHI::FilterMode::Linear, .mipmapFilter = GVM::RHI::MipmapFilterMode::Linear, .lodMinClamp = 0, .lodMaxClamp = 12, .maxAnisotropy = 16});

    auto bindGroupLayout = device->createBindGroupLayout(
        {.label = "TextureQuad_BindGroupLayout",
         .entries = {{.binding = 0, .visibility = GVM::RHI::ShaderStage::Fragment, .texture = {.sampleType = GVM::RHI::TextureSampleType::Float, .viewDimension = GVM::RHI::TextureViewDimension::e2D}}, {.binding = 1, .visibility = GVM::RHI::ShaderStage::Fragment, .buffer = {.type = GVM::RHI::BufferBindingType::Storage, .access = GVM::RHI::StorageBufferAccess::ReadOnly}}, {.binding = 2, .visibility = GVM::RHI::ShaderStage::Fragment, .sampler = {.type = GVM::RHI::SamplerBindingType::Filtering}}}});

    auto bindGroup = device->createBindGroup({.label = "BindGroup",
                                              .layout = bindGroupLayout,
                                              .entries = {
                                                  {.binding = 0, .textureView = texture->createView()},
                                                  {.binding = 1, .buffer = buffer},
                                                  {.binding = 2, .sampler = sampler},
                                              }});

    auto triangleShader = device->createShaderModule({.code = g_shaderCode});
    GVM::RHI::RenderPipeline triangleRenderPipeline = device->createRenderPipeline({
        .label = "TextureQuad",
        .layout = device->createPipelineLayout({.label = "PipelineLayout", .bindGroupLayouts = {bindGroupLayout}}),
        .vertex = {.module = triangleShader, .entryPoint = "render_vertex", .buffers = {{.arrayStride = sizeof(float) * 12, .stepMode = GVM::RHI::VertexStepMode::Vertex, .attributes = {{.format = GVM::RHI::VertexFormat::Float32x4, .offset = 0, .shaderLocation = 0}, {.format = GVM::RHI::VertexFormat::Float32x4, .offset = sizeof(float) * 4, .shaderLocation = 1}, {.format = GVM::RHI::VertexFormat::Float32x4, .offset = sizeof(float) * 8, .shaderLocation = 2}}}}},
        .fragment = {.module = triangleShader, .entryPoint = "render_fragment", .targets = {{.format = GVM::RHI::TextureFormat::RGBA8Unorm}}},
    });

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

            GVM::RHI::RenderPassDescriptor passDescriptor = {.colorAttachments = {GVM::RHI::RenderPassColorAttachment{.view = nextTextureStatus.texture.get()->createView(), .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store, .clearValue = color}}};
            queue->renderPass(
                     "SampleTriangleWithTexturePass",
                     GVM::RHI::Samples::SampleFramebuffer{.descriptor = passDescriptor},
                     GVM::Core::RenderPassTaskDescriptor{
                         .drawFn = [&](GVM::RHI::RenderPassEncoder renderPassEncoder) {
                             renderPassEncoder->setPipeline(triangleRenderPipeline);
                             renderPassEncoder->setVertexBuffer(GVM::RHI::BufferRange(vertexBuffer), 0);
                             renderPassEncoder->setIndexBuffer(GVM::RHI::BufferRange(indexBuffer), GVM::RHI::IndexFormat::Uint32);
                             renderPassEncoder->setBindGroup(bindGroup, 0);
                             renderPassEncoder->drawIndexed(indexData.size(), 1, 0, 0, 0);
                         },
                     })
                ->submit();

            swapchain->present();
        }
    }
    device->freeBuffer(vertexBuffer);
    device->freeBuffer(indexBuffer);
    device->freeSampler(sampler);

    device->freeBuffer(buffer);
    device->freeTexture(texture);

    instance->destroy();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
