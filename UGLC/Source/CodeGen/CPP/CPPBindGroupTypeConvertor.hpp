#pragma once
#include <CodeGen/AbstractTypeConvertor.hpp>
namespace UGLC::CodeGen::CPP
{

    class CPPBindGroupTypeConvertor final : public AbstractTypeConvertor
    {
    public:
        virtual std::string convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const override;
        virtual bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const override;
        virtual bool checkShouldIgnoreUsingDecl(const std::string &canonicalName) const override;

    private:
    };

} // namespace UGLC::CodeGen::CPP
