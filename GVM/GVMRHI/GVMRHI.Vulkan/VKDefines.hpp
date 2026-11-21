#pragma once

#include <cstdint>

namespace GVM::RHI::Vulkan
{
    /// Bounds backend timeline storage for the main, compute, and transfer queue selections.
    constexpr uint32_t MaxDeviceQueues = 3;

    class VKInstance;
    class VKDevice;
    class VKQueue;
    class VKQuerySet;
    class VKSwapchain;
    class VKCommandEncoder;
    struct VKCommandBufferContext;
    class VKTaskDependencyResolver;
    class VKBlitPassEncoder;
    class VKComputePassEncoder;
    class VKRenderPassEncoder;
    class VKBuffer;
    class VKTexture;
    class VKTextureView;
    class VKSampler;
    class VKShaderModule;
    class VKBindGroupLayout;
    class VKBindGroup;
    class VKResourceStateDB;
    class VKPresentManager;
    class VKRetireManager;
    class VKPipelineLayout;
    class VKComputePipeline;
    class VKRenderPipeline;
    class VKTransientDescriptorAllocator;
} // namespace GVM::RHI::Vulkan
