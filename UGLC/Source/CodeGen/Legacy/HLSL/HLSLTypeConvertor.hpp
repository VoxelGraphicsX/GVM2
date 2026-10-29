#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/HLSL/HLSLTextureTypes.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    class HLSLTypeConvertor final : public AbstractTypeConvertor
    {
    public:
        std::string convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const override;
        bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const override;
        bool checkShouldIgnoreUsingDecl(const std::string &canonicalName) const override;
        bool shouldMaterializeTemplateSpecializationName(const std::string &canonicalName) const override;

    };
}
