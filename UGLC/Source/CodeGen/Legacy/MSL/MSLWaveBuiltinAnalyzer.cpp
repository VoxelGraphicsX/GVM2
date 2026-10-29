#include "MSLWaveBuiltinAnalyzer.hpp"

#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <llvm/Support/Casting.h>

#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace UGLC::CodeGen::MSL
{
    namespace
    {
        constexpr const char *WaveLaneIndexHiddenBaseName = "__uglc_hidden_wave_lane_index";
        constexpr const char *WaveLaneCountHiddenBaseName = "__uglc_hidden_wave_lane_count";

        struct WaveBuiltinFunctionInfo
        {
            MSLWaveBuiltinAnalyzer::Requirements directRequirements;
            std::unordered_set<const clang::FunctionDecl *> callees;
        };

        std::string makeUniqueWaveBuiltinName(const std::string &baseName, std::unordered_set<std::string> &usedNames)
        {
            std::string candidate = baseName;
            int suffix = 0;
            while (usedNames.contains(candidate))
            {
                ++suffix;
                candidate = baseName + "_" + std::to_string(suffix);
            }
            usedNames.emplace(candidate);
            return candidate;
        }

        void collectDeclaredNamesFromStmt(const clang::Stmt *stmt, std::unordered_set<std::string> &names);

        void collectDeclaredNamesFromDecl(const clang::Decl *decl, std::unordered_set<std::string> &names)
        {
            if (decl == nullptr)
            {
                return;
            }

            if (const auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl))
            {
                const std::string name = namedDecl->getNameAsString();
                if (name.empty() == false)
                {
                    names.emplace(name);
                }
            }

            if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(decl))
            {
                for (const auto *param : functionDecl->parameters())
                {
                    collectDeclaredNamesFromDecl(param, names);
                }
                collectDeclaredNamesFromStmt(functionDecl->getBody(), names);
            }

            if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
            {
                for (const auto *innerDecl : recordDecl->decls())
                {
                    collectDeclaredNamesFromDecl(innerDecl, names);
                }
            }
            else if (const auto *functionTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            {
                collectDeclaredNamesFromDecl(functionTemplateDecl->getTemplatedDecl(), names);
            }
            else if (const auto *classTemplateDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
            {
                collectDeclaredNamesFromDecl(classTemplateDecl->getTemplatedDecl(), names);
            }
        }

        void collectDeclaredNamesFromStmt(const clang::Stmt *stmt, std::unordered_set<std::string> &names)
        {
            if (stmt == nullptr)
            {
                return;
            }

            if (const auto *declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt))
            {
                for (const auto *decl : declStmt->decls())
                {
                    collectDeclaredNamesFromDecl(decl, names);
                }
            }

            for (const auto *child : stmt->children())
            {
                collectDeclaredNamesFromStmt(child, names);
            }
        }

        class WaveBuiltinScanner
        {
        public:
            /**
             * @brief Records direct builtin usage and normal helper call edges for one function.
             */
            [[nodiscard]] WaveBuiltinFunctionInfo scan(const clang::FunctionDecl *func) const
            {
                WaveBuiltinFunctionInfo info;
                scanStmt(func->getBody(), info);
                return info;
            }

        private:
            void scanStmt(const clang::Stmt *stmt, WaveBuiltinFunctionInfo &info) const
            {
                if (stmt == nullptr)
                {
                    return;
                }

                if (const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(stmt))
                {
                    if (const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl()))
                    {
                        const auto *canonicalDecl = calleeDecl->getCanonicalDecl();
                        const std::string qualifiedName = canonicalDecl->getQualifiedNameAsString();
                        if (qualifiedName == mUGLFunctionWaveGetLaneIndexName)
                        {
                            info.directRequirements.laneIndex = true;
                            info.directRequirements.explicitLaneIndexQuery = true;
                        }
                        else if (qualifiedName == mUGLFunctionWaveGetLaneCountName)
                        {
                            info.directRequirements.laneCount = true;
                            info.directRequirements.explicitLaneCountQuery = true;
                        }
                        else if (qualifiedName == mUGLFunctionWaveActiveBallotName)
                        {
                            info.directRequirements.laneCount = true;
                        }
                        else if (qualifiedName == mUGLFunctionWaveMatchName)
                        {
                            info.directRequirements.laneIndex = true;
                            info.directRequirements.laneCount = true;
                        }
                        else
                        {
                            info.callees.emplace(canonicalDecl);
                        }
                    }
                }

                for (const auto *child : stmt->children())
                {
                    scanStmt(child, info);
                }
            }
        };
    } // namespace

    bool MSLWaveBuiltinAnalyzer::Requirements::any() const
    {
        return laneIndex || laneCount;
    }

    bool MSLWaveBuiltinAnalyzer::Requirements::usesExplicitQueries() const
    {
        return explicitLaneIndexQuery || explicitLaneCountQuery;
    }

    void MSLWaveBuiltinAnalyzer::reset()
    {
        mRequirementsByFunction.clear();
        mLaneIndexHiddenName.clear();
        mLaneCountHiddenName.clear();
    }

    void MSLWaveBuiltinAnalyzer::analyze(const std::vector<const clang::Decl *> &shaderDefs,
                                         const clang::CXXRecordDecl *shaderClassDecl,
                                         const clang::FunctionDecl *mainFunc,
                                         const BaseASTVisitor &visitor)
    {
        reset();

        WaveBuiltinScanner scanner;
        std::unordered_map<const clang::FunctionDecl *, WaveBuiltinFunctionInfo> functionInfos;

        auto registerFunction = [&](const clang::FunctionDecl *func) {
            if (func == nullptr || func->hasBody() == false)
            {
                return;
            }

            const auto *canonicalFunc = func->getCanonicalDecl();
            if (functionInfos.contains(canonicalFunc))
            {
                return;
            }

            for (const auto *param : func->parameters())
            {
                if (visitor.checkAttibuteByName(param, mUGLAttributeWaveLaneIndexName))
                {
                    throw std::runtime_error("Shader function \"" + func->getQualifiedNameAsString() + "\" still uses removed parameter attribute [[WaveLaneIndex]] on \"" + param->getNameAsString() + "\". Use WaveGetLaneIndex() inside shader code instead.");
                }
                if (visitor.checkAttibuteByName(param, mUGLAttributeWaveLaneCountName))
                {
                    throw std::runtime_error("Shader function \"" + func->getQualifiedNameAsString() + "\" still uses removed parameter attribute [[WaveLaneCount]] on \"" + param->getNameAsString() + "\". Use WaveGetLaneCount() inside shader code instead.");
                }
            }

            functionInfos.emplace(canonicalFunc, scanner.scan(func));
        };

        std::function<void(const clang::Decl *)> registerDeclFunctions = [&](const clang::Decl *decl) {
            if (decl == nullptr)
            {
                return;
            }

            if (const auto *func = llvm::dyn_cast<clang::FunctionDecl>(decl))
            {
                registerFunction(func);
                return;
            }
            if (const auto *functionTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            {
                registerFunction(functionTemplateDecl->getTemplatedDecl());
                return;
            }
            if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
            {
                for (const auto *innerDecl : recordDecl->decls())
                {
                    registerDeclFunctions(innerDecl);
                }
                return;
            }
            if (const auto *classTemplateDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
            {
                registerDeclFunctions(classTemplateDecl->getTemplatedDecl());
            }
        };

        for (const auto *decl : shaderDefs)
        {
            registerDeclFunctions(decl);
        }

        if (mainFunc != nullptr)
        {
            registerFunction(mainFunc);
        }

        for (const auto &[func, info] : functionInfos)
        {
            Requirements requirements;
            requirements.laneIndex = info.directRequirements.laneIndex;
            requirements.laneCount = info.directRequirements.laneCount;
            requirements.explicitLaneIndexQuery = info.directRequirements.explicitLaneIndexQuery;
            requirements.explicitLaneCountQuery = info.directRequirements.explicitLaneCountQuery;
            mRequirementsByFunction.emplace(func, requirements);
        }

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto &[func, info] : functionInfos)
            {
                auto &resolvedRequirements = mRequirementsByFunction[func];
                for (const auto *callee : info.callees)
                {
                    if (const auto found = mRequirementsByFunction.find(callee); found != mRequirementsByFunction.end())
                    {
                        if (found->second.laneIndex && resolvedRequirements.laneIndex == false)
                        {
                            resolvedRequirements.laneIndex = true;
                            changed = true;
                        }
                        if (found->second.laneCount && resolvedRequirements.laneCount == false)
                        {
                            resolvedRequirements.laneCount = true;
                            changed = true;
                        }
                        if (found->second.explicitLaneIndexQuery && resolvedRequirements.explicitLaneIndexQuery == false)
                        {
                            resolvedRequirements.explicitLaneIndexQuery = true;
                            changed = true;
                        }
                        if (found->second.explicitLaneCountQuery && resolvedRequirements.explicitLaneCountQuery == false)
                        {
                            resolvedRequirements.explicitLaneCountQuery = true;
                            changed = true;
                        }
                    }
                }
            }
        }

        const Requirements mainRequirements = getRequirements(mainFunc);
        if (mainRequirements.usesExplicitQueries() && (mainFunc == nullptr || mainFunc->getNameAsString() != mUGLComputeShaderFunctionName))
        {
            validateWaveLaneQuerySupportOrThrow(getMSLShaderBackendCapabilities(), mainFunc);
        }

        bool anyWaveBuiltinRequired = false;
        for (const auto &[func, requirements] : mRequirementsByFunction)
        {
            (void)func;
            if (requirements.any())
            {
                anyWaveBuiltinRequired = true;
                break;
            }
        }
        if (anyWaveBuiltinRequired == false)
        {
            return;
        }

        std::unordered_set<std::string> usedNames;
        collectDeclaredNamesFromDecl(shaderClassDecl, usedNames);
        for (const auto *decl : shaderDefs)
        {
            collectDeclaredNamesFromDecl(decl, usedNames);
        }

        if (mainRequirements.laneIndex)
        {
            mLaneIndexHiddenName = makeUniqueWaveBuiltinName(WaveLaneIndexHiddenBaseName, usedNames);
        }
        if (mainRequirements.laneCount)
        {
            mLaneCountHiddenName = makeUniqueWaveBuiltinName(WaveLaneCountHiddenBaseName, usedNames);
        }
    }

    MSLWaveBuiltinAnalyzer::Requirements MSLWaveBuiltinAnalyzer::getRequirements(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return {};
        }

        if (const auto found = mRequirementsByFunction.find(func->getCanonicalDecl()); found != mRequirementsByFunction.end())
        {
            return found->second;
        }
        return {};
    }

    void MSLWaveBuiltinAnalyzer::appendInjectedParams(std::vector<std::string> &params,
                                                      const Requirements &requirements,
                                                      bool entryPoint) const
    {
        if (requirements.laneIndex)
        {
            std::string param = "uint " + mLaneIndexHiddenName;
            if (entryPoint)
            {
                param += " [[thread_index_in_simdgroup]]";
            }
            params.emplace_back(std::move(param));
        }

        if (requirements.laneCount)
        {
            std::string param = "uint " + mLaneCountHiddenName;
            if (entryPoint)
            {
                param += " [[threads_per_simdgroup]]";
            }
            params.emplace_back(std::move(param));
        }
    }

    void MSLWaveBuiltinAnalyzer::appendInjectedArgs(const clang::FunctionDecl *calleeDecl,
                                                    const clang::FunctionDecl *currentFunction,
                                                    std::vector<std::string> &args) const
    {
        if (calleeDecl == nullptr)
        {
            return;
        }

        const Requirements calleeRequirements = getRequirements(calleeDecl);
        if (calleeRequirements.any() == false)
        {
            return;
        }

        const Requirements currentRequirements = getRequirements(currentFunction);
        if (calleeRequirements.laneIndex)
        {
            if (currentRequirements.laneIndex == false)
            {
                throw std::runtime_error("Internal error: function \"" + currentFunction->getQualifiedNameAsString() + "\" does not have access to WaveGetLaneIndex() while calling \"" + calleeDecl->getQualifiedNameAsString() + "\".");
            }
            args.emplace_back(mLaneIndexHiddenName);
        }
        if (calleeRequirements.laneCount)
        {
            if (currentRequirements.laneCount == false)
            {
                throw std::runtime_error("Internal error: function \"" + currentFunction->getQualifiedNameAsString() + "\" does not have access to WaveGetLaneCount() while calling \"" + calleeDecl->getQualifiedNameAsString() + "\".");
            }
            args.emplace_back(mLaneCountHiddenName);
        }
    }

    std::string MSLWaveBuiltinAnalyzer::getCurrentBuiltinValueName(const clang::FunctionDecl *currentFunction, bool laneIndex) const
    {
        if (currentFunction == nullptr)
        {
            throw std::runtime_error("Internal error: wave builtin was translated outside of a function body.");
        }

        const Requirements currentRequirements = getRequirements(currentFunction);
        if (laneIndex)
        {
            if (currentRequirements.laneIndex == false)
            {
                throw std::runtime_error("Internal error: function \"" + currentFunction->getQualifiedNameAsString() + "\" tried to use WaveGetLaneIndex() without an injected Metal builtin parameter.");
            }
            return mLaneIndexHiddenName;
        }

        if (currentRequirements.laneCount == false)
        {
            throw std::runtime_error("Internal error: function \"" + currentFunction->getQualifiedNameAsString() + "\" tried to use WaveGetLaneCount() without an injected Metal builtin parameter.");
        }
        return mLaneCountHiddenName;
    }
} // namespace UGLC::CodeGen::MSL
