#include "HLSLTypeConvertor.hpp"
#include "HLSLIdentifierUtils.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace UGLC::CodeGen::HLSL
{
    namespace
    {
        std::string makeQualifiedHLSLTypeName(const std::string &qualifiedName)
        {
            return flattenQualifiedHLSLIdentifierForHelperName(qualifiedName);
        }
    } // namespace

    std::string HLSLTypeConvertor::convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {
        std::string normalizedCanonicalName = canonicalName;
        while (normalizedCanonicalName.starts_with("::"))
        {
            normalizedCanonicalName.erase(0, 2);
        }

        if (normalizedCanonicalName == "UGL::ColorAttachment")
        {
            return MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        if (normalizedCanonicalName == "UGL::DepthStencilAttachment")
        {
            return MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        if (normalizedCanonicalName == "UGL::PixelLocalColorAttachment")
        {
            return MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        if (normalizedCanonicalName == "UGL::PixelLocalDepthAttachment")
        {
            return MakeFormatToVectorTypeForFrameBuffer(templateArgs.front());
        }
        if (normalizedCanonicalName == "UGL::UniformBuffer")
        {
            return "ConstantBuffer<" + templateArgs.front() + ">";
        }
        if (normalizedCanonicalName == "UGL::StructuredBuffer")
        {
            return "StructuredBuffer<" + templateArgs.front() + ">";
        }
        if (normalizedCanonicalName == "UGL::RWStructuredBuffer")
        {
            return "RWStructuredBuffer<" + templateArgs.front() + ">";
        }
        if (normalizedCanonicalName == "UGL::StorageBuffer")
        {
            throw std::runtime_error("UGL::StorageBuffer<T> has been removed. Use UGL::StructuredBuffer<T> for read-only bindings or UGL::RWStructuredBuffer<T> for read-write bindings.");
        }
        if (normalizedCanonicalName == "UGL::Texture2D")
        {
            return "Texture2D<" + MakeSampledTextureType(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::Texture2DArray")
        {
            return "Texture2DArray<" + MakeSampledTextureType(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::Texture3D")
        {
            return "Texture3D<" + MakeSampledTextureType(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::RWTexture2D")
        {
            return "RWTexture2D<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::RWTexture2DArray")
        {
            return "RWTexture2DArray<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::RWTexture3D")
        {
            return "RWTexture3D<" + MakeFormatToVectorTypeForStorageTexture(templateArgs.front()) + ">";
        }
        if (normalizedCanonicalName == "UGL::Sampler")
        {
            return "SamplerState";
        }
        if (normalizedCanonicalName.starts_with("UGL::Matrix"))
        {
            // UGL/Metal subscripts select stored vectors; HLSL spells matrix dimensions in the opposite order.
            return templateArgs.at(0) + templateArgs.at(2) + "x" + templateArgs.at(1);
        }
        if (normalizedCanonicalName == "UGL::Atomic")
        {
            return templateArgs.empty() ? "uint" : templateArgs.front();
        }
        if (normalizedCanonicalName == "UGL::uint_t" || normalizedCanonicalName == "uint_t" || normalizedCanonicalName == "uint32_t" || normalizedCanonicalName == "std::uint32_t")
        {
            return "uint";
        }
        if (normalizedCanonicalName == "UGL::int_t" || normalizedCanonicalName == "int_t" || normalizedCanonicalName == "int32_t" || normalizedCanonicalName == "std::int32_t")
        {
            return "int";
        }
        if (normalizedCanonicalName == "UGL::bool_t" || normalizedCanonicalName == "bool_t")
        {
            return "bool";
        }
        if (normalizedCanonicalName == "UGL::float_t" || normalizedCanonicalName == "float_t")
        {
            return "float";
        }
        if (normalizedCanonicalName == "UGL::half_t" || normalizedCanonicalName == "half_t")
        {
            return "half";
        }
        if (normalizedCanonicalName == "UGL::double_t" || normalizedCanonicalName == "double_t")
        {
            return "float";
        }
        if (normalizedCanonicalName == "_Bool")
        {
            return "bool";
        }
        if (normalizedCanonicalName.starts_with("double"))
        {
            return "float";
        }
        if (normalizedCanonicalName.starts_with("UGL::double"))
        {
            return "float" + normalizedCanonicalName.substr(11);
        }
        if (normalizedCanonicalName == "UGL::GroupShared")
        {
            return "groupshared " + templateArgs.front();
        }
        if (normalizedCanonicalName == "UGL::Texture" || normalizedCanonicalName == "UGL::Buffer")
        {
            return "int";
        }
        if (normalizedCanonicalName == "UGL::OutputPatch")
        {
            return templateArgs.front();
        }
        if (normalizedCanonicalName.starts_with("UGL::"))
        {
            std::string valueName = normalizedCanonicalName.substr(5);
            if ((valueName.starts_with("float") || valueName.starts_with("half") || valueName.starts_with("int") || valueName.starts_with("uint")) &&
                valueName.size() >= 5 && valueName[valueName.size() - 2] == 'x' &&
                valueName[valueName.size() - 3] >= '2' && valueName[valueName.size() - 3] <= '4' &&
                valueName.back() >= '2' && valueName.back() <= '4')
            {
                std::swap(valueName[valueName.size() - 3], valueName.back());
            }
            return valueName;
        }
        if (normalizedCanonicalName.find("::") == std::string::npos)
        {
            return normalizedCanonicalName;
        }
        return makeQualifiedHLSLTypeName(normalizedCanonicalName);
    }

    bool HLSLTypeConvertor::checkShouldIgnoreTemplateParams(const std::string &canonicalName) const
    {
        return canonicalName == "UGL::ColorAttachment" ||
               canonicalName == "UGL::DepthStencilAttachment" ||
               canonicalName == "UGL::PixelLocalColorAttachment" ||
               canonicalName == "UGL::PixelLocalDepthAttachment" ||
               canonicalName.starts_with("UGL::Matrix") ||
               canonicalName == "UGL::Atomic" ||
               canonicalName == "UGL::Texture2D" ||
               canonicalName == "UGL::Texture2DArray" ||
               canonicalName == "UGL::Texture3D" ||
               canonicalName == "UGL::RWTexture2D" ||
               canonicalName == "UGL::RWTexture2DArray" ||
               canonicalName == "UGL::RWTexture3D" ||
               canonicalName == "UGL::UniformBuffer" ||
               canonicalName == "UGL::StructuredBuffer" ||
               canonicalName == "UGL::RWStructuredBuffer" ||
               canonicalName == "UGL::StorageBuffer" ||
               canonicalName == "UGL::Texture" ||
               canonicalName == "UGL::Buffer" ||
               canonicalName == "UGL::GroupShared" ||
               canonicalName == "UGL::OutputPatch";
    }

    bool HLSLTypeConvertor::checkShouldIgnoreUsingDecl(const std::string &canonicalName) const
    {
        return canonicalName.starts_with("UGL::TextureFormat::");
    }

    bool HLSLTypeConvertor::shouldMaterializeTemplateSpecializationName(const std::string &canonicalName) const
    {
        return !canonicalName.empty() && !canonicalName.starts_with("UGL::") && !canonicalName.starts_with("std::");
    }
} // namespace UGLC::CodeGen::HLSL
