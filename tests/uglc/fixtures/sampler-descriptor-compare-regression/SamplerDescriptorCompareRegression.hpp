#ifndef UGLC_TEST_SAMPLER_DESCRIPTOR_COMPARE_REGRESSION_HPP
#define UGLC_TEST_SAMPLER_DESCRIPTOR_COMPARE_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

class SamplerDescriptorCompareRegressionRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Sampler samplerFromDescriptor;
    Sampler samplerFromAggregate;

public:
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;

        SamplerDescriptor descriptor = {};
        descriptor.label = "SamplerFromDescriptor";
        descriptor.addressModeU = AddressMode::ClampToEdge;
        descriptor.addressModeV = AddressMode::ClampToEdge;
        descriptor.addressModeW = AddressMode::ClampToEdge;
        descriptor.magFilter = FilterMode::Linear;
        descriptor.minFilter = FilterMode::Linear;
        descriptor.mipmapFilter = MipmapFilterMode::Linear;
        descriptor.lodMinClamp = 0.0;
        descriptor.lodMaxClamp = 4.0;
        descriptor.compare = CompareFunction::LessEqual;
        descriptor.maxAnisotropy = 2;
        samplerFromDescriptor = device->createSampler(descriptor);

        samplerFromAggregate = device->createSampler({
            .label = "SamplerFromAggregate",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Linear,
            .lodMinClamp = 0.0,
            .lodMaxClamp = 8.0,
            .compare = CompareFunction::GreaterEqual,
            .maxAnisotropy = 1
        });
    }

    void render() override
    {
    }

    void destroy() override
    {
    }
};

#endif
