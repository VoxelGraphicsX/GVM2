#pragma once
#include <string>
namespace UGLC::CodeGen
{
    class AbstractAttributeConvertor
    {
    public:
        virtual ~AbstractAttributeConvertor() = default;
        virtual std::string convertAttribute(const std::string &canonicalName) const = 0;
    };
} // namespace UGLC::CodeGen
