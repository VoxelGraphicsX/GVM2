#pragma once

#include "VKCommon.hpp"

#include <cstdint>

namespace GVM::RHI::Vulkan::Detail
{
    uint64_t hashShaderModuleDescriptor(const ShaderModuleDescriptor &descriptor);
    bool equalShaderModuleDescriptor(const ShaderModuleDescriptor &lhs, const ShaderModuleDescriptor &rhs);

    uint64_t hashBindGroupLayoutDescriptor(const BindGroupLayoutDescriptor &descriptor);
    bool equalBindGroupLayoutDescriptor(const BindGroupLayoutDescriptor &lhs, const BindGroupLayoutDescriptor &rhs);

    uint64_t hashPipelineLayoutDescriptor(const PipelineLayoutDescriptor &descriptor);
    bool equalPipelineLayoutDescriptor(const PipelineLayoutDescriptor &lhs, const PipelineLayoutDescriptor &rhs);

    uint64_t hashBindGroupDescriptor(const BindGroupDescriptor &descriptor);

    uint64_t hashComputePipelineDescriptor(const ComputePipelineDescriptor &descriptor);
    bool equalComputePipelineDescriptor(const ComputePipelineDescriptor &lhs, const ComputePipelineDescriptor &rhs);

    uint64_t hashRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor);
    bool equalRenderPipelineDescriptor(const RenderPipelineDescriptor &lhs, const RenderPipelineDescriptor &rhs);
} // namespace GVM::RHI::Vulkan::Detail
