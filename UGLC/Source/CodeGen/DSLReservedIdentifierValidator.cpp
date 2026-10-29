#include "DSLReservedIdentifierValidator.hpp"

#include "BaseASTVisitor.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Stmt.h>
#include <llvm/Support/Casting.h>

#include <string>

namespace UGLC::CodeGen
{
    namespace
    {
        /**
         * Returns the qualified function context used in reserved-identifier diagnostics.
         */
        std::string describeFunctionContext(const clang::FunctionDecl *functionDecl)
        {
            if (functionDecl == nullptr)
            {
                return {};
            }
            return " in function \"" + functionDecl->getQualifiedNameAsString() + "\"";
        }

        /**
         * Returns the qualified record context used in reserved-identifier diagnostics.
         */
        std::string describeRecordContext(const clang::DeclContext *declContext)
        {
            const auto *recordDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(declContext);
            if (recordDecl == nullptr)
            {
                return {};
            }
            return " in record \"" + recordDecl->getQualifiedNameAsString() + "\"";
        }

        /**
         * Returns the qualified shader-class context used in binding diagnostics.
         */
        std::string describeShaderClassContext(const clang::CXXRecordDecl *shaderClassDecl)
        {
            if (shaderClassDecl == nullptr)
            {
                return {};
            }
            return " in shader class \"" + shaderClassDecl->getQualifiedNameAsString() + "\"";
        }

        /**
         * Returns whether the declaration should be ignored by shader DSL identifier validation.
         */
        bool shouldSkipDeclaration(BaseASTVisitor &visitor, const clang::Decl *decl)
        {
            return decl == nullptr || decl->isImplicit() || visitor.isFromExcludedFile(decl->getLocation());
        }

        /**
         * Reports one use of a reserved shader DSL variable identifier.
         */
        void throwReservedVariableIdentifierError(BaseASTVisitor &visitor,
                                                  const clang::NamedDecl *decl,
                                                  const std::string &role,
                                                  const std::string &context)
        {
            const std::string name = decl == nullptr ? std::string{} : decl->getNameAsString();
            visitor.throwCodegenError(decl,
                                      "UGL DSL reserves variable identifier \"" + name
                                          + "\" for shader stage names. Rename " + role + " \"" + name + "\"" + context + ".");
        }

        /**
         * Validates one named variable-like declaration against the DSL reserved table.
         */
        void validateVariableLikeNameOrThrow(BaseASTVisitor &visitor,
                                             const clang::NamedDecl *decl,
                                             const std::string &role,
                                             const std::string &context)
        {
            if (shouldSkipDeclaration(visitor, decl))
            {
                return;
            }

            const std::string name = decl->getNameAsString();
            if (!isReservedDSLVariableIdentifier(name))
            {
                return;
            }

            throwReservedVariableIdentifierError(visitor, decl, role, context);
        }

        /**
         * Validates one variable declaration and any initializer expression it owns.
         */
        void validateVarDeclOrThrow(BaseASTVisitor &visitor, const clang::VarDecl *varDecl, const clang::FunctionDecl *owningFunction);

        /**
         * Validates local declarations nested inside one shader-visible statement tree.
         */
        void validateStmtOrThrow(BaseASTVisitor &visitor, const clang::Stmt *stmt, const clang::FunctionDecl *owningFunction)
        {
            if (stmt == nullptr)
            {
                return;
            }

            if (const auto *declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt))
            {
                for (const clang::Decl *decl : declStmt->decls())
                {
                    if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
                    {
                        validateVarDeclOrThrow(visitor, varDecl, owningFunction);
                    }
                }
            }

            for (const clang::Stmt *child : stmt->children())
            {
                validateStmtOrThrow(visitor, child, owningFunction);
            }
        }

