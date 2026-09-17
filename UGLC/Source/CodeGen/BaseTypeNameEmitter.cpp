#include "BaseTypeNameEmitter.hpp"

#include "AbstractTypeConvertor.hpp"
#include "BaseASTVisitor.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <cctype>
#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen
{
    namespace
    {
        /** Converts a concrete template spelling into an identifier-safe shader type suffix. */
        std::string sanitizeTemplateSpecializationComponent(const std::string &input)
        {
            std::string result;
            result.reserve(input.size());
            bool lastWasSeparator = false;
            for (const unsigned char c : input)
            {
                if (std::isalnum(c) != 0)
                {
                    result.push_back(static_cast<char>(c));
                    lastWasSeparator = false;
                    continue;
                }

                if (!lastWasSeparator)
                {
                    result.push_back('_');
                    lastWasSeparator = true;
                }
            }

            while (!result.empty() && result.front() == '_')
            {
                result.erase(result.begin());
            }
            while (!result.empty() && result.back() == '_')
            {
                result.pop_back();
            }
            return result.empty() ? "Value" : result;
        }
    } // namespace

    BaseTypeNameEmitter::BaseTypeNameEmitter(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::string BaseTypeNameEmitter::generateTypeCanonicalName(const clang::QualType &qt, const AbstractTypeConvertor *typeConvertor) const
    {
        std::string result;
        if (const auto resolvedType = mVisitor.tryResolveTemplateSubstitutionType(qt); resolvedType.has_value())
        {
            return generateTypeCanonicalName(*resolvedType, typeConvertor);
        }

        clang::QualType realQt = mVisitor.getUnqualifiedType(qt);
        if (auto record = realQt->getAsCXXRecordDecl())
        {
            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record))
            {
                if (const auto erasedName = mVisitor.getErasedTemplateSpecializationName(specializationDecl, typeConvertor); erasedName.has_value())
                {
                    return *erasedName;
                }
            }

            std::vector<std::string> templateArgStrs;
            const auto args = mVisitor.getTemplateArgumentsFromType(realQt);

            for (const auto &arg : args)
            {
                templateArgStrs.push_back(translateTemplateArgument(arg, typeConvertor));
            }

            result = mVisitor.getClassCanonicalName(record);

            if (typeConvertor != nullptr)
            {
                const std::string className = mVisitor.getClassCanonicalName(record);
                result = typeConvertor->convertType(className, templateArgStrs);
                if (llvm::isa<clang::ClassTemplateSpecializationDecl>(record) &&
                    !templateArgStrs.empty() &&
                    typeConvertor->shouldMaterializeTemplateSpecializationName(className))
                {
                    std::string materializedName = result;
                    for (const auto &templateArgStr : templateArgStrs)
                    {
                        materializedName += "__";
                        materializedName += sanitizeTemplateSpecializationComponent(templateArgStr);
                    }
                    return materializedName;
                }
                if (typeConvertor->checkShouldIgnoreTemplateParams(className))
                {
                    return result;
                }
            }

            if (!templateArgStrs.empty())
            {
                result += "<";
                for (size_t i = 0; i < templateArgStrs.size(); ++i)
                {
                    if (i > 0)
                    {
                        result += ", ";
                    }
                    result += templateArgStrs[i];
                }
                result += ">";
            }
        }
        else
        {
            result = typeConvertor == nullptr ? realQt.getAsString() : typeConvertor->convertType(realQt.getAsString(), {});
        }
        return result;
    }

    std::string BaseTypeNameEmitter::generateTypeTemplateArgs(const clang::QualType &qt, const AbstractTypeConvertor *typeConvertor) const
    {
        const std::vector<clang::TemplateArgument> args = mVisitor.getTemplateArgumentsFromType(qt);
        if (args.empty())
        {
            return "";
        }

        std::vector<std::string> argStrs;
        argStrs.reserve(args.size());
        for (const auto &arg : args)
        {
            argStrs.push_back(translateTemplateArgument(arg, typeConvertor));
        }

        return "<" + stringJoin(argStrs, ", ") + ">";
    }

    std::string BaseTypeNameEmitter::translateTemplateArgument(const clang::TemplateArgument &arg, const AbstractTypeConvertor *typeConvertor) const
    {
        switch (arg.getKind())
        {
        case clang::TemplateArgument::Type:
            return generateTypeCanonicalName(arg.getAsType(), typeConvertor);

        case clang::TemplateArgument::Integral:
            return std::to_string(arg.getAsIntegral().getSExtValue());

        case clang::TemplateArgument::Expression:
            if (auto *expr = arg.getAsExpr())
            {
                clang::Expr::EvalResult intResult;
                if (expr->EvaluateAsInt(intResult, *mVisitor.Context))
                {
                    return std::to_string(intResult.Val.getInt().getExtValue());
                }
                return mVisitor.TranslateExpr(expr);
            }
            break;

        case clang::TemplateArgument::Pack: {
            std::vector<std::string> argStrs;
            for (auto it = arg.pack_begin(); it != arg.pack_end(); ++it)
            {
                argStrs.push_back(translateTemplateArgument(*it, typeConvertor));
            }
            return stringJoin(argStrs, ", ");
        }

        case clang::TemplateArgument::Declaration:
            if (auto *decl = arg.getAsDecl())
            {
                if (auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl))
                {
                    return namedDecl->getQualifiedNameAsString();
                }
            }
            break;

        case clang::TemplateArgument::Template:
            if (auto *templateDecl = arg.getAsTemplate().getAsTemplateDecl())
            {
                return templateDecl->getQualifiedNameAsString();
            }
            break;

        case clang::TemplateArgument::Null:
            return "";

        case clang::TemplateArgument::NullPtr:
            return "nullptr";

        default:
            return mVisitor.makeUnsupportedPlaceholder("template argument", "kind " + std::to_string(static_cast<int>(arg.getKind())));
        }

        return mVisitor.makeUnsupportedPlaceholder("template argument", "invalid");
    }

    std::string BaseTypeNameEmitter::translateNestedNameSpecifier(const clang::NestedNameSpecifier *specifier,
                                                                  const AbstractTypeConvertor *typeConvertor) const
    {
        if (specifier == nullptr)
        {
            return "";
        }

        const std::string prefix = translateNestedNameSpecifier(specifier->getPrefix(), typeConvertor);
        switch (specifier->getKind())
        {
        case clang::NestedNameSpecifier::Identifier:
            return prefix + specifier->getAsIdentifier()->getName().str() + "::";

        case clang::NestedNameSpecifier::Namespace: {
            const std::string namespaceName = specifier->getAsNamespace()->getNameAsString();
            return namespaceName.empty() ? prefix : prefix + namespaceName + "::";
        }

        case clang::NestedNameSpecifier::NamespaceAlias: {
            const std::string namespaceAlias = specifier->getAsNamespaceAlias()->getNameAsString();
            return namespaceAlias.empty() ? prefix : prefix + namespaceAlias + "::";
        }

        case clang::NestedNameSpecifier::TypeSpec:
        case clang::NestedNameSpecifier::TypeSpecWithTemplate: {
            const clang::Type *type = specifier->getAsType();
            if (type->getAs<clang::RecordType>() != nullptr)
            {
                return generateTypeCanonicalName(clang::QualType(type, 0), typeConvertor) + "::";
            }
            if (const auto *enumType = type->getAs<clang::EnumType>())
            {
                if (typeConvertor != nullptr)
                {
                    return prefix + typeConvertor->convertType(enumType->getDecl()->getQualifiedNameAsString(), {}) + "::";
                }
                return prefix + enumType->getDecl()->getQualifiedNameAsString() + "::";
            }
            return prefix + generateTypeCanonicalName(clang::QualType(type, 0), typeConvertor) + "::";
        }

        case clang::NestedNameSpecifier::Global:
            return "::";

        default:
            return prefix;
        }
    }

    std::string BaseTypeNameEmitter::generateTemplateCallArguments(const clang::Expr *calleeExpr, const AbstractTypeConvertor *typeConvertor) const
    {
        const auto args = generateTemplateCallArgumentsStr(calleeExpr, typeConvertor);
        return args.empty() ? "" : "<" + stringJoin(args, ", ") + ">";
    }

    std::vector<std::string> BaseTypeNameEmitter::generateTemplateCallArgumentsStr(const clang::Expr *calleeExpr,
                                                                                   const AbstractTypeConvertor *typeConvertor) const
    {
        const auto *callee = calleeExpr->IgnoreParenCasts();

        auto extractArgs = [this, typeConvertor](auto *nodeWithArgs) -> std::vector<std::string> {
            if (nodeWithArgs == nullptr || !nodeWithArgs->hasExplicitTemplateArgs())
            {
                return {};
            }

            std::vector<std::string> args;
            const clang::TemplateArgumentLoc *templateArgs = nodeWithArgs->getTemplateArgs();
            for (unsigned i = 0; i < nodeWithArgs->getNumTemplateArgs(); ++i)
            {
                args.emplace_back(translateTemplateArgument(templateArgs[i].getArgument(), typeConvertor));
            }
            return args;
        };

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(callee))
        {
            return extractArgs(declRefExpr);
        }
        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(callee))
        {
            return extractArgs(memberExpr);
        }

        return {};
    }
} // namespace UGLC::CodeGen
