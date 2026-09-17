#include "VKRenderToSwapchainExecutor.hpp"

#include "VKDevice.hpp"
#include "VKRenderPassEncoder.hpp"
#include "VKRenderToSwapchainShaders.hpp"

namespace GVM::RHI::Vulkan
{
    namespace
    {
        constexpr size_t kMaxCachedRenderToSwapchainBindGroups = 3u;
    }

    void VKRenderToSwapchainExecutorImpl::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw makeInvalidArgument("VKRenderToSwapchainExecutorImpl::init requires a valid device.");
        }

        mDevice = device;
        mSampler = mDevice->createSampler({
            .label = "RenderToSwapchainSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .compare = CompareFunction::Undefined,
            .maxAnisotropy = 0,
        });

        mBindGroupLayout = mDevice->createBindGroupLayout({
            .label = "RenderToSwapchainBindGroupLayout",
            .entries = {
                {
                    .binding = 0,
                    .visibility = ShaderStage::Fragment,
                    .texture = {
                        .sampleType = TextureSampleType::Float,
                        .viewDimension = TextureViewDimension::e2D,
                    },
                },
                {
                    .binding = 1,
                    .visibility = ShaderStage::Fragment,
                    .sampler = {
                        .type = SamplerBindingType::Filtering,
                    },
                },
            },
        });

        mPipelineLayout = mDevice->createPipelineLayout({
            .label = "RenderToSwapchainPipelineLayout",
            .bindGroupLayouts = {mBindGroupLayout},
        });

        mVertexShader = mDevice->createShaderModule({
            .label = "RenderToSwapchainVertexShader",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainVertexShaderSpirv,
                Detail::RenderToSwapchainVertexShaderSpirv + Detail::RenderToSwapchainVertexShaderSpirvWordCount),
        });
        mFragmentShader = mDevice->createShaderModule({
            .label = "RenderToSwapchainFragmentShader",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainFragmentShaderSpirv,
                Detail::RenderToSwapchainFragmentShaderSpirv + Detail::RenderToSwapchainFragmentShaderSpirvWordCount),
        });
        mFlippedFragmentShader = mDevice->createShaderModule({
            .label = "RenderToSwapchainFragmentShaderFlipped",
            .spirv = eastl::vector<uint32_t>(
                Detail::RenderToSwapchainFragmentFlippedShaderSpirv,
                Detail::RenderToSwapchainFragmentFlippedShaderSpirv + Detail::RenderToSwapchainFragmentFlippedShaderSpirvWordCount),
        });
    }

    RenderPipeline VKRenderToSwapchainExecutorImpl::getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &renderDescriptor)
    {
        const bool flipYAxis = renderDescriptor.flipYAxis != False;
        const uint64_t pipelineKey = static_cast<uint64_t>(static_cast<uint32_t>(targetFormat)) | (static_cast<uint64_t>(flipYAxis) << 32u);
        if (const auto it = mPipelines.find(pipelineKey); it != mPipelines.end())
        {
            return it->second;
        }

        RenderPipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor.label = flipYAxis ? "RenderToSwapchainPipelineFlipped" : "RenderToSwapchainPipeline";
        pipelineDescriptor.layout = mPipelineLayout;
        pipelineDescriptor.vertex = {
            .module = mVertexShader,
            .entryPoint = "vertexMain",
        };
        pipelineDescriptor.primitive = {
            .topology = PrimitiveTopology::TriangleList,
            .stripIndexFormat = IndexFormat::Undefined,
            .frontFace = FrontFace::CW,
            .cullMode = CullMode::None,
        };
        pipelineDescriptor.depthStencil = {};
        pipelineDescriptor.fragment = {
            .module = flipYAxis ? mFlippedFragmentShader : mFragmentShader,
            .entryPoint = flipYAxis ? "fragmentMainFlipped" : "fragmentMain",
            .targets = {
                {
                    .format = targetFormat,
                    .blend = {},
                    .blendEnabled = False,
                    .writeMask = ColorWriteMask::All,
                },
            },
        };

        RenderPipeline pipeline = mDevice->createRenderPipeline(pipelineDescriptor);
        mPipelines.emplace(pipelineKey, pipeline);
        return pipeline;
    }

    BindGroup VKRenderToSwapchainExecutorImpl::getOrCreateBindGroup(Texture sourceTexture)
    {
        const TextureView sourceView = sourceTexture->createView();
        for (const CachedBindGroupEntry &entry : mCachedBindGroups)
        {
            if (entry.sourceView.get() == sourceView.get())
            {
                return entry.bindGroup;
            }
        }

        BindGroup bindGroup = mDevice->createBindGroup({
            .label = "RenderToSwapchainBindGroup",
            .layout = mBindGroupLayout,
            .entries = {
                {
                    .binding = 0,
                    .textureView = {sourceView},
                },
                {
                    .binding = 1,
                    .sampler = {mSampler},
                },
            },
        });

        if (mCachedBindGroups.size() >= kMaxCachedRenderToSwapchainBindGroups)
        {
            mCachedBindGroups.erase(mCachedBindGroups.begin());
        }
        mCachedBindGroups.push_back(CachedBindGroupEntry{
            .sourceView = sourceView,
            .bindGroup = bindGroup,
        });
        return bindGroup;
    }

    void VKRenderToSwapchainExecutorImpl::encode(VKRenderPassEncoder *encoder,
                                                 Texture sourceTexture,
                                                 TextureFormat targetFormat,
                                                 const RenderToSwapchainDescriptor &descriptor)
    {
        if (encoder == nullptr)
        {
            throw makeInvalidArgument("VKRenderToSwapchainExecutorImpl::encode requires a valid render pass encoder.");
        }
        if (sourceTexture.isNull())
        {
            throw makeInvalidArgument("VKRenderToSwapchainExecutorImpl::encode requires a valid source texture.");
        }

        // RenderToSwapchain is effectively a tiny fixed-function fullscreen blit.
        // Rebuild its descriptor set only when the sampled source view changes.
        BindGroup bindGroup = getOrCreateBindGroup(sourceTexture);

        encoder->setPipeline(getOrCreatePipeline(targetFormat, descriptor));
        encoder->setBindGroup(bindGroup, 0u);
        encoder->draw(3, 1, 0, 0);
    }
} // namespace GVM::RHI::Vulkan
