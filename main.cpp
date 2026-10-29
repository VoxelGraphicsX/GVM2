#include <GVMRHI/GVMRHI.hpp>
#include <SDL.h>
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
  return myArgumentBuffer.myTexture.sample(myArgumentBuffer.mySampler, vertexIn.uv);
}
)D4LIN4R";

int main()
{
    static constexpr int screen_width = 640;
    static constexpr int screen_height = 480;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    // SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("SDL Metal", -1, -1, screen_width, screen_height, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    GVM::RHI::Instance instance = GVM::RHI::createInstance();
    GVM::RHI::Device device = instance->createDevice();
    GVM::RHI::Swapchain swapchain = instance->createSwapchain({.A = SDL_RenderGetMetalLayer(renderer)});

    GVM::RHI::Texture texture = device->createTexture({.size = {screen_width, screen_height, 1}, .mipLevelCount = 1, .format = GVM::RHI::TextureFormat::RGBA8Unorm, .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding, .label = "TextureQuad_Texture"});

    GVM::RHI::Buffer buffer = device->createBuffer({.size = screen_width * screen_height * 4, .usage = GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite, .label = "TextureQuad_Buffer"});

    auto mainQueue = device->getMainQueue();
    GVM::RHI::Color color = {.r = 0, .g = 0, .b = 0, .a = 1};

    eastl::vector<float> vertexData = {-1.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, -1.0, 0.0, 1.0, 0.3, 1.0, 0.3, 1.0, 1.0, 0.0, 0.0, 0.0, -1.0, -1.0, 0.0, 1.0, 0.3, 0.3, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.3, 0.3, 1.0, 1.0, 1.0, 0.0, 0.0};

    eastl::vector<uint32_t> indexData = {0, 3, 1, 0, 1, 2};

    auto vertexBuffer = device->createBuffer({.size = sizeof(vertexData[0]) * vertexData.size(), .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst, .label = "VertexBuffer"});
    auto indexBuffer = device->createBuffer({.size = sizeof(indexData[0]) * indexData.size(), .usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .label = "IndexBuffer"});

    mainQueue->writeBuffer(indexBuffer, indexData.data(), sizeof(uint32_t) * indexData.size());
    mainQueue->writeBuffer(vertexBuffer, vertexData.data(), sizeof(float) * vertexData.size());

    eastl::vector<uint8_t> bufferData = eastl::vector<uint8_t>(screen_width * screen_height * 4, 255);
    mainQueue->writeBuffer(buffer, bufferData.data(), bufferData.size());

    auto sampler = device->createSampler({.label = "TextureQuad_Sampler", .addressModeU = GVM::RHI::AddressMode::ClampToEdge, .addressModeV = GVM::RHI::AddressMode::ClampToEdge, .addressModeW = GVM::RHI::AddressMode::ClampToEdge, .magFilter = GVM::RHI::FilterMode::Linear, .minFilter = GVM::RHI::FilterMode::Linear, .mipmapFilter = GVM::RHI::MipmapFilterMode::Linear, .lodMinClamp = 0, .lodMaxClamp = 12, .maxAnisotropy = 16});

    auto bindGroupLayout = device->createBindGroupLayout(
        {.label = "TextureQuad_BindGroupLayout",
         .entries = {{.binding = 0, .visibility = GVM::RHI::ShaderStage::Fragment, .texture = {.sampleType = GVM::RHI::TextureSampleType::Float, .viewDimension = GVM::RHI::TextureViewDimension::e2D}}, {.binding = 1, .visibility = GVM::RHI::ShaderStage::Fragment, .buffer = {.type = GVM::RHI::BufferBindingType::Storage, .access = GVM::RHI::StorageBufferAccess::ReadOnly}}, {.binding = 2, .visibility = GVM::RHI::ShaderStage::Fragment, .sampler = {.type = GVM::RHI::SamplerBindingType::Filtering}}}});
    // GVM::RHI::BufferRange bufferRange = {.buffer = buffer, .offset = 0, .size = bufferData.size()};
    auto bindGroup = device->createBindGroup({.label = "TextureQuad_BindGroup",
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
        .fragment = {.module = triangleShader, .entryPoint = "render_fragment", .targets = {{.format = GVM::RHI::TextureFormat::BGRA8Unorm}}},
    });

    mainQueue->writeTexture({.texture = texture, .origin = {0, 0, 0}, .mipLevel = 0, .aspect = GVM::RHI::TextureAspect::All}, bufferData.data(), bufferData.size(), {}, {screen_width, screen_height, 1});

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
            auto commandEncoder = mainQueue->createCommandEncoder();
            commandEncoder->begin();
            auto renderPassEncoder = commandEncoder->beginRenderPass(passDescriptor);
            renderPassEncoder->setPipeline(triangleRenderPipeline);
            renderPassEncoder->setVertexBuffer(vertexBuffer, 0);
            renderPassEncoder->setIndexBuffer(indexBuffer, GVM::RHI::IndexFormat::Uint32);
            renderPassEncoder->setBindGroup(bindGroup, 0);
            renderPassEncoder->drawIndexed(indexData.size(), 1, 0, 0, 0);
            renderPassEncoder->end();
            commandEncoder->end();

            mainQueue->submit({commandEncoder});

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
