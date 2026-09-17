#pragma once

namespace GVM::RHI::Vulkan
{
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
    class VKUploadAllocatorSlab;
} // namespace GVM::RHI::Vulkan
