#pragma once
#include <CodeGen/AbstractTypeConvertor.hpp>
namespace UGLC::CodeGen::CPP
{

    class CPPTypeConvertor final : public AbstractTypeConvertor
    {
    public:
        /** Selects texture-view handles when emitting constructor Args rather than host texture allocations. */
        explicit CPPTypeConvertor(bool argumentResources = false) : mArgumentResources(argumentResources) {}
        virtual std::string convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const override;
        virtual bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const override;
        virtual bool checkShouldIgnoreUsingDecl(const std::string &canonicalName) const override;

    private:
        bool mArgumentResources = false;
    };

} // namespace UGLC::CodeGen::CPP
