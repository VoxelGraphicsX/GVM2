#include "VKRenderPipeline.hpp"

#include "VKDevice.hpp"
#include "VKBindGroupLayout.hpp"
#include "VKEnumUtils.hpp"
#include "VKPipelineLayout.hpp"
#include "VKShaderModule.hpp"
#include "VKShaderReflection.hpp"
#include "VKRenderToSwapchainShaders.hpp"
#include "Private/PixelLocalPassAccess.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        bool isStripTopology(PrimitiveTopology topology)
        {
            return topology == PrimitiveTopology::LineStrip || topology == PrimitiveTopology::TriangleStrip;
        }

        /// Resolves the Vulkan color-output ABI for render pipeline creation.
        eastl::vector<ColorTargetState> resolveVulkanPipelineColorTargets(const RenderPipelineDescriptor &descriptor)
        {
            return descriptor.fragment.targets;
        }

        ShaderModule createPixelLocalFullscreenVertexShader(VKDevice &device, const RenderPipelineDescriptor &descriptor)
        {
            const eastl::string label = descriptor.label.empty()
                ? eastl::string("PixelLocalFullscreenVertexShader")
                : descriptor.label + ".PixelLocalFullscreenVertexShader";
            return device.createShaderModule({
                .label = label,
                .spirv = eastl::vector<uint32_t>(
                    Detail::RenderToSwapchainVertexShaderSpirv,
                    Detail::RenderToSwapchainVertexShaderSpirv + Detail::RenderToSwapchainVertexShaderSpirvWordCount),
            });
        }

        /// Owns the transient arrays and state records referenced by a Vulkan graphics pipeline create-info.
        struct GraphicsPipelineCreateInfoStorage
        {
            eastl::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
            eastl::vector<vk::VertexInputBindingDescription> bindingDescriptions;
            eastl::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
            vk::PipelineVertexInputStateCreateInfo vertexInputState = {};
            vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
            vk::PipelineViewportStateCreateInfo viewportState = {};
            vk::PipelineRasterizationStateCreateInfo rasterizationState = {};
            vk::PipelineMultisampleStateCreateInfo multisampleState = {};
            vk::PipelineDepthStencilStateCreateInfo depthStencilState = {};
            eastl::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
            vk::PipelineColorBlendStateCreateInfo colorBlendState = {};
            eastl::vector<vk::DynamicState> dynamicStates;
            vk::PipelineDynamicStateCreateInfo dynamicState = {};
            vk::PipelineTessellationStateCreateInfo tessellationState = {};
            vk::GraphicsPipelineCreateInfo createInfo = {};
        };

        /// Builds one complete Vulkan graphics pipeline create-info from an RHI render pipeline descriptor.
        void buildGraphicsPipelineCreateInfo(
            GraphicsPipelineCreateInfoStorage &storage,
            const RenderPipelineDescriptor &descriptor,
            vk::PipelineLayout nativePipelineLayout,
            vk::RenderPass renderPass,
            uint32_t subpassIndex)
        {
            storage = GraphicsPipelineCreateInfoStorage{};

            const auto &vertexShader = static_cast<const VKShaderModule &>(*descriptor.vertex.module);
            const auto &fragmentShader = static_cast<const VKShaderModule &>(*descriptor.fragment.module);

            storage.shaderStages.reserve(descriptor.tessellation.has_value() ? 4u : 2u);
            storage.shaderStages.push_back(vk::PipelineShaderStageCreateInfo{
                {},
                vk::ShaderStageFlagBits::eVertex,
                vertexShader.getNativeShaderModule(),
                descriptor.vertex.entryPoint.c_str()});

            if (descriptor.tessellation.has_value())
            {
                const auto &tessellationControlShader = static_cast<const VKShaderModule &>(*descriptor.tessellation->control.module);
                const auto &tessellationEvaluationShader = static_cast<const VKShaderModule &>(*descriptor.tessellation->evaluation.module);
                storage.shaderStages.push_back(vk::PipelineShaderStageCreateInfo{
                    {},
                    vk::ShaderStageFlagBits::eTessellationControl,
                    tessellationControlShader.getNativeShaderModule(),
                    descriptor.tessellation->control.entryPoint.c_str()});
                storage.shaderStages.push_back(vk::PipelineShaderStageCreateInfo{
                    {},
                    vk::ShaderStageFlagBits::eTessellationEvaluation,
                    tessellationEvaluationShader.getNativeShaderModule(),
                    descriptor.tessellation->evaluation.entryPoint.c_str()});
            }

            storage.shaderStages.push_back(vk::PipelineShaderStageCreateInfo{
                {},
                vk::ShaderStageFlagBits::eFragment,
                fragmentShader.getNativeShaderModule(),
                descriptor.fragment.entryPoint.c_str()});

            storage.bindingDescriptions.reserve(descriptor.vertex.buffers.size());
            for (uint32_t bindingIndex = 0; bindingIndex < descriptor.vertex.buffers.size(); ++bindingIndex)
            {
                const VertexBufferLayout &bufferLayout = descriptor.vertex.buffers[bindingIndex];
                if (bufferLayout.stepMode == VertexStepMode::VertexBufferNotUsed)
                {
                    continue;
                }

                storage.bindingDescriptions.push_back(vk::VertexInputBindingDescription{
                    bindingIndex,
                    static_cast<uint32_t>(bufferLayout.arrayStride),
                    translateVertexStepMode(bufferLayout.stepMode)});
                for (const VertexAttribute &attribute : bufferLayout.attributes)
                {
                    storage.attributeDescriptions.push_back(translateVertexAttribute(attribute, bindingIndex));
                }
            }

            storage.vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(storage.bindingDescriptions.size());
            storage.vertexInputState.pVertexBindingDescriptions = storage.bindingDescriptions.data();
            storage.vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(storage.attributeDescriptions.size());
            storage.vertexInputState.pVertexAttributeDescriptions = storage.attributeDescriptions.data();

            storage.inputAssemblyState.topology = translatePrimitiveTopology(descriptor.primitive.topology);
            storage.inputAssemblyState.primitiveRestartEnable = isStripTopology(descriptor.primitive.topology) && descriptor.primitive.stripIndexFormat != IndexFormat::Undefined;

            storage.viewportState.viewportCount = 1;
            storage.viewportState.scissorCount = 1;

            storage.rasterizationState.depthClampEnable = vk::False;
            storage.rasterizationState.rasterizerDiscardEnable = vk::False;
            storage.rasterizationState.polygonMode = vk::PolygonMode::eFill;
            storage.rasterizationState.cullMode = translateCullMode(descriptor.primitive.cullMode);
            storage.rasterizationState.frontFace = translateFrontFace(descriptor.primitive.frontFace);
            storage.rasterizationState.depthBiasEnable =
                descriptor.depthStencil.depthBias != 0 ||
                descriptor.depthStencil.depthBiasClamp != 0.0f ||
                descriptor.depthStencil.depthBiasSlopeScale != 0.0f;
            storage.rasterizationState.depthBiasConstantFactor = static_cast<float>(descriptor.depthStencil.depthBias);
            storage.rasterizationState.depthBiasClamp = descriptor.depthStencil.depthBiasClamp;
            storage.rasterizationState.depthBiasSlopeFactor = descriptor.depthStencil.depthBiasSlopeScale;
            storage.rasterizationState.lineWidth = 1.0f;

            storage.multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

            if (descriptor.depthStencil.format != TextureFormat::Undefined)
            {
                storage.depthStencilState.depthTestEnable = descriptor.depthStencil.depthTestEnabled ? vk::True : vk::False;
                storage.depthStencilState.depthWriteEnable = descriptor.depthStencil.depthWriteEnabled ? vk::True : vk::False;
                storage.depthStencilState.depthCompareOp = translateCompareFunction(descriptor.depthStencil.depthCompare);
                storage.depthStencilState.depthBoundsTestEnable = vk::False;
                storage.depthStencilState.stencilTestEnable = hasStencilAspect(descriptor.depthStencil.format) ? vk::True : vk::False;
            }

            const eastl::vector<ColorTargetState> colorTargets = resolveVulkanPipelineColorTargets(descriptor);
            const uint32_t colorBlendAttachmentCount = static_cast<uint32_t>(colorTargets.size());
            storage.colorBlendAttachments.reserve(colorBlendAttachmentCount);
            for (uint32_t targetIndex = 0u; targetIndex < colorBlendAttachmentCount; ++targetIndex)
            {
                const ColorTargetState &target = colorTargets[targetIndex];
                vk::PipelineColorBlendAttachmentState attachment = {};
                attachment.blendEnable = target.blendEnabled ? vk::True : vk::False;
                attachment.srcColorBlendFactor = translateBlendFactor(target.blend.color.srcFactor);
                attachment.dstColorBlendFactor = translateBlendFactor(target.blend.color.dstFactor);
                attachment.colorBlendOp = translateBlendOperation(target.blend.color.operation);
                attachment.srcAlphaBlendFactor = translateBlendFactor(target.blend.alpha.srcFactor);
                attachment.dstAlphaBlendFactor = translateBlendFactor(target.blend.alpha.dstFactor);
                attachment.alphaBlendOp = translateBlendOperation(target.blend.alpha.operation);
                attachment.colorWriteMask = translateColorWriteMask(target.writeMask);
                storage.colorBlendAttachments.push_back(attachment);
            }

            storage.colorBlendState.attachmentCount = static_cast<uint32_t>(storage.colorBlendAttachments.size());
            storage.colorBlendState.pAttachments = storage.colorBlendAttachments.data();

            storage.dynamicStates.push_back(vk::DynamicState::eViewport);
            storage.dynamicStates.push_back(vk::DynamicState::eScissor);
            storage.dynamicState.dynamicStateCount = static_cast<uint32_t>(storage.dynamicStates.size());
            storage.dynamicState.pDynamicStates = storage.dynamicStates.data();

            if (descriptor.tessellation.has_value())
            {
                storage.tessellationState.patchControlPoints = descriptor.tessellation->patchControlPoints;
            }

            storage.createInfo.stageCount = static_cast<uint32_t>(storage.shaderStages.size());
            storage.createInfo.pStages = storage.shaderStages.data();
            storage.createInfo.pVertexInputState = &storage.vertexInputState;
            storage.createInfo.pInputAssemblyState = &storage.inputAssemblyState;
            storage.createInfo.pViewportState = &storage.viewportState;
            storage.createInfo.pRasterizationState = &storage.rasterizationState;
            storage.createInfo.pMultisampleState = &storage.multisampleState;
            storage.createInfo.pDepthStencilState = descriptor.depthStencil.format == TextureFormat::Undefined ? nullptr : &storage.depthStencilState;
            storage.createInfo.pColorBlendState = &storage.colorBlendState;
            storage.createInfo.pDynamicState = &storage.dynamicState;
            storage.createInfo.pTessellationState = descriptor.tessellation.has_value() ? &storage.tessellationState : nullptr;
            storage.createInfo.layout = nativePipelineLayout;
            storage.createInfo.renderPass = renderPass;
            storage.createInfo.subpass = subpassIndex;
        }

    } // namespace

    void VKRenderPipeline::init(VKDevice &device, const RenderPipelineDescriptor &descriptor)
    {
        if (descriptor.layout == nullptr)
        {
            throw makeInvalidArgument("VKRenderPipeline::init requires a valid pipeline layout.");
        }
        if (descriptor.fragment.module == nullptr)
        {
            throw makeInvalidArgument("VKRenderPipeline::init currently requires a fragment shader.");
        }
        if (!isStripTopology(descriptor.primitive.topology) && descriptor.primitive.stripIndexFormat != IndexFormat::Undefined)
        {
            throw makeInvalidArgument("VKRenderPipeline::init received stripIndexFormat for a non-strip topology.");
        }

        RenderPipelineDescriptor effectiveDescriptor = descriptor;
        if (effectiveDescriptor.vertex.module == nullptr)
        {
            if (!GVM::RHI::Private::isPixelLocalRenderPipelineDescriptor(effectiveDescriptor))
            {
                throw makeInvalidArgument("VKRenderPipeline::init requires a valid vertex shader for non-pixelLocal pipelines.");
            }
            effectiveDescriptor.vertex.module = createPixelLocalFullscreenVertexShader(device, descriptor);
            effectiveDescriptor.vertex.entryPoint = "vertexMain";
        }
        if (GVM::RHI::Private::isPixelLocalRenderPipelineDescriptor(effectiveDescriptor))
        {
            effectiveDescriptor.fragment.targets = resolveVulkanPipelineColorTargets(effectiveDescriptor);
        }

        const auto &pipelineLayout = static_cast<const VKPipelineLayout &>(*effectiveDescriptor.layout);
        const auto &vertexShader = static_cast<const VKShaderModule &>(*effectiveDescriptor.vertex.module);
        const Detail::ReflectedEntryPoint &vertexEntryPoint = Detail::requireShaderEntryPoint(
            vertexShader.getReflection(),
            effectiveDescriptor.vertex.entryPoint,
            vk::ShaderStageFlagBits::eVertex,
            "VKRenderPipeline::init");
        Detail::validatePipelineLayoutAgainstEntryPoint(pipelineLayout, vertexEntryPoint, "VKRenderPipeline::init");
        Detail::validateVertexInputsAgainstEntryPoint(effectiveDescriptor.vertex, vertexEntryPoint, "VKRenderPipeline::init");

        const auto &fragmentShader = static_cast<const VKShaderModule &>(*effectiveDescriptor.fragment.module);
        const Detail::ReflectedEntryPoint &fragmentEntryPoint = Detail::requireShaderEntryPoint(
            fragmentShader.getReflection(),
            effectiveDescriptor.fragment.entryPoint,
            vk::ShaderStageFlagBits::eFragment,
            "VKRenderPipeline::init");
        Detail::validatePipelineLayoutAgainstEntryPoint(pipelineLayout, fragmentEntryPoint, "VKRenderPipeline::init");
        Detail::validateFragmentTargetsAgainstEntryPoint(effectiveDescriptor.fragment, fragmentEntryPoint, "VKRenderPipeline::init");

        mDevice = &device;
        mDescriptor = effectiveDescriptor;
        mLabelName = effectiveDescriptor.label;
        mPipelineLayout = effectiveDescriptor.layout;
        initPixelLocalInputLayout(fragmentEntryPoint);

        if (effectiveDescriptor.tessellation.has_value())
        {
            if (effectiveDescriptor.tessellation->control.module == nullptr ||
                effectiveDescriptor.tessellation->evaluation.module == nullptr)
            {
                throw makeInvalidArgument("VKRenderPipeline::init requires both tessellation stages when tessellation is enabled.");
            }

            const auto &tessellationControlShader = static_cast<const VKShaderModule &>(*effectiveDescriptor.tessellation->control.module);
            const Detail::ReflectedEntryPoint &tessellationControlEntryPoint = Detail::requireShaderEntryPoint(
                tessellationControlShader.getReflection(),
                effectiveDescriptor.tessellation->control.entryPoint,
                vk::ShaderStageFlagBits::eTessellationControl,
                "VKRenderPipeline::init");
            Detail::validatePipelineLayoutAgainstEntryPoint(pipelineLayout, tessellationControlEntryPoint, "VKRenderPipeline::init");

            const auto &tessellationEvaluationShader = static_cast<const VKShaderModule &>(*effectiveDescriptor.tessellation->evaluation.module);
            const Detail::ReflectedEntryPoint &tessellationEvaluationEntryPoint = Detail::requireShaderEntryPoint(
                tessellationEvaluationShader.getReflection(),
                effectiveDescriptor.tessellation->evaluation.entryPoint,
                vk::ShaderStageFlagBits::eTessellationEvaluation,
                "VKRenderPipeline::init");
            Detail::validatePipelineLayoutAgainstEntryPoint(pipelineLayout, tessellationEvaluationEntryPoint, "VKRenderPipeline::init");
            Detail::validateStageInterface(tessellationEvaluationEntryPoint, fragmentEntryPoint, "VKRenderPipeline::init");
        }
        else
        {
            Detail::validateStageInterface(vertexEntryPoint, fragmentEntryPoint, "VKRenderPipeline::init");
        }

        if (!usesPixelLocalInputAttachments())
        {
            GraphicsPipelineCreateInfoStorage createInfoStorage;
            buildGraphicsPipelineCreateInfo(
                createInfoStorage,
                effectiveDescriptor,
                pipelineLayout.getNativePipelineLayout(),
                device.getOrCreatePipelineRenderPass(effectiveDescriptor),
                0u);
            mPipeline = device.getNativeDevice().createGraphicsPipelineUnique(nullptr, createInfoStorage.createInfo).value;
        }
    }

    void VKRenderPipeline::initPixelLocalInputLayout(const Detail::ReflectedEntryPoint &fragmentEntryPoint)
    {
        mPixelLocalInputBindings.clear();
        mPixelLocalInputDescriptorPoolSizes.clear();
        mPixelLocalInputDescriptorSetIndex = 0u;
        mPixelLocalInputDescriptorSetLayout.reset();
        mPixelLocalPipelineLayout.reset();

        bool hasInputDescriptorSet = false;
        uint32_t inputDescriptorSet = 0u;
        for (const Detail::ReflectedDescriptorBinding &binding : fragmentEntryPoint.descriptorBindings)
        {
            if (binding.descriptorType != vk::DescriptorType::eInputAttachment)
            {
                continue;
            }
            if (!hasInputDescriptorSet)
            {
                inputDescriptorSet = binding.set;
                hasInputDescriptorSet = true;
            }
            else if (inputDescriptorSet != binding.set)
            {
                throw makeInvalidArgument("VKRenderPipeline::initPixelLocalInputLayout requires all pixelLocal input attachments to use one internal descriptor set.");
            }
            mPixelLocalInputBindings.push_back(PixelLocalInputBinding{
                .binding = binding.binding,
                .inputAttachmentIndex = binding.inputAttachmentIndex,
            });
        }

        if (mPixelLocalInputBindings.empty())
        {
            return;
        }

        const auto &pipelineLayout = static_cast<const VKPipelineLayout &>(*mPipelineLayout);
        const uint32_t expectedInputDescriptorSet = pipelineLayout.getBindGroupLayoutCount();
        if (inputDescriptorSet != expectedInputDescriptorSet)
        {
            throw makeInvalidArgument(eastl::string("VKRenderPipeline::initPixelLocalInputLayout expected pixelLocal input attachments to use descriptor set ") + eastl::to_string(expectedInputDescriptorSet) + " after all user bind groups, but the shader uses set " + eastl::to_string(inputDescriptorSet) + ".");
        }
        mPixelLocalInputDescriptorSetIndex = inputDescriptorSet;

        eastl::vector<vk::DescriptorSetLayoutBinding> nativeBindings;
        nativeBindings.reserve(mPixelLocalInputBindings.size());
        for (const PixelLocalInputBinding &binding : mPixelLocalInputBindings)
        {
            nativeBindings.push_back(vk::DescriptorSetLayoutBinding{
                binding.binding,
                vk::DescriptorType::eInputAttachment,
                1u,
                vk::ShaderStageFlagBits::eFragment,
                nullptr});
        }

        vk::DescriptorSetLayoutCreateInfo inputLayoutCreateInfo = {};
        inputLayoutCreateInfo.bindingCount = static_cast<uint32_t>(nativeBindings.size());
        inputLayoutCreateInfo.pBindings = nativeBindings.data();
        mPixelLocalInputDescriptorSetLayout = mDevice->getNativeDevice().createDescriptorSetLayoutUnique(inputLayoutCreateInfo);
        mPixelLocalInputDescriptorPoolSizes.push_back(vk::DescriptorPoolSize{
            vk::DescriptorType::eInputAttachment,
            static_cast<uint32_t>(mPixelLocalInputBindings.size())});

        eastl::vector<vk::DescriptorSetLayout> setLayouts;
        setLayouts.reserve(pipelineLayout.getBindGroupLayoutCount() + 1u);
        for (uint32_t index = 0; index < pipelineLayout.getBindGroupLayoutCount(); ++index)
        {
            const BindGroupLayout &layoutHandle = pipelineLayout.getBindGroupLayoutHandle(index);
            if (layoutHandle == nullptr)
            {
                if (!mPixelLocalEmptyDescriptorSetLayout)
                {
                    vk::DescriptorSetLayoutCreateInfo emptyLayoutCreateInfo = {};
                    mPixelLocalEmptyDescriptorSetLayout = mDevice->getNativeDevice().createDescriptorSetLayoutUnique(emptyLayoutCreateInfo);
                }
                setLayouts.push_back(mPixelLocalEmptyDescriptorSetLayout.get());
                continue;
            }
            setLayouts.push_back(static_cast<const VKBindGroupLayout &>(*layoutHandle).getNativeDescriptorSetLayout());
        }
        setLayouts.push_back(mPixelLocalInputDescriptorSetLayout.get());

        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutCreateInfo.pSetLayouts = setLayouts.data();
        mPixelLocalPipelineLayout = mDevice->getNativeDevice().createPipelineLayoutUnique(pipelineLayoutCreateInfo);
    }

    vk::UniquePipeline VKRenderPipeline::createNativePipelineForRenderPassSubpass(vk::RenderPass renderPass, uint32_t subpassIndex)
    {
        if (mDevice == nullptr || renderPass == vk::RenderPass{})
        {
            throw makeInvalidArgument("VKRenderPipeline::createNativePipelineForRenderPassSubpass requires a valid device and render pass.");
        }

        const RenderPipelineDescriptor &descriptor = mDescriptor;
        vk::PipelineLayout nativePipelineLayout = mPixelLocalPipelineLayout.get();
        if (!usesPixelLocalInputAttachments())
        {
            nativePipelineLayout = static_cast<const VKPipelineLayout &>(*mPipelineLayout).getNativePipelineLayout();
        }

        GraphicsPipelineCreateInfoStorage createInfoStorage;
        buildGraphicsPipelineCreateInfo(
            createInfoStorage,
            descriptor,
            nativePipelineLayout,
            renderPass,
            subpassIndex);
        return mDevice->getNativeDevice().createGraphicsPipelineUnique(nullptr, createInfoStorage.createInfo).value;
    }

    vk::Pipeline VKRenderPipeline::getNativePipeline() const
    {
        return mPipeline.get();
    }

    vk::Pipeline VKRenderPipeline::getNativePipelineForRenderPassSubpass(vk::RenderPass renderPass, uint32_t subpassIndex)
    {
        if (renderPass == vk::RenderPass{})
        {
            throw makeInvalidArgument("VKRenderPipeline::getNativePipelineForRenderPassSubpass requires a valid render pass.");
        }

        for (NativePipelineCacheEntry &cached : mNativePipelineCacheByRenderPassSubpass)
        {
            if (cached.renderPass == renderPass && cached.subpassIndex == subpassIndex)
            {
                return cached.pipeline.get();
            }
        }

        NativePipelineCacheEntry cached = {};
        cached.renderPass = renderPass;
        cached.subpassIndex = subpassIndex;
        cached.pipeline = createNativePipelineForRenderPassSubpass(renderPass, subpassIndex);
        mNativePipelineCacheByRenderPassSubpass.push_back(eastl::move(cached));
        return mNativePipelineCacheByRenderPassSubpass.back().pipeline.get();
    }

    vk::PipelineLayout VKRenderPipeline::getNativePipelineLayout() const
    {
        if (usesPixelLocalInputAttachments())
        {
            return mPixelLocalPipelineLayout.get();
        }
        return static_cast<const VKPipelineLayout &>(*mPipelineLayout).getNativePipelineLayout();
    }

    bool VKRenderPipeline::usesPixelLocalInputAttachments() const
    {
        return !mPixelLocalInputBindings.empty();
    }

    vk::DescriptorSetLayout VKRenderPipeline::getPixelLocalInputDescriptorSetLayout() const
    {
        return mPixelLocalInputDescriptorSetLayout.get();
    }

    uint32_t VKRenderPipeline::getPixelLocalInputDescriptorSetIndex() const
    {
        return mPixelLocalInputDescriptorSetIndex;
    }

    const eastl::vector<vk::DescriptorPoolSize> &VKRenderPipeline::getPixelLocalInputDescriptorPoolSizes() const
    {
        return mPixelLocalInputDescriptorPoolSizes;
    }

    const eastl::vector<VKRenderPipeline::PixelLocalInputBinding> &VKRenderPipeline::getPixelLocalInputBindings() const
    {
        return mPixelLocalInputBindings;
    }

    const PipelineLayout &VKRenderPipeline::getPipelineLayoutHandle() const
    {
        return mPipelineLayout;
    }

    const RenderPipelineDescriptor &VKRenderPipeline::getDescriptor() const
    {
        return mDescriptor;
    }

    size_t VKRenderPipeline::getNativePipelineCacheSizeForTesting() const
    {
        return mNativePipelineCacheByRenderPassSubpass.size();
    }
} // namespace GVM::RHI::Vulkan
