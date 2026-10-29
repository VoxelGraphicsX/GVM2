#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>

#include <clang/AST/Decl.h>

#include <optional>
#include <string>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Validates HLSL render entry interfaces and maps DSL semantics to HLSL semantics.
     *
     * Render-class vertex, fragment, and compute entries must use explicit DSL
     * attributes for stage inputs and builtins. This class centralizes those
     * checks and the attribute-to-HLSL semantic suffix mapping used by record and
     * function signature emission.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * struct VSOut { [[Position]] UGL::float4 pos; [[Attribute0]] UGL::float2 uv; };
     * void compute([[DispatchThreadID]] UGL::uint3 tid);
     * ```
     *
     * ```hlsl
     * float4 pos : SV_Position;
     * uint3 tid : SV_DispatchThreadID;
     * ```
     */
    class HLSLRenderInterfaceValidator
    {
    public:
        /**
         * @brief Creates a validator that uses the owning visitor for AST utilities.
         */
        HLSLRenderInterfaceValidator(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor);

        /**
         * @brief Returns the HLSL semantic suffix for a record field.
         */
        [[nodiscard]] std::string getFieldSemanticSuffix(const clang::FieldDecl *field, std::optional<int> colorIndexOverride = std::nullopt) const;

        /**
         * @brief Returns the HLSL semantic suffix for a shader entry parameter.
         */
        [[nodiscard]] std::string getParameterAttributeOrSemantic(const clang::ParmVarDecl *param) const;

        /**
         * @brief Validates vertex/fragment record contracts for one render-class entry.
         */
        void validateEntryRenderInterfaceOrThrow(const clang::CXXRecordDecl *shaderClassDecl, const clang::FunctionDecl *entryFunction) const;

        /**
         * @brief Validates builtin parameter placement for vertex, fragment, and compute entries.
         */
        void validateShaderEntryBuiltinParametersOrThrow(const clang::FunctionDecl *shaderFunc) const;

        /**
         * @brief Validates RenderEntity builtin usage against the active bind-group set.
         */
        void validateRenderEntityBuiltinContractOrThrow(const clang::FunctionDecl *shaderFunc, const BindGroupInfoMap &bindGroupInfoMap) const;

    private:
        [[nodiscard]] int getValidatedVertexInputAttributeLocation(const clang::FieldDecl *field, const std::string &renderClassName, const std::string &vertexInputTypeName) const;
        void validateVertexInputRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const;
        [[nodiscard]] bool isRenderVaryingSystemSemanticField(const clang::FieldDecl *field) const;
        [[nodiscard]] int getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *field, const std::string &renderClassName, const std::string &recordRole, const std::string &recordTypeName) const;
        void validateRenderVaryingRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName, const std::string &recordRole, bool requirePosition = false) const;
        [[nodiscard]] static const clang::CXXRecordDecl *getSelfOrPointeeCXXRecordDecl(clang::QualType type);
        [[nodiscard]] static std::string makeVulkanLocationSemantic(int location);

        BaseASTVisitor &mVisitor;
        AbstractTypeConvertor *mTypeConvertor = nullptr;
    };
} // namespace UGLC::CodeGen::HLSL
