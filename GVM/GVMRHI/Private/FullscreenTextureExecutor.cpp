#include "Private/FullscreenTextureExecutor.hpp"

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Maximum bind groups retained for recently sampled source views. */
        constexpr size_t kMaxCachedFullscreenTextureBindGroups = 3u;
    } // namespace

    void FullscreenTextureExecutor::init(DeviceImpl &device, const FullscreenTextureShaders &shaders)
    {
        if (shaders.vertex == nullptr || shaders.vertexEntryPoint == nullptr ||
            shaders.fragment == nullptr || shaders.fragmentEntryPoint == nullptr ||
            shaders.flippedFragment == nullptr || shaders.flippedFragmentEntryPoint == nullptr)
        {
            throw std::invalid_argument("FullscreenTextureExecutor::init requires valid vertex, fragment, and flipped fragment shader modules and entry points.");
        }

        mDevice = &device;
        mShaders = shaders;
        mSampler = device.createSampler({
            .label = "FullscreenTextureSampler",
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

        mBindGroupLayout = device.createBindGroupLayout({
            .label = "FullscreenTextureBindGroupLayout",
            .entries = {
                {
                    .binding = 0u,
                    .visibility = ShaderStage::Fragment,
                    .texture = {
                        .sampleType = TextureSampleType::Float,
                        .viewDimension = TextureViewDimension::e2D,
                    },
                },
                {
                    .binding = 1u,
                    .visibility = ShaderStage::Fragment,
                    .sampler = {
                        .type = SamplerBindingType::Filtering,
                    },
                },
            },
        });

        mPipelineLayout = device.createPipelineLayout({
            .label = "FullscreenTexturePipelineLayout",
            .bindGroupLayouts = {mBindGroupLayout},
        });
    }

    RenderPipeline FullscreenTextureExecutor::getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor)
    {
        const bool flipYAxis = descriptor.flipYAxis != False;

        RenderPipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor.label = flipYAxis ? "FullscreenTexturePipelineFlipped" : "FullscreenTexturePipeline";
        pipelineDescriptor.layout = mPipelineLayout;
        pipelineDescriptor.vertex = {
            .module = mShaders.vertex,
            .entryPoint = mShaders.vertexEntryPoint,
        };
        pipelineDescriptor.primitive = {
            .topology = PrimitiveTopology::TriangleList,
            .stripIndexFormat = IndexFormat::Undefined,
            .frontFace = FrontFace::CW,
            .cullMode = CullMode::None,
        };
        pipelineDescriptor.depthStencil = {};
        pipelineDescriptor.fragment = {
            .module = flipYAxis ? mShaders.flippedFragment : mShaders.fragment,
            .entryPoint = flipYAxis ? mShaders.flippedFragmentEntryPoint : mShaders.fragmentEntryPoint,
            .targets = {
                {
                    .format = targetFormat,
                    .blend = {},
                    .blendEnabled = False,
                    .writeMask = ColorWriteMask::All,
                },
            },
        };

        return mDevice->createRenderPipeline(pipelineDescriptor);
    }

    BindGroup FullscreenTextureExecutor::getOrCreateBindGroup(Texture source)
    {
        const TextureView sourceView = source->createView();
        for (const CachedBindGroupEntry &entry : mCachedBindGroups)
        {
            if (entry.sourceView.get() == sourceView.get())
            {
                return entry.bindGroup;
            }
        }

        BindGroup bindGroup = mDevice->createBindGroup({
            .label = "FullscreenTextureBindGroup",
            .layout = mBindGroupLayout,
            .entries = {
                {
                    .binding = 0u,
                    .textureView = {sourceView},
                },
                {
                    .binding = 1u,
                    .sampler = {mSampler},
                },
            },
        });

        if (mCachedBindGroups.size() >= kMaxCachedFullscreenTextureBindGroups)
        {
            mCachedBindGroups.erase(mCachedBindGroups.begin());
        }
        mCachedBindGroups.push_back(CachedBindGroupEntry{
            .sourceView = sourceView,
            .bindGroup = bindGroup,
        });
        return bindGroup;
    }

    void FullscreenTextureExecutor::encode(RenderPassEncoderImpl &encoder, Texture source, TextureFormat targetFormat, const RenderToSwapchainDescriptor &descriptor)
    {
        if (source.isNull())
        {
            throw std::invalid_argument("FullscreenTextureExecutor::encode requires a valid source texture.");
        }

        // A fullscreen texture draw is a tiny fixed-function blit. Rebuild its bind group
        // only when the sampled source view changes.
        BindGroup bindGroup = getOrCreateBindGroup(source);

        encoder.setPipeline(getOrCreatePipeline(targetFormat, descriptor));
        encoder.setBindGroup(bindGroup, 0u);
        encoder.draw(3u, 1u, 0u, 0u);
    }
} // namespace GVM::RHI::Private
