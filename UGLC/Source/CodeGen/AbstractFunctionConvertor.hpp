#pragma once
#include <string>
#include <vector>
namespace UGLC::CodeGen
{
    class AbstractFunctionConvertor
    {
    public:
        virtual ~AbstractFunctionConvertor() = default;
        virtual std::string convertFunc(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const = 0;
        // virtual bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const = 0;
        virtual bool checkShouldIgnoreParams(const std::string &canonicalName) const = 0;
    };
} // namespace UGLC::CodeGen