        /**
         * Validates one variable declaration and any initializer expression it owns.
         */
        void validateVarDeclOrThrow(BaseASTVisitor &visitor, const clang::VarDecl *varDecl, const clang::FunctionDecl *owningFunction)
        {
            if (shouldSkipDeclaration(visitor, varDecl) || llvm::isa<clang::ParmVarDecl>(varDecl))
            {
                return;
            }

            const bool isGlobalVariable = varDecl->isFileVarDecl() || varDecl->isStaticDataMember();
            const std::string role = isGlobalVariable ? "global variable" : "local variable";
            validateVariableLikeNameOrThrow(visitor, varDecl, role, describeFunctionContext(owningFunction));
            validateStmtOrThrow(visitor, varDecl->getInit(), owningFunction);
        }

        /**
         * Returns whether a C++ method body is emitted as shader helper code.
         */
        bool shouldValidateMethodBody(const clang::CXXMethodDecl *methodDecl)
        {
            return methodDecl != nullptr &&
                   methodDecl->hasBody() &&
                   methodDecl->isUserProvided() &&
                   !methodDecl->isCopyAssignmentOperator() &&
                   !methodDecl->isMoveAssignmentOperator() &&
                   !llvm::isa<clang::CXXConstructorDecl>(methodDecl) &&
                   !llvm::isa<clang::CXXDestructorDecl>(methodDecl);
        }

        /**
         * Validates one shader-visible function declaration or definition.
         */
        void validateFunctionDeclOrThrow(BaseASTVisitor &visitor, const clang::FunctionDecl *functionDecl)
        {
            if (shouldSkipDeclaration(visitor, functionDecl))
            {
                return;
            }

            if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(functionDecl);
                methodDecl != nullptr && !shouldValidateMethodBody(methodDecl))
            {
                return;
            }

            const std::string functionContext = describeFunctionContext(functionDecl);
            for (const clang::ParmVarDecl *paramDecl : functionDecl->parameters())
            {
                validateVariableLikeNameOrThrow(visitor, paramDecl, "parameter", functionContext);
            }

