#pragma once

#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>

#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * @brief Describes how DSL entry-point and factory methods are selected from C++ overload sets.
     *
     * The lookup rules intentionally keep syntax-level cues, such as UGL attributes and
     * const qualification, separate from the caller. Backends use this structure to pick
     * the same semantic method regardless of whether the selected method later emits HLSL,
     * MSL, or host C++.
     */
    struct MethodLookupOptions
    {
        /** @brief Required number of parameters when present. */
        std::optional<unsigned> parameterCount;
        /** @brief Required const qualification when present. */
        std::optional<bool> constQualified;
        /** @brief Requires the selected declaration to have a body. */
        bool requireBody = false;
        /** @brief Attributes that must be present on the candidate method. */
        std::vector<std::string> requiredMethodAttributes;
        /** @brief Attributes that must be present on at least one candidate parameter. */
        std::vector<std::string> requiredParameterAttributes;
        /** @brief Parameter attributes that break otherwise ambiguous overload sets. */
        std::vector<std::string> preferredParameterAttributes;
        /** @brief Preferred canonical name of the first parameter type for overload disambiguation. */
        std::optional<std::string> preferredFirstParamTypeName;
    };

    /**
     * @brief Resolves DSL-relevant C++ methods while preserving BaseASTVisitor's public API.
     *
     * Example DSL selection:
     *
     * ```cpp
     * struct Pass : UGL::IRenderClass {
     *     Out vertex(In input [[VertexInput0]]);
     *     Out vertex(uint vertexID [[VertexID]]);
     *     float4 fragment(Out input);
     * };
     * ```
     *
     * `makeVertexShaderMethodLookupOptions()` prefers parameters tagged with vertex
     * input or vertex-id attributes, and `makeFragmentShaderMethodLookupOptions()`
     * can prefer the vertex output type as the fragment input type. The chosen
     * `clang::CXXMethodDecl` is then passed unchanged to backend emitters.
     */
    class ASTMethodLookup
    {
    public:
        /**
         * @brief Creates a method resolver that uses the owning visitor for attributes, type names, and diagnostics.
         */
        explicit ASTMethodLookup(const BaseASTVisitor &visitor);

        /** @brief Returns true when a method carries a matching UGL annotation attribute. */
        [[nodiscard]] bool methodHasAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const;

        /** @brief Returns true when any parameter carries a matching UGL annotation attribute. */
        [[nodiscard]] bool methodHasParameterAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const;

        /** @brief Applies the hard filters from a lookup option set. */
        [[nodiscard]] bool methodMatchesLookupOptions(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const;

        /** @brief Scores a candidate for preferred, non-mandatory disambiguation hints. */
        [[nodiscard]] int scoreMethodLookupCandidate(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const;

        /** @brief Produces a human-readable overload signature for diagnostics. */
        [[nodiscard]] std::string describeMethodOverload(const clang::CXXMethodDecl *method) const;

        /** @brief Returns all methods with a given unqualified name from a record declaration. */
        [[nodiscard]] std::vector<clang::CXXMethodDecl *> getMethodsFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const;

        /** @brief Returns the unique matching method or null when no candidate matches. */
        [[nodiscard]] clang::CXXMethodDecl *getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const;

        /** @brief Returns the best matching method according to hard filters and preferred hints. */
        [[nodiscard]] clang::CXXMethodDecl *getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name, const MethodLookupOptions &options) const;

        /** @brief Builds lookup rules for DSL `create` factory methods. */
        [[nodiscard]] MethodLookupOptions makeCreateMethodLookupOptions() const;

        /** @brief Builds lookup rules for DSL vertex shader entry methods. */
        [[nodiscard]] MethodLookupOptions makeVertexShaderMethodLookupOptions() const;

        /** @brief Builds lookup rules for DSL fragment shader entry methods. */
        [[nodiscard]] MethodLookupOptions makeFragmentShaderMethodLookupOptions(const std::optional<clang::QualType> &preferredFirstParamType = std::nullopt) const;

        /** @brief Builds lookup rules for DSL compute shader entry methods. */
        [[nodiscard]] MethodLookupOptions makeComputeShaderMethodLookupOptions() const;

        /** @brief Builds lookup rules for DSL domain shader entry methods. */
        [[nodiscard]] MethodLookupOptions makeDomainShaderMethodLookupOptions() const;

        /** @brief Builds lookup rules for DSL hull shader entry methods. */
        [[nodiscard]] MethodLookupOptions makeHullShaderMethodLookupOptions() const;

    private:
        /** @brief Owning visitor used for attribute queries and canonical type spelling. */
        const BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
