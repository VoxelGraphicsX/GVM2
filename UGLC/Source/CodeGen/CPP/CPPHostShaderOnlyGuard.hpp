#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Type.h>

namespace UGLC::CodeGen::CPP
{
    class CPPVisitor;

    /**
     * Detects shader-only DSL operations that must be rejected from generated host C++.
     *
     * The host backend keeps generated headers includable, but a real host call into a
     * shader-only body must fail at compile time. This guard owns the recursive
     * function-body scan, delete-declaration cache, and stub expression synthesis used by
     * CPPVisitor.
     *
     * DSL example:
     * @code
     * float4 color = texture.sample(linearSampler, uv);
     * @endcode
     *
     * Generated host C++ example:
     * @code
     * []<class __UGLC_Dummy = void>() -> float4 {
     *     static_assert(::UGL::UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>,
     *                   "UGLC generated host stub reached for shader-only operation: UGL::Texture2D::sample");
     *     return {};
     * }()
     * @endcode
     */
    class CPPHostShaderOnlyGuard
    {
    public:
        /** Creates a guard that uses the owning visitor for type spelling and diagnostics. */
        explicit CPPHostShaderOnlyGuard(CPPVisitor &visitor);

        /** Returns a user-facing operation label when a texture member call is shader-only on host. */
        std::optional<std::string> describeTextureOperation(const std::string &objectTypeName, const std::string &methodName) const;

        /** Emits a fail-fast host expression for a shader-only operation with a value return type. */
        std::string makeCallExpr(const std::string &returnTypeName, const std::string &operationName) const;

        /** Emits a fail-fast host expression for a shader-only operation with void return type. */
        std::string makeVoidCallExpr(const std::string &operationName) const;

        /** Infers the host stub return type for shader-only texture member calls. */
        std::optional<std::string> inferTextureReturnType(const clang::QualType &objectType, const std::string &methodName);

        /** Returns true when a free function is a shader-only builtin on the host backend. */
        bool isKnownFunction(const clang::FunctionDecl *func) const;

        /** Scans a statement subtree for calls that make the enclosing host function deleted. */
        bool stmtUsesShaderOnlyOperation(const clang::Stmt *stmt);

        /** Returns true when a function body should be emitted as a deleted host declaration. */
        bool functionRequiresDeletedDefinition(const clang::FunctionDecl *func);

        /** Emits a deleted host declaration for a function that depends on shader-only operations. */
        std::string generateDeletedFunctionDeclaration(const clang::FunctionDecl *func, const std::string &funcNameOverride = "");

    private:
        std::string inferTextureAccessReturnType(const clang::QualType &textureElementType, int unwrapDepth = 0);
        std::string normalizeScalarVectorCanonicalName(const std::string &canonicalName) const;

        CPPVisitor &mVisitor;
        std::unordered_map<const clang::FunctionDecl *, bool> mDeletedFunctionCache;
        std::unordered_set<const clang::FunctionDecl *> mDeletedFunctionInProgress;
    };
} // namespace UGLC::CodeGen::CPP
