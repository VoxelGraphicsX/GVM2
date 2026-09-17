#include "MRenderPipeline.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MShaderModule.hpp"
#include "Metal/Metal.hpp"
#include "Private/PixelLocalPassAccess.hpp"
#include <string>
namespace GVM::RHI::Metal
{
    namespace
    {
        bool isStripTopology(PrimitiveTopology topology)
        {
            return topology == PrimitiveTopology::LineStrip || topology == PrimitiveTopology::TriangleStrip;
        }

        const char *PixelLocalFullscreenVertexShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct GVMPixelLocalFullscreenVertexOutput
{
    float4 position [[position]];
};

vertex GVMPixelLocalFullscreenVertexOutput gvmPixelLocalFullscreenVertex(uint vertexID [[vertex_id]])
{
    float2 position = vertexID == 0u ? float2(-1.0, -1.0) : (vertexID == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    GVMPixelLocalFullscreenVertexOutput outputValue;
    outputValue.position = float4(position, 0.0, 1.0);
    return outputValue;
}
)";

        ShaderModule createPixelLocalFullscreenVertexShader(MDevice &device, const RenderPipelineDescriptor &descriptor)
        {
            ShaderModuleDescriptor shaderDescriptor = {};
            if (descriptor.label.empty())
            {
                shaderDescriptor.label = "PixelLocalFullscreenVertexShader";
            }
            else
            {
                shaderDescriptor.label = descriptor.label;
                shaderDescriptor.label += ".PixelLocalFullscreenVertexShader";
            }
            shaderDescriptor.code = PixelLocalFullscreenVertexShaderSource;
            return device.createShaderModule(shaderDescriptor);
        }

        void collectArgumentBufferIndices(NS::Array *arguments, eastl::unordered_set<uint32_t> &indices)
        {
            indices.clear();
            if (arguments == nullptr)
            {
                return;
            }

            for (NS::UInteger i = 0; i < arguments->count(); ++i)
            {
                auto *argument = arguments->object<MTL::Argument>(i);
                if (argument == nullptr || !argument->active())
                {
                    continue;
                }
                if (argument->type() != MTL::ArgumentTypeBuffer)
                {
                    continue;
                }
                indices.emplace(static_cast<uint32_t>(argument->index()));
            }
        }
    } // namespace

    MRenderPipeline::MRenderPipeline()
    {
    }

    MRenderPipeline::~MRenderPipeline()
    {
        if (mNativePipelineState != nullptr)
        {
            mNativePipelineState->release();
            mNativePipelineState = nullptr;
        }
        if (mNativeDepthStencilState != nullptr)
        {
            mNativeDepthStencilState->release();
            mNativeDepthStencilState = nullptr;
        }
    }

