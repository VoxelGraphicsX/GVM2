#pragma once

#include <CodeGen/AbstractTypeConvertor.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRenderInterfaceValidator.hpp>

#include <clang/AST/DeclCXX.h>

#include <string>
#include <unordered_set>

namespace UGLC::CodeGen::HLSL
{
    /**
     * @brief Emits HLSL record, framebuffer, and shader-scope declarations.
     *
     * This class keeps record-layout text generation separate from the main
     * HLSL visitor. It handles HLSL field declarations, framebuffer attachment
     * semantic mapping, nested shader-class declarations, and method bodies that
     * belong inside emitted records.
     *
     * Example DSL -> HLSL:
     *
     * ```cpp
     * struct VSOut { [[Position]] UGL::float4 pos; [[Attribute0]] UGL::float2 uv; };
     * ```
     *
     * ```hlsl
     * struct VSOut
     * {
     *     float4 pos : SV_Position;
     *     float2 uv : TEXCOORD0;
     * };
     * ```
     */
    class HLSLRecordEmitter
    {
    public:
        /**
         * @brief Creates a record emitter backed by the owning visitor services.
         */
        HLSLRecordEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor, HLSLRenderInterfaceValidator &renderInterfaceValidator);

        /**
         * @brief Emits one HLSL field declaration, including semantics and static const initializers.
         */
        [[nodiscard]] std::string generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor = nullptr);

        /**
         * @brief Emits a normal HLSL class or struct definition.
         */
        [[nodiscard]] std::string generateRecordDefinitionDetailed(const clang::CXXRecordDecl *decl);

        /**
         * @brief Emits the HLSL representation for a DSL framebuffer record.
         */
        [[nodiscard]] std::string generateFramebufferClass(const clang::CXXRecordDecl *decl, const std::unordered_set<std::string> *includedOutputFields = nullptr);

        /**
         * @brief Emits nested declarations that must remain addressable through the shader class scope.
         */
        [[nodiscard]] std::string generateShaderClassNestedDeclarations(const clang::CXXRecordDecl *decl);

    private:
        /** @brief Owning visitor service object used for formatting and recursive code generation. */
        BaseASTVisitor &mVisitor;
        /** @brief HLSL type converter used for record fields and framebuffer attachments. */
        AbstractTypeConvertor *mTypeConvertor = nullptr;
        /** @brief Render interface semantic mapper shared with entry validation. */
        HLSLRenderInterfaceValidator &mRenderInterfaceValidator;
    };
} // namespace UGLC::CodeGen::HLSL
