#include "CPPTypeConvertor.hpp"

#include <stdexcept>

namespace UGLC::CodeGen::CPP
{

    std::string CPPTypeConvertor::convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {

        std::string result = canonicalName;
        if (canonicalName == "UGL::Device")
        {
            result = "GVM::Core::DeviceProxy";
        }
        else if (canonicalName == "UGL::Swapchain")
        {
            result = "GVM::RHI::Swapchain";
        }
        else if (canonicalName == "UGL::RenderClass" || canonicalName == "UGL::ComputeClass")
        {
            result = "eastl::intrusive_ptr";
        }
        else if (canonicalName == "UGL::SwapchainQueryResult")
        {
            result = "GVM::RHI::SwapchainQueryResult";
        }
        else if (canonicalName == "UGL::SwapchainNextTextureQueryStatus")
        {
            result = "GVM::RHI::SwapchainNextTextureQueryStatus";
        }
        else if (canonicalName == "UGL::RenderToSwapchainDescriptor")
        {
            result = "GVM::RHI::RenderToSwapchainDescriptor";
        }
        else if (canonicalName == "UGL::GpuTimestampFrameProfiler")
        {
            result = "GVM::RHI::GpuTimestampFrameProfiler";
        }
        else if (canonicalName == "UGL::GpuTimestampFrameProfiler::Scope")
        {
            result = "GVM::RHI::GpuTimestampFrameProfiler::Scope";
        }
        else if (canonicalName == "UGL::GpuPassCounterFrameProfiler")
        {
            result = "GVM::RHI::GpuPassCounterFrameProfiler";
        }
        else if (canonicalName == "UGL::GpuPassCounterFrameProfiler::Scope")
        {
            result = "GVM::RHI::GpuPassCounterFrameProfiler::Scope";
        }
        else if (canonicalName == "UGL::PassCounterQuerySupport")
        {
            result = "GVM::RHI::PassCounterQuerySupport";
        }
        else if (canonicalName == "UGL::PassCounterStageUtilizationRawResult")
        {
            result = "GVM::RHI::PassCounterStageUtilizationRawResult";
        }
        else if (canonicalName == "UGL::PassCounterStatisticRawResult")
        {
            result = "GVM::RHI::PassCounterStatisticRawResult";
        }
        else if (canonicalName == "UGL::PassCounterRangeResult")
        {
            result = "GVM::RHI::PassCounterRangeResult";
        }
        else if (canonicalName == "UGL::LoadOp")
        {
            result = "GVM::RHI::LoadOp";
        }
        else if (canonicalName == "UGL::StoreOp")
        {
            result = "GVM::RHI::StoreOp";
        }
        else if (canonicalName == "UGL::StructuredBuffer" || canonicalName == "UGL::RWStructuredBuffer")
        {
            result = "GVM::RHI::Buffer";
        }
        else if (canonicalName == "UGL::StorageBuffer")
        {
            throw std::runtime_error("UGL::StorageBuffer<T> has been removed. Use UGL::StructuredBuffer<T> for read-only bindings or UGL::RWStructuredBuffer<T> for read-write bindings.");
        }
        else if (canonicalName == "UGL::Texture2D" || canonicalName == "UGL::RWTexture2D" || canonicalName == "UGL::Texture2DArray" || canonicalName == "UGL::RWTexture2DArray" || canonicalName == "UGL::Texture3D" || canonicalName == "UGL::RWTexture3D")
        {
            result = "GVM::RHI::Texture";
        }
        else if (canonicalName == "UGL::ColorAttachment")
        {
            result = "GVM::Core::ColorAttachment";
        }
        else if (canonicalName == "UGL::DepthStencilAttachment")
        {
            result = "GVM::Core::DepthStencilAttachment";
        }
        else if (canonicalName == "UGL::PixelLocalColorAttachment")
        {
            result = "GVM::Core::PixelLocalColorAttachment";
        }
        else if (canonicalName == "UGL::PixelLocalDepthAttachment")
        {
            result = "GVM::Core::PixelLocalDepthAttachment";
        }
        else if (canonicalName == "UGL::PixelLocalPassTaskDescriptor")
        {
            result = "GVM::Core::PixelLocalPassTaskDescriptor";
        }
        else if (canonicalName == "UGL::PixelLocalAccess")
        {
            result = "GVM::Core::PixelLocalAccess";
        }
        else if (canonicalName == "UGL::PixelLocalStorage")
        {
            result = "GVM::Core::PixelLocalStorage";
        }
        else if (canonicalName == "UGL::PixelLocalLoad")
        {
            result = "GVM::Core::PixelLocalLoad";
        }
        else if (canonicalName == "UGL::PixelLocalStore")
        {
            result = "GVM::Core::PixelLocalStore";
        }
        else if (canonicalName == "UGL::Buffer")
        {
            result = "GVM::RHI::Buffer";
        }
        else if (canonicalName == "UGL::BufferRange")
        {
            result = "GVM::RHI::BufferRange";
        }
        else if (canonicalName == "UGL::Texture")
        {
            result = "GVM::RHI::Texture";
        }
        else if (canonicalName == "UGL::TextureView")
        {
            result = "GVM::RHI::TextureView";
        }
        else if (canonicalName == "UGL::Sampler")
        {
            result = "GVM::RHI::Sampler";
        }
        else if (canonicalName == "UGL::BindGroup")
        {
            result = "eastl::intrusive_ptr";
        }
        else if (canonicalName == "UGL::AddressMode")
        {
            result = "GVM::RHI::AddressMode";
        }
        else if (canonicalName == "UGL::FilterMode")
        {
            result = "GVM::RHI::FilterMode";
        }
        else if (canonicalName == "UGL::MipmapFilterMode")
        {
            result = "GVM::RHI::MipmapFilterMode";
        }
        else if (canonicalName == "UGL::CompareFunction")
        {
            result = "GVM::RHI::CompareFunction";
        }
        else if (canonicalName == "UGL::SamplerDescriptor")
        {
            result = "GVM::RHI::SamplerDescriptor";
        }
        else if (canonicalName.starts_with("UGL::Matrix") && templateArgs.size() == 3)
        {
            if (templateArgs.at(0) == "GVM::Core::Math::ShaderHalf")
            {
                result = "glm::mat<" + templateArgs.at(1) + ", " + templateArgs.at(2) + ", GVM::Core::Math::ShaderHalf, glm::packed_highp>";
            }
            else
            {
                result = templateArgs.at(0) + templateArgs.at(1) + "x" + templateArgs.at(2);
            }
        }
        else if (canonicalName == "UGL::RenderSet")
        {
            result = "eastl::intrusive_ptr";
        }
        else if (canonicalName == "UGL::BufferComponent")
        {
            result = "eastl::intrusive_ptr<GVM::Core::BufferComponent>";
        }
        else if (canonicalName == "UGL::TextureComponent")
        {
            result = "eastl::intrusive_ptr<GVM::Core::TextureComponent>";
        }
        /* else if (canonicalName == "UGL::Texture2DArray" || canonicalName == "UGL::RWTexture2DArray")
        {
            result = "eastl::array<" + templateArgs[1] + ">";
        } */
        else if (canonicalName == "_Bool")
        {
            result = "bool";
        }
        else if (canonicalName == ("UGL::UObject"))
        {
            result = "GVM::Core::UObject";
        }
        else if (canonicalName == "UGL::SPTR")
        {
            result = "GVM::Core::SPTR";
        }
        else if (canonicalName == "UGL::TextureViewDescriptor")
        {
            result = "GVM::RHI::TextureViewDescriptor";
        }
        else if (canonicalName == "UGL::Queue")
        {
            result = "GVM::Core::QueueProxy";
        }
        else if (canonicalName == "UGL::Atomic")
        {
            result = templateArgs.front();
        }
        else if (canonicalName == "UGL::CullMode")
        {
            result = "GVM::RHI::CullMode";
        }
        else if (canonicalName == "UGL::PrimitiveTopology")
        {
            result = "GVM::RHI::PrimitiveTopology";
        }
        else if (canonicalName == "UGL::IndexFormat")
        {
            result = "GVM::RHI::IndexFormat";
        }
        else if (canonicalName == "UGL::BlendFactor")
        {
            result = "GVM::RHI::BlendFactor";
        }
        else if (canonicalName == "UGL::BlendOperation")
        {
            result = "GVM::RHI::BlendOperation";
        }
        else if (canonicalName == "UGL::BlendComponent")
        {
            result = "GVM::RHI::BlendComponent";
        }
        else if (canonicalName == "UGL::BlendState")
        {
            result = "GVM::RHI::BlendState";
        }
        else if (canonicalName == "UGL::RenderPipelineDescriptor")
        {
            result = "GVM::RHI::RenderPipelineDescriptor";
        }
        else if (canonicalName == "UGL::RenderPassTaskDescriptor" || canonicalName == "UGL::ComputePassTaskDescriptor" || canonicalName == "UGL::BlitPassTaskDescriptor")
        {
            result = "GVM::Core::" + canonicalName.substr(5);
        }
        else if (canonicalName.starts_with("UGL::TextureFormat::"))
        {
            result = "GVM::RHI::" + canonicalName.substr(5);
        }
        else if (canonicalName.starts_with("UGL::half") || canonicalName.starts_with("UGL::float") || canonicalName.starts_with("UGL::double") || canonicalName.starts_with("UGL::uint") || canonicalName.starts_with("UGL::int"))
        {
            if (canonicalName.starts_with("UGL::half"))
            {
                const std::string dimensions = canonicalName.substr(9);
                if (dimensions.empty() || dimensions == "1" || dimensions == "_t")
                {
                    result = "GVM::Core::Math::ShaderHalf";
                }
                else if (dimensions.size() == 1 && dimensions[0] >= '2' && dimensions[0] <= '4')
                {
                    result = "glm::vec<" + dimensions + ", GVM::Core::Math::ShaderHalf, glm::packed_highp>";
                }
                else if (dimensions.size() == 3 && dimensions[1] == 'x')
                {
                    result = "glm::mat<" + dimensions.substr(0, 1) + ", " + dimensions.substr(2, 1) + ", GVM::Core::Math::ShaderHalf, glm::packed_highp>";
                }
                else
                {
                    throw std::runtime_error("Unsupported host half type: " + canonicalName);
                }
            }
            else
            {
                result = canonicalName.substr(5);
            }
        }


        return result;
    }

