#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Casting.h>

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UGLC::CodeGen
{
    /** Stores the result of resolving a DSL integer constant expression for attributes and template-driven layout values. */
    struct IntegerConstantEvaluationResult
    {
        bool success = false;
        int64_t value = 0;
        std::string resolvedExpression;
        std::string error;
    };

    namespace Detail
    {
        inline bool isIdentifierStartChar(char c)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            return std::isalpha(byte) || c == '_';
        }

        inline bool isIdentifierBodyChar(char c)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            return std::isalnum(byte) || c == '_';
        }

        inline bool isNumericLiteralStart(const std::string &expression, size_t offset)
        {
            if (offset >= expression.size())
            {
                return false;
            }

            const unsigned char current = static_cast<unsigned char>(expression[offset]);
            if (std::isdigit(current))
            {
                return true;
            }

            return expression[offset] == '.'
                   && offset + 1 < expression.size()
                   && std::isdigit(static_cast<unsigned char>(expression[offset + 1]));
        }

        inline size_t consumeNumericLiteral(const std::string &expression, size_t offset)
        {
            while (offset < expression.size())
            {
                const char current = expression[offset];
                const unsigned char currentByte = static_cast<unsigned char>(current);
                if (std::isalnum(currentByte) || current == '_' || current == '\'' || current == '.')
                {
                    ++offset;
                    continue;
                }
                if ((current == '+' || current == '-')
                    && offset > 0
                    && (expression[offset - 1] == 'e' || expression[offset - 1] == 'E' || expression[offset - 1] == 'p' || expression[offset - 1] == 'P'))
                {
                    ++offset;
                    continue;
                }
                break;
            }
            return offset;
        }

        inline bool isScopedIdentifierStart(const std::string &expression, size_t offset)
        {
            if (offset >= expression.size())
            {
                return false;
            }

            if (isIdentifierStartChar(expression[offset]))
            {
                return true;
            }

            return expression[offset] == ':'
                   && offset + 2 < expression.size()
                   && expression[offset + 1] == ':'
                   && isIdentifierStartChar(expression[offset + 2]);
        }

        inline size_t consumeScopedIdentifier(const std::string &expression, size_t offset)
        {
            if (expression[offset] == ':' && offset + 1 < expression.size() && expression[offset + 1] == ':')
            {
                offset += 2;
            }

            while (offset < expression.size() && isIdentifierBodyChar(expression[offset]))
            {
                ++offset;
            }

            while (offset + 2 < expression.size()
                   && expression[offset] == ':'
                   && expression[offset + 1] == ':'
                   && isIdentifierStartChar(expression[offset + 2]))
            {
                offset += 2;
                while (offset < expression.size() && isIdentifierBodyChar(expression[offset]))
                {
                    ++offset;
                }
            }

            return offset;
        }

        inline const clang::NamedDecl *unwrapUsingShadowNamedDecl(const clang::NamedDecl *namedDecl)
        {
            if (const auto *usingShadowDecl = llvm::dyn_cast_or_null<clang::UsingShadowDecl>(namedDecl))
            {
                return llvm::dyn_cast_or_null<clang::NamedDecl>(usingShadowDecl->getTargetDecl()->getCanonicalDecl());
            }
            return namedDecl;
        }

        inline const clang::ValueDecl *tryGetConstantValueDecl(const clang::NamedDecl *namedDecl)
        {
            namedDecl = unwrapUsingShadowNamedDecl(namedDecl);
            const auto *valueDecl = llvm::dyn_cast_or_null<clang::ValueDecl>(namedDecl);
            if (valueDecl == nullptr || llvm::isa<clang::FunctionDecl>(valueDecl))
            {
                return nullptr;
            }
            return (llvm::isa<clang::VarDecl>(valueDecl) || llvm::isa<clang::EnumConstantDecl>(valueDecl)) ? valueDecl : nullptr;
        }

        inline const clang::DeclContext *tryGetLookupDeclContext(const clang::NamedDecl *namedDecl)
        {
            namedDecl = unwrapUsingShadowNamedDecl(namedDecl);
            if (const auto *namespaceDecl = llvm::dyn_cast_or_null<clang::NamespaceDecl>(namedDecl))
            {
                return namespaceDecl;
            }
            if (const auto *recordDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(namedDecl))
            {
                return recordDecl;
            }
            if (const auto *classTemplateDecl = llvm::dyn_cast_or_null<clang::ClassTemplateDecl>(namedDecl))
            {
                return classTemplateDecl->getTemplatedDecl();
            }
            if (const auto *enumDecl = llvm::dyn_cast_or_null<clang::EnumDecl>(namedDecl))
            {
                return enumDecl;
            }
            return nullptr;
        }

        inline std::vector<std::string> splitScopedIdentifier(const std::string &identifier)
        {
            std::vector<std::string> segments;
            size_t offset = identifier.starts_with("::") ? 2 : 0;
            while (offset < identifier.size())
            {
                const size_t separator = identifier.find("::", offset);
                const std::string segment = separator == std::string::npos
                                                ? identifier.substr(offset)
                                                : identifier.substr(offset, separator - offset);
                if (!segment.empty())
                {
                    segments.emplace_back(segment);
                }
                if (separator == std::string::npos)
                {
                    break;
                }
                offset = separator + 2;
            }
            return segments;
        }

        inline std::optional<std::string> tryEvaluateCompileTimeIntegerValueDeclToLiteral(const clang::ValueDecl *valueDecl,
                                                                                           clang::ASTContext &context)
        {
            if (const auto *enumConstantDecl = llvm::dyn_cast_or_null<clang::EnumConstantDecl>(valueDecl))
            {
                const llvm::APSInt initValue = enumConstantDecl->getInitVal();
                llvm::SmallString<32> literalBuffer;
                initValue.toString(literalBuffer, 10);
                return std::string(literalBuffer.str()) + (initValue.isUnsigned() ? "u" : "");
            }

            const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(valueDecl);
            if (varDecl == nullptr || !varDecl->hasInit())
            {
                return std::nullopt;
            }
            if (!varDecl->getType()->isIntegralOrEnumerationType())
            {
                return std::nullopt;
            }
            if (!varDecl->mightBeUsableInConstantExpressions(context))
            {
                return std::nullopt;
            }

            clang::Expr::EvalResult intResult;
            if (!varDecl->getInit()->EvaluateAsInt(intResult, context))
            {
                return std::nullopt;
            }

            const llvm::APSInt &value = intResult.Val.getInt();
            llvm::SmallString<32> literalBuffer;
            value.toString(literalBuffer, 10);
            return std::string(literalBuffer.str()) + (value.isUnsigned() ? "u" : "");
        }

        inline const clang::ValueDecl *resolveVisibleConstantValueDecl(const clang::DeclContext *declContext,
                                                                       const std::string &identifier,
                                                                       clang::ASTContext &context)
        {
            if (declContext == nullptr || identifier.empty())
            {
                return nullptr;
            }

            clang::IdentifierInfo &identifierInfo = context.Idents.get(identifier);
            const clang::DeclarationName declarationName(&identifierInfo);

            for (const clang::DeclContext *currentContext = declContext; currentContext != nullptr; currentContext = currentContext->getParent())
            {
                for (const auto *decl : currentContext->lookup(declarationName))
                {
                    const auto *candidateDecl = llvm::dyn_cast<clang::NamedDecl>(decl->getCanonicalDecl());
                    if (const auto *valueDecl = tryGetConstantValueDecl(candidateDecl))
                    {
                        return valueDecl;
                    }
                }
            }

            return nullptr;
        }

        inline const clang::ValueDecl *resolveQualifiedConstantValueDecl(const std::string &qualifiedIdentifier,
                                                                         clang::ASTContext &context)
        {
            const std::vector<std::string> segments = splitScopedIdentifier(qualifiedIdentifier);
            if (segments.empty())
            {
                return nullptr;
            }

            const clang::DeclContext *currentContext = context.getTranslationUnitDecl();
            for (size_t index = 0; index + 1 < segments.size(); ++index)
            {
                clang::IdentifierInfo &identifierInfo = context.Idents.get(segments[index]);
                const clang::DeclarationName declarationName(&identifierInfo);
                const clang::DeclContext *nextContext = nullptr;
                for (const auto *decl : currentContext->lookup(declarationName))
                {
                    const auto *candidateDecl = llvm::dyn_cast<clang::NamedDecl>(decl->getCanonicalDecl());
                    if (const auto *lookupContext = tryGetLookupDeclContext(candidateDecl))
                    {
                        nextContext = lookupContext;
                        break;
                    }
                }

                if (nextContext == nullptr)
                {
                    return nullptr;
                }
                currentContext = nextContext;
            }

            clang::IdentifierInfo &identifierInfo = context.Idents.get(segments.back());
            const clang::DeclarationName declarationName(&identifierInfo);
            for (const auto *decl : currentContext->lookup(declarationName))
            {
                const auto *candidateDecl = llvm::dyn_cast<clang::NamedDecl>(decl->getCanonicalDecl());
                if (const auto *valueDecl = tryGetConstantValueDecl(candidateDecl))
                {
                    return valueDecl;
                }
            }

            return nullptr;
        }

        class IntegerConstantExpressionParser
        {
        public:
            explicit IntegerConstantExpressionParser(std::string_view input)
                : mInput(input)
            {
            }

            bool parse(int64_t &value, std::string &error)
            {
                skipWhitespace();
                if (!parseShift(value, error))
                {
                    return false;
                }
                skipWhitespace();
                if (mOffset != mInput.size())
                {
                    error = "contains unsupported trailing token near \"" + std::string(mInput.substr(mOffset)) + "\"";
                    return false;
                }
                return true;
            }

        private:
            std::string_view mInput;
            size_t mOffset = 0;

            void skipWhitespace()
            {
                while (mOffset < mInput.size() && std::isspace(static_cast<unsigned char>(mInput[mOffset])))
                {
                    ++mOffset;
                }
            }

            bool consumeChar(char expected)
            {
                skipWhitespace();
                if (mOffset < mInput.size() && mInput[mOffset] == expected)
                {
                    ++mOffset;
                    return true;
                }
                return false;
            }

            bool consumeToken(std::string_view token)
            {
                skipWhitespace();
                if (mInput.substr(mOffset).starts_with(token))
                {
                    mOffset += token.size();
                    return true;
                }
                return false;
            }

            bool parseShift(int64_t &value, std::string &error)
            {
                if (!parseAdditive(value, error))
                {
                    return false;
                }

                while (true)
                {
                    if (consumeToken("<<"))
                    {
                        int64_t rhs = 0;
                        if (!parseAdditive(rhs, error))
                        {
                            return false;
                        }
                        if (rhs < 0)
                        {
                            error = "uses a negative shift count";
                            return false;
                        }
                        value <<= rhs;
                        continue;
                    }
                    if (consumeToken(">>"))
                    {
                        int64_t rhs = 0;
                        if (!parseAdditive(rhs, error))
                        {
                            return false;
                        }
                        if (rhs < 0)
                        {
                            error = "uses a negative shift count";
                            return false;
                        }
                        value >>= rhs;
                        continue;
                    }
                    return true;
                }
            }

            bool parseAdditive(int64_t &value, std::string &error)
            {
                if (!parseMultiplicative(value, error))
                {
                    return false;
                }

                while (true)
                {
                    if (consumeChar('+'))
                    {
                        int64_t rhs = 0;
                        if (!parseMultiplicative(rhs, error))
                        {
                            return false;
                        }
                        value += rhs;
                        continue;
                    }
                    if (consumeChar('-'))
                    {
                        int64_t rhs = 0;
                        if (!parseMultiplicative(rhs, error))
                        {
                            return false;
                        }
                        value -= rhs;
                        continue;
                    }
                    return true;
                }
            }

            bool parseMultiplicative(int64_t &value, std::string &error)
            {
                if (!parseUnary(value, error))
                {
                    return false;
                }

                while (true)
                {
                    if (consumeChar('*'))
                    {
                        int64_t rhs = 0;
                        if (!parseUnary(rhs, error))
                        {
                            return false;
                        }
                        value *= rhs;
                        continue;
                    }
                    if (consumeChar('/'))
                    {
                        int64_t rhs = 0;
                        if (!parseUnary(rhs, error))
                        {
                            return false;
                        }
                        if (rhs == 0)
                        {
                            error = "divides by zero";
                            return false;
                        }
                        value /= rhs;
                        continue;
                    }
                    if (consumeChar('%'))
                    {
                        int64_t rhs = 0;
                        if (!parseUnary(rhs, error))
                        {
                            return false;
                        }
                        if (rhs == 0)
                        {
                            error = "takes modulo by zero";
                            return false;
                        }
                        value %= rhs;
                        continue;
                    }
                    return true;
                }
            }

            bool parseUnary(int64_t &value, std::string &error)
            {
                if (consumeChar('+'))
                {
                    return parseUnary(value, error);
                }
                if (consumeChar('-'))
                {
                    if (!parseUnary(value, error))
                    {
                        return false;
                    }
                    value = -value;
                    return true;
                }
                return parsePrimary(value, error);
            }

            bool parsePrimary(int64_t &value, std::string &error)
            {
                skipWhitespace();
                if (consumeChar('('))
                {
                    if (!parseShift(value, error))
                    {
                        return false;
                    }
                    if (!consumeChar(')'))
                    {
                        error = "is missing a closing ')'";
                        return false;
                    }
                    return true;
                }

                if (mOffset >= mInput.size())
                {
                    error = "ends unexpectedly";
                    return false;
                }

                const std::string inputCopy(mInput);
                if (isNumericLiteralStart(inputCopy, mOffset))
                {
                    const size_t literalEnd = consumeNumericLiteral(inputCopy, mOffset);
                    const std::string token(inputCopy.substr(mOffset, literalEnd - mOffset));
                    mOffset = literalEnd;
                    return parseIntegerLiteralToken(token, value, error);
                }

                if (isScopedIdentifierStart(inputCopy, mOffset))
                {
                    const size_t identifierEnd = consumeScopedIdentifier(inputCopy, mOffset);
                    error = "references unresolved identifier \"" + inputCopy.substr(mOffset, identifierEnd - mOffset) + "\"";
                    return false;
                }

                error = "contains unsupported token near \"" + inputCopy.substr(mOffset, 1) + "\"";
                return false;
            }

            static bool parseIntegerLiteralToken(const std::string &token, int64_t &value, std::string &error)
            {
                std::string sanitized;
                sanitized.reserve(token.size());
                for (char c : token)
                {
                    if (c != '\'')
                    {
                        sanitized.push_back(c);
                    }
                }

                size_t suffixPos = sanitized.size();
                while (suffixPos > 0)
                {
                    const char suffixChar = sanitized[suffixPos - 1];
                    if (suffixChar == 'u' || suffixChar == 'U' ||
                        suffixChar == 'l' || suffixChar == 'L' ||
                        suffixChar == 'z' || suffixChar == 'Z')
                    {
                        --suffixPos;
                        continue;
                    }
                    break;
                }

                const std::string numericPart = sanitized.substr(0, suffixPos);
                if (numericPart.empty())
                {
                    error = "contains malformed numeric literal \"" + token + "\"";
                    return false;
                }

                try
                {
                    size_t consumed = 0;
                    value = std::stoll(numericPart, &consumed, 0);
                    if (consumed != numericPart.size())
                    {
                        error = "contains malformed numeric literal \"" + token + "\"";
                        return false;
                    }
                    return true;
                }
                catch (const std::exception &)
                {
                    error = "contains malformed numeric literal \"" + token + "\"";
                    return false;
                }
            }
        };

        /** Resolves an integer non-type template parameter from a concrete class template specialization. */
        inline std::optional<std::string> tryResolveClassTemplateSpecializationIntegerArgument(const clang::DeclContext *declContext,
                                                                                               const std::string &identifier,
                                                                                               clang::ASTContext &context)
        {
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(declContext);
            if (specializationDecl == nullptr)
            {
                return std::nullopt;
            }

            const clang::TemplateParameterList *parameterList = specializationDecl->getSpecializedTemplate()->getTemplateParameters();
            const clang::TemplateArgumentList &argumentList = specializationDecl->getTemplateArgs();
            const unsigned parameterCount = std::min(parameterList->size(), argumentList.size());
            for (unsigned index = 0; index < parameterCount; ++index)
            {
                const auto *parameterDecl = parameterList->getParam(index);
                if (parameterDecl == nullptr || parameterDecl->getNameAsString() != identifier)
                {
                    continue;
                }

                const clang::TemplateArgument &argument = argumentList.get(index);
                if (argument.getKind() == clang::TemplateArgument::Integral)
                {
                    return std::to_string(argument.getAsIntegral().getSExtValue());
                }
                if (argument.getKind() == clang::TemplateArgument::Expression && argument.getAsExpr() != nullptr)
                {
                    clang::Expr::EvalResult evalResult;
                    if (argument.getAsExpr()->EvaluateAsInt(evalResult, context))
                    {
                        return std::to_string(evalResult.Val.getInt().getExtValue());
                    }
                }
                return std::nullopt;
            }

            return std::nullopt;
        }
    } // namespace Detail

    inline IntegerConstantEvaluationResult evaluateCompileTimeIntegerExpression(const std::string &expression,
                                                                                const clang::DeclContext *declContext,
                                                                                clang::ASTContext &context)
    {
        IntegerConstantEvaluationResult result;
        if (expression.empty())
        {
            result.error = "is empty";
            return result;
        }

        std::string resolvedExpression;
        resolvedExpression.reserve(expression.size() + 16);

        size_t offset = 0;
        while (offset < expression.size())
        {
            if (Detail::isNumericLiteralStart(expression, offset))
            {
                const size_t literalEnd = Detail::consumeNumericLiteral(expression, offset);
                resolvedExpression.append(expression, offset, literalEnd - offset);
                offset = literalEnd;
                continue;
            }

            if (Detail::isScopedIdentifierStart(expression, offset))
            {
                const size_t identifierEnd = Detail::consumeScopedIdentifier(expression, offset);
                const std::string identifier = expression.substr(offset, identifierEnd - offset);
                const clang::ValueDecl *resolvedDecl = nullptr;
                if (identifier.find("::") != std::string::npos)
                {
                    resolvedDecl = Detail::resolveQualifiedConstantValueDecl(identifier, context);
                }
                else
                {
                    resolvedDecl = Detail::resolveVisibleConstantValueDecl(declContext, identifier, context);
                }

                if (resolvedDecl == nullptr)
                {
                    if (const auto templateArgument = Detail::tryResolveClassTemplateSpecializationIntegerArgument(declContext, identifier, context);
                        templateArgument.has_value())
                    {
                        resolvedExpression += *templateArgument;
                        offset = identifierEnd;
                        continue;
                    }
                    result.error = "references unknown identifier \"" + identifier + "\"";
                    return result;
                }

                if (const auto literalValue = Detail::tryEvaluateCompileTimeIntegerValueDeclToLiteral(resolvedDecl, context); literalValue.has_value())
                {
                    resolvedExpression += *literalValue;
                }
                else
                {
                    result.error = "is not a compile-time integer constant";
                    return result;
                }
                offset = identifierEnd;
                continue;
            }

            resolvedExpression += expression[offset];
            ++offset;
        }

        result.resolvedExpression = resolvedExpression;

        Detail::IntegerConstantExpressionParser parser(resolvedExpression);
        if (!parser.parse(result.value, result.error))
        {
            return result;
        }

        result.success = true;
        return result;
    }
} // namespace UGLC::CodeGen
