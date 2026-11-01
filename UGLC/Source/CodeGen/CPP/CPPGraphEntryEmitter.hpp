#pragma once

#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <optional>
#include <string>
#include <unordered_set>

namespace UGLC::CodeGen::CPP
{
    class CPPVisitor;

    /// Validates owned CPU graph effects and emits static callable adapters for graph entry points.
    class CPPGraphEntryEmitter final
    {
    public:
        /// Binds graph lowering to the host visitor's source locations and type conversion.
        explicit CPPGraphEntryEmitter(CPPVisitor &visitor);
        /// Lowers graph.add and runtime.submit while leaving unrelated calls unchanged.
        std::optional<std::string> translateCall(const clang::CXXMemberCallExpr &call);
        /// Adds an Entries descriptor after an exported graph function definition.
        std::string emitEntry(const clang::FunctionDecl &function);

    private:
        /// Rejects resources, borrowed storage and opaque records in ordinary CPU values.
        void validateValue(clang::QualType type, clang::SourceLocation location, bool graphConstruction = false);
        /// Validates a callable body and all reachable user functions without revisiting recursion.
        void validateFunction(const clang::FunctionDecl &function, bool graphConstruction);
        /// Checks local state, direct calls and pointer operations in a reachable body.
        void validateStatement(const clang::Stmt &statement, bool graphConstruction);
        /// Allows global constants only when their recursively owned storage cannot be mutated.
        void validateGlobal(const clang::VarDecl &variable, clang::SourceLocation location);
        /// Detects mutable subobjects that would make a constexpr global shared writable state.
        bool hasMutableSubobject(clang::QualType type) const;
        /// Finds the statically named function or the unique ordinary class call operator.
        const clang::FunctionDecl &resolveCallable(const clang::Expr &expression);
        /// Emits a native static adapter for a function declaration without pointer decay in the DSL.
        std::string callableExpression(const clang::Expr &expression, const clang::FunctionDecl &function);
        /// Identifies source-only graph result wrappers by their canonical template declaration.
        bool isTask(clang::QualType type) const;
        /// Identifies approved owning EASTL containers whose implementation internals are not DSL storage.
        bool isOwningContainer(const clang::CXXRecordDecl &record) const;

        CPPVisitor &mVisitor;
        std::unordered_set<const clang::CXXRecordDecl *> mValidatedValues;
        std::unordered_set<const clang::FunctionDecl *> mValidatedFunctions;
        std::unordered_set<const clang::FunctionDecl *> mValidatedBuilders;
    };
}