    bool CPPTypeConvertor::checkShouldIgnoreTemplateParams(const std::string &canonicalName) const
    {
        if (canonicalName == "UGL::StructuredBuffer" || canonicalName == "UGL::RWStructuredBuffer" || canonicalName == "UGL::StorageBuffer")
        {
            return true;
        }
        else if (canonicalName == "UGL::Buffer")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture")
        {
            return true;
        }
        else if (canonicalName == "UGL::TextureView")
        {
            return true;
        }
        else if (canonicalName == "UGL::BufferRange")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture2D" || canonicalName == "UGL::RWTexture2D")
        {
            return true;
        }
        else if (canonicalName == "UGL::ColorAttachment")
        {
            return true;
        }
        else if (canonicalName == "UGL::DepthStencilAttachment")
        {
            return true;
        }
        else if (canonicalName == "UGL::PixelLocalColorAttachment")
        {
            return true;
        }
        else if (canonicalName == "UGL::PixelLocalDepthAttachment")
        {
            return true;
        }
        else if (canonicalName == "UGL::BufferComponent")
        {
            return true;
        }
        else if (canonicalName == "UGL::TextureComponent")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture2DArray" || canonicalName == "UGL::RWTexture2DArray")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture3D" || canonicalName == "UGL::RWTexture3D")
        {
            return true;
        }
        else if (canonicalName.starts_with("UGL::Matrix"))
        {
            return true;
        }
        else if (canonicalName == "UGL::Atomic")
        {
            return true;
        }
        return false;
    }

    bool CPPTypeConvertor::checkShouldIgnoreUsingDecl(const std::string &canonicalName) const
    {
        if (canonicalName.starts_with("UGL::TextureFormat::"))
        {
            return true;
        }

        return false;
    }


} // namespace UGLC::CodeGen::CPP
