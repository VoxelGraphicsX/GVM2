#pragma once

#include "BaseShaderBinding.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>

#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * @brief Resolves backend-neutral shader resource bindings from UGL BindGroup declarations.
     *
     * The resolver owns the common descriptor ABI interpretation shared by HLSL, MSL,
     * and host C++ emitters. It converts DSL declarations such as:
     *
     * ```cpp
     * struct Material : UGL::BindGroup<Material> {
     *     void create(UGL::Texture2D<UGL::float4> albedo [[Binding0]],
     *                 UGL::Sampler linearSampler [[Binding1]]);
     * };
     * ```
     *
     * into ordered `BaseShaderResourceBinding` entries. Backends then lower those
     * entries into their own target declarations, for example HLSL `Texture2D<float4>`
     * plus `SamplerState`, or MSL texture/sampler arguments.
     */
    class BaseShaderBindingResolver
    {
    public:
        /** @brief Creates a resolver backed by the shared visitor utility surface. */
        explicit BaseShaderBindingResolver(BaseASTVisitor &visitor);

        /** @brief Maps a sampled texture element type to the backend-neutral sample category. */
        [[nodiscard]] BaseShaderTextureSampleType resolveTextureSampleType(const clang::QualType &sampleType,
                                                                            const clang::FieldDecl *fieldDecl,
                                                                            const clang::CXXRecordDecl *bindGroupDecl,
                                                                            int unwrapDepth = 0) const;

        /** @brief Returns the exact template argument list expected by a shader resource wrapper. */
        [[nodiscard]] std::vector<clang::TemplateArgument> requireTemplateArguments(const clang::QualType &resourceType,
                                                                                   size_t expectedCount,
                                                                                   const clang::FieldDecl *fieldDecl,
                                                                                   const clang::CXXRecordDecl *bindGroupDecl) const;

        /** @brief Returns a concrete type template argument from a shader resource wrapper. */
        [[nodiscard]] clang::QualType requireConcreteTypeTemplateArgument(const clang::QualType &resourceType,
                                                                          size_t index,
                                                                          const clang::FieldDecl *fieldDecl,
                                                                          const clang::CXXRecordDecl *bindGroupDecl) const;

        /** @brief Validates that buffer element records contain only plain shader data. */
        void validateShaderBufferElementType(const clang::QualType &elementType,
                                             const clang::FieldDecl *fieldDecl,
                                             const clang::CXXRecordDecl *bindGroupDecl,
                                             const std::string &wrapperTypeName) const;

        /** @brief Validates that storage texture wrappers use supported non-depth texture formats. */
        void validateStorageTextureElementType(const clang::QualType &elementType,
                                               const clang::FieldDecl *fieldDecl,
                                               const clang::CXXRecordDecl *bindGroupDecl,
                                               const std::string &resourceTypeName) const;

        /** @brief Resolves explicit `[[BindingN]]` annotations on BindGroup fields. */
        [[nodiscard]] std::vector<BindGroupFieldBindingInfo> resolveFieldBindings(const clang::CXXRecordDecl *bindGroupDecl,
                                                                                  const clang::FunctionDecl *createFunc = nullptr) const;

        /** @brief Resolves full base resource metadata for a BindGroup declaration. */
        [[nodiscard]] std::vector<BaseShaderResourceBinding> resolveResourceBindings(const clang::CXXRecordDecl *bindGroupDecl,
                                                                                     const clang::FunctionDecl *createFunc = nullptr) const;

    private:
        /** @brief Visitor utility surface used for type names, attributes, and diagnostics. */
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
