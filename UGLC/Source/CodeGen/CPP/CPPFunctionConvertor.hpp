#pragma once
#include <CodeGen/AbstractFunctionConvertor.hpp>
namespace UGLC::CodeGen::CPP
{

    class CPPFunctionConvertor final : public AbstractFunctionConvertor
    {
    public:
        virtual std::string convertFunc(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const override;
        virtual bool checkShouldIgnoreParams(const std::string &canonicalName) const override;

    private:
    };

} // namespace UGLC::CodeGen::CPP
