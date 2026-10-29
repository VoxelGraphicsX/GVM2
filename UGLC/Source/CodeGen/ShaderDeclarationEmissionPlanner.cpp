#include <CodeGen/ShaderDeclarationEmissionPlanner.hpp>

#include <llvm/Support/Casting.h>

#include <unordered_set>

namespace UGLC::CodeGen
{
    std::string ShaderDeclarationEmissionSections::render() const
    {
        std::string result;
        result += recordForwardDeclarations;
        result += typeAliasDefinitions;
        result += typeAndValueDefinitions;
        result += earlyHelperPrototypes;
        result += earlyRecordDefinitions;
        result += backendTypeDefinitions;
        result += shaderClassScopeDefinitions;
        result += backendResourceDeclarations;
        result += groupSharedDeclarations;
        result += helperPrototypes;
        result += backendSpecializedHelperPrototypes;
        result += recordDefinitions;
        result += lateRecordDefinitions;
        result += backendGeneratedHelperDefinitions;
        result += helperDefinitions;
        result += methodDefinitions;
        result += backendSpecializedHelperDefinitions;
        result += entryDefinition;
        return result;
    }

    ShaderDeclarationEmissionPlan ShaderDeclarationEmissionPlanner::build(const std::vector<const clang::Decl *> &shaderDefs) const
    {
        return build(shaderDefs, nullptr);
    }

    ShaderDeclarationEmissionPlan ShaderDeclarationEmissionPlanner::build(const std::vector<const clang::Decl *> &shaderDefs,
                                                                          const clang::FunctionDecl *entryFunction) const
    {
        ShaderDeclarationEmissionPlan plan;
        std::unordered_set<const clang::CXXRecordDecl *> forwardedRecords;
        const clang::FunctionDecl *canonicalEntryFunction = entryFunction == nullptr ? nullptr : entryFunction->getCanonicalDecl();

        for (const clang::Decl *decl : shaderDefs)
        {
            if (const auto *recordDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(decl))
            {
                const clang::CXXRecordDecl *canonicalRecord = recordDecl->getCanonicalDecl();
                if (canonicalRecord != nullptr && forwardedRecords.insert(canonicalRecord).second)
                {
                    plan.recordForwardDeclarations.emplace_back(recordDecl);
                }
                plan.recordAndVariableDefinitions.emplace_back(recordDecl);
                continue;
            }

            if (const auto *methodDecl = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(decl))
            {
                if (canonicalEntryFunction == nullptr || methodDecl->getCanonicalDecl() != canonicalEntryFunction)
                {
                    plan.helperMethods.emplace_back(methodDecl);
                }
                continue;
            }

            if (const auto *functionDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl))
            {
                if (canonicalEntryFunction == nullptr || functionDecl->getCanonicalDecl() != canonicalEntryFunction)
                {
                    plan.helperFunctions.emplace_back(functionDecl);
                }
                continue;
            }

            if (const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(decl))
            {
                plan.recordAndVariableDefinitions.emplace_back(varDecl);
                continue;
            }

            if (const auto *typedefDecl = llvm::dyn_cast_or_null<clang::TypedefNameDecl>(decl))
            {
                plan.typedefs.emplace_back(typedefDecl);
                continue;
            }

            if (const auto *classTemplateDecl = llvm::dyn_cast_or_null<clang::ClassTemplateDecl>(decl))
            {
                plan.classTemplates.emplace_back(classTemplateDecl);
                continue;
            }

            if (const auto *functionTemplateDecl = llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(decl))
            {
                plan.functionTemplates.emplace_back(functionTemplateDecl);
            }
        }

        return plan;
    }
} // namespace UGLC::CodeGen
