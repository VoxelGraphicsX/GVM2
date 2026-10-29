#pragma once

#include <string_view>
#include <vector>

#include "ShaderBindGroupInfo.hpp"

namespace clang
{
    class CXXRecordDecl;
    class Decl;
}

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    /**
     * Returns whether the identifier is reserved by UGL DSL variable naming rules.
     *
     * Use this for names that can be emitted as shader-side variables, parameters, or
     * fields. Shader entry function names are intentionally outside this predicate.
     */
    bool isReservedDSLVariableIdentifier(std::string_view identifier);

    /**
     * Validates shader-visible declarations against UGL DSL reserved variable names.
     *
     * The caller must pass the declaration surface that will be emitted into shader
     * source for one artifact. Host-only declarations should not be included.
     */
    void validateShaderDSLReservedIdentifiersOrThrow(BaseASTVisitor &visitor, const std::vector<const clang::Decl *> &shaderDeclarations);

    /**
     * Validates shader-class binding variable names against UGL DSL reserved names.
     *
     * Use this for `BindGroup<T>` and `RenderSet<T>` fields that are bound into a
     * shader artifact through `[[SlotN]]`. Other host-only shader-class fields are
     * intentionally outside this validation surface.
     */
    void validateShaderDSLReservedBindingIdentifiersOrThrow(BaseASTVisitor &visitor,
                                                            const clang::CXXRecordDecl *shaderClassDecl,
                                                            const BindGroupInfoMap &bindGroupInfoMap);
} // namespace UGLC::CodeGen
