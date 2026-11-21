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
        /// Lowers a source-only Task<Record> field into a nonblocking typed resolver.
        std::optional<std::string> translateProjection(const clang::MemberExpr &member);
        /// Emits a RenderNode member with an invocation parameter hidden from the source DSL.
        std::optional<std::string> emitRenderMethod(const clang::FunctionDecl &function);
        /// Adds an Entries descriptor after an exported graph function definition.
        std::string emitEntry(const clang::FunctionDecl &function);

        /// Identifies the explicit subgraph protocol through ordinary class inheritance.
        bool isRenderRecord(const clang::CXXRecordDecl &record) const;
        /// Identifies inline non-render methods returning UGL::Task<T> so plain functor classes lower subgraph types.
        bool isSubgraphMethod(const clang::CXXMethodDecl &method) const;
        /// Describes resource-bearing result records using generated metadata without changing their public fields.
        std::string emitResourceVisitor(const clang::CXXRecordDecl &record) const;
        /// Emits lazy dependent resource metadata and visitor methods for user-defined class templates.
        std::string emitTemplateResourceVisitor(const clang::CXXRecordDecl &record) const;

    private:
        /// Validates CPU values and explicit-destruction GPU allocation outputs without exporting standalone views.
        void validateResult(const clang::FunctionDecl &function);
        /// Identifies nonstatic user methods that must receive invocation-local state.
        bool isRenderMethod(const clang::FunctionDecl &function) const;
        /// Returns true if a possibly cv- or reference-qualified type refers to UGL::RenderNode.
        bool isRenderNodeType(clang::QualType type) const;
        /// Returns true if a possibly cv- or reference-qualified type refers to UGL::TaskGraph.
        bool isTaskGraphType(clang::QualType type) const;
        /// Validates explicit RenderNode & parameters on a function and returns the parameter name when present.
        std::optional<std::string> validateExplicitRenderNodeParam(const clang::FunctionDecl &function, bool allowExplicitRenderNode) const;
        /// Validates explicit TaskGraph & parameters on a subgraph builder and returns the parameter name when present.
        std::optional<std::string> validateExplicitTaskGraphParam(const clang::FunctionDecl &function, bool allowExplicitTaskGraph) const;
        /// Validates a graph target using its inferred CPU or subgraph effects.
        void validateTarget(const clang::FunctionDecl &function);
        /// Lowers registered graph construction intrinsics and synchronous node helpers.
        std::optional<std::string> translateRenderCall(const clang::CXXMemberCallExpr &call);
        /// Rejects resources, borrowed storage and opaque records in ordinary CPU values.
        void validateValue(clang::QualType type, clang::SourceLocation location, bool graphConstruction = false);
        /// Validates user-defined element constructors and assignments reachable through owning container operations.
        void validateContainerElementOperations(clang::QualType containerOrElementType, bool graphConstruction);
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
        /// Identifies an eastl::vector whose elements are UGL::Task<T> values.
        bool isTaskVector(clang::QualType type) const;
        /// Identifies approved owning EASTL containers whose implementation internals are not DSL storage.
        bool isOwningContainer(const clang::CXXRecordDecl &record) const;
        /// Recognizes fixed built-in vector values whose source emulation contains implementation pointers.
        bool isMathValue(const clang::CXXRecordDecl &record) const;
        /// Finds allocation handles through ordinary records and approved owning containers, stopping recursive types.
        bool containsResources(clang::QualType type, std::unordered_set<const clang::CXXRecordDecl *> &visited) const;

        CPPVisitor &mVisitor;
        const clang::FunctionDecl *mRenderMethod = nullptr;
        std::string mRenderNodeVarName = "__gvm_node";
        bool mRenderValidation = false;
        bool mExportValidation = false;
        std::unordered_set<const clang::CXXRecordDecl *> mValidatedRenderValues;
        std::unordered_set<const clang::CXXRecordDecl *> mValidatedExportValues;
        std::unordered_set<const clang::CXXRecordDecl *> mValidatedGraphValues;
        std::unordered_set<const clang::FunctionDecl *> mValidatedRenderFunctions;
        std::unordered_set<const clang::CXXRecordDecl *> mValidatedValues;
        std::unordered_set<const clang::FunctionDecl *> mValidatedFunctions;
        std::unordered_set<const clang::FunctionDecl *> mValidatedBuilders;
    };
}