    void MRenderPipeline::init(MDevice *device, const RenderPipelineDescriptor &inputDescriptor)
    {
        RenderPipelineDescriptor descriptor = inputDescriptor;
        if (descriptor.fragment.module == nullptr)
        {
            throw std::runtime_error("MRenderPipeline::init requires a fragment shader");
        }
        const bool pixelLocalPipeline = GVM::RHI::Private::isPixelLocalRenderPipelineDescriptor(descriptor);
        const bool vertexShaderMissing = descriptor.vertex.module == nullptr;
        const bool pixelLocalFramebufferFetchPipeline = pixelLocalPipeline && vertexShaderMissing;
        if (vertexShaderMissing && !pixelLocalFramebufferFetchPipeline)
        {
            throw std::runtime_error("MRenderPipeline::init requires a vertex shader for non-pixelLocal pipelines");
        }
        if (pixelLocalFramebufferFetchPipeline)
        {
            mInjectedFullscreenVertexShader = createPixelLocalFullscreenVertexShader(*device, descriptor);
            descriptor.vertex.module = mInjectedFullscreenVertexShader;
            descriptor.vertex.entryPoint = "gvmPixelLocalFullscreenVertex";
            descriptor.vertex.buffers.clear();
        }

        this->mDevice = device;
        this->mDescriptor = descriptor;
        this->mLabelName = descriptor.label;
        mIsPixelLocalFramebufferFetchPipeline = pixelLocalFramebufferFetchPipeline;
        mPrimitiveTopology = descriptor.primitive.topology;
        mStripIndexFormat = descriptor.primitive.stripIndexFormat;
        mPrimitiveType = translatePrimitiveTopologyToMTL(descriptor.primitive.topology);
        mFrontFaceWinding = translateFrontFaceToMTL(descriptor.primitive.frontFace);
        mCullMode = translateCullMode(descriptor.primitive.cullMode);
        mDepthBias = static_cast<float>(descriptor.depthStencil.depthBias);
        mDepthBiasSlopeScale = descriptor.depthStencil.depthBiasSlopeScale;
        mDepthBiasClamp = descriptor.depthStencil.depthBiasClamp;

        if (!isStripTopology(descriptor.primitive.topology) && descriptor.primitive.stripIndexFormat != IndexFormat::Undefined)
        {
            throw std::runtime_error("stripIndexFormat is only valid for line-strip or triangle-strip pipelines");
        }

        MTL::RenderPipelineDescriptor *pipelineDescriptor = MTL::RenderPipelineDescriptor::alloc()->init();
        pipelineDescriptor->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
        pipelineDescriptor->setVertexFunction(eastl::static_pointer_cast<MShaderModule>(descriptor.vertex.module)->makeLibrary(descriptor.vertex.entryPoint));

        pipelineDescriptor->setFragmentFunction(eastl::static_pointer_cast<MShaderModule>(descriptor.fragment.module)->makeLibrary(descriptor.fragment.entryPoint));

        for (int i = 0; i < descriptor.fragment.targets.size(); i++)
        {
            auto colorAttachment = descriptor.fragment.targets[i];
            auto colorAttachmentDescriptor = pipelineDescriptor->colorAttachments()->object(i);
            colorAttachmentDescriptor->setPixelFormat(translateTextureFormatToMTL(colorAttachment.format));
            colorAttachmentDescriptor->setBlendingEnabled(colorAttachment.blendEnabled > 0);
            colorAttachmentDescriptor->setSourceRGBBlendFactor(translateBlendFactorToMTL(colorAttachment.blend.color.srcFactor));
            colorAttachmentDescriptor->setDestinationRGBBlendFactor(translateBlendFactorToMTL(colorAttachment.blend.color.dstFactor));
            colorAttachmentDescriptor->setRgbBlendOperation(translateBlendOperationToMTL(colorAttachment.blend.color.operation));
            colorAttachmentDescriptor->setSourceAlphaBlendFactor(translateBlendFactorToMTL(colorAttachment.blend.alpha.srcFactor));
            colorAttachmentDescriptor->setDestinationAlphaBlendFactor(translateBlendFactorToMTL(colorAttachment.blend.alpha.dstFactor));
            colorAttachmentDescriptor->setAlphaBlendOperation(translateBlendOperationToMTL(colorAttachment.blend.alpha.operation));
            colorAttachmentDescriptor->setWriteMask(colorAttachment.writeMask);
        }

        if (descriptor.depthStencil.format != GVM::RHI::TextureFormat::Undefined)
        {
            pipelineDescriptor->setDepthAttachmentPixelFormat(translateTextureFormatToMTL(descriptor.depthStencil.format));
        }
        auto depthStateDesp = MTL::DepthStencilDescriptor::alloc()->init();
        if (descriptor.depthStencil.depthTestEnabled || descriptor.depthStencil.depthWriteEnabled)
        {
            depthStateDesp->setDepthCompareFunction(translateCompareFunctionToMTL(descriptor.depthStencil.depthCompare));
            depthStateDesp->setDepthWriteEnabled(descriptor.depthStencil.depthWriteEnabled);
        }
        this->mNativeDepthStencilState = device->getNativeDevice()->newDepthStencilState(depthStateDesp);
        depthStateDesp->release();
        MTL::VertexDescriptor *vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
        {
            for (int i = 0; i < descriptor.vertex.buffers.size(); i++)
            {
                const auto &buffer = descriptor.vertex.buffers[i];
                auto vertexBufferDescriptor = vertexDescriptor->layouts()->object(i);
                vertexBufferDescriptor->setStride(buffer.arrayStride);
                vertexBufferDescriptor->setStepFunction(translateVertexStepModeToMTL(buffer.stepMode));
                if (descriptor.tessellation)
                {
                    vertexBufferDescriptor->setStepFunction(MTL::VertexStepFunction::VertexStepFunctionPerPatchControlPoint);
                }
                for (int j = 0; j < buffer.attributes.size(); j++)
                {
                    const auto &attribute = buffer.attributes[j];
                    auto vertexAttributeDescriptor = vertexDescriptor->attributes()->object(attribute.shaderLocation);
                    vertexAttributeDescriptor->setFormat(translateVertexFormatToMTL(attribute.format));
                    vertexAttributeDescriptor->setOffset(attribute.offset);
                    vertexAttributeDescriptor->setBufferIndex(i);
                }
            }
        }
        pipelineDescriptor->setVertexDescriptor(vertexDescriptor);
        pipelineDescriptor->setSupportIndirectCommandBuffers(true);
        pipelineDescriptor->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));

        NS::Error *err = nullptr;
        MTL::AutoreleasedRenderPipelineReflection reflection = nullptr;

        mNativePipelineState = device->getNativeDevice()->newRenderPipelineState(pipelineDescriptor, MTL::PipelineOptionArgumentInfo, &reflection, &err);
        pipelineDescriptor->release();
        vertexDescriptor->release();
        if (err)
        {
            throw std::runtime_error(std::string("Error: ") + err->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding));
        }
        if (reflection != nullptr)
        {
            collectArgumentBufferIndices(reflection->vertexArguments(), mVertexArgumentBufferIndices);
            collectArgumentBufferIndices(reflection->fragmentArguments(), mFragmentArgumentBufferIndices);
        }
    }

    MTL::RenderPipelineState *MRenderPipeline::getNativePipelineState() const
    {
        return mNativePipelineState;
    }

    MTL::DepthStencilState *MRenderPipeline::getNativeDepthState() const
    {
        return mNativeDepthStencilState;
    }

    int MRenderPipeline::getVertexBufferCount() const
    {
        return mDescriptor.vertex.buffers.size();
    }

    MTL::CullMode MRenderPipeline::getCullMode() const
    {
        return mCullMode;
    }

    MTL::PrimitiveType MRenderPipeline::getPrimitiveType() const
    {
        return mPrimitiveType;
    }

    MTL::Winding MRenderPipeline::getFrontFaceWinding() const
    {
        return mFrontFaceWinding;
    }

    PrimitiveTopology MRenderPipeline::getPrimitiveTopology() const
    {
        return mPrimitiveTopology;
    }

    IndexFormat MRenderPipeline::getStripIndexFormat() const
    {
        return mStripIndexFormat;
    }

    float MRenderPipeline::getDepthBias() const
    {
        return mDepthBias;
    }

    float MRenderPipeline::getDepthBiasSlopeScale() const
    {
        return mDepthBiasSlopeScale;
    }

    float MRenderPipeline::getDepthBiasClamp() const
    {
        return mDepthBiasClamp;
    }

    bool MRenderPipeline::usesVertexArgumentBuffer(uint32_t index) const
    {
        return mVertexArgumentBufferIndices.find(index) != mVertexArgumentBufferIndices.end();
    }

    bool MRenderPipeline::usesFragmentArgumentBuffer(uint32_t index) const
    {
        return mFragmentArgumentBufferIndices.find(index) != mFragmentArgumentBufferIndices.end();
    }

    bool MRenderPipeline::isPixelLocalFramebufferFetchPipeline() const
    {
        return mIsPixelLocalFramebufferFetchPipeline;
    }

    const RenderPipelineDescriptor &MRenderPipeline::getDescriptor() const
    {
        return mDescriptor;
    }

} // namespace GVM::RHI::Metal
