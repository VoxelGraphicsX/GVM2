#include "ASTMethodLookup.hpp"

#include "BaseASTVisitor.hpp"
#include "UGLC.Constants.hpp"

#include <stdexcept>

namespace UGLC::CodeGen
{
    ASTMethodLookup::ASTMethodLookup(const BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    bool ASTMethodLookup::methodHasAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const
    {
        return method != nullptr && mVisitor.checkAttibuteByName(method, attributeName);
    }

    bool ASTMethodLookup::methodHasParameterAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const
    {
        if (method == nullptr)
        {
            return false;
        }

        for (const auto *param : method->parameters())
        {
            if (mVisitor.checkAttibuteByName(param, attributeName))
            {
                return true;
            }
        }
        return false;
    }

    bool ASTMethodLookup::methodMatchesLookupOptions(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const
    {
        if (method == nullptr)
        {
            return false;
        }
        if (options.requireBody && !method->hasBody())
        {
            return false;
        }
        if (options.parameterCount.has_value() && method->getNumParams() != options.parameterCount.value())
        {
            return false;
        }
        if (options.constQualified.has_value() && method->isConst() != options.constQualified.value())
        {
            return false;
        }
        for (const auto &attributeName : options.requiredMethodAttributes)
        {
            if (!methodHasAttribute(method, attributeName))
            {
                return false;
            }
        }
        for (const auto &attributeName : options.requiredParameterAttributes)
        {
            if (!methodHasParameterAttribute(method, attributeName))
            {
                return false;
            }
        }
        return true;
    }

    int ASTMethodLookup::scoreMethodLookupCandidate(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const
    {
        if (method == nullptr)
        {
            return -1;
        }

        int score = 0;
        if (options.preferredFirstParamTypeName.has_value() && method->getNumParams() > 0)
        {
            const auto paramType = mVisitor.getUnqualifiedType(method->getParamDecl(0)->getType());
            const std::string firstParamTypeName = const_cast<BaseASTVisitor &>(mVisitor).generateTypeCanonicalName(paramType);
            if (firstParamTypeName == options.preferredFirstParamTypeName.value())
            {
                score += 100;
            }
        }
        for (const auto &attributeName : options.preferredParameterAttributes)
        {
            if (methodHasParameterAttribute(method, attributeName))
            {
                score += 10;
            }
        }
        return score;
    }

    std::string ASTMethodLookup::describeMethodOverload(const clang::CXXMethodDecl *method) const
    {
        std::vector<std::string> paramTypes;
        for (const auto *param : method->parameters())
        {
            paramTypes.emplace_back(const_cast<BaseASTVisitor &>(mVisitor).generateTypeCanonicalName(param->getType()));
        }

        std::string signature = method->getQualifiedNameAsString() + "(" + stringJoin(paramTypes, ", ") + ")";
        if (method->isConst())
        {
            signature += " const";
        }
        return signature;
    }

    std::vector<clang::CXXMethodDecl *> ASTMethodLookup::getMethodsFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        std::vector<clang::CXXMethodDecl *> methods;
        if (decl == nullptr)
        {
            return methods;
        }
        for (auto *method : decl->methods())
        {
            if (method->getNameAsString() == name)
            {
                methods.emplace_back(method);
            }
        }
        return methods;
    }

    clang::CXXMethodDecl *ASTMethodLookup::getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        return getMethodFromClass(decl, name, {});
    }

    clang::CXXMethodDecl *ASTMethodLookup::getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name, const MethodLookupOptions &options) const
    {
        auto methods = getMethodsFromClass(decl, name);
        std::vector<clang::CXXMethodDecl *> filteredMethods;
        for (auto *method : methods)
        {
            if (methodMatchesLookupOptions(method, options))
            {
                filteredMethods.emplace_back(method);
            }
        }
        if (filteredMethods.empty())
        {
            return nullptr;
        }
        if (filteredMethods.size() == 1)
        {
            return filteredMethods.front();
        }

        clang::CXXMethodDecl *bestCandidate = nullptr;
        int bestScore = -1;
        bool uniqueBest = false;
        for (auto *method : filteredMethods)
        {
            const int score = scoreMethodLookupCandidate(method, options);
            if (score > bestScore)
            {
                bestScore = score;
                bestCandidate = method;
                uniqueBest = true;
            }
            else if (score == bestScore)
            {
                uniqueBest = false;
            }
        }
        if (bestCandidate != nullptr && uniqueBest && bestScore > 0)
        {
            return bestCandidate;
        }

        std::vector<std::string> overloadDescriptions;
        for (auto *method : filteredMethods)
        {
            overloadDescriptions.emplace_back(describeMethodOverload(method));
        }
        throw std::runtime_error(
            "Found multiple overloaded functions named \"" + name + "\" in class \"" + decl->getQualifiedNameAsString()
            + "\" and could not disambiguate them for DSL code generation. Matching overloads: " + stringJoin(overloadDescriptions, "; "));
    }

    MethodLookupOptions ASTMethodLookup::makeCreateMethodLookupOptions() const
    {
        MethodLookupOptions options;
        options.requiredMethodAttributes = {mUGLCTORName};
        options.requireBody = true;
        return options;
    }

    MethodLookupOptions ASTMethodLookup::makeVertexShaderMethodLookupOptions() const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        options.preferredParameterAttributes = {
            mUGLAttributeVertexInputName + "0",
            mUGLAttributeVertexIDName,
            mUGLAttributeInstanceIDName,
            mUGLAttributeRenderEntityIDName,
            mUGLAttributeRenderEntityInstanceIDName,
        };
        return options;
    }

    MethodLookupOptions ASTMethodLookup::makeFragmentShaderMethodLookupOptions(const std::optional<clang::QualType> &preferredFirstParamType) const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        options.preferredParameterAttributes = {
            mUGLAttributeBarycentricsName,
            mUGLAttributePrimitiveIDName,
        };
        if (preferredFirstParamType.has_value())
        {
            const auto paramType = mVisitor.getUnqualifiedType(preferredFirstParamType.value());
            options.preferredFirstParamTypeName = const_cast<BaseASTVisitor &>(mVisitor).generateTypeCanonicalName(paramType);
        }
        return options;
    }

    MethodLookupOptions ASTMethodLookup::makeComputeShaderMethodLookupOptions() const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        options.preferredParameterAttributes = {
            mUGLAttributeDispatchThreadIDName,
            mUGLAttributeGroupThreadIDName,
            mUGLAttributeGroupIDName,
            mUGLAttributeGroupIndexName,
        };
        return options;
    }

    MethodLookupOptions ASTMethodLookup::makeDomainShaderMethodLookupOptions() const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        options.preferredParameterAttributes = {
            mUGLAttributeDomainLocationName,
        };
        return options;
    }

    MethodLookupOptions ASTMethodLookup::makeHullShaderMethodLookupOptions() const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        return options;
    }
} // namespace UGLC::CodeGen
