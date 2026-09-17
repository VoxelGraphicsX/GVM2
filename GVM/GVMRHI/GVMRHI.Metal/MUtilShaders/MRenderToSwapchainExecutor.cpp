#include "MRenderToSwapchainExecutor.hpp"

#include "../MDevice.hpp"
#include "../MRenderPassEncoder.hpp"

#include <stdexcept>

namespace GVM::RHI::Metal
{
    namespace
    {
        eastl::string getRenderToSwapchainVertexShader()
        {
            return R"(
#include <metal_stdlib>
using namespace metal;

struct RenderToSwapchainVertexOutput
{
    float4 pos [[position]];
    float2 texCoord [[user(locn0)]];
};

vertex RenderToSwapchainVertexOutput vertexMain(uint vid [[vertex_id]])
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    RenderToSwapchainVertexOutput output;
    output.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.pos.y = -output.pos.y;
    output.texCoord = uv;
    return output;
}
)";
        }

        eastl::string getRenderToSwapchainFragmentShader()
        {
            return R"(
#include <metal_stdlib>
using namespace metal;

struct RenderToSwapchainVertexOutput
{
    float4 pos [[position]];
    float2 texCoord [[user(locn0)]];
};

struct RenderToSwapchainFrameBuffer
{
    half4 color [[color(0)]];
};

struct RenderToSwapchainBindGroup
{
    texture2d<float> sourceTexture [[id(0)]];
    sampler sourceSampler [[id(1)]];
};

fragment RenderToSwapchainFrameBuffer fragmentMain(
    RenderToSwapchainVertexOutput input [[stage_in]],
    const constant RenderToSwapchainBindGroup *bindGroup [[buffer(0)]])
{
    RenderToSwapchainFrameBuffer output;
    output.color = half4(bindGroup->sourceTexture.sample(bindGroup->sourceSampler, input.texCoord));
    return output;
}

fragment RenderToSwapchainFrameBuffer fragmentMainFlipped(
    RenderToSwapchainVertexOutput input [[stage_in]],
    const constant RenderToSwapchainBindGroup *bindGroup [[buffer(0)]])
{
    RenderToSwapchainFrameBuffer output;
    output.color = half4(bindGroup->sourceTexture.sample(bindGroup->sourceSampler, float2(input.texCoord.x, 1.0 - input.texCoord.y)));
    return output;
}
)";
        }
    } // namespace

    void MRenderToSwapchainExecutorImpl::create(MDevice *device)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("MRenderToSwapchainExecutorImpl::create requires a valid device.");
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
            .code = getRenderToSwapchainVertexShader(),
        });
        mFragmentShader = mDevice->createShaderModule({
            .label = "RenderToSwapchainFragmentShader",
            .code = getRenderToSwapchainFragmentShader(),
        });
    }

    RenderPipeline MRenderToSwapchainExecutorImpl::getOrCreatePipeline(TextureFormat targetFormat, const RenderToSwapchainDescriptor &renderDescriptor)
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
            .module = mFragmentShader,
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

    void MRenderToSwapchainExecutorImpl::encode(MRenderPassEncoder *encoder,
                                                Texture sourceTexture,
                                                TextureFormat targetFormat,
                                                const RenderToSwapchainDescriptor &descriptor)
    {
        if (encoder == nullptr)
        {
            throw std::invalid_argument("MRenderToSwapchainExecutorImpl::encode requires a valid render pass encoder.");
        }
        if (sourceTexture.isNull())
        {
            throw std::invalid_argument("MRenderToSwapchainExecutorImpl::encode requires a valid source texture.");
        }

        BindGroup bindGroup = mDevice->createBindGroup({
            .label = "RenderToSwapchainBindGroup",
            .layout = mBindGroupLayout,
            .entries = {
                {
                    .binding = 0,
                    .textureView = {sourceTexture->createView()},
                },
                {
                    .binding = 1,
                    .sampler = {mSampler},
                },
            },
        });

        encoder->setPipeline(getOrCreatePipeline(targetFormat, descriptor));
        encoder->setBindGroup(bindGroup, 0);
        encoder->draw(3, 1, 0, 0);
    }
} // namespace GVM::RHI::Metal
