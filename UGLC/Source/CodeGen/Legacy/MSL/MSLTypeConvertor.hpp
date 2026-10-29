#pragma once
#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/MSL/MSLTextureTypes.hpp>
namespace UGLC::CodeGen::MSL
{
    class MSLTypeConvertor final : public AbstractTypeConvertor
    {
    public:
        virtual std::string convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const override;
        virtual bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const override;
        virtual bool checkShouldIgnoreUsingDecl(const std::string &canonicalName) const override;
        virtual bool shouldMaterializeTemplateSpecializationName(const std::string &canonicalName) const override;
    };
}
