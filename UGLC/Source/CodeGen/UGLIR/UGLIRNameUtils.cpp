#include "UGLIRNameUtils.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclLookups.h>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/SourceManager.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/GlobalDecl.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/Type.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/raw_ostream.h>

#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Returns true when the string starts with the requested prefix. */
        bool startsWith(const std::string &value, const std::string &prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }

        /** Removes common C++ elaborated-type prefixes from a diagnostic or type spelling. */
        std::string stripElaboratedPrefix(std::string name)
        {
            const std::string structPrefix = "struct ";
            const std::string classPrefix = "class ";
            if (startsWith(name, structPrefix))
            {
                return name.substr(structPrefix.size());
            }
            if (startsWith(name, classPrefix))
            {
                return name.substr(classPrefix.size());
            }
            return name;
        }

        /** Returns a compact type spelling suitable for deterministic template argument encoding. */
        std::string typeSpelling(const clang::QualType &type)
        {
            clang::LangOptions langOptions;
            clang::PrintingPolicy policy(langOptions);
            policy.SuppressTagKeyword = true;
            policy.SuppressScope = false;
            policy.FullyQualifiedName = false;
            return stripElaboratedPrefix(type.getUnqualifiedType().getAsString(policy));
        }

        /** Converts an arbitrary C++ spelling into a deterministic identifier fragment. */
        std::string encodeSymbolFragment(const std::string &value)
        {
            std::string result;
            result.reserve(value.size());
            for (size_t index = 0; index < value.size(); ++index)
            {
                const char ch = value[index];
                if (ch == ':' && index + 1 < value.size() && value[index + 1] == ':')
                {
                    result.push_back('_');
                    ++index;
                    continue;
                }
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    result.push_back(ch);
                    continue;
                }
                result.push_back('_');
            }
            while (!result.empty() && result.front() == '_')
            {
                result.erase(result.begin());
            }
            while (!result.empty() && result.back() == '_')
            {
                result.pop_back();
            }
            return result.empty() ? "unnamed" : result;
        }

        /** Encodes raw identity bytes as lowercase hexadecimal identifier characters. */
        std::string encodeHexBytes(const std::string &value)
        {
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(value.size() * 2u);
            for (const unsigned char byte : value)
            {
                result += hex[byte >> 4u];
                result += hex[byte & 15u];
            }
            return result;
        }

        /** Returns the qualified primary-template record name when the record is a template specialization. */
        std::string getRecordPrimaryName(const clang::ClassTemplateSpecializationDecl &specializationDecl)
        {
            if (const clang::ClassTemplateDecl *templateDecl = specializationDecl.getSpecializedTemplate())
            {
                if (const clang::CXXRecordDecl *templatedDecl = templateDecl->getTemplatedDecl())
                {
                    return templatedDecl->getQualifiedNameAsString();
                }
            }
            return specializationDecl.getQualifiedNameAsString();
        }

        /** Returns the qualified primary-template function name when the function is a template specialization. */
        std::string getFunctionPrimaryName(const clang::FunctionDecl &functionDecl)
        {
            if (const clang::FunctionTemplateDecl *templateDecl = functionDecl.getPrimaryTemplate())
            {
                if (const clang::FunctionDecl *templatedDecl = templateDecl->getTemplatedDecl())
                {
                    return templatedDecl->getQualifiedNameAsString();
                }
            }
            return functionDecl.getQualifiedNameAsString();
        }

        /** Returns true when a template parameter is an unconstrained or constrained auto NTTP. */
        bool hasAutoTemplateParameter(const clang::TemplateParameterList *parameters)
        {
            if (parameters == nullptr)
            {
                return false;
            }
            for (const clang::NamedDecl *parameter : *parameters)
            {
                const auto *nonTypeParameter = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(parameter);
                if (nonTypeParameter != nullptr && nonTypeParameter->getType()->getContainedAutoType() != nullptr)
                {
                    return true;
                }
            }
            return false;
        }

        /** Returns true when an existing template argument fragment intentionally omits its identity. */
        bool hasLossyTemplateArgument(const clang::TemplateArgument &argument)
        {
            switch (argument.getKind())
            {
            case clang::TemplateArgument::Declaration:
            case clang::TemplateArgument::Expression:
            case clang::TemplateArgument::Template:
            case clang::TemplateArgument::TemplateExpansion:
            case clang::TemplateArgument::StructuralValue:
                return true;
            case clang::TemplateArgument::Pack:
                for (const clang::TemplateArgument &packArgument : argument.pack_elements())
                {
                    if (hasLossyTemplateArgument(packArgument))
                    {
                        return true;
                    }
                }
                return false;
            default:
                return false;
            }
        }

        /** Returns true when a specialization needs a Clang mangle identity suffix. */
        bool needsMangledTemplateIdentity(const clang::TemplateArgumentList &arguments,
                                          const clang::TemplateParameterList *parameters)
        {
            if (hasAutoTemplateParameter(parameters))
            {
                return true;
            }
            for (const clang::TemplateArgument &argument : arguments.asArray())
            {
                if (hasLossyTemplateArgument(argument))
                {
                    return true;
                }
            }
            return false;
        }

        /** Returns the Clang canonical type identity for a concrete record specialization. */
        std::string mangleRecordIdentity(const clang::ClassTemplateSpecializationDecl &specializationDecl)
        {
            std::string mangled;
            llvm::raw_string_ostream stream(mangled);
            std::unique_ptr<clang::MangleContext> mangleContext(
                specializationDecl.getASTContext().createMangleContext());
            mangleContext->mangleCanonicalTypeName(
                specializationDecl.getASTContext().getRecordType(&specializationDecl), stream);
            stream.flush();
            return mangled;
        }

        /** Returns the Clang ABI identity for a concrete function specialization. */
        std::string mangleFunctionIdentity(const clang::FunctionDecl &functionDecl)
        {
            std::string mangled;
            llvm::raw_string_ostream stream(mangled);
            std::unique_ptr<clang::MangleContext> mangleContext(
                functionDecl.getASTContext().createMangleContext());
            if (const auto *constructorDecl = llvm::dyn_cast<clang::CXXConstructorDecl>(&functionDecl))
            {
                mangleContext->mangleName(clang::GlobalDecl(constructorDecl, clang::Ctor_Complete), stream);
            }
            else if (const auto *destructorDecl = llvm::dyn_cast<clang::CXXDestructorDecl>(&functionDecl))
            {
                mangleContext->mangleName(clang::GlobalDecl(destructorDecl, clang::Dtor_Complete), stream);
            }
            else
            {
                mangleContext->mangleName(clang::GlobalDecl(&functionDecl), stream);
            }
            stream.flush();
            return mangled;
        }

        /** Appends a byte-safe Clang identity suffix while retaining the readable UGLIR name prefix. */
        void appendMangledTemplateIdentity(std::string &name, const std::string &mangled)
        {
            name += "__M";
            name += encodeHexBytes(mangled);
        }

        /** Encodes one template argument into a stable fragment used by UGLIR specialization symbols. */
        std::string encodeTemplateArgument(const clang::TemplateArgument &argument)
        {
            switch (argument.getKind())
            {
            case clang::TemplateArgument::Type:
                return "T" + encodeSymbolFragment(typeSpelling(argument.getAsType()));
            case clang::TemplateArgument::Integral:
            {
                llvm::SmallString<32> valueText;
                argument.getAsIntegral().toString(valueText, 10);
                return "I" + std::string(valueText.str());
            }
            case clang::TemplateArgument::Declaration:
                if (const clang::ValueDecl *decl = argument.getAsDecl())
                {
                    return "D" + encodeSymbolFragment(decl->getQualifiedNameAsString());
                }
                return "Dnull";
            case clang::TemplateArgument::NullPtr:
                return "Null";
            case clang::TemplateArgument::Template:
                return "Template";
            case clang::TemplateArgument::TemplateExpansion:
                return "TemplateExpansion";
            case clang::TemplateArgument::Expression:
                return "Expr";
            case clang::TemplateArgument::Pack:
            {
                std::vector<std::string> packFragments;
                for (const clang::TemplateArgument &packArgument : argument.pack_elements())
                {
                    packFragments.push_back(encodeTemplateArgument(packArgument));
                }
                std::ostringstream stream;
                stream << "Pack";
                for (const std::string &fragment : packFragments)
                {
                    stream << '_' << fragment;
                }
                return stream.str();
            }
            case clang::TemplateArgument::Null:
                return "NullArg";
            case clang::TemplateArgument::StructuralValue:
                return "Structural";
            }
            return "Arg";
        }

        /** Encodes all template arguments into the suffix used by a specialization symbol. */
        std::string encodeTemplateArgumentList(const clang::TemplateArgumentList &arguments)
        {
            std::ostringstream stream;
            for (unsigned index = 0; index < arguments.size(); ++index)
            {
                if (index == 0)
                {
                    stream << "__";
                }
                else
                {
                    stream << '_';
                }
                stream << encodeTemplateArgument(arguments[index]);
            }
            return stream.str();
        }

        /** Builds a stable symbol for a class-template specialization and preserves legacy argument fragments. */
        std::string makeRecordSpecializationSymbolName(const clang::ClassTemplateSpecializationDecl &specializationDecl)
        {
            const clang::ClassTemplateDecl *templateDecl = specializationDecl.getSpecializedTemplate();
            const clang::TemplateParameterList *parameters = templateDecl == nullptr ? nullptr : templateDecl->getTemplateParameters();
            const clang::TemplateArgumentList &arguments = specializationDecl.getTemplateArgs();
            std::string name = getRecordPrimaryName(specializationDecl) + encodeTemplateArgumentList(arguments);
            if (needsMangledTemplateIdentity(arguments, parameters))
            {
                appendMangledTemplateIdentity(name, mangleRecordIdentity(specializationDecl));
            }
            return name;
        }
    } // namespace

    std::string makeUGLIRRecordSymbolName(const clang::CXXRecordDecl &recordDecl)
    {
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&recordDecl))
        {
            return makeRecordSpecializationSymbolName(*specializationDecl);
        }

        const clang::CXXRecordDecl &resolvedDecl = recordDecl.getDefinition() == nullptr ? recordDecl : *recordDecl.getDefinition();
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&resolvedDecl))
        {
            return makeRecordSpecializationSymbolName(*specializationDecl);
        }
        return resolvedDecl.getQualifiedNameAsString();
    }

    std::string makeUGLIRFunctionSymbolName(const clang::FunctionDecl &functionDecl)
    {
        const clang::FunctionDecl *definition = nullptr;
        const clang::FunctionDecl &resolvedDecl = functionDecl.hasBody(definition) && definition != nullptr ? *definition : functionDecl;
        std::string name = resolvedDecl.getQualifiedNameAsString();
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&resolvedDecl))
            name = makeUGLIRRecordSymbolName(*method->getParent()) + "." + method->getNameAsString();
        else if (resolvedDecl.getPrimaryTemplate() != nullptr)
            name = getFunctionPrimaryName(resolvedDecl);
        if (const auto *arguments = resolvedDecl.getTemplateSpecializationArgs())
        {
            name += encodeTemplateArgumentList(*arguments);
            const clang::FunctionTemplateDecl *templateDecl = resolvedDecl.getPrimaryTemplate();
            const clang::TemplateParameterList *parameters = templateDecl == nullptr ? nullptr : templateDecl->getTemplateParameters();
            if (needsMangledTemplateIdentity(*arguments, parameters))
            {
                appendMangledTemplateIdentity(name, mangleFunctionIdentity(resolvedDecl));
            }
        }

        const clang::Decl *firstFunction = nullptr;
        bool overloaded = false;
        for (const auto *candidate : resolvedDecl.getDeclContext()->lookup(resolvedDecl.getDeclName()))
        {
            if (!llvm::isa<clang::FunctionDecl>(candidate) && !llvm::isa<clang::FunctionTemplateDecl>(candidate))
                continue;
            const auto *canonical = candidate->getCanonicalDecl();
            if (firstFunction != nullptr && canonical != firstFunction)
                overloaded = true;
            firstFunction = canonical;
        }
        const auto &sources = resolvedDecl.getASTContext().getSourceManager();
        const auto location = sources.getExpansionLoc(resolvedDecl.getLocation());
        const auto filename = sources.getFilename(location);
        if ((overloaded || resolvedDecl.isOverloadedOperator() || llvm::isa<clang::CXXConversionDecl>(resolvedDecl)) && !sources.isInSystemHeader(location) && !filename.contains("/UGLHeaders/") && !filename.contains("\\UGLHeaders\\"))
        {
            // Encode every byte, rather than sanitizing punctuation that can
            // collapse distinct pointer/reference and qualified signatures.
            name += "__S";
            name += encodeHexBytes(resolvedDecl.getNameAsString() + ":" + typeSpelling(resolvedDecl.getType().getCanonicalType()));
        }
        return name;
    }

    std::string makeUGLIRLambdaSymbolName(const std::string &ownerFunctionSymbol, uint32_t ordinal)
    {
        return ownerFunctionSymbol + ".lambda" + std::to_string(ordinal);
    }

    std::string makeUGLIRDebugArtifactStem(const std::string &moduleName)
    {
        std::string result;
        result.reserve(moduleName.size());
        for (size_t index = 0; index < moduleName.size(); ++index)
        {
            const char ch = moduleName[index];
            if (ch == ':' && index + 1 < moduleName.size() && moduleName[index + 1] == ':')
            {
                result.push_back('_');
                ++index;
                continue;
            }
            if (ch == '.')
            {
                result += "__";
                continue;
            }
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')
            {
                result.push_back(ch);
                continue;
            }
            result.push_back('_');
        }
        return result.empty() ? "UGLIRModule" : result;
    }
} // namespace UGLC::CodeGen::UGLIR
