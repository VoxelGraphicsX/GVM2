#include "UGLBin/generate_result.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <SDL.h>
#include <chrono>
#include <type_traits>
static constexpr double targetFrameTime = 1000.0 / 200.0;
double getCurrentTimeMillis()
{
    // 获取当前时间点
    auto now = std::chrono::high_resolution_clock::now();

    // 将时间点转换为自纪元以来的毫秒数
    auto milliseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    // 转换为double类型
    return static_cast<double>(milliseconds) * 0.001;
}

class APP
{
    GVM::RHI::Instance instance;
    GVM::RHI::Device device;
    GVM::RHI::Swapchain swapchain;
    MyRenderer renderer = makeMyRenderer();

protected:
    auto createDefaultDevice()
    {

        instance = GVM::RHI::createInstance({
            .diagnosticsOverlay = {.enabled = GVM::RHI::True},
        });
        device = instance->createDevice();
        return device;
    }

    auto getDefaultSwapchain()
    {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_Window *window = SDL_CreateWindow("SDL Metal", -1, -1, 640, 480, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
        SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        swapchain = instance->createSwapchain({.A = SDL_RenderGetMetalLayer(renderer)});
        return swapchain;
    }


public:
    void update()
    {
        renderer->init(createDefaultDevice(), getDefaultSwapchain());
        bool isQuited = false;
        static bool isLeftMouseBtnDown = false;
        static double lastTicks = getCurrentTimeMillis();

        while (!isQuited)
        {
            const auto nowTicks = getCurrentTimeMillis();

            SDL_Event e;
            int mouseX = 0;
            int mouseY = 0;
            int cameraMove = 0;
            while (SDL_PollEvent(&e) != 0)
            {
                if (e.type == SDL_QUIT)
                {
                    isQuited = true;
                }
                else if (e.type == SDL_KEYDOWN)
                {
                    // 处理键盘按下事件

                    switch (e.key.keysym.sym)
                    {
                    case SDLK_w:
                        cameraMove |= 1;
                        break;
                    case SDLK_a:
                        cameraMove |= 4;
                        break;
                    case SDLK_s:
                        cameraMove |= 2;
                        break;
                    case SDLK_d:
                        cameraMove |= 8;
                        break;
                    }
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN)
                {
                    // 处理鼠标按钮按下事件
                    switch (e.button.button)
                    {
                    case SDL_BUTTON_LEFT:
                        // 处理鼠标左键
                        isLeftMouseBtnDown = true;
                        break;
                    case SDL_BUTTON_RIGHT:
                        // 处理鼠标右键
                        break;
                        // 添加更多鼠标按钮处理
                    }
                }
                else if (e.type == SDL_MOUSEBUTTONUP)
                {
                    // 处理鼠标按钮按下事件
                    switch (e.button.button)
                    {
                    case SDL_BUTTON_LEFT:
                        // 处理鼠标左键
                        isLeftMouseBtnDown = false;
                        break;
                    case SDL_BUTTON_RIGHT:
                        // 处理鼠标右键
                        break;
                        // 添加更多鼠标按钮处理
                    }
                }
                else if (e.type == SDL_MOUSEMOTION)
                {
                    mouseX = e.motion.x;
                    mouseY = e.motion.y;
                }
            }

            renderer->setMouseLeftClick(isLeftMouseBtnDown ? 1 : 0);

            if (mouseX != 0 && mouseY != 0)
            {
                renderer->setMouseMoveX(mouseX);
                renderer->setMouseMoveY(mouseY);
            }

            renderer->setCameraMove(cameraMove);
            renderer->setDurationTicks((nowTicks - lastTicks));

            renderer->render();
            const double frameTime = getCurrentTimeMillis() - nowTicks;
            if (frameTime < targetFrameTime)
            {
                SDL_Delay((targetFrameTime - frameTime));
            }

            lastTicks = nowTicks;
        }
    }
};

int main()
{
    APP app;
    app.update();
}
