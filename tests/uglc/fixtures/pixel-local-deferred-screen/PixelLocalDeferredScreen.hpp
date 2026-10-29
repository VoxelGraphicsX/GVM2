#ifndef UGLC_TEST_PIXEL_LOCAL_DEFERRED_SCREEN_HPP
#define UGLC_TEST_PIXEL_LOCAL_DEFERRED_SCREEN_HPP

#include "UGL.h"

using namespace UGL;

struct PixelLocalDeferredVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

struct PixelLocalDeferredFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;

    PixelLocalColorAttachment<TextureFormat::R32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        gbufferDepth;

    PixelLocalColorAttachment<TextureFormat::RGBA16Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::DontCare,
                              PixelLocalStore::Discard>
        lighting;

    ColorAttachment<TextureFormat::BGRA8Unorm> present;

    PixelLocalDepthAttachment<TextureFormat::Depth32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        depth;
};

class PixelLocalGBufferPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    PixelLocalDeferredVertexOutput vertex(uint vertexID [[VertexID]])
    {
        PixelLocalDeferredVertexOutput outputValue;
        float2 p;
        if (vertexID == 0u)
        {
            p = float2(-1.0f, -1.0f);
        }
        else if (vertexID == 1u)
        {
            p = float2(3.0f, -1.0f);
        }
        else
        {
            p = float2(-1.0f, 3.0f);
        }
        float2 uv = p * 0.5f + float2(0.5f, 0.5f);
        outputValue.position = float4(p, 0.25f + uv.x * 0.5f, 1.0f);
        outputValue.uv = uv;
        return outputValue;
    }

    PixelLocalDeferredFrame fragment(PixelLocalDeferredVertexOutput inputValue)
    {
        PixelLocalDeferredFrame outputValue;
        outputValue.albedo = half4(inputValue.uv.x, inputValue.uv.y, 0.25f, 1.0f);
        outputValue.gbufferDepth = 0.25f + inputValue.uv.x * 0.5f;
        return outputValue;
    }
};

class PixelLocalLightingPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    PixelLocalDeferredFrame pixel(PixelLocalDeferredFrame inputValue [[PixelLocalInput]])
    {
        PixelLocalDeferredFrame outputValue;
        half4 albedoValue = inputValue.albedo.read();
        float gbufferDepth = inputValue.gbufferDepth.read();
        half depthLight = half(saturate(gbufferDepth));
        half lightValue = half(0.15f) + albedoValue.x * half(0.45f) + albedoValue.y * half(0.15f) + depthLight * half(0.25f);
        outputValue.lighting = half4(albedoValue.x * lightValue + half(0.04f),
                                     albedoValue.y * lightValue + half(0.05f),
                                     albedoValue.z * lightValue + half(0.08f),
                                     1.0f);
        return outputValue;
    }
};

class PixelLocalTonemapPass final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    PixelLocalDeferredFrame pixel(PixelLocalDeferredFrame inputValue [[PixelLocalInput]])
    {
        PixelLocalDeferredFrame outputValue;
        half4 lightingValue = inputValue.lighting.read();
        outputValue.present = half4(lightingValue.x / (lightingValue.x + half(1.0f)),
                                    lightingValue.y / (lightingValue.y + half(1.0f)),
                                    lightingValue.z / (lightingValue.z + half(1.0f)),
                                    1.0f);
        return outputValue;
    }
};

class PixelLocalDeferredScreenRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue queue;
    PixelLocalDeferredFrame deferredFrame;
    RenderClass<PixelLocalGBufferPass> gbufferPass;
    RenderClass<PixelLocalLightingPass> lightingPass;
    RenderClass<PixelLocalTonemapPass> tonemapPass;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        queue = device->graphicsQueue(0);
        gbufferPass = device->createRenderClass<PixelLocalGBufferPass>();
        lightingPass = device->createRenderClass<PixelLocalLightingPass>();
        tonemapPass = device->createRenderClass<PixelLocalTonemapPass>();
    }

    void render() override
    {
        queue->renderPass("PixelLocalDeferredScreen",
                          deferredFrame,
                          pixelLocalPass(gbufferPass(3u, 1u, 0u, 0u),
                                         nextPixelLocalPass(),
                                         lightingPass(),
                                         nextPixelLocalPass(),
                                         tonemapPass()));
    }

    void destroy() override
    {
    }
};

#endif
