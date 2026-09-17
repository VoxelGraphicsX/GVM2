#include "UGLIRSourceVerifier.hpp"

#include <CodeGen/DSLReservedIdentifierValidator.hpp>
#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/LambdaCapture.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/Type.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Stores the DFS state used while checking the shader call graph for recursion. */
        enum class FunctionCycleState
        {
            Unvisited,
            Visiting,
            Visited,
        };

        class UGLIRSourceVerifier;

        /** Reports a reserved DSL identifier on a shader-authored declaration. */
        void diagnoseReservedShaderIdentifier(UGLIRSourceVerifier &owner,
                                              const clang::NamedDecl *decl,
                                              const std::string &role);

        /** Visits one shader-reachable function body and reports forbidden source constructs. */
        class UGLIRFunctionBodyVerifier : public clang::RecursiveASTVisitor<UGLIRFunctionBodyVerifier>
        {
        public:
            /** Creates a body verifier that reports findings back to the owning source verifier. */
            UGLIRFunctionBodyVerifier(UGLIRSourceVerifier &owner, const clang::FunctionDecl *functionDecl);

            /** Visits only the selected constexpr branch while retaining initializer checks. */
            bool TraverseIfStmt(clang::IfStmt *statement);

            /** Checks local declarations for banned shader-visible types. */
            bool VisitVarDecl(const clang::VarDecl *decl);

            /** Rejects shader-visible references to mutable global and static storage. */
            bool VisitDeclRefExpr(const clang::DeclRefExpr *expr);

            /** Rejects shader-visible references to mutable static data members. */
            bool VisitMemberExpr(const clang::MemberExpr *expr);

            /** Rejects lambda forms that cannot be lowered into a non-escaping UGLIR helper. */
            bool VisitLambdaExpr(const clang::LambdaExpr *expr);

            /** Rejects returning a lambda closure from shader-reachable code. */
            bool VisitReturnStmt(const clang::ReturnStmt *stmt);

            /** Rejects exception throwing inside shader-reachable code. */
            bool VisitCXXThrowExpr(const clang::CXXThrowExpr *expr);

            /** Rejects try/catch statements inside shader-reachable code. */
            bool VisitCXXTryStmt(const clang::CXXTryStmt *stmt);

            /** Rejects typeid usage inside shader-reachable code. */
            bool VisitCXXTypeidExpr(const clang::CXXTypeidExpr *expr);

            /** Rejects dynamic_cast usage inside shader-reachable code. */
            bool VisitCXXDynamicCastExpr(const clang::CXXDynamicCastExpr *expr);

            /** Rejects dynamic allocation inside shader-reachable code. */
            bool VisitCXXNewExpr(const clang::CXXNewExpr *expr);

            /** Rejects dynamic deallocation inside shader-reachable code. */
            bool VisitCXXDeleteExpr(const clang::CXXDeleteExpr *expr);

            /** Rejects coroutine body lowering inside shader-reachable code. */
            bool VisitCoroutineBodyStmt(const clang::CoroutineBodyStmt *stmt);

            /** Rejects co_await expressions inside shader-reachable code. */
            bool VisitCoawaitExpr(const clang::CoawaitExpr *expr);

            /** Rejects co_yield expressions inside shader-reachable code. */
            bool VisitCoyieldExpr(const clang::CoyieldExpr *expr);

            /** Rejects co_return statements inside shader-reachable code. */
            bool VisitCoreturnStmt(const clang::CoreturnStmt *stmt);

            /** Rejects the built-in address-of operator inside shader-reachable code. */
            bool VisitUnaryOperator(const clang::UnaryOperator *expr);

            /** Rejects overloaded address-of and explicit allocation operators inside shader-reachable code. */
            bool VisitCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *expr);

            /** Rejects nullptr usage inside shader-reachable code. */
            bool VisitCXXNullPtrLiteralExpr(const clang::CXXNullPtrLiteralExpr *expr);

            /** Records shader-reachable calls and rejects banned call targets. */
            bool VisitCallExpr(const clang::CallExpr *expr);

            /** Checks temporary construction types for banned shader-visible classes. */
            bool VisitCXXConstructExpr(const clang::CXXConstructExpr *expr);

        private:
            UGLIRSourceVerifier &mOwner;
            const clang::FunctionDecl *mFunctionDecl = nullptr;
        };

        /** Performs source-level validation for the UGLIR shader pipeline. */
        class UGLIRSourceVerifier
        {
        public:
            /** Creates a verifier bound to one Clang AST context. */
            explicit UGLIRSourceVerifier(clang::ASTContext &context);

            /** Runs the full shader-root discovery, source validation, and recursion check. */
            UGLIRVerificationResult run(llvm::ArrayRef<ShaderClassRoot> roots);

            /** Reports a forbidden UGLIR source feature at the requested location. */
            void addDiagnostic(clang::SourceLocation location, const std::string &feature, const std::string &detail);

            /** Checks a type that appears in a shader-visible declaration or function signature. */
            void inspectShaderVisibleType(const clang::QualType &type, clang::SourceLocation location, const std::string &usageContext);

            /** Checks a shader-visible parameter while preserving fixed-size array parameter spelling before Clang pointer adjustment. */
            void inspectShaderVisibleParameterType(const clang::ParmVarDecl *paramDecl, const std::string &usageContext);

            /** Checks a type that appears only as an expression result and should not reject DSL smart-pointer internals. */
            void inspectExpressionType(const clang::QualType &type, clang::SourceLocation location, const std::string &usageContext);

            /** Returns true when a type is a C++ lambda closure type. */
            bool isLambdaClosureType(const clang::QualType &type) const;

            /** Records a call edge from the currently analyzed function to a shader-reachable user function. */
            void recordReachableCall(const clang::FunctionDecl *caller, const clang::FunctionDecl *callee, clang::SourceLocation location);

            /** Checks a direct call target for banned virtual, allocation, thread, or STL behavior. */
            void inspectCallTarget(const clang::CallExpr *expr, const clang::FunctionDecl *callee);

            /** Returns true when a declaration belongs to shader-authored source instead of system or UGL support headers. */
            bool isUserAuthoredDecl(const clang::Decl *decl) const;

        private:
            /** Validates shader class methods and seeds the shader entry call graph roots. */
            void validateShaderClassRoot(const ShaderClassRoot &root);

            /** Validates one shader-reachable function signature and body. */
            void analyzeReachableFunction(const clang::FunctionDecl *functionDecl);

            /** Validates all queued shader-reachable functions until the call graph reaches a fixed point. */
            void analyzeReachableFunctions();

            /** Checks whether a method name is a permitted entry point for the shader class kind. */
            bool isPermittedEntryName(ShaderClassKind kind, const std::string &methodName) const;

            /** Checks whether a method is the DSL-generated create method rather than a shader entry. */
            bool isCreateMethod(const clang::CXXMethodDecl *methodDecl) const;

            /** Adds a shader entry method to the reachable function queue. */
            void enqueueReachableFunction(const clang::FunctionDecl *functionDecl);

            /** Returns the definition used to analyze a function body, or null when the call cannot be analyzed. */
            const clang::FunctionDecl *getAnalyzableDefinition(const clang::FunctionDecl *functionDecl) const;

            /** Returns true when a function definition should be traversed as shader-authored source. */
            bool isUserAuthoredFunction(const clang::FunctionDecl *functionDecl) const;

            /** Returns the fixed-size array spelling of a parameter when Clang adjusted it to pointer type for function ABI. */
            clang::QualType getFixedSizeArrayParameterType(const clang::ParmVarDecl *paramDecl) const;

            /** Checks one type recursively for banned pointer, host-resource, STL, and thread constructs. */
            void inspectTypeRecursive(const clang::QualType &type,
                                      clang::SourceLocation location,
                                      const std::string &usageContext,
                                      bool reportPointerTypes,
                                      std::unordered_set<const clang::Type *> &visitedTypes);

            /** Checks fields on a user-authored record that is visible from shader code. */
            void inspectUserRecordFields(const clang::CXXRecordDecl *recordDecl,
                                         clang::SourceLocation location,
                                         const std::string &usageContext,
                                         std::unordered_set<const clang::Type *> &visitedTypes);

            /** Checks template type arguments that are visible from shader code. */
            void inspectTemplateArguments(const clang::ClassTemplateSpecializationDecl *specializationDecl,
                                          clang::SourceLocation location,
                                          const std::string &usageContext,
                                          bool reportPointerTypes,
                                          std::unordered_set<const clang::Type *> &visitedTypes);

            /** Checks the final call graph for direct or mutual recursion. */
            void diagnoseRecursion();

            /** Performs a DFS recursion check starting at one function. */
            bool diagnoseRecursionFrom(const clang::FunctionDecl *functionDecl,
                                       std::unordered_map<const clang::FunctionDecl *, FunctionCycleState> &states,
                                       std::vector<const clang::FunctionDecl *> &stack);

            /** Returns true when a source location belongs to system or UGL implementation headers. */
            bool isIgnoredLocation(clang::SourceLocation location) const;

            /** Returns a stable qualified name for a named declaration. */
            std::string getQualifiedName(const clang::NamedDecl *decl) const;

            /** Normalizes libc++ inline namespace spellings into stable std:: names. */
            std::string normalizeStdName(std::string name) const;

            /** Returns true when a qualified name is one of the banned complex STL containers. */
            bool isComplexSTLName(const std::string &qualifiedName) const;

            /** Returns true when a qualified name names a CPU thread or synchronization primitive. */
            bool isCPUThreadPrimitiveName(const std::string &qualifiedName) const;

            /** Returns true when a qualified name names a host-only UGL resource handle. */
            bool isHostResourceHandleName(const std::string &qualifiedName) const;

            /** Returns true when a function name names a pthread API that must not be reachable from shaders. */
            bool isPThreadFunctionName(const std::string &qualifiedName) const;

            clang::ASTContext &mContext;
            clang::SourceManager &mSourceManager;
            UGLIRVerificationResult mResult;
            std::vector<const clang::FunctionDecl *> mReachableQueue;
            std::unordered_set<const clang::FunctionDecl *> mQueuedFunctions;
            std::unordered_set<const clang::FunctionDecl *> mAnalyzedFunctions;
            std::unordered_map<const clang::FunctionDecl *, std::vector<const clang::FunctionDecl *>> mCallGraph;
        };

        void diagnoseReservedShaderIdentifier(UGLIRSourceVerifier &owner,
                                              const clang::NamedDecl *decl,
                                              const std::string &role)
        {
            if (!owner.isUserAuthoredDecl(decl))
            {
                return;
            }

            const std::string name = decl->getNameAsString();
            if (!UGLC::CodeGen::isReservedDSLVariableIdentifier(name))
            {
                return;
            }

            owner.addDiagnostic(decl->getLocation(),
                                "reserved identifier",
                                "UGL DSL reserves variable identifier \"" + name
                                    + "\" for shader stage names. Rename " + role + " \"" + name + "\".");
        }

        /** Returns the canonical declaration pointer used as a call-graph key. */
        const clang::FunctionDecl *canonicalFunctionDecl(const clang::FunctionDecl *functionDecl)
        {
            if (functionDecl == nullptr)
            {
                return nullptr;
            }
            return functionDecl->getCanonicalDecl();
        }

        /** Returns the record definition when Clang has one available. */
        const clang::CXXRecordDecl *recordDefinition(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return nullptr;
            }
            if (const clang::CXXRecordDecl *definition = recordDecl->getDefinition())
            {
                return definition;
            }
            return recordDecl;
        }

        /** Returns true for the UGL scalar/vector records whose constructors are shader-value syntax. */
        bool isKnownUGLConstantValueRecord(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return false;
            }

            const std::string name = recordDefinition(recordDecl)->getQualifiedNameAsString();
            static constexpr const char *knownNames[] = {
                "UGL::half", "UGL::half2", "UGL::half3", "UGL::half4",
                "UGL::float2", "UGL::float3", "UGL::float4",
                "UGL::int2", "UGL::int3", "UGL::int4",
                "UGL::uint2", "UGL::uint3", "UGL::uint4",
                "UGL::bool2", "UGL::bool3", "UGL::bool4",
                "UGL::double2", "UGL::double3", "UGL::double4",
            };
            for (const char *knownName : knownNames)
            {
                if (name == knownName)
                {
                    return true;
                }
            }
            return false;
        }

        /** Returns true when a type has no pointer, reference, volatile, mutable, or externally owned state. */
        bool isImmutableConstantType(const clang::QualType &type,
                                     std::unordered_set<const clang::CXXRecordDecl *> &activeRecords)
        {
            if (type.isNull() || type.isVolatileQualified())
            {
                return false;
            }

            const clang::QualType canonicalType = type.getCanonicalType();
            if (canonicalType.isVolatileQualified() || canonicalType->isPointerType() ||
                canonicalType->isReferenceType() || canonicalType->isMemberPointerType())
            {
                return false;
            }
            if (const auto *arrayType = llvm::dyn_cast<clang::ArrayType>(canonicalType.getTypePtr()))
            {
                return isImmutableConstantType(arrayType->getElementType(), activeRecords);
            }
            if (canonicalType->isIntegerType() || canonicalType->isFloatingType() || canonicalType->isEnumeralType() ||
                canonicalType->isBooleanType())
            {
                return true;
            }

            const auto *recordDecl = canonicalType->getAsCXXRecordDecl();
            if (recordDecl == nullptr)
            {
                return false;
            }
            recordDecl = recordDefinition(recordDecl);
            if (isKnownUGLConstantValueRecord(recordDecl))
            {
                return true;
            }
            if (!recordDecl->isCompleteDefinition() || !activeRecords.insert(recordDecl).second)
            {
                return false;
            }

            bool immutable = true;
            for (const clang::CXXBaseSpecifier &base : recordDecl->bases())
            {
                if (!isImmutableConstantType(base.getType(), activeRecords))
                {
                    immutable = false;
                    break;
                }
            }
            if (immutable)
            {
                for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
                {
                    if (fieldDecl->isMutable() || !isImmutableConstantType(fieldDecl->getType(), activeRecords))
                    {
                        immutable = false;
                        break;
                    }
                }
            }
            activeRecords.erase(recordDecl);
            return immutable;
        }

        /** Returns true for a narrow UGL vector/half construction or an aggregate of Clang constant expressions. */
        bool isNarrowUGLConstantExpression(const clang::Expr *expression, clang::ASTContext &context)
        {
            if (expression == nullptr)
            {
                return false;
            }

            const bool isClangConstantExpression = expression->isCXX11ConstantExpr(context);
            if (isClangConstantExpression)
            {
                return true;
            }
            const clang::Expr *current = expression;
            if (const auto *parenExpr = llvm::dyn_cast<clang::ParenExpr>(current))
            {
                return isNarrowUGLConstantExpression(parenExpr->getSubExpr(), context);
            }
            if (const auto *cleanupExpr = llvm::dyn_cast<clang::ExprWithCleanups>(current))
            {
                return isNarrowUGLConstantExpression(cleanupExpr->getSubExpr(), context);
            }
            if (const auto *temporaryExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
            {
                return isNarrowUGLConstantExpression(temporaryExpr->getSubExpr(), context);
            }
            if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
            {
                return isNarrowUGLConstantExpression(bindTemporaryExpr->getSubExpr(), context);
            }
            if (const auto *implicitCastExpr = llvm::dyn_cast<clang::ImplicitCastExpr>(current))
            {
                const clang::Expr *subExpr = implicitCastExpr->getSubExpr();
                if (implicitCastExpr->getCastKind() != clang::CK_NoOp || subExpr == nullptr ||
                    subExpr->getType().getUnqualifiedType() != current->getType().getUnqualifiedType() ||
                    !isKnownUGLConstantValueRecord(subExpr->getType()->getAsCXXRecordDecl()))
                {
                    return false;
                }
                return isNarrowUGLConstantExpression(subExpr, context);
            }
            if (const auto *functionalCastExpr = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(current))
            {
                if (functionalCastExpr->getCastKind() != clang::CK_ConstructorConversion ||
                    !isKnownUGLConstantValueRecord(functionalCastExpr->getType()->getAsCXXRecordDecl()))
                {
                    return false;
                }
                return isNarrowUGLConstantExpression(functionalCastExpr->getSubExpr(), context);
            }
            if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(current))
            {
                return isNarrowUGLConstantExpression(constantExpr->getSubExpr(), context);
            }
            if (const auto *defaultArgumentExpr = llvm::dyn_cast<clang::CXXDefaultArgExpr>(current))
            {
                return isNarrowUGLConstantExpression(defaultArgumentExpr->getExpr(), context);
            }
            if (const auto *defaultInitializerExpr = llvm::dyn_cast<clang::CXXDefaultInitExpr>(current))
            {
                return isNarrowUGLConstantExpression(defaultInitializerExpr->getExpr(), context);
            }
            if (const auto *initListExpr = llvm::dyn_cast<clang::InitListExpr>(current))
            {
                for (const clang::Expr *initializer : initListExpr->inits())
                {
                    if (!isNarrowUGLConstantExpression(initializer, context))
                    {
                        return false;
                    }
                }
                if (const clang::Expr *arrayFiller = initListExpr->getArrayFiller(); arrayFiller != nullptr &&
                    !isNarrowUGLConstantExpression(arrayFiller, context))
                {
                    return false;
                }
                return true;
            }
            if (llvm::isa<clang::ImplicitValueInitExpr>(current))
            {
                std::unordered_set<const clang::CXXRecordDecl *> activeRecords;
                return isImmutableConstantType(current->getType(), activeRecords);
            }
            if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(current))
            {
                const clang::CXXConstructorDecl *constructorDecl = constructExpr->getConstructor();
                if (constructorDecl == nullptr || !isKnownUGLConstantValueRecord(constructorDecl->getParent()))
                {
                    return false;
                }
                for (const clang::Expr *argument : constructExpr->arguments())
                {
                    if (!isNarrowUGLConstantExpression(argument, context))
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }

        /** Returns true when const qualification applies to an object or to the elements of an array object. */
        bool hasConstStorageQualifier(const clang::QualType &type)
        {
            if (type.isNull())
            {
                return false;
            }
            if (type.isConstQualified())
            {
                return true;
            }
            const clang::QualType canonicalType = type.getCanonicalType();
            if (const auto *arrayType = llvm::dyn_cast<clang::ArrayType>(canonicalType.getTypePtr()))
            {
                return hasConstStorageQualifier(arrayType->getElementType());
            }
            return false;
        }

        /** Returns true when a declaration is a const, immutable, compile-time shader constant. */
        bool isSafeConstantVarDecl(const clang::VarDecl *varDecl, clang::ASTContext &context)
        {
            if (varDecl == nullptr || varDecl->getType().isVolatileQualified() ||
                (!varDecl->isConstexpr() && !hasConstStorageQualifier(varDecl->getType())))
            {
                return false;
            }
            std::unordered_set<const clang::CXXRecordDecl *> activeRecords;
            if (!isImmutableConstantType(varDecl->getType(), activeRecords))
            {
                return false;
            }
            const clang::Expr *initializer = varDecl->getAnyInitializer();
            return initializer != nullptr && isNarrowUGLConstantExpression(initializer, context);
        }

        /** Returns true when a string starts with the requested prefix. */
        bool startsWith(const std::string &value, const std::string &prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }

        /** Returns true when a string contains the requested path fragment. */
        bool containsPathFragment(const std::string &value, const std::string &fragment)
        {
            return value.find(fragment) != std::string::npos;
        }

        /** Returns a lambda expression hidden behind common wrapper expressions, or null when none exists. */
        const clang::LambdaExpr *getWrappedLambdaExpression(const clang::Expr *expr)
        {
            if (expr == nullptr)
            {
                return nullptr;
            }
            expr = expr->IgnoreParenImpCasts();
            if (const auto *cleanupExpr = llvm::dyn_cast<clang::ExprWithCleanups>(expr))
            {
                return getWrappedLambdaExpression(cleanupExpr->getSubExpr());
            }
            if (const auto *temporaryExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr))
            {
                return getWrappedLambdaExpression(temporaryExpr->getSubExpr());
            }
            if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr))
            {
                return getWrappedLambdaExpression(bindTemporaryExpr->getSubExpr());
            }
            return llvm::dyn_cast<clang::LambdaExpr>(expr);
        }

        /** Creates a verifier for one reachable function body. */
        UGLIRFunctionBodyVerifier::UGLIRFunctionBodyVerifier(UGLIRSourceVerifier &owner, const clang::FunctionDecl *functionDecl)
            : mOwner(owner)
            , mFunctionDecl(functionDecl)
        {
        }

        bool UGLIRFunctionBodyVerifier::TraverseIfStmt(clang::IfStmt *statement)
        {
            if (!statement->isConstexpr())
                return clang::RecursiveASTVisitor<UGLIRFunctionBodyVerifier>::TraverseIfStmt(statement);
            const auto selected = statement->getNondiscardedCase(mFunctionDecl->getASTContext());
            if (!selected.has_value())
            {
                mOwner.addDiagnostic(statement->getBeginLoc(), "dependent if constexpr", "shader semantic preparation did not select a branch.");
                return true;
            }
            return TraverseStmt(statement->getInit()) &&
                   TraverseStmt(statement->getConditionVariableDeclStmt()) &&
                   TraverseStmt(statement->getCond()) && TraverseStmt(*selected);
        }

        bool UGLIRFunctionBodyVerifier::VisitVarDecl(const clang::VarDecl *decl)
        {
            if (decl != nullptr && !decl->isImplicit())
            {
                diagnoseReservedShaderIdentifier(mOwner, decl, "local variable");
                if (decl->isStaticLocal() && !isSafeConstantVarDecl(decl, mFunctionDecl->getASTContext()))
                {
                    mOwner.addDiagnostic(decl->getLocation(), "static storage", "shader-reachable static locals must have immutable compile-time constant initialization.");
                }
                if (mOwner.isLambdaClosureType(decl->getType()) && getWrappedLambdaExpression(decl->getInit()) == nullptr)
                {
                    mOwner.addDiagnostic(decl->getLocation(), "escaping lambda", "lambda closures may only be stored in their defining local variable.");
                }
                mOwner.inspectShaderVisibleType(decl->getType(), decl->getLocation(), "local variable \"" + decl->getNameAsString() + "\"");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitDeclRefExpr(const clang::DeclRefExpr *expr)
        {
            if (expr == nullptr)
            {
                return true;
            }
            const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(expr->getDecl());
            if (varDecl != nullptr && varDecl->hasGlobalStorage() && !varDecl->isStaticLocal() &&
                !isSafeConstantVarDecl(varDecl, mFunctionDecl->getASTContext()))
            {
                mOwner.addDiagnostic(expr->getBeginLoc(), "global/static storage", "shader-reachable global variables must be immutable compile-time constants.");
            }
            if (varDecl != nullptr && varDecl->hasGlobalStorage() && !varDecl->isStaticLocal())
            {
                diagnoseReservedShaderIdentifier(mOwner, varDecl, "global variable");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitMemberExpr(const clang::MemberExpr *expr)
        {
            if (expr == nullptr)
            {
                return true;
            }
            const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(expr->getMemberDecl());
            if (varDecl != nullptr && varDecl->hasGlobalStorage() && !varDecl->isStaticLocal() &&
                !isSafeConstantVarDecl(varDecl, mFunctionDecl->getASTContext()))
            {
                mOwner.addDiagnostic(expr->getBeginLoc(), "global/static storage", "shader-reachable static data members must be immutable compile-time constants.");
            }
            if (varDecl != nullptr && varDecl->hasGlobalStorage() && !varDecl->isStaticLocal())
            {
                diagnoseReservedShaderIdentifier(mOwner, varDecl, "global variable");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitLambdaExpr(const clang::LambdaExpr *expr)
        {
            if (expr == nullptr)
            {
                return true;
            }
            if (expr->isGenericLambda())
            {
                mOwner.addDiagnostic(expr->getBeginLoc(), "generic lambda", "generic lambdas are not supported by Phase 7 UGLIR lowering.");
            }
            for (const clang::LambdaCapture &capture : expr->captures())
            {
                if (capture.getCaptureKind() == clang::LCK_ByRef)
                {
                    mOwner.addDiagnostic(capture.getLocation(), "lambda reference capture", "lambda captures must be by value or capture this explicitly.");
                }
                if (capture.capturesVLAType())
                {
                    mOwner.addDiagnostic(capture.getLocation(), "lambda capture", "variable-length-array captures are not supported.");
                }
                if (capture.capturesVariable())
                {
                    const clang::ValueDecl *capturedDecl = capture.getCapturedVar();
                    if (capturedDecl != nullptr)
                    {
                        mOwner.inspectShaderVisibleType(capturedDecl->getType(), capture.getLocation(), "lambda capture \"" + capturedDecl->getNameAsString() + "\"");
                    }
                }
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitReturnStmt(const clang::ReturnStmt *stmt)
        {
            if (stmt != nullptr && stmt->getRetValue() != nullptr && mOwner.isLambdaClosureType(stmt->getRetValue()->getType()))
            {
                mOwner.addDiagnostic(stmt->getReturnLoc(), "escaping lambda", "returning a lambda closure is not supported.");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXThrowExpr(const clang::CXXThrowExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getThrowLoc(), "exception", "throw expressions are not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXTryStmt(const clang::CXXTryStmt *stmt)
        {
            mOwner.addDiagnostic(stmt == nullptr ? clang::SourceLocation() : stmt->getBeginLoc(), "exception", "try/catch statements are not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXTypeidExpr(const clang::CXXTypeidExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "RTTI", "typeid is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXDynamicCastExpr(const clang::CXXDynamicCastExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "RTTI", "dynamic_cast is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXNewExpr(const clang::CXXNewExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "new/delete", "dynamic allocation is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXDeleteExpr(const clang::CXXDeleteExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "new/delete", "dynamic deallocation is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCoroutineBodyStmt(const clang::CoroutineBodyStmt *stmt)
        {
            mOwner.addDiagnostic(stmt == nullptr ? clang::SourceLocation() : stmt->getBeginLoc(), "coroutine", "coroutine bodies are not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCoawaitExpr(const clang::CoawaitExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "coroutine", "co_await is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCoyieldExpr(const clang::CoyieldExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "coroutine", "co_yield is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCoreturnStmt(const clang::CoreturnStmt *stmt)
        {
            mOwner.addDiagnostic(stmt == nullptr ? clang::SourceLocation() : stmt->getBeginLoc(), "coroutine", "co_return is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitUnaryOperator(const clang::UnaryOperator *expr)
        {
            if (expr != nullptr && expr->getOpcode() == clang::UO_AddrOf)
            {
                mOwner.addDiagnostic(expr->getOperatorLoc(), "address-of", "taking an address with '&' is not supported.");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *expr)
        {
            if (expr == nullptr)
            {
                return true;
            }
            const clang::OverloadedOperatorKind op = expr->getOperator();
            if (op == clang::OO_Amp && expr->getNumArgs() == 1)
            {
                mOwner.addDiagnostic(expr->getOperatorLoc(), "address-of", "overloaded operator& is not supported.");
            }
            if (op == clang::OO_New || op == clang::OO_Array_New || op == clang::OO_Delete || op == clang::OO_Array_Delete)
            {
                mOwner.addDiagnostic(expr->getOperatorLoc(), "new/delete", "explicit allocation operators are not supported.");
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXNullPtrLiteralExpr(const clang::CXXNullPtrLiteralExpr *expr)
        {
            mOwner.addDiagnostic(expr == nullptr ? clang::SourceLocation() : expr->getBeginLoc(), "nullptr", "nullptr pointer flow is not supported.");
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCallExpr(const clang::CallExpr *expr)
        {
            if (expr == nullptr)
            {
                return true;
            }
            const clang::FunctionDecl *callee = expr->getDirectCallee();
            mOwner.inspectCallTarget(expr, callee);
            mOwner.recordReachableCall(mFunctionDecl, callee, expr->getBeginLoc());
            mOwner.inspectExpressionType(expr->getType(), expr->getBeginLoc(), "call expression");
            const auto *methodDecl = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
            const bool lambdaCall = methodDecl != nullptr && methodDecl->getParent() != nullptr && methodDecl->getParent()->isLambda();
            if (!lambdaCall)
            {
                for (const clang::Expr *arg : expr->arguments())
                {
                    if (arg != nullptr && mOwner.isLambdaClosureType(arg->getType()))
                    {
                        mOwner.addDiagnostic(arg->getBeginLoc(), "escaping lambda", "passing a lambda closure to another function is not supported.");
                    }
                }
            }
            return true;
        }

        bool UGLIRFunctionBodyVerifier::VisitCXXConstructExpr(const clang::CXXConstructExpr *expr)
        {
            if (expr != nullptr)
            {
                mOwner.inspectExpressionType(expr->getType(), expr->getBeginLoc(), "constructed temporary");
                mOwner.recordReachableCall(mFunctionDecl, expr->getConstructor(), expr->getBeginLoc());
            }
            return true;
        }

        UGLIRSourceVerifier::UGLIRSourceVerifier(clang::ASTContext &context)
            : mContext(context)
            , mSourceManager(context.getSourceManager())
        {
        }

        UGLIRVerificationResult UGLIRSourceVerifier::run(llvm::ArrayRef<ShaderClassRoot> roots)
        {
            for (const ShaderClassRoot &root : roots)
            {
                validateShaderClassRoot(root);
            }
            analyzeReachableFunctions();
            diagnoseRecursion();
            return mResult;
        }

        void UGLIRSourceVerifier::addDiagnostic(clang::SourceLocation location, const std::string &feature, const std::string &detail)
        {
            UGLIRVerificationDiagnostic diagnostic;
            diagnostic.feature = feature;
            diagnostic.message = "UGLIR verifier: " + feature + " is not supported in shader-reachable code";
            if (!detail.empty())
            {
                diagnostic.message += ". " + detail;
            }

            location = UGLC::CodeGen::normalizeDiagnosticLocation(mSourceManager, location);
            if (location.isValid())
            {
                const clang::PresumedLoc presumedLoc = mSourceManager.getPresumedLoc(location);
                if (presumedLoc.isValid())
                {
                    diagnostic.filePath = presumedLoc.getFilename();
                    diagnostic.line = presumedLoc.getLine();
                    diagnostic.column = presumedLoc.getColumn();
                    diagnostic.hasSourceLocation = true;
                }
            }
            mResult.diagnostics.push_back(std::move(diagnostic));
        }

        void UGLIRSourceVerifier::inspectShaderVisibleType(const clang::QualType &type, clang::SourceLocation location, const std::string &usageContext)
        {
            std::unordered_set<const clang::Type *> visitedTypes;
            inspectTypeRecursive(type, location, usageContext, true, visitedTypes);
        }

        void UGLIRSourceVerifier::inspectShaderVisibleParameterType(const clang::ParmVarDecl *paramDecl, const std::string &usageContext)
        {
            if (paramDecl == nullptr)
            {
                return;
            }

            const clang::QualType fixedSizeArrayType = getFixedSizeArrayParameterType(paramDecl);
            if (!fixedSizeArrayType.isNull())
            {
                inspectShaderVisibleType(fixedSizeArrayType, paramDecl->getLocation(), usageContext);
                return;
            }

            inspectShaderVisibleType(paramDecl->getType(), paramDecl->getLocation(), usageContext);
        }

        void UGLIRSourceVerifier::inspectExpressionType(const clang::QualType &type, clang::SourceLocation location, const std::string &usageContext)
        {
            std::unordered_set<const clang::Type *> visitedTypes;
            inspectTypeRecursive(type, location, usageContext, false, visitedTypes);
        }

        clang::QualType UGLIRSourceVerifier::getFixedSizeArrayParameterType(const clang::ParmVarDecl *paramDecl) const
        {
            if (paramDecl == nullptr)
            {
                return {};
            }

            clang::QualType originalType = paramDecl->getOriginalType();
            if (originalType.isNull())
            {
                return {};
            }
            if (const auto *decayedType = originalType->getAs<clang::DecayedType>())
            {
                originalType = decayedType->getOriginalType();
            }

            const clang::ArrayType *arrayType = mContext.getAsArrayType(originalType.getNonReferenceType());
            if (arrayType == nullptr || !llvm::isa<clang::ConstantArrayType>(arrayType))
            {
                return {};
            }
            return originalType;
        }

        bool UGLIRSourceVerifier::isLambdaClosureType(const clang::QualType &type) const
        {
            if (type.isNull())
            {
                return false;
            }
            const clang::CXXRecordDecl *recordDecl = type.getNonReferenceType()->getAsCXXRecordDecl();
            recordDecl = recordDefinition(recordDecl);
            return recordDecl != nullptr && recordDecl->isLambda();
        }

        void UGLIRSourceVerifier::recordReachableCall(const clang::FunctionDecl *caller, const clang::FunctionDecl *callee, clang::SourceLocation location)
        {
            (void)location;
            const clang::FunctionDecl *callerKey = canonicalFunctionDecl(caller);
            const clang::FunctionDecl *calleeDefinition = getAnalyzableDefinition(callee);
            if (callerKey == nullptr || calleeDefinition == nullptr || !isUserAuthoredFunction(calleeDefinition))
            {
                return;
            }

            const clang::FunctionDecl *calleeKey = canonicalFunctionDecl(calleeDefinition);
            mCallGraph[callerKey].push_back(calleeKey);
            enqueueReachableFunction(calleeDefinition);
        }

        void UGLIRSourceVerifier::inspectCallTarget(const clang::CallExpr *expr, const clang::FunctionDecl *callee)
        {
            if (expr == nullptr || callee == nullptr)
            {
                return;
            }

            const std::string qualifiedName = normalizeStdName(getQualifiedName(callee));
            const clang::OverloadedOperatorKind op = callee->getOverloadedOperator();
            if (op == clang::OO_New || op == clang::OO_Array_New || op == clang::OO_Delete || op == clang::OO_Array_Delete)
            {
                addDiagnostic(expr->getBeginLoc(), "new/delete", "explicit operator new/delete calls are not supported.");
            }
            if (isCPUThreadPrimitiveName(qualifiedName) || isPThreadFunctionName(qualifiedName))
            {
                addDiagnostic(expr->getBeginLoc(), "thread", "CPU thread primitive call \"" + qualifiedName + "\" is not supported.");
            }
            if (isComplexSTLName(qualifiedName))
            {
                addDiagnostic(expr->getBeginLoc(), "complex STL container", "call target \"" + qualifiedName + "\" belongs to a banned STL facility.");
            }

            if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(callee))
            {
                if (methodDecl->isVirtual() && isUserAuthoredDecl(methodDecl))
                {
                    addDiagnostic(expr->getBeginLoc(), "virtual dispatch", "virtual method \"" + methodDecl->getQualifiedNameAsString() + "\" is shader-reachable.");
                }
            }
        }

        bool UGLIRSourceVerifier::isUserAuthoredDecl(const clang::Decl *decl) const
        {
            return decl != nullptr && !decl->isImplicit() && !isIgnoredLocation(decl->getLocation());
        }

        void UGLIRSourceVerifier::validateShaderClassRoot(const ShaderClassRoot &root)
        {
            const clang::CXXRecordDecl *recordDecl = recordDefinition(root.recordDecl);
            if (recordDecl == nullptr)
            {
                return;
            }

            for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
            {
                inspectShaderVisibleType(fieldDecl->getType(), fieldDecl->getLocation(), "shader class field \"" + fieldDecl->getNameAsString() + "\"");
            }

            for (const clang::CXXMethodDecl *methodDecl : recordDecl->methods())
            {
                if (methodDecl == nullptr || methodDecl->isImplicit() || isCreateMethod(methodDecl) || llvm::isa<clang::CXXConstructorDecl>(methodDecl) || llvm::isa<clang::CXXDestructorDecl>(methodDecl))
                {
                    continue;
                }

                const std::string methodName = methodDecl->getNameAsString();
                if (isPermittedEntryName(root.kind, methodName))
                {
                    enqueueReachableFunction(methodDecl);
                    continue;
                }

                if (methodDecl->hasBody())
                {
                    addDiagnostic(methodDecl->getLocation(), "stage interface", "shader class \"" + recordDecl->getNameAsString() + "\" exposes unsupported method \"" + methodName + "\".");
                }
            }
        }

        void UGLIRSourceVerifier::analyzeReachableFunction(const clang::FunctionDecl *functionDecl)
        {
            const clang::FunctionDecl *definition = getAnalyzableDefinition(functionDecl);
            if (definition == nullptr)
            {
                return;
            }

            inspectShaderVisibleType(definition->getReturnType(), definition->getLocation(), "return type of \"" + definition->getNameAsString() + "\"");
            for (const clang::ParmVarDecl *paramDecl : definition->parameters())
            {
                diagnoseReservedShaderIdentifier(*this, paramDecl, "parameter");
                inspectShaderVisibleParameterType(paramDecl, "parameter \"" + paramDecl->getNameAsString() + "\"");
            }

            if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(definition))
            {
                if (methodDecl->isVirtual() && isUserAuthoredDecl(methodDecl))
                {
                    addDiagnostic(methodDecl->getLocation(), "virtual dispatch", "virtual method \"" + methodDecl->getQualifiedNameAsString() + "\" is shader-reachable.");
                }
            }

            if (const clang::Stmt *body = definition->getBody())
            {
                UGLIRFunctionBodyVerifier bodyVerifier(*this, definition);
                if (const auto *constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(definition))
                    for (const auto *initializer : constructor->inits())
                        bodyVerifier.TraverseStmt(initializer->getInit());
                bodyVerifier.TraverseStmt(const_cast<clang::Stmt *>(body));
            }
        }

        void UGLIRSourceVerifier::analyzeReachableFunctions()
        {
            for (size_t index = 0; index < mReachableQueue.size(); ++index)
            {
                const clang::FunctionDecl *functionDecl = canonicalFunctionDecl(mReachableQueue[index]);
                if (functionDecl == nullptr || mAnalyzedFunctions.find(functionDecl) != mAnalyzedFunctions.end())
                {
                    continue;
                }
                mAnalyzedFunctions.insert(functionDecl);
                analyzeReachableFunction(mReachableQueue[index]);
            }
        }

        bool UGLIRSourceVerifier::isPermittedEntryName(ShaderClassKind kind, const std::string &methodName) const
        {
            if (kind == ShaderClassKind::Compute)
            {
                return methodName == UGLC::CodeGen::mUGLComputeShaderFunctionName;
            }
            if (kind == ShaderClassKind::Render)
            {
                return methodName == UGLC::CodeGen::mUGLVertexShaderFunctionName || methodName == UGLC::CodeGen::mUGLFragmentShaderFunctionName;
            }
            return methodName == UGLC::CodeGen::mUGLPixelShaderFunctionName;
        }

        bool UGLIRSourceVerifier::isCreateMethod(const clang::CXXMethodDecl *methodDecl) const
        {
            return methodDecl != nullptr && methodDecl->getNameAsString() == UGLC::CodeGen::mUGLCTORFunctionName;
        }

        void UGLIRSourceVerifier::enqueueReachableFunction(const clang::FunctionDecl *functionDecl)
        {
            const clang::FunctionDecl *definition = getAnalyzableDefinition(functionDecl);
            const clang::FunctionDecl *key = canonicalFunctionDecl(definition);
            if (definition == nullptr || key == nullptr || mQueuedFunctions.find(key) != mQueuedFunctions.end())
            {
                return;
            }
            mQueuedFunctions.insert(key);
            mReachableQueue.push_back(definition);
        }

        const clang::FunctionDecl *UGLIRSourceVerifier::getAnalyzableDefinition(const clang::FunctionDecl *functionDecl) const
        {
            if (functionDecl == nullptr)
            {
                return nullptr;
            }
            const clang::FunctionDecl *definition = nullptr;
            if (functionDecl->hasBody(definition) && definition != nullptr)
            {
                return definition;
            }
            return nullptr;
        }

        bool UGLIRSourceVerifier::isUserAuthoredFunction(const clang::FunctionDecl *functionDecl) const
        {
            return functionDecl != nullptr && isUserAuthoredDecl(functionDecl);
        }

        void UGLIRSourceVerifier::inspectTypeRecursive(const clang::QualType &type,
                                                       clang::SourceLocation location,
                                                       const std::string &usageContext,
                                                       bool reportPointerTypes,
                                                       std::unordered_set<const clang::Type *> &visitedTypes)
        {
            if (type.isNull())
            {
                return;
            }

            const clang::QualType canonicalType = type.getCanonicalType();
            const clang::Type *typePtr = canonicalType.getTypePtrOrNull();
            if (typePtr == nullptr || visitedTypes.find(typePtr) != visitedTypes.end())
            {
                return;
            }
            visitedTypes.insert(typePtr);

            if (reportPointerTypes)
            {
                if (typePtr->isFunctionPointerType())
                {
                    addDiagnostic(location, "function pointer", usageContext + " has function pointer type \"" + canonicalType.getAsString() + "\".");
                    return;
                }
                if (typePtr->isMemberPointerType())
                {
                    addDiagnostic(location, "member pointer", usageContext + " has member pointer type \"" + canonicalType.getAsString() + "\".");
                    return;
                }
                if (typePtr->isBlockPointerType())
                {
                    addDiagnostic(location, "block pointer", usageContext + " has block pointer type \"" + canonicalType.getAsString() + "\".");
                    return;
                }
                if (typePtr->isPointerType())
                {
                    addDiagnostic(location, "raw pointer", usageContext + " has raw pointer type \"" + canonicalType.getAsString() + "\".");
                    return;
                }
            }

            if (const clang::ArrayType *arrayType = mContext.getAsArrayType(canonicalType))
            {
                inspectTypeRecursive(arrayType->getElementType(), location, usageContext, reportPointerTypes, visitedTypes);
                return;
            }

            const clang::CXXRecordDecl *recordDecl = canonicalType->getAsCXXRecordDecl();
            if (recordDecl == nullptr)
            {
                return;
            }
            recordDecl = recordDefinition(recordDecl);
            const std::string qualifiedName = normalizeStdName(getQualifiedName(recordDecl));
            if (isComplexSTLName(qualifiedName))
            {
                addDiagnostic(location, "complex STL container", usageContext + " uses banned type \"" + qualifiedName + "\".");
            }
            if (isCPUThreadPrimitiveName(qualifiedName))
            {
                addDiagnostic(location, "thread", usageContext + " uses CPU thread primitive \"" + qualifiedName + "\".");
            }
            if (isHostResourceHandleName(qualifiedName))
            {
                addDiagnostic(location, "host resource handle", usageContext + " uses host-only resource handle \"" + qualifiedName + "\".");
            }

            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordDecl))
            {
                inspectTemplateArguments(specializationDecl, location, usageContext, reportPointerTypes, visitedTypes);
            }

            inspectUserRecordFields(recordDecl, location, usageContext, visitedTypes);
        }

        void UGLIRSourceVerifier::inspectUserRecordFields(const clang::CXXRecordDecl *recordDecl,
                                                          clang::SourceLocation location,
                                                          const std::string &usageContext,
                                                          std::unordered_set<const clang::Type *> &visitedTypes)
        {
            if (recordDecl == nullptr || !isUserAuthoredDecl(recordDecl))
            {
                return;
            }

            if (const auto *destructor = recordDecl->getDestructor();
                destructor != nullptr && !destructor->isTrivial())
                addDiagnostic(location, "non-trivial destructor", usageContext + " requires cleanup that is not supported by shader lowering.");

            for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
            {
                diagnoseReservedShaderIdentifier(*this, fieldDecl, "field");
                const clang::SourceLocation fieldLocation = fieldDecl->getLocation().isValid() ? fieldDecl->getLocation() : location;
                inspectTypeRecursive(fieldDecl->getType(), fieldLocation, usageContext + " field \"" + fieldDecl->getNameAsString() + "\"", true, visitedTypes);
            }
        }

        void UGLIRSourceVerifier::inspectTemplateArguments(const clang::ClassTemplateSpecializationDecl *specializationDecl,
                                                           clang::SourceLocation location,
                                                           const std::string &usageContext,
                                                           bool reportPointerTypes,
                                                           std::unordered_set<const clang::Type *> &visitedTypes)
        {
            if (specializationDecl == nullptr)
            {
                return;
            }

            const clang::TemplateArgumentList &arguments = specializationDecl->getTemplateArgs();
            for (unsigned index = 0; index < arguments.size(); ++index)
            {
                const clang::TemplateArgument &argument = arguments[index];
                if (argument.getKind() == clang::TemplateArgument::Type)
                {
                    inspectTypeRecursive(argument.getAsType(), location, usageContext, reportPointerTypes, visitedTypes);
                }
            }
        }

        void UGLIRSourceVerifier::diagnoseRecursion()
        {
            std::unordered_map<const clang::FunctionDecl *, FunctionCycleState> states;
            std::vector<const clang::FunctionDecl *> stack;
            for (const auto &entry : mCallGraph)
            {
                if (states[entry.first] == FunctionCycleState::Unvisited)
                {
                    (void)diagnoseRecursionFrom(entry.first, states, stack);
                }
            }
        }

        bool UGLIRSourceVerifier::diagnoseRecursionFrom(const clang::FunctionDecl *functionDecl,
                                                        std::unordered_map<const clang::FunctionDecl *, FunctionCycleState> &states,
                                                        std::vector<const clang::FunctionDecl *> &stack)
        {
            states[functionDecl] = FunctionCycleState::Visiting;
            stack.push_back(functionDecl);

            const auto edgeIter = mCallGraph.find(functionDecl);
            if (edgeIter != mCallGraph.end())
            {
                for (const clang::FunctionDecl *callee : edgeIter->second)
                {
                    const FunctionCycleState state = states[callee];
                    if (state == FunctionCycleState::Visiting)
                    {
                        addDiagnostic(callee->getLocation(), "recursion", "recursive call graph reaches function \"" + callee->getNameAsString() + "\".");
                        stack.pop_back();
                        states[functionDecl] = FunctionCycleState::Visited;
                        return true;
                    }
                    if (state == FunctionCycleState::Unvisited && diagnoseRecursionFrom(callee, states, stack))
                    {
                        stack.pop_back();
                        states[functionDecl] = FunctionCycleState::Visited;
                        return true;
                    }
                }
            }

            stack.pop_back();
            states[functionDecl] = FunctionCycleState::Visited;
            return false;
        }

        bool UGLIRSourceVerifier::isIgnoredLocation(clang::SourceLocation location) const
        {
            location = UGLC::CodeGen::normalizeDiagnosticLocation(mSourceManager, location);
            if (location.isInvalid())
            {
                return true;
            }
            if (mSourceManager.isInSystemHeader(location) || mSourceManager.isInExternCSystemHeader(location))
            {
                return true;
            }

            const clang::PresumedLoc presumedLoc = mSourceManager.getPresumedLoc(location);
            if (presumedLoc.isInvalid())
            {
                return true;
            }

            const std::string filePath = presumedLoc.getFilename();
            return containsPathFragment(filePath, "/GVM/UGLHeaders/") || containsPathFragment(filePath, "\\GVM\\UGLHeaders\\") || containsPathFragment(filePath, "/UGLHeaders/") || containsPathFragment(filePath, "\\UGLHeaders\\");
        }

        std::string UGLIRSourceVerifier::getQualifiedName(const clang::NamedDecl *decl) const
        {
            if (decl == nullptr)
            {
                return {};
            }
            return decl->getQualifiedNameAsString();
        }

        std::string UGLIRSourceVerifier::normalizeStdName(std::string name) const
        {
            const std::string libcxxPrefix = "std::__1::";
            if (startsWith(name, libcxxPrefix))
            {
                return "std::" + name.substr(libcxxPrefix.size());
            }
            return name;
        }

        bool UGLIRSourceVerifier::isComplexSTLName(const std::string &qualifiedName) const
        {
            return qualifiedName == "std::vector" ||
                   qualifiedName == "std::map" ||
                   qualifiedName == "std::unordered_map" ||
                   qualifiedName == "std::set" ||
                   qualifiedName == "std::deque" ||
                   qualifiedName == "std::list" ||
                   qualifiedName == "std::basic_string" ||
                   qualifiedName == "std::string" ||
                   qualifiedName == "std::function";
        }

        bool UGLIRSourceVerifier::isCPUThreadPrimitiveName(const std::string &qualifiedName) const
        {
            return qualifiedName == "std::thread" ||
                   qualifiedName == "std::jthread" ||
                   qualifiedName == "std::mutex" ||
                   qualifiedName == "std::condition_variable" ||
                   qualifiedName == "std::future" ||
                   qualifiedName == "std::async";
        }

        bool UGLIRSourceVerifier::isHostResourceHandleName(const std::string &qualifiedName) const
        {
            return qualifiedName == UGLC::CodeGen::mUGLHostBufferName ||
                   qualifiedName == UGLC::CodeGen::mUGLHostBufferRangeName ||
                   qualifiedName == UGLC::CodeGen::mUGLHostTextureName ||
                   qualifiedName == UGLC::CodeGen::mUGLHostTextureViewName ||
                   qualifiedName == UGLC::CodeGen::mUGLHostDeviceName ||
                   qualifiedName == UGLC::CodeGen::mUGLHostQueueName;
        }

        bool UGLIRSourceVerifier::isPThreadFunctionName(const std::string &qualifiedName) const
        {
            return startsWith(qualifiedName, "pthread_") || startsWith(qualifiedName, "::pthread_");
        }
    } // namespace

    bool isSafeConstantVarDeclForUGLIR(const clang::VarDecl *varDecl, clang::ASTContext &context)
    {
        return isSafeConstantVarDecl(varDecl, context);
    }

    UGLIRVerificationResult verifyTranslationUnitForUGLIR(clang::ASTContext &context, llvm::ArrayRef<ShaderClassRoot> roots)
    {
        UGLIRSourceVerifier verifier(context);
        return verifier.run(roots);
    }

    std::string formatUGLIRVerificationDiagnostics(const UGLIRVerificationResult &result)
    {
        std::string output;
        for (const UGLIRVerificationDiagnostic &diagnostic : result.diagnostics)
        {
            if (diagnostic.hasSourceLocation)
            {
                output += UGLC::CodeGen::formatClangStyleDiagnostic(diagnostic.filePath,
                                                                     diagnostic.line,
                                                                     diagnostic.column,
                                                                     diagnostic.message);
            }
            else
            {
                output += UGLC::CodeGen::formatUnlocatedDiagnostic(diagnostic.message);
            }
            output += '\n';
        }
        return output;
    }
} // namespace UGLC::CodeGen::UGLIR
