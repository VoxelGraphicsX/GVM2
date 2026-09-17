#include "MSLTypeConvertor.hpp"

#include <stdexcept>

namespace UGLC::CodeGen::MSL
{

    std::string MSLTypeConvertor::convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {

        std::string result = canonicalName;
        if (canonicalName == "UGL::ColorAttachment")
        {
            result = MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        else if (canonicalName == "UGL::DepthStencilAttachment")
        {
            // throw std::runtime_error("can not handle depth attachment");
            result = MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        else if (canonicalName == "UGL::PixelLocalColorAttachment")
        {
            result = MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        else if (canonicalName == "UGL::PixelLocalDepthAttachment")
        {
            result = MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        else if (canonicalName == "UGL::UniformBuffer" || canonicalName == "UGL::RWStructuredBuffer")
        {
            result = templateArgs.front() + "*";
        }
        else if (canonicalName == "UGL::StructuredBuffer")
        {
            result = "const " + templateArgs.front() + "*";
        }
        else if (canonicalName == "UGL::StorageBuffer")
        {
            throw std::runtime_error("UGL::StorageBuffer<T> has been removed. Use UGL::StructuredBuffer<T> for read-only bindings or UGL::RWStructuredBuffer<T> for read-write bindings.");
        }
        else if (canonicalName == "UGL::Texture2D")
        {
            result = "texture2d<" + MakeFormatToVectorTypeForTexture(templateArgs.front()) + ">";
        }
        else if (canonicalName == "UGL::Texture2DArray")
        {
            result = "texture2d_array<" + MakeFormatToVectorTypeForTexture(templateArgs.front()) + ">"; // getBindlessTextureWrapperFromTemplateType(false, MakeFormatToVectorTypeForTexture(templateArgs.front())) + "*";
        }
        else if (canonicalName == "UGL::Texture3D")
        {
            result = "texture3d<" + MakeFormatToVectorTypeForTexture(templateArgs.front()) + ">";
        }
        else if (canonicalName == "UGL::RWTexture2DArray")
        {
            result = "texture2d_array<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ", access::read_write>"; // getBindlessTextureWrapperFromTemplateType(true, MakeFormatToVectorTypeForTexture(templateArgs.front())) + "*";
        }
        else if (canonicalName == "UGL::RWTexture3D")
        {
            result = "texture3d<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ", access::read_write>";
        }
        else if (canonicalName == "UGL::RWTexture2D")
        {
            result = "texture2d<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ", access::read_write>";
        }
        else if (canonicalName == "UGL::Sampler")
        {
            result = "sampler";
        }
        else if (canonicalName.starts_with("UGL::Matrix"))
        {
            result = templateArgs.at(0) + templateArgs.at(1) + "x" + templateArgs.at(2);
        }
        else if (canonicalName == "UGL::Atomic")
        {
            result = "atomic";
        }
        else if (canonicalName == "_Bool")
        {
            return "bool";
        }
        else if (canonicalName.starts_with("double"))
        {
            return "float";
        }
        else if (canonicalName.starts_with("UGL::double"))
        {
            return "float" + canonicalName.substr(11);
        }
        else if (canonicalName == "UGL::GroupShared")
        {
            return "threadgroup " + templateArgs.front();
        }
        else if (canonicalName == "UGL::Texture" || canonicalName == "UGL::Buffer")
        {
            return "int";
        }
        else if (canonicalName == "UGL::OutputPatch")
        {
            return "patch_control_point<" + templateArgs.front() + ">";
        }
        else if (canonicalName == "UGL::BindGroup")
        {
            if (templateArgs.empty())
            {
                throw std::runtime_error("UGL::BindGroup<T> requires a concrete bind-group type for MSL code generation.");
            }
            return templateArgs.front();
        }
        else if (canonicalName.starts_with("UGL::"))
        {
            result = canonicalName.substr(5); // Remove "UGL::" prefix
        }


        return result;
    }

    bool MSLTypeConvertor::checkShouldIgnoreTemplateParams(const std::string &canonicalName) const
    {
        if (canonicalName == "UGL::ColorAttachment")
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
        else if (canonicalName.starts_with("UGL::Matrix"))
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
        else if (canonicalName == "UGL::UniformBuffer" || canonicalName == "UGL::StructuredBuffer" || canonicalName == "UGL::RWStructuredBuffer" || canonicalName == "UGL::StorageBuffer")
        {
            return true;
        }
        else if (canonicalName == "UGL::Texture" || canonicalName == "UGL::Buffer")
        {
            return true;
        }
        else if (canonicalName == "UGL::GroupShared")
        {
            return true;
        }
        else if (canonicalName == "UGL::OutputPatch")
        {
            return true;
        }
        else if (canonicalName == "UGL::BindGroup")
        {
            return true;
        }
        return false;
    }

    bool MSLTypeConvertor::checkShouldIgnoreUsingDecl(const std::string &canonicalName) const
    {
        if (canonicalName.starts_with("UGL::TextureFormat::"))
        {
            return true;
        }

        return false;
    }

    bool MSLTypeConvertor::shouldMaterializeTemplateSpecializationName(const std::string &canonicalName) const
    {
        return !canonicalName.empty() && !canonicalName.starts_with("UGL::") && !canonicalName.starts_with("std::");
    }

} // namespace UGLC::CodeGen::MSL
