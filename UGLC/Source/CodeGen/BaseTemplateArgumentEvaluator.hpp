#pragma once

#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>

#include <cstdint>
#include <vector>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * @brief Extracts and evaluates C++ template arguments used by UGL DSL wrappers.
     *
     * Backends need a single, Clang-aware path for template packs and constant
     * template expressions so that DSL declarations are lowered consistently.
     * Example DSL:
     *
     * ```cpp
     * UGL::RenderSetResource<UGL::Texture2D<UGL::float4>, 8> textures;
     * ```
     *
     * `getArgumentsFromType()` returns the flattened arguments for the wrapper
     * type, and `evaluateIntegerArgument()` converts the `8` into an integral
     * descriptor count used by HLSL, MSL, and host C++ emitters.
     */
    class BaseTemplateArgumentEvaluator
    {
    public:
        /** @brief Creates an evaluator backed by BaseASTVisitor diagnostics and type utilities. */
        explicit BaseTemplateArgumentEvaluator(const BaseASTVisitor &visitor);

        /** @brief Returns the flattened template argument list from a possibly sugared Clang type. */
        [[nodiscard]] std::vector<clang::TemplateArgument> getArgumentsFromType(const clang::QualType &qt) const;

        /** @brief Appends a template argument, expanding nested packs into ordinary arguments. */
        void collectFlattenedArguments(const clang::TemplateArgument &arg, std::vector<clang::TemplateArgument> &outArgs) const;

        /** @brief Evaluates an integral template argument or reports a source-aware code generation error. */
        [[nodiscard]] int64_t evaluateIntegerArgument(const clang::TemplateArgument &arg) const;

    private:
        /** @brief Visitor surface used for type normalization, AST context access, and diagnostics. */
        const BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
