#ifndef UGLC_TEST_RESOURCE_USAGE_RESET_HPP
#define UGLC_TEST_RESOURCE_USAGE_RESET_HPP

#include "UGL.h"

using namespace UGL;

class ResourceUsageResetRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;

    Buffer<float4, BufferUsage<Vertex, CopyDst>> vertexBuffer;
    Buffer<uint, BufferUsage<Uniform>> uniformBuffer;

    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> sampledTexture;
    Texture<TextureFormat::R32Float, TextureUsage<StorageBinding>, TextureDimension::e2D> storageTexture;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;

        vertexBuffer = device->createBuffer("VertexBuffer", 4);
        uniformBuffer = device->createBuffer("UniformBuffer", 1);

        sampledTexture = device->createTexture("SampledTexture", 64, 64, 1);
        storageTexture = device->createTexture("StorageTexture", 32, 32, 1);
    }

    void render() override
    {
    }

    void destroy() override
    {
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeTexture(sampledTexture);
        device->freeTexture(storageTexture);
    }
};

#endif
