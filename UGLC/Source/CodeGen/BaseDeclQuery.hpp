#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * Performs backend-neutral declaration and type queries shared by all codegen visitors.
     *
     * The class centralizes Clang AST classification that does not directly emit HLSL, MSL,
     * or host C++ text. It keeps the high-level visitors focused on generation rules while
     * preserving their existing public helper methods.
     *
     * DSL example:
     * @code
     * struct Material : UGL::BindGroup
     * {
     *     UGL::Texture2D<UGL::float4> albedo [[Binding0]];
     * };
     * @endcode
     *
     * `checkDerivedClassByName(Material, "UGL::BindGroup")` identifies the shader binding
     * class, and `isShaderResourceOrBindingType(albedoType)` identifies the resource field
     * before backend-specific binding code is emitted.
     */
    class BaseDeclQuery
    {
    public:
        /**
         * Creates a query helper that delegates formatting and annotation checks back to the
         * owning visitor without taking ownership of it.
         */
        explicit BaseDeclQuery(const BaseASTVisitor &visitor);

        /** Returns true when the declaration is lexically placed in a translation unit or namespace. */
        bool isTopLevel(const clang::Decl *decl) const;

        /** Returns true when a record directly derives from the named canonical base class. */
        bool checkDerivedClassByName(const clang::CXXRecordDecl *decl, const std::string &name) const;

        /** Returns true when the canonical record type name matches the requested name. */
        bool checkTypeCanonicalName(const clang::QualType &type, const std::string &name) const;

        /** Returns true when the type is the deprecated UGL::StorageBuffer<T> wrapper. */
        bool isLegacyStorageBufferType(const clang::QualType &type) const;

        /** Throws the preserved migration diagnostic for deprecated storage buffer usage. */
        [[noreturn]] void throwLegacyStorageBufferMigrationError(const std::string &usageContext) const;

        /** Returns true when the type is `UGL::BindGroup<T>` and can be used as a shader handle. */
        bool isBindGroupHandleType(const clang::QualType &type) const;

        /** Returns true when the type is `UGL::RenderSet<T>` and can be used as a shader handle. */
        bool isRenderSetHandleType(const clang::QualType &type) const;

        /** Returns true when the type is a direct shader resource handle such as a buffer, texture, or sampler. */
        bool isShaderResourceHandleType(const clang::QualType &type) const;

        /** Returns true when the type is a sampled texture or sampler handle that must be passed by input only. */
        bool isSampledTextureOrSamplerHandleType(const clang::QualType &type) const;

        /** Returns true when a shader handle parameter must not use output-style attributes. */
        bool isInputOnlyShaderHandleParameterType(const clang::QualType &type) const;

        /** Returns true when the type is any shader-side resource handle, including bind groups and render sets. */
        bool isAnyShaderResourceHandleType(const clang::QualType &type) const;

        /** Returns true when the type is a host-only resource handle that must not be emitted into shader code. */
        bool isHostResourceHandleType(const clang::QualType &type) const;

        /** Returns true when the type itself or a nested template argument contains a host-only resource handle. */
        bool typeContainsHostResourceHandle(const clang::QualType &type) const;

        /** Returns true when a function signature mentions a shader resource handle type. */
        bool functionSignatureUsesShaderResourceHandles(const clang::FunctionDecl *func) const;

        /** Returns true when a record should be treated as shader-only resource-handle behavior. */
        bool recordUsesShaderResourceHandles(const clang::CXXRecordDecl *decl) const;

        /** Recursively checks whether a record contains fields with host-only resource handles. */
        bool recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl) const;

        /** Returns true when the type is a shader resource, bind group, or host resource wrapper. */
        bool isShaderResourceOrBindingType(const clang::QualType &type) const;

        /** Recursively checks whether a record contains shader resource or binding fields. */
        bool recordContainsShaderResourceOrBindingFields(const clang::CXXRecordDecl *decl) const;

        /** Returns true when a function declaration has the requested unqualified name. */
        bool checkFunctionName(const clang::FunctionDecl *decl, const std::string &name) const;

        /** Returns true when a nested record should be emitted separately inside its parent. */
        bool shouldEmitNestedRecordDefinition(const clang::CXXRecordDecl *parentDecl, const clang::CXXRecordDecl *nestedDecl) const;

        /** Returns the record fields in declaration order, or an empty list for null input. */
        std::vector<clang::FieldDecl *> getAllFieldsFromRecord(const clang::CXXRecordDecl *decl) const;

        /** Finds the first record field that carries the requested UGL annotation. */
        clang::FieldDecl *getFieldFromClassWithAttribute(const clang::CXXRecordDecl *decl, const std::string &name) const;

        /** Finds the first function parameter that carries the requested UGL annotation. */
        const clang::ParmVarDecl *getParamFromFunctionWithAttribute(const clang::FunctionDecl *decl, const std::string &name) const;

        /** Finds the first function parameter with the requested source name. */
        const clang::ParmVarDecl *getParamFromFunctionByName(const clang::FunctionDecl *decl, const std::string &name) const;

        /** Resolves nested `using TrueType = ...` / `typedef ... TrueType` aliases on wrapper records. */
        std::optional<clang::QualType> resolveRecordNestedTrueType(const clang::QualType &type) const;

        /** Removes pointer, reference, array, and cv qualifiers from a type for codegen classification. */
        clang::QualType getUnqualifiedType(const clang::QualType &type) const;

        /** Returns the non-anonymous namespace path that owns a declaration. */
        std::string getFullNamespace(const clang::Decl *decl) const;

    private:
        /** Recursively checks one template argument for nested host-only resource handle types. */
        bool templateArgumentContainsHostResourceHandle(const clang::TemplateArgument &arg, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const;

        /** Recursively checks one type using the caller-provided visited-record set. */
        bool typeContainsHostResourceHandle(const clang::QualType &type, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const;

        /** Recursively checks one record using the caller-provided visited-record set. */
        bool recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const;

        const BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen
