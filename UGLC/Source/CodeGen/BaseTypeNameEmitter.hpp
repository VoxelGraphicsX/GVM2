#pragma once

#include <clang/AST/Expr.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>

#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    class AbstractTypeConvertor;
    class BaseASTVisitor;

    /**
     * @brief Emits backend-neutral C++ type and template argument spellings for shared visitor code.
     *
     * The class keeps Clang template specialization handling out of `BaseASTVisitor`
     * while preserving the same public visitor API. Example DSL:
     *
     * ```cpp
     * UGL::StructuredBuffer<UGL::float4> vertices;
     * value = helper<UGL::float4, 4>(input);
     * ```
     *
     * `generateTypeCanonicalName()` produces the wrapper type spelling used by
     * backend type convertors, and `generateTemplateCallArguments()` lowers the
     * explicit function template call into a string such as `<float4, 4>`.
     */
    class BaseTypeNameEmitter
    {
    public:
        /** @brief Creates an emitter backed by BaseASTVisitor type utilities and expression translation. */
        explicit BaseTypeNameEmitter(BaseASTVisitor &visitor);

        /** @brief Emits a canonical type name after applying an optional backend type convertor. */
        [[nodiscard]] std::string generateTypeCanonicalName(const clang::QualType &qt,
                                                            const AbstractTypeConvertor *typeConvertor = nullptr) const;

        /** @brief Emits the `<...>` suffix for a template specialization type. */
        [[nodiscard]] std::string generateTypeTemplateArgs(const clang::QualType &qt,
                                                           const AbstractTypeConvertor *typeConvertor = nullptr) const;

        /** @brief Translates one Clang template argument into the shared textual form. */
        [[nodiscard]] std::string translateTemplateArgument(const clang::TemplateArgument &arg,
                                                            const AbstractTypeConvertor *typeConvertor = nullptr) const;

        /** @brief Translates a nested name qualifier such as `Namespace::Type::`. */
        [[nodiscard]] std::string translateNestedNameSpecifier(const clang::NestedNameSpecifier *specifier,
                                                               const AbstractTypeConvertor *typeConvertor = nullptr) const;

        /** @brief Emits explicit function template arguments as a complete `<...>` suffix. */
        [[nodiscard]] std::string generateTemplateCallArguments(const clang::Expr *calleeExpr,
                                                                const AbstractTypeConvertor *typeConvertor = nullptr) const;

        /** @brief Returns explicit function template arguments without surrounding angle brackets. */
        [[nodiscard]] std::vector<std::string> generateTemplateCallArgumentsStr(const clang::Expr *calleeExpr,
                                                                                const AbstractTypeConvertor *typeConvertor = nullptr) const;

    private:
        /** @brief Visitor surface used for Clang normalization, expression translation, and diagnostics placeholders. */
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
