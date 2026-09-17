#pragma once

#include "CPPVisitor.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

namespace UGLC::CodeGen::CPP
{
    /**
     * Validates UGL DSL class contracts before host C++ code generation.
     *
     * The host backend emits concrete C++ wrappers for frame buffers, bind groups,
     * render classes, compute classes, render sets, and renderers. This validator owns
     * the structural checks that protect those emitters from ambiguous layouts: slot
     * ranges, required `create(...)` / `render(...)` methods, vertex attributes,
     * vertex-fragment varying contracts, framebuffer attachment order, and render-set
     * component annotations.
     *
     * DSL example:
     * @code
     * struct VertexOut
     * {
     *     float4 position [[Position]];
     *     float2 uv [[Attribute0]];
     * };
     * @endcode
     *
     * Generated host C++ expectation:
     * @code
     * // The backend accepts this record because it has one Position field and
     * // each non-system varying has a unique AttributeN location.
     * @endcode
     */
    class CPPHostClassValidator
    {
    public:
        /** Creates a validator using the owning visitor for AST queries and diagnostics. */
        explicit CPPHostClassValidator(CPPVisitor &visitor);

        /** Returns the validated compute workgroup size from `[[LocalWorkGroupSize(x, y, z)]]`. */
        std::array<std::string, 3> getValidatedLocalWorkGroupSize(const clang::CXXRecordDecl *decl);

        /** Returns true when a parameter type is a shader-class bind group or render set handle. */
        bool isShaderClassBindGroupParamType(const clang::QualType &qt) const;

        /** Validates that shader-class bind group parameters use unique supported `[[SlotN]]` attributes. */
        void validateShaderClassBindGroupSlots(const clang::CXXRecordDecl *decl,
                                               const clang::FunctionDecl *createFunc,
                                               const std::string &ownerKind,
                                               int bindgroupBufferOffset);

        /** Validates and returns one vertex-input field location. */
        int getValidatedVertexAttributeLocation(const clang::FieldDecl *fieldDecl,
                                                const std::string &renderClassName,
                                                const std::string &vertexInputTypeName);

        /** Validates and returns a sorted vertex-input layout for host pipeline creation. */
        std::vector<CPPVisitor::VertexAttributeLayoutInfo> getValidatedVertexAttributeLayout(const clang::ParmVarDecl *vertexInputParam,
                                                                                            const std::string &renderClassName);

        /** Returns true when a varying field uses a system semantic instead of an interpolated attribute. */
        bool isRenderVaryingSystemSemanticField(const clang::FieldDecl *fieldDecl) const;

        /** Validates and returns one non-system render varying location. */
        int getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *fieldDecl,
                                                       const std::string &renderClassName,
                                                       const std::string &recordRole,
                                                       const std::string &recordTypeName) const;

        /** Collects a render varying record into an attribute-location map and checks duplicate semantics. */
        std::unordered_map<int, CPPVisitor::RenderVaryingLayoutInfo> collectValidatedRenderVaryingLayout(const clang::CXXRecordDecl *recordDecl,
                                                                                                         const std::string &renderClassName,
                                                                                                         const std::string &recordRole,
                                                                                                         bool requirePosition = false) const;

        /** Validates that fragment inputs match the vertex output attributes they consume. */
        void validateVertexFragmentVaryingContract(const clang::CXXRecordDecl *vertexOutputRecord,
                                                   const clang::CXXRecordDecl *fragmentInputRecord,
                                                   const std::string &renderClassName) const;

        /** Finds the required DSL `create(...)` method or reports a codegen diagnostic. */
        const clang::FunctionDecl *requireCreateMethod(const clang::CXXRecordDecl *decl, const std::string &ownerKind) const;

        /** Finds the required renderer `render(...)` method or reports a codegen diagnostic. */
        const clang::FunctionDecl *requireRendererRenderMethod(const clang::CXXRecordDecl *decl) const;

        /** Validates host framebuffer attachment field types and depth attachment ordering. */
        void validateFrameBufferFields(const clang::CXXRecordDecl *decl, const std::string &framebufferName) const;

        /** Returns one attachment format template type or reports a codegen diagnostic. */
        clang::QualType requireAttachmentTemplateType(const clang::FieldDecl *fieldDecl,
                                                      const std::string &ownerKind,
                                                      const std::string &ownerName) const;

        /** Validates a render target record used by a render class. */
        void validateRenderTargetRecord(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const;

        /** Rejects binding-style annotations on render-set component fields. */
        void validateRenderSetComponentFieldAttributes(const clang::FieldDecl *fieldDecl, const std::string &renderSetName) const;

        /** Validates `TextureComponent<T, MaxResourceCount>` resource counts. */
        void validateRenderSetTextureResourceCount(const clang::FieldDecl *fieldDecl, const std::string &renderSetName, int maxResourceCount) const;

    private:
        CPPVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::CPP