            validateStmtOrThrow(visitor, functionDecl->getBody(), functionDecl);
        }

        /**
         * Validates one shader-visible record declaration and declarations emitted with it.
         */
        void validateRecordDeclOrThrow(BaseASTVisitor &visitor, const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl != nullptr && recordDecl->getDefinition() != nullptr)
            {
                recordDecl = recordDecl->getDefinition();
            }

            if (shouldSkipDeclaration(visitor, recordDecl))
            {
                return;
            }

            const std::string recordContext = describeRecordContext(recordDecl);
            for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
            {
                validateVariableLikeNameOrThrow(visitor, fieldDecl, "field", recordContext);
            }

            for (const clang::Decl *innerDecl : recordDecl->decls())
            {
                if (const auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
                {
                    if (visitor.shouldEmitNestedRecordDefinition(recordDecl, nestedRecord))
                    {
                        validateRecordDeclOrThrow(visitor, nestedRecord);
                    }
                }
            }

            for (const clang::CXXMethodDecl *methodDecl : recordDecl->methods())
            {
                if (shouldValidateMethodBody(methodDecl))
                {
                    validateFunctionDeclOrThrow(visitor, methodDecl);
                }
            }
        }

        /**
         * Validates one function template through its emitted templated declaration.
         */
        void validateFunctionTemplateDeclOrThrow(BaseASTVisitor &visitor, const clang::FunctionTemplateDecl *templateDecl)
        {
            if (shouldSkipDeclaration(visitor, templateDecl))
            {
                return;
            }
            validateFunctionDeclOrThrow(visitor, templateDecl->getTemplatedDecl());
        }

        /**
         * Validates one class template through its emitted templated declaration.
         */
        void validateClassTemplateDeclOrThrow(BaseASTVisitor &visitor, const clang::ClassTemplateDecl *templateDecl)
        {
            if (shouldSkipDeclaration(visitor, templateDecl))
            {
                return;
            }
            validateRecordDeclOrThrow(visitor, templateDecl->getTemplatedDecl());
        }

        /**
         * Validates all shader-visible declarations nested in one namespace declaration.
         */
        void validateNamespaceDeclOrThrow(BaseASTVisitor &visitor, const clang::NamespaceDecl *namespaceDecl);

        /**
         * Validates one shader-visible declaration node.
         */
        void validateDeclOrThrow(BaseASTVisitor &visitor, const clang::Decl *decl)
        {
            if (shouldSkipDeclaration(visitor, decl))
            {
                return;
            }

            if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
            {
                validateRecordDeclOrThrow(visitor, recordDecl);
            }
            else if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(decl))
            {
                validateFunctionDeclOrThrow(visitor, functionDecl);
            }
            else if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(decl))
            {
                validateNamespaceDeclOrThrow(visitor, namespaceDecl);
            }
            else if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
            {
                validateVarDeclOrThrow(visitor, varDecl, nullptr);
            }
            else if (const auto *functionTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            {
                validateFunctionTemplateDeclOrThrow(visitor, functionTemplateDecl);
            }
            else if (const auto *classTemplateDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
            {
                validateClassTemplateDeclOrThrow(visitor, classTemplateDecl);
            }
        }

        /**
         * Validates all shader-visible declarations nested in one namespace declaration.
         */
        void validateNamespaceDeclOrThrow(BaseASTVisitor &visitor, const clang::NamespaceDecl *namespaceDecl)
        {
            if (shouldSkipDeclaration(visitor, namespaceDecl) || namespaceDecl->isAnonymousNamespace())
            {
                return;
            }

            for (const clang::Decl *subDecl : namespaceDecl->decls())
            {
                validateDeclOrThrow(visitor, subDecl);
            }
        }

        /**
         * Finds the shader-class field that produced one shader binding name.
         */
        const clang::FieldDecl *findShaderBindingField(BaseASTVisitor &visitor,
                                                       const clang::CXXRecordDecl *shaderClassDecl,
                                                       const ShaderBindGroupInfo &bindGroupInfo)
        {
            if (shaderClassDecl == nullptr)
            {
                return nullptr;
            }

            for (const clang::FieldDecl *fieldDecl : visitor.getAllFieldFromRecord(shaderClassDecl))
            {
                if (fieldDecl != nullptr && fieldDecl->getNameAsString() == bindGroupInfo.name)
                {
                    return fieldDecl;
                }
            }
            return nullptr;
        }
    } // namespace

    bool isReservedDSLVariableIdentifier(std::string_view identifier)
    {
        return identifier == "vertex";
    }

    void validateShaderDSLReservedIdentifiersOrThrow(BaseASTVisitor &visitor, const std::vector<const clang::Decl *> &shaderDeclarations)
    {
        for (const clang::Decl *decl : shaderDeclarations)
        {
            validateDeclOrThrow(visitor, decl);
        }
    }

    void validateShaderDSLReservedBindingIdentifiersOrThrow(BaseASTVisitor &visitor,
                                                            const clang::CXXRecordDecl *shaderClassDecl,
                                                            const BindGroupInfoMap &bindGroupInfoMap)
    {
        const std::string shaderClassContext = describeShaderClassContext(shaderClassDecl);
        for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
        {
            (void)slotIndex;
            if (!isReservedDSLVariableIdentifier(bindGroupInfo.name))
            {
                continue;
            }

            const clang::FieldDecl *fieldDecl = findShaderBindingField(visitor, shaderClassDecl, bindGroupInfo);
            if (fieldDecl != nullptr)
            {
                throwReservedVariableIdentifierError(visitor, fieldDecl, "shader binding variable", shaderClassContext);
            }

            visitor.throwCodegenError("UGL DSL reserves variable identifier \"" + bindGroupInfo.name
                                      + "\" for shader stage names. Rename shader binding variable \""
                                      + bindGroupInfo.name + "\"" + shaderClassContext + ".");
        }
    }
} // namespace UGLC::CodeGen
