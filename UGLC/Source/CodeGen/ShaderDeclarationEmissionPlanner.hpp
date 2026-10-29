#pragma once

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>

#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    /** Describes selected shader declarations grouped by the emission stage that needs them while preserving source order inside each group. */
    struct ShaderDeclarationEmissionPlan
    {
        std::vector<const clang::CXXRecordDecl *> recordForwardDeclarations;
        /** Keeps records and variables in their original relative source order for full definition emission. */
        std::vector<const clang::Decl *> recordAndVariableDefinitions;
        std::vector<const clang::FunctionDecl *> helperFunctions;
        std::vector<const clang::CXXMethodDecl *> helperMethods;
        std::vector<const clang::TypedefNameDecl *> typedefs;
        std::vector<const clang::ClassTemplateDecl *> classTemplates;
        std::vector<const clang::FunctionTemplateDecl *> functionTemplates;
    };

    /** Stores generated shader declaration text in backend-neutral visibility order before the final shader entry is appended. */
    struct ShaderDeclarationEmissionSections
    {
        std::string recordForwardDeclarations;
        std::string typeAliasDefinitions;
        std::string typeAndValueDefinitions;
        std::string earlyHelperPrototypes;
        std::string earlyRecordDefinitions;
        std::string backendTypeDefinitions;
        std::string shaderClassScopeDefinitions;
        std::string backendResourceDeclarations;
        std::string groupSharedDeclarations;
        std::string helperPrototypes;
        std::string backendSpecializedHelperPrototypes;
        std::string recordDefinitions;
        std::string lateRecordDefinitions;
        std::string backendGeneratedHelperDefinitions;
        std::string helperDefinitions;
        std::string methodDefinitions;
        std::string backendSpecializedHelperDefinitions;
        std::string entryDefinition;

        /** Concatenates every declaration section using the shared cross-backend declaration visibility order. */
        [[nodiscard]] std::string render() const;
    };

    /** Builds backend-neutral declaration emission buckets from an already reachability-filtered shader declaration list. */
    class ShaderDeclarationEmissionPlanner final
    {
    public:
        /** Groups selected shader declarations by emission role without changing the source order inside any bucket. */
        [[nodiscard]] ShaderDeclarationEmissionPlan build(const std::vector<const clang::Decl *> &shaderDefs) const;

        /** Groups selected shader declarations and separates helper functions from the active shader entry. */
        [[nodiscard]] ShaderDeclarationEmissionPlan build(const std::vector<const clang::Decl *> &shaderDefs,
                                                          const clang::FunctionDecl *entryFunction) const;
    };
} // namespace UGLC::CodeGen
