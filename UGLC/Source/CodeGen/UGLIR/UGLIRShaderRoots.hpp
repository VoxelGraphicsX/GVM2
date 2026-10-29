#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <vector>

namespace clang
{
    class ASTContext;
    class CXXRecordDecl;
}

namespace UGLC::CodeGen::UGLIR
{
    /** Identifies the DSL family of a concrete shader class. */
    enum class ShaderClassKind { Compute, Render, PixelLocal };

    /** Borrows a concrete shader declaration only during its frontend lifetime. */
    struct ShaderClassRoot
    {
        const clang::CXXRecordDecl *recordDecl = nullptr;
        ShaderClassKind kind = ShaderClassKind::Compute;
    };

    /** Discovers concrete shader roots once for semantic preparation and lowering. */
    std::vector<ShaderClassRoot> discoverShaderRoots(clang::ASTContext &context);
}
