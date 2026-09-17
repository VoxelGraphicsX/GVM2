#include "CPPBindGroupTypeConvertor.hpp"

#include <stdexcept>

namespace UGLC::CodeGen::CPP
{

    std::string CPPBindGroupTypeConvertor::convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {
        (void)templateArgs;

        std::string result = canonicalName;
        if (canonicalName == "UGL::StructuredBuffer" || canonicalName == "UGL::RWStructuredBuffer" || canonicalName == "UGL::UniformBuffer")
        {
            result = "GVM::RHI::BufferRange";
        }
        else if (canonicalName == "UGL::StorageBuffer")
        {
            throw std::runtime_error("UGL::StorageBuffer<T> has been removed. Use UGL::StructuredBuffer<T> for read-only bindings or UGL::RWStructuredBuffer<T> for read-write bindings.");
        }
        else if (canonicalName == "UGL::Texture2D" || canonicalName == "UGL::RWTexture2D" || canonicalName == "UGL::Texture2DArray" || canonicalName == "UGL::RWTexture2DArray" || canonicalName == "UGL::Texture3D" || canonicalName == "UGL::RWTexture3D")
        {
            result = "GVM::RHI::TextureView";
        }
        /*  else if (canonicalName == "UGL::Texture2DArray" || canonicalName == "UGL::RWTexture2DArray")
         {
             result = "eastl::array<GVM::RHI::TextureView, " + templateArgs.at(1) + ">";
         } */
        else if (canonicalName == "UGL::Sampler")
        {
            result = "GVM::RHI::Sampler";
        }
        else if (canonicalName.starts_with("UGL::TextureFormat::"))
        {
            result = canonicalName;
            const std::string from = "UGL::TextureFormat::";
            const std::string to = "GVM::RHI::TextureFormat::";
            size_t start_pos = result.find(from);
            if (start_pos != std::string::npos)
            {
                result.replace(start_pos, from.length(), to);
            }
        }
        return result;
    }

    bool CPPBindGroupTypeConvertor::checkShouldIgnoreTemplateParams(const std::string &canonicalName) const
    {
        if (canonicalName == "UGL::StructuredBuffer" || canonicalName == "UGL::RWStructuredBuffer" || canonicalName == "UGL::UniformBuffer" || canonicalName == "UGL::StorageBuffer")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture2D" || canonicalName == "UGL::RWTexture2D")
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
        else if (canonicalName == "UGL::ColorAttachment")
        {
            return true;
        }
        else if (canonicalName == "UGL::DepthStencilAttachment")
        {
            return true;
        }
        return false;
    }

    bool CPPBindGroupTypeConvertor::checkShouldIgnoreUsingDecl(const std::string &canonicalName) const
    {
        (void)canonicalName;
        return false;
    }


} // namespace UGLC::CodeGen::CPP
