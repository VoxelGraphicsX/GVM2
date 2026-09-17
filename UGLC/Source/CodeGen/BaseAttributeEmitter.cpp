#include "BaseAttributeEmitter.hpp"

#include "AbstractAttributeConvertor.hpp"
#include "UGLC.Constants.hpp"

#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen
{
    bool BaseAttributeEmitter::hasAttribute(const clang::Decl *decl, const std::string &name) const
    {
        if (decl == nullptr)
        {
            return false;
        }
        if (decl->hasAttrs())
        {
            for (const auto *attr : decl->getAttrs())
            {
                if (const auto *annotateAttr = llvm::dyn_cast<clang::AnnotateAttr>(attr))
                {
                    const std::string rawAnnotation = annotateAttr->getAnnotation().str();
                    if (name == rawAnnotation)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::string BaseAttributeEmitter::getSlotNameByIndex(int index) const
    {
        return mUGLAttributeSlotName + std::to_string(index);
    }

    std::string BaseAttributeEmitter::getVertexInputNameByIndex(int index) const
    {
        return mUGLAttributeVertexInputName + std::to_string(index);
    }

    bool BaseAttributeEmitter::isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName) const
    {
        if (!rawAttribute.starts_with(attributeName) || rawAttribute.size() == attributeName.size())
        {
            return false;
        }

        for (size_t i = attributeName.size(); i < rawAttribute.size(); ++i)
        {
            const char ch = rawAttribute[i];
            if (ch < '0' || ch > '9')
            {
                return false;
            }
        }
        return true;
    }

    int BaseAttributeEmitter::getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName) const
    {
        if (!isExactIndexedAttribute(rawAttribute, attributeName))
        {
            return -1;
        }

        int result = 0;
        for (size_t i = attributeName.size(); i < rawAttribute.size(); ++i)
        {
            result = result * 10 + static_cast<int>(rawAttribute[i] - '0');
        }
        return result;
    }

    std::string BaseAttributeEmitter::generateAttributes(const clang::Decl *decl, const AbstractAttributeConvertor *convertor) const
    {
        std::string result;
        if (decl->hasAttrs())
        {
            for (const auto *attr : decl->getAttrs())
            {
                if (const auto *annotateAttr = llvm::dyn_cast<clang::AnnotateAttr>(attr))
                {
                    result += generateAttribute(annotateAttr, convertor);
                }
            }
        }
        return result;
    }

    std::vector<const clang::AnnotateAttr *> BaseAttributeEmitter::getAllAttributes(const clang::Decl *decl) const
    {
        std::vector<const clang::AnnotateAttr *> results;
        if (decl == nullptr)
        {
            return results;
        }
        if (decl->hasAttrs())
        {
            for (const auto *attr : decl->getAttrs())
            {
                if (const auto *annotateAttr = llvm::dyn_cast<clang::AnnotateAttr>(attr))
                {
                    results.emplace_back(annotateAttr);
                }
            }
        }
        return results;
    }

    std::string BaseAttributeEmitter::generateAttribute(const clang::AnnotateAttr *attribute, const AbstractAttributeConvertor *convertor) const
    {
        std::string result;
        std::string rawAnnotation = attribute->getAnnotation().str();
        if (convertor != nullptr)
        {
            rawAnnotation = convertor->convertAttribute(rawAnnotation);
        }
        if (!rawAnnotation.empty())
        {
            result += " [[" + rawAnnotation + "]]";
        }
        return result;
    }

    std::string BaseAttributeEmitter::generateRawAttribute(const clang::AnnotateAttr *attribute, const AbstractAttributeConvertor *convertor) const
    {
        std::string rawAnnotation = attribute->getAnnotation().str();
        if (convertor != nullptr)
        {
            rawAnnotation = convertor->convertAttribute(rawAnnotation);
        }
        return rawAnnotation;
    }

    std::vector<std::string> BaseAttributeEmitter::getAttributeParams(const clang::Decl *decl,
                                                                      const std::string &funcName,
                                                                      const AbstractAttributeConvertor *convertor) const
    {
        for (const auto *attr : getAllAttributes(decl))
        {
            const std::string attrStr = generateAttribute(attr, convertor);
            if (attrStr.starts_with(funcName))
            {
                const size_t leftParen = attrStr.find('(');
                const size_t rightParen = attrStr.rfind(')');
                if (leftParen != std::string::npos && rightParen != std::string::npos && rightParen > leftParen + 1)
                {
                    const std::string params = attrStr.substr(leftParen + 1, rightParen - leftParen - 1);
                    std::vector<std::string> result;
                    size_t start = 0;
                    size_t end = 0;
                    while ((end = params.find(',', start)) != std::string::npos)
                    {
                        result.push_back(params.substr(start, end - start));
                        start = end + 1;
                    }
                    result.push_back(params.substr(start));

                    for (auto &param : result)
                    {
                        const size_t first = param.find_first_not_of(" \t");
                        const size_t last = param.find_last_not_of(" \t");
                        if (first == std::string::npos || last == std::string::npos)
                        {
                            param = "";
                        }
                        else
                        {
                            param = param.substr(first, last - first + 1);
                        }
                    }
                    return result;
                }
                return {};
            }
        }
        return {};
    }
} // namespace UGLC::CodeGen
