#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/BaseShaderBinding.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

#include <string>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Emits HLSL resources and helper signatures for `UGL::BindGroup<T>`.
     *
     * The DSL passes a bind group as one object. HLSL still keeps the actual
     * Vulkan descriptors as flat globals, but shader helper code can use a local
     * handle struct that groups those resource handles together. This class owns
     * the normal BindGroup resource naming, uniform-buffer wrapper structs,
     * local handle struct definitions, and handle materialization code.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * [[Slot0]] UGL::BindGroup<MyBindings> bindGroup;
     * void helper([[IN]] UGL::BindGroup<MyBindings> bg);
     * helper(bindGroup);
     * ```
     *
     * ```hlsl
     * [[vk::binding(0, 0)]] Texture2D<float4> bindGroup_albedo : register(t0, space0);
     * struct MyBindings_UGLBindGroupHandle
     * {
     *     Texture2D<float4> albedo;
     * };
     * void helper(MyBindings_UGLBindGroupHandle bg);
     * helper(bindGroup);
     * ```
     */
    class HLSLResourceBindingEmitter
    {
    public:
        /**
         * @brief Creates an emitter that uses the owning visitor for AST and formatting services.
         */
        HLSLResourceBindingEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor);

        /**
         * @brief Returns the lowered global resource name for one BindGroup field.
         */
        [[nodiscard]] std::string getResourceGlobalName(const std::string &bindGroupName, const std::string &resourceName) const;

        /**
         * @brief Returns the generated HLSL local handle struct type for one concrete BindGroup record.
         */
        [[nodiscard]] std::string getBindGroupHandleTypeName(const clang::CXXRecordDecl *bindGroupDecl) const;

        /**
         * @brief Returns the generated HLSL local handle struct type for a `UGL::BindGroup<T>` type.
         */
        [[nodiscard]] std::string getBindGroupHandleTypeNameFromType(const clang::QualType &type) const;

        /**
         * @brief Resolves a `UGL::BindGroup<T>` parameter type to the underlying record declaration.
         */
        [[nodiscard]] const clang::CXXRecordDecl *tryGetBindGroupTypeDeclFromType(const clang::QualType &type) const;

        /**
         * @brief Returns true when a type is a concrete `UGL::BindGroup<T>` parameter.
         */
        [[nodiscard]] bool isBindGroupParameterType(const clang::QualType &type) const;

        /**
         * @brief Resolves all concrete resource bindings exposed by a BindGroup parameter.
         */
        [[nodiscard]] std::vector<BaseShaderResourceBinding> resolveParameterBindings(const clang::QualType &type);

        /**
         * @brief Emits the HLSL helper parameter for one BindGroup handle.
         */
        [[nodiscard]] std::string generateHandleParameter(const clang::ParmVarDecl *param);

        /**
         * @brief Emits one HLSL record field that stores a BindGroup handle.
         */
        [[nodiscard]] std::string generateHandleFieldDecl(const clang::FieldDecl *fieldDecl);

        /**
         * @brief Emits every local BindGroup handle struct needed by one shader.
         */
        [[nodiscard]] std::string generateHandleStructDefinitions(const BindGroupInfoMap &bindGroupInfoMap,
                                                                  const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls);

        /**
         * @brief Emits one local handle variable initialized from flat descriptor globals.
         */
        [[nodiscard]] std::string generateHandleMaterialization(const ShaderBindGroupInfo &bindGroupInfo);

        /**
         * @brief Emits all uniform-buffer wrapper struct definitions for active BindGroups.
         */
        [[nodiscard]] std::string generateUniformWrapperDefinitions(const BindGroupInfoMap &bindGroupInfoMap);

        /**
         * @brief Emits uniform-buffer wrapper structs for active and helper-only BindGroups.
         */
        [[nodiscard]] std::string generateUniformWrapperDefinitions(const BindGroupInfoMap &bindGroupInfoMap,
                                                                    const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls);

        /**
         * @brief Emits HLSL descriptor declarations for one non-RenderSet BindGroup.
         */
        [[nodiscard]] std::string generateResourceDeclarations(const ShaderBindGroupInfo &bindGroupInfo);

    private:
        /** Describes one concrete BindGroup record and the resource bindings emitted for it. */
        struct BindGroupRecordBindings
        {
            const clang::CXXRecordDecl *bindGroupDecl = nullptr;
            std::vector<BaseShaderResourceBinding> resourceBindings;
        };

        /** Collects active and helper-only BindGroup records once, preserving first-use order. */
        [[nodiscard]] std::vector<BindGroupRecordBindings> collectUniqueBindGroupRecordBindings(const BindGroupInfoMap &bindGroupInfoMap,
                                                                                                const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls);

        [[nodiscard]] std::string getUniformWrapperTypeName(const clang::CXXRecordDecl *bindGroupDecl, const clang::FieldDecl *fieldDecl) const;
        [[nodiscard]] std::string generateHandleStructDefinition(const clang::CXXRecordDecl *bindGroupDecl, const std::vector<BaseShaderResourceBinding> &resourceBindings);
        [[nodiscard]] std::string generateHandleResourceFieldDecl(const clang::CXXRecordDecl *bindGroupDecl, const BaseShaderResourceBinding &resourceBinding);
        [[nodiscard]] std::string generateUniformWrapperTypeDefinition(const clang::CXXRecordDecl *bindGroupDecl, const BaseShaderResourceBinding &resourceBinding);
        [[nodiscard]] static std::string makeVulkanBindingAttribute(int bindingIndex, int bindGroupIndex);

        /** @brief Owning visitor service object used for AST utilities and output formatting. */
        BaseASTVisitor &mVisitor;
        /** @brief HLSL type converter used for canonical resource declarations. */
        AbstractTypeConvertor *mTypeConvertor = nullptr;
    };
} // namespace UGLC::CodeGen::HLSL
