#include "UGLBin/generate_result.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <SDL.h>
#include <SDL_metal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    enum class DebugMode
    {
        Procedural,
        Buffered,
        Textured,
        Renderer,
    };

    struct FeatureGlobalsData
    {
        float tint[4];
    };

    struct AppContext
    {
        SDL_Window *window = nullptr;
        SDL_MetalView metalView = nullptr;
        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device rawDevice = nullptr;
        GVM::Core::DeviceProxy device;
        GVM::RHI::Swapchain swapchain = nullptr;
        int width = 960;
        int height = 640;
    };

    using FeaturePresentTexture = GVM::RHI::Texture;

    FeaturePresentTexture createFeaturePresentTexture(AppContext &ctx)
    {
        return ctx.device->createTexture({
            .label = "FeaturePresentTexture",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::TextureBinding,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {
                .width = static_cast<uint32_t>(ctx.width),
                .height = static_cast<uint32_t>(ctx.height),
                .depth = 1u,
            },
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
    }

    void logStep(const std::string &message)
    {
        std::cerr << "[render-feature-debug] " << message << std::endl;
    }

    DebugMode parseMode(const char *value)
    {
        if (value == nullptr)
        {
            return DebugMode::Procedural;
        }

        if (std::strcmp(value, "buffered") == 0)
        {
            return DebugMode::Buffered;
        }
        if (std::strcmp(value, "textured") == 0)
        {
            return DebugMode::Textured;
        }
        if (std::strcmp(value, "renderer") == 0)
        {
            return DebugMode::Renderer;
        }
        return DebugMode::Procedural;
    }

    int parseRendererMode(int argc, char **argv)
    {
        for (int index = 1; index < argc - 1; ++index)
        {
            if (std::strcmp(argv[index], "--renderer-mode") == 0)
            {
                return std::clamp(std::atoi(argv[index + 1]), 0, 8);
            }
        }
        return 0;
    }

    int parseFrames(int argc, char **argv)
    {
        for (int index = 1; index < argc - 1; ++index)
        {
            if (std::strcmp(argv[index], "--frames") == 0)
            {
                return std::max(1, std::atoi(argv[index + 1]));
            }
        }
        return 600;
    }

    bool hasFlag(int argc, char **argv, const char *flag)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (std::strcmp(argv[index], flag) == 0)
            {
                return true;
            }
        }
        return false;
    }

    DebugMode parseModeArg(int argc, char **argv)
    {
        for (int index = 1; index < argc - 1; ++index)
        {
            if (std::strcmp(argv[index], "--mode") == 0)
            {
                return parseMode(argv[index + 1]);
            }
        }
        return DebugMode::Procedural;
    }

    const char *modeName(DebugMode mode)
    {
        switch (mode)
        {
        case DebugMode::Procedural:
            return "procedural";
        case DebugMode::Buffered:
            return "buffered";
        case DebugMode::Textured:
            return "textured";
        case DebugMode::Renderer:
            return "renderer";
        }
        return "procedural";
    }

    void pumpEvents(bool &quitRequested)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                quitRequested = true;
            }
        }
    }

    void initAppContext(AppContext &ctx)
    {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

        logStep("Initializing SDL.");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        {
            throw std::runtime_error(SDL_GetError());
        }

        logStep("Creating SDL window.");
        ctx.window = SDL_CreateWindow(
            "GVM Render Feature Debug",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            ctx.width,
            ctx.height,
            SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_METAL
        );
        if (ctx.window == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        logStep("Creating SDL Metal view.");
        ctx.metalView = SDL_Metal_CreateView(ctx.window);
        if (ctx.metalView == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        logStep("Creating GVM instance and device.");
        ctx.instance = GVM::RHI::createInstance({
            .diagnosticsOverlay = {.enabled = GVM::RHI::True},
        });
        ctx.rawDevice = ctx.instance->createDevice();
        if (ctx.instance == nullptr || ctx.rawDevice == nullptr)
        {
            throw std::runtime_error("Failed to create GVM instance or device.");
        }
        ctx.device = GVM::Core::DeviceProxy(ctx.rawDevice);

        logStep("Creating GVM swapchain.");
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Metal_GetDrawableSize(ctx.window, &drawableWidth, &drawableHeight);
        GVM::RHI::Extent3D swapchainExtent = {
            .width = static_cast<uint32_t>(std::max(drawableWidth, 1)),
            .height = static_cast<uint32_t>(std::max(drawableHeight, 1)),
            .depth = 1,
        };
        ctx.swapchain = ctx.instance->createSwapchain({
            .A = SDL_Metal_GetLayer(ctx.metalView),
            .B = &swapchainExtent,
        });
        if (ctx.swapchain == nullptr)
        {
            throw std::runtime_error("Failed to create swapchain.");
        }
        ctx.width = static_cast<int>(swapchainExtent.width);
        ctx.height = static_cast<int>(swapchainExtent.height);
    }

    void destroyAppContext(AppContext &ctx)
    {
        if (ctx.swapchain != nullptr)
        {
            ctx.swapchain->destroy();
            ctx.swapchain = nullptr;
        }
        if (ctx.rawDevice != nullptr)
        {
            ctx.rawDevice->destroy();
            delete ctx.rawDevice;
            ctx.rawDevice = nullptr;
        }
        if (ctx.instance != nullptr)
        {
            delete ctx.instance;
            ctx.instance = nullptr;
        }
        if (ctx.metalView != nullptr)
        {
            SDL_Metal_DestroyView(ctx.metalView);
            ctx.metalView = nullptr;
        }
        if (ctx.window != nullptr)
        {
            SDL_DestroyWindow(ctx.window);
            ctx.window = nullptr;
        }
        SDL_Quit();
    }

    FeatureFrameBuffer makeOffscreenFramebuffer(FeaturePresentTexture &presentTexture)
    {
        FeatureFrameBuffer framebuffer = {};
        framebuffer.color = presentTexture->createView();
        framebuffer.color.loadOp = GVM::RHI::LoadOp::Clear;
        framebuffer.color.clearValue = {0.04f, 0.05f, 0.10f, 1.0f};
        framebuffer.color.storeOp = GVM::RHI::StoreOp::Store;
        return framebuffer;
    }

    void presentOffscreenTexture(AppContext &ctx, FeaturePresentTexture &presentTexture)
    {
        auto nextTexture = ctx.swapchain->queryNextTexture();
        ctx.device->graphicsQueue(0)->renderToSwapchain(nextTexture, presentTexture)->submit();
        ctx.swapchain->present();
    }

    void createFeatureGeometry(
        GVM::Core::DeviceProxy device,
        GVM::RHI::Buffer &vertexBuffer,
        GVM::RHI::Buffer &indexBuffer)
    {
        logStep("create feature geometry buffers");
        vertexBuffer = device->createBuffer({
            .label = "FeatureVertexBuffer",
            .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(float) * 30,
        });
        indexBuffer = device->createBuffer({
            .label = "FeatureIndexBuffer",
            .usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(uint32_t) * 3,
        });

        const std::vector<float> featureVertexData = {
            -0.70f, -0.60f, 0.0f, 1.0f,   1.0f, 0.25f, 0.25f, 1.0f,   0.0f, 1.0f,
             0.00f,  0.72f, 0.0f, 1.0f,   0.25f, 1.0f, 0.35f, 1.0f,   0.5f, 0.0f,
             0.74f, -0.60f, 0.0f, 1.0f,   0.25f, 0.45f, 1.0f, 1.0f,   1.0f, 1.0f
        };
        const std::vector<uint32_t> featureIndexData = {0, 1, 2};

        logStep("upload feature geometry buffers");
        device->graphicsQueue(0)->writeBuffer(GVM::RHI::BufferRange(vertexBuffer), featureVertexData.data(), sizeof(float) * featureVertexData.size());
        device->graphicsQueue(0)->writeBuffer(GVM::RHI::BufferRange(indexBuffer), featureIndexData.data(), sizeof(uint32_t) * featureIndexData.size());
    }

    auto createFeatureGlobalsBindGroup(GVM::Core::DeviceProxy device)
    {
        logStep("create feature globals buffer");
        auto featureGlobalsBuffer = device->createBuffer({
            .label = "FeatureGlobalsBuffer",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(FeatureGlobalsData),
        });

        const FeatureGlobalsData featureGlobalsData = {
            .tint = {1.0f, 1.0f, 1.0f, 1.0f},
        };

        device->graphicsQueue(0)->writeBuffer(
            GVM::RHI::BufferRange(featureGlobalsBuffer),
            &featureGlobalsData,
            sizeof(featureGlobalsData));

        logStep("create feature globals bind group");
        return device->createBindGroup<FeatureGlobalsBindGroup>(featureGlobalsBuffer);
    }

    void renderBuffered(AppContext &ctx, int frames)
    {
        auto featureGlobalsBindGroup = createFeatureGlobalsBindGroup(ctx.device);
        logStep("createRenderClass<BufferedTrianglePass>()");
        auto bufferedTriangle = ctx.device->createRenderClass<BufferedTrianglePass>(featureGlobalsBindGroup);
        FeaturePresentTexture presentTexture = createFeaturePresentTexture(ctx);

        GVM::RHI::Buffer vertexBuffer = {};
        GVM::RHI::Buffer indexBuffer = {};
        createFeatureGeometry(ctx.device, vertexBuffer, indexBuffer);

        bool quitRequested = false;
        for (int frame = 0; frame < frames && !quitRequested; ++frame)
        {
            pumpEvents(quitRequested);
            auto framebuffer = makeOffscreenFramebuffer(presentTexture);
            ctx.device->graphicsQueue(0)->renderPass(
                "FeatureBufferedTrianglePass",
                framebuffer,
                bufferedTriangle->setVertexBuffer(vertexBuffer),
                bufferedTriangle->setIndexBuffer(indexBuffer),
                bufferedTriangle->run(3, 1, 0, 0))->submit();
            presentOffscreenTexture(ctx, presentTexture);
            SDL_Delay(16);
        }
    }

    void renderTextured(AppContext &ctx, int frames)
    {
        GVM::RHI::Buffer vertexBuffer = {};
        GVM::RHI::Buffer indexBuffer = {};
        createFeatureGeometry(ctx.device, vertexBuffer, indexBuffer);

        logStep("create feature texture");
        auto featureTexture = ctx.device->createTexture({
            .label = "FeatureTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {.width = 2, .height = 2, .depth = 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        const std::vector<uint8_t> featureTextureData = {
            255, 32, 32, 255,   32, 255, 64, 255,
            32, 96, 255, 255,   250, 220, 64, 255
        };
        ctx.device->graphicsQueue(0)->writeTexture(featureTexture, featureTextureData.data(), sizeof(uint8_t) * featureTextureData.size());

        logStep("create sampler");
        auto featureSampler = ctx.device->createSampler({
            .label = "FeatureSampler",
            .addressModeU = GVM::RHI::AddressMode::ClampToEdge,
            .addressModeV = GVM::RHI::AddressMode::ClampToEdge,
            .addressModeW = GVM::RHI::AddressMode::ClampToEdge,
            .magFilter = GVM::RHI::FilterMode::Linear,
            .minFilter = GVM::RHI::FilterMode::Linear,
            .mipmapFilter = GVM::RHI::MipmapFilterMode::Linear,
            .lodMinClamp = 0,
            .lodMaxClamp = 1,
            .maxAnisotropy = 1
        });

        logStep("create textured bind group");
        auto texturedBindGroup = ctx.device->createBindGroup<TexturedTriangleBindGroup>(featureTexture->createView(), featureSampler);
        logStep("createRenderClass<TexturedTrianglePass>()");
        auto texturedTriangle = ctx.device->createRenderClass<TexturedTrianglePass>(texturedBindGroup);
        FeaturePresentTexture presentTexture = createFeaturePresentTexture(ctx);

        bool quitRequested = false;
        for (int frame = 0; frame < frames && !quitRequested; ++frame)
        {
            pumpEvents(quitRequested);
            auto framebuffer = makeOffscreenFramebuffer(presentTexture);
            ctx.device->graphicsQueue(0)->renderPass(
                "FeatureTexturedTrianglePass",
                framebuffer,
                texturedTriangle->setVertexBuffer(vertexBuffer),
                texturedTriangle->setIndexBuffer(indexBuffer),
                texturedTriangle->run(3, 1, 0, 0))->submit();
            presentOffscreenTexture(ctx, presentTexture);
            SDL_Delay(16);
        }
    }

    void renderProcedural(AppContext &ctx, int frames)
    {
        auto featureGlobalsBindGroup = createFeatureGlobalsBindGroup(ctx.device);
        logStep("createRenderClass<ProceduralTrianglePass>()");
        auto proceduralTriangle = ctx.device->createRenderClass<ProceduralTrianglePass>(featureGlobalsBindGroup);
        FeaturePresentTexture presentTexture = createFeaturePresentTexture(ctx);

        bool quitRequested = false;
        for (int frame = 0; frame < frames && !quitRequested; ++frame)
        {
            pumpEvents(quitRequested);
            auto framebuffer = makeOffscreenFramebuffer(presentTexture);
            ctx.device->graphicsQueue(0)->renderPass("FeatureProceduralTrianglePass", framebuffer, proceduralTriangle->run(3, 1, 0, 0))->submit();
            presentOffscreenTexture(ctx, presentTexture);
            SDL_Delay(16);
        }
    }

    void renderViaRenderer(AppContext &ctx, int frames, int renderMode, bool initOnly)
    {
        logStep("makeMyRenderer()");
        auto renderer = makeMyRenderer();
        renderer->setRenderMode(renderMode);

        logStep("renderer->init()");
        renderer->init(ctx.device, ctx.swapchain);
        logStep("renderer->init() completed");

        if (initOnly)
        {
            logStep("init-only mode active; waiting for window close");
            bool quitRequested = false;
            while (!quitRequested)
            {
                pumpEvents(quitRequested);
                SDL_Delay(16);
            }
            renderer->destroy();
            return;
        }

        bool quitRequested = false;
        for (int frame = 0; frame < frames && !quitRequested; ++frame)
        {
            pumpEvents(quitRequested);
            renderer->render();
            SDL_Delay(16);
        }
        renderer->destroy();
    }
} // namespace

int main(int argc, char **argv)
{
    const DebugMode mode = parseModeArg(argc, argv);
    const int frames = parseFrames(argc, argv);
    const bool initOnly = hasFlag(argc, argv, "--init-only");
    const int rendererMode = parseRendererMode(argc, argv);

    AppContext ctx;
    try
    {
        logStep(std::string("Starting mode: ") + modeName(mode));
        initAppContext(ctx);

        switch (mode)
        {
        case DebugMode::Procedural:
            renderProcedural(ctx, frames);
            break;
        case DebugMode::Buffered:
            renderBuffered(ctx, frames);
            break;
        case DebugMode::Textured:
            renderTextured(ctx, frames);
            break;
        case DebugMode::Renderer:
            renderViaRenderer(ctx, frames, rendererMode, initOnly);
            break;
        }
    }
    catch (const std::exception &ex)
    {
        logStep(std::string("fatal error: ") + ex.what());
        destroyAppContext(ctx);
        return 1;
    }
    catch (...)
    {
        logStep("fatal error: unknown exception");
        destroyAppContext(ctx);
        return 1;
    }

    destroyAppContext(ctx);
    return 0;
}
