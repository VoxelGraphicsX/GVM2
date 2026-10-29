#pragma once
#include <string>
#include <vector>
namespace UGLC::CodeGen
{
    class AbstractTypeConvertor
    {
    public:
        virtual ~AbstractTypeConvertor() = default;
        virtual std::string convertType(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const = 0;
        virtual bool checkShouldIgnoreTemplateParams(const std::string &canonicalName) const = 0;
        virtual bool checkShouldIgnoreUsingDecl(const std::string &canonicalName) const = 0;
        /** Returns true when backend source should emit a concrete identifier instead of C++ template syntax for record specializations. */
        virtual bool shouldMaterializeTemplateSpecializationName(const std::string &canonicalName) const
        {
            (void)canonicalName;
            return false;
        }
    };
} // namespace UGLC::CodeGen
