#ifndef UGLC_TEST_INVALID_PIXEL_LOCAL_MISSING_NEXT_PASS_HPP
#define UGLC_TEST_INVALID_PIXEL_LOCAL_MISSING_NEXT_PASS_HPP

#include "UGL.h"

using namespace UGL;

struct MissingNextPassVertexOutput
{
    float4 position [[Position]];
};

struct MissingNextPassFrame final : public IFrameBuffer
{
    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        albedo;

    PixelLocalColorAttachment<TextureFormat::RGBA8Unorm,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::DontCare,
                              PixelLocalStore::Discard>
        lighting;

    PixelLocalDepthAttachment<TextureFormat::Depth32Float,
                              PixelLocalAccess::ReadWrite,
                              PixelLocalStorage::Transient,
                              PixelLocalLoad::Clear,
                              PixelLocalStore::Discard>
        depth;
};

class MissingNextPassGBuffer final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    MissingNextPassVertexOutput vertex(uint vertexID [[VertexID]])
    {
        MissingNextPassVertexOutput outputValue;
        outputValue.position = vertexID == 0u ? float4(-1.0f, -1.0f, 0.5f, 1.0f)
                                              : vertexID == 1u ? float4(3.0f, -1.0f, 0.5f, 1.0f)
                                                               : float4(-1.0f, 3.0f, 0.5f, 1.0f);
        return outputValue;
    }

    MissingNextPassFrame fragment(MissingNextPassVertexOutput inputValue)
    {
        MissingNextPassFrame outputValue;
        outputValue.albedo = half4(0.25f, 0.50f, 0.75f, 1.0f);
        return outputValue;
    }
};

class MissingNextPassLighting final : public IPixelLocalRenderClass
{
public:
    constructor()
    {
    }

private:
    MissingNextPassFrame pixel(MissingNextPassFrame inputValue [[PixelLocalInput]])
    {
        MissingNextPassFrame outputValue;
        half4 albedoValue = inputValue.albedo.read();
        outputValue.lighting = half4(albedoValue.xyz, 1.0f);
        return outputValue;
    }
};

class InvalidPixelLocalMissingNextPassRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue queue;
    MissingNextPassFrame frame;
    RenderClass<MissingNextPassGBuffer> gbufferPass;
    RenderClass<MissingNextPassLighting> lightingPass;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        queue = device->graphicsQueue(0);
        gbufferPass = device->createRenderClass<MissingNextPassGBuffer>();
        lightingPass = device->createRenderClass<MissingNextPassLighting>();
    }

    void render() override
    {
        auto invalidPixelLocalPhase = pixelLocalPass(gbufferPass(3u, 1u, 0u, 0u),
                                                     lightingPass());
        queue->renderPass("InvalidPixelLocalMissingNextPass",
                          frame,
                          invalidPixelLocalPhase);
    }

    void destroy() override
    {
    }
};

#endif
