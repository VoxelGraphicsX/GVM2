#include "HLSLVisitorLocalStorage.hpp"

#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/ExprCXX.h>
#include <llvm/Support/Casting.h>

#include <utility>

namespace UGLC::CodeGen::HLSL
{
    namespace
    {
        const clang::Expr *stripTransparentExprWrappers(const clang::Expr *expr)
        {
            const clang::Expr *current = expr;
            while (current != nullptr)
            {
                current = current->IgnoreParens();
                if (const auto *castExpr = llvm::dyn_cast<clang::ImplicitCastExpr>(current))
                {
                    current = castExpr->getSubExpr();
                    continue;
                }
                if (const auto *cleanupsExpr = llvm::dyn_cast<clang::ExprWithCleanups>(current))
                {
                    current = cleanupsExpr->getSubExpr();
                    continue;
                }
                if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
                {
                    current = bindTemporaryExpr->getSubExpr();
                    continue;
                }
                if (const auto *materializeExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
                {
                    current = materializeExpr->getSubExpr();
                    continue;
                }
                return current;
            }
            return expr;
        }

        const clang::Expr *unwrapLocalStorageBufferAliasExpr(const clang::Expr *expr)
        {
            const clang::Expr *current = expr;
            while (current != nullptr)
            {
                current = stripTransparentExprWrappers(current);
                if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(current))
                {
                    current = constantExpr->getSubExpr();
                    continue;
                }
                if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(current); constructExpr != nullptr && constructExpr->getNumArgs() == 1)
                {
                    current = constructExpr->getArg(0);
                    continue;
                }
                if (const auto *functionalCastExpr = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(current))
                {
                    current = functionalCastExpr->getSubExpr();
                    continue;
                }
                if (const auto *explicitCastExpr = llvm::dyn_cast<clang::ExplicitCastExpr>(current))
                {
                    current = explicitCastExpr->getSubExpr();
                    continue;
                }
                return current;
            }
            return expr;
        }

        const clang::ValueDecl *getReferencedValueDecl(const clang::Expr *expr)
        {
            const clang::Expr *strippedExpr = stripTransparentExprWrappers(expr);
            if (const auto *declRefExpr = llvm::dyn_cast_or_null<clang::DeclRefExpr>(strippedExpr))
            {
                return llvm::dyn_cast<clang::ValueDecl>(declRefExpr->getDecl()->getCanonicalDecl());
            }
            return nullptr;
        }

        /** Returns the sanitized object name used for a BindGroup expression. */
        std::string extractBindGroupObjectName(const clang::Expr *expr)
        {
            const clang::Expr *strippedExpr = stripTransparentExprWrappers(expr);
            if (const auto *declRefExpr = llvm::dyn_cast_or_null<clang::DeclRefExpr>(strippedExpr))
            {
                return sanitizeHLSLIdentifier(declRefExpr->getDecl()->getNameAsString());
            }
            if (const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(strippedExpr))
            {
                return sanitizeHLSLIdentifier(memberExpr->getMemberNameInfo().getAsString());
            }
            if (const auto *operatorCallExpr = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(strippedExpr);
                operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Arrow && operatorCallExpr->getNumArgs() > 0)
            {
                return extractBindGroupObjectName(operatorCallExpr->getArg(0));
            }
            return {};
        }

        /** Builds a flat global resource expression for aliases sourced from shader entry bind groups. */
        std::optional<std::string> tryBuildFlatBindGroupResourceExpression(BaseASTVisitor &visitor,
                                                                           const std::unordered_set<std::string> &flatBindGroupNames,
                                                                           const clang::Expr *expr)
        {
            const clang::Expr *normalizedExpr = unwrapLocalStorageBufferAliasExpr(expr);
            const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(normalizedExpr);
            if (memberExpr == nullptr)
            {
                return std::nullopt;
            }

            const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl());
            if (fieldDecl == nullptr)
            {
                return std::nullopt;
            }

            const clang::Expr *ownerExpr = stripTransparentExprWrappers(memberExpr->getBase());
            if (const auto *operatorCallExpr = llvm::dyn_cast_or_null<clang::CXXOperatorCallExpr>(ownerExpr);
                operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Arrow && operatorCallExpr->getNumArgs() > 0)
            {
                ownerExpr = stripTransparentExprWrappers(operatorCallExpr->getArg(0));
            }
            if (ownerExpr == nullptr || !visitor.checkTypeCanonicalName(ownerExpr->getType(), mUGLBindGroupName))
            {
                return std::nullopt;
            }

            const std::string bindGroupName = extractBindGroupObjectName(ownerExpr);
            if (bindGroupName.empty() || !flatBindGroupNames.contains(bindGroupName))
            {
                return std::nullopt;
            }

            return bindGroupName + "_" + sanitizeHLSLIdentifier(fieldDecl->getNameAsString());
        }
    } // namespace

    HLSLVisitorLocalStorage::HLSLVisitorLocalStorage(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor)
        : mVisitor(visitor), mTypeConvertor(typeConvertor)
    {
    }

    void HLSLVisitorLocalStorage::resetShaderScope()
    {
        resetFunctionScope();
        mPendingHelperDefinitions.clear();
        mPendingHelperNames.clear();
        mFlatBindGroupNames.clear();
    }

    void HLSLVisitorLocalStorage::setFlatBindGroupNames(std::unordered_set<std::string> bindGroupNames)
    {
        mFlatBindGroupNames = std::move(bindGroupNames);
    }

    void HLSLVisitorLocalStorage::resetFunctionScope()
    {
        mAliases.clear();
    }

    const std::vector<std::string> &HLSLVisitorLocalStorage::pendingHelperDefinitions() const
    {
        return mPendingHelperDefinitions;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::getAliasSelectorName(const clang::ValueDecl *decl) const
    {
        if (decl == nullptr)
        {
            return std::nullopt;
        }
        const auto *canonicalValueDecl = llvm::dyn_cast<clang::ValueDecl>(decl->getCanonicalDecl());
        if (canonicalValueDecl == nullptr)
        {
            return std::nullopt;
        }
        if (const auto aliasIt = mAliases.find(canonicalValueDecl); aliasIt != mAliases.end())
        {
            return aliasIt->second.selectorName;
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryTranslateAliasVarDecl(const clang::VarDecl *decl)
    {
        if (decl == nullptr || !decl->isLocalVarDecl())
        {
            return std::nullopt;
        }
        if (!mVisitor.checkTypeCanonicalName(decl->getType(), mUGLShaderStructuredBufferName)
            && !mVisitor.checkTypeCanonicalName(decl->getType(), mUGLShaderRWStructuredBufferName))
        {
            return std::nullopt;
        }

        auto &aliasState = mAliases[decl->getCanonicalDecl()];
        const std::string sanitizedName = sanitizeHLSLIdentifier(decl->getNameAsString());
        aliasState.selectorName = makeGeneratedIdentifier(sanitizedName + "_resource_selector", decl->getLocation());
        aliasState.helperFunctionName = makeGeneratedIdentifier(sanitizedName + "_resource_read", decl->getLocation());
        aliasState.writeHelperFunctionName = makeGeneratedIdentifier(sanitizedName + "_resource_write", decl->getLocation());
        aliasState.writable = mVisitor.checkTypeCanonicalName(decl->getType(), mUGLShaderRWStructuredBufferName);
        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(decl->getType());
        aliasState.elementType = templateArgs.empty() ? clang::QualType() : mVisitor.getUnqualifiedType(templateArgs.front().getAsType());
        aliasState.resourceExpressions.clear();

        std::string initializer = "0u";
        if (decl->hasInit() && !isImplicitOrDefaultInitializer(decl->getInit()))
        {
            if (const auto selectorExpression = tryBuildSelectorExpression(aliasState, decl->getInit()); selectorExpression.has_value())
            {
                initializer = *selectorExpression;
            }
        }
        return "uint " + aliasState.selectorName + " = " + initializer;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryTranslateAliasWrite(const clang::Expr *baseExpr,
                                                                               const clang::Expr *indexExpr,
                                                                               const clang::Expr *rhsExpr)
    {
        if (const auto *aliasState = findAlias(baseExpr); aliasState != nullptr && aliasState->writable)
        {
            return aliasState->writeHelperFunctionName + "(" + aliasState->selectorName + ", " + mVisitor.TranslateExpr(indexExpr) + ", " + mVisitor.TranslateExpr(rhsExpr) + ")";
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryTranslateOperatorAssignment(const clang::CXXOperatorCallExpr *expr)
    {
        if (expr == nullptr || expr->getOperator() != clang::OO_Equal)
        {
            return std::nullopt;
        }

        if (const auto *lhsSubscriptExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(stripTransparentExprWrappers(expr->getArg(0))); lhsSubscriptExpr != nullptr && lhsSubscriptExpr->getOperator() == clang::OO_Subscript)
        {
            if (auto *lhsAliasState = findAlias(lhsSubscriptExpr->getArg(0)); lhsAliasState != nullptr && lhsAliasState->writable)
            {
                return lhsAliasState->writeHelperFunctionName + "(" + lhsAliasState->selectorName + ", " + mVisitor.TranslateExpr(lhsSubscriptExpr->getArg(1)) + ", " + mVisitor.TranslateExpr(expr->getArg(1)) + ")";
            }
        }

        if (auto *lhsAliasState = findAlias(expr->getArg(0)); lhsAliasState != nullptr)
        {
            if (const auto selectorExpression = tryBuildSelectorExpression(*lhsAliasState, expr->getArg(1)); selectorExpression.has_value())
            {
                return lhsAliasState->selectorName + " = " + *selectorExpression;
            }
        }

        return std::nullopt;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryTranslateArraySubscript(const clang::CXXOperatorCallExpr *expr)
    {
        if (expr == nullptr || expr->getOperator() != clang::OO_Subscript || expr->getNumArgs() < 2)
        {
            return std::nullopt;
        }
        if (const auto *aliasState = findAlias(expr->getArg(0)); aliasState != nullptr)
        {
            return buildAliasSubscript(*aliasState, mVisitor.TranslateExpr(expr->getArg(1)));
        }
        return std::nullopt;
    }

    bool HLSLVisitorLocalStorage::hasAtomicTarget(const clang::Expr *targetExpr) const
    {
        return hasAtomicTargetImpl(targetExpr);
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryBuildAtomicDispatchBlock(const clang::Expr *targetExpr,
                                                                                    std::string_view interlockedFunctionName,
                                                                                    const std::string &resultVariableName,
                                                                                    const std::string &resultTypeName,
                                                                                    const std::vector<std::string> &atomicArgumentExpressions,
                                                                                    bool compareExchange)
    {
        const auto atomicTarget = tryResolveAtomicTarget(targetExpr);
        if (!atomicTarget.has_value())
        {
            return std::nullopt;
        }

        std::string result;
        result += "{ " + stripIllegalLocalVariableQualifiers(resultTypeName) + " " + resultVariableName + mVisitor.EOS();
        for (size_t candidateIndex = 0; candidateIndex < atomicTarget->aliasState->resourceExpressions.size(); ++candidateIndex)
        {
            if (candidateIndex == 0)
            {
                result += "if (" + atomicTarget->aliasState->selectorName + " == 0u)" + mVisitor.NewLine();
            }
            else if (candidateIndex + 1 < atomicTarget->aliasState->resourceExpressions.size())
            {
                result += "else if (" + atomicTarget->aliasState->selectorName + " == " + std::to_string(candidateIndex) + "u)" + mVisitor.NewLine();
            }
            else
            {
                result += "else" + mVisitor.NewLine();
            }
            result += "{" + mVisitor.NewLine();
            result += "    " + buildAtomicCallStatement(*atomicTarget, candidateIndex, interlockedFunctionName, resultVariableName, atomicArgumentExpressions, compareExchange);
            result += "}" + mVisitor.NewLine();
        }
        result += "}";
        return result;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryBuildAtomicDispatchStatements(const clang::Expr *targetExpr,
                                                                                         std::string_view interlockedFunctionName,
                                                                                         const std::string &resultVariableName,
                                                                                         const std::vector<std::string> &atomicArgumentExpressions,
                                                                                         bool compareExchange,
                                                                                         std::string_view indent)
    {
        const auto atomicTarget = tryResolveAtomicTarget(targetExpr);
        if (!atomicTarget.has_value())
        {
            return std::nullopt;
        }

        std::string result;
        for (size_t candidateIndex = 0; candidateIndex < atomicTarget->aliasState->resourceExpressions.size(); ++candidateIndex)
        {
            if (candidateIndex == 0)
            {
                result += std::string(indent) + "if (" + atomicTarget->aliasState->selectorName + " == 0u)" + mVisitor.NewLine();
            }
            else if (candidateIndex + 1 < atomicTarget->aliasState->resourceExpressions.size())
            {
                result += std::string(indent) + "else if (" + atomicTarget->aliasState->selectorName + " == " + std::to_string(candidateIndex) + "u)" + mVisitor.NewLine();
            }
            else
            {
                result += std::string(indent) + "else" + mVisitor.NewLine();
            }
            result += std::string(indent) + "{" + mVisitor.NewLine();
            result += std::string(indent) + "    " + buildAtomicCallStatement(*atomicTarget, candidateIndex, interlockedFunctionName, resultVariableName, atomicArgumentExpressions, compareExchange);
            result += std::string(indent) + "}" + mVisitor.NewLine();
        }
        return result;
    }

    void HLSLVisitorLocalStorage::captureFunctionHelperDefinitions()
    {
        for (const auto &[decl, aliasState] : mAliases)
        {
            (void)decl;
            if (aliasState.resourceExpressions.empty())
            {
                continue;
            }
            if (mPendingHelperNames.insert(aliasState.helperFunctionName).second)
            {
                mPendingHelperDefinitions.push_back(makeReadHelperDefinition(aliasState));
            }
            if (aliasState.writable && mPendingHelperNames.insert(aliasState.writeHelperFunctionName).second)
            {
                mPendingHelperDefinitions.push_back(makeWriteHelperDefinition(aliasState));
            }
        }
    }

    HLSLVisitorLocalStorage::AliasState *HLSLVisitorLocalStorage::findAlias(const clang::Expr *expr)
    {
        const auto *decl = getReferencedValueDecl(expr);
        if (decl == nullptr)
        {
            return nullptr;
        }
        if (auto aliasIt = mAliases.find(decl); aliasIt != mAliases.end())
        {
            return &aliasIt->second;
        }
        return nullptr;
    }

    const HLSLVisitorLocalStorage::AliasState *HLSLVisitorLocalStorage::findAlias(const clang::Expr *expr) const
    {
        const auto *decl = getReferencedValueDecl(expr);
        if (decl == nullptr)
        {
            return nullptr;
        }
        if (auto aliasIt = mAliases.find(decl); aliasIt != mAliases.end())
        {
            return &aliasIt->second;
        }
        return nullptr;
    }

    bool HLSLVisitorLocalStorage::hasAtomicTargetImpl(const clang::Expr *targetExpr) const
    {
        const clang::Expr *normalizedExpr = stripTransparentExprWrappers(targetExpr);
        if (normalizedExpr == nullptr)
        {
            return false;
        }

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(normalizedExpr))
        {
            return hasAtomicTargetImpl(memberExpr->getBase());
        }

        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(normalizedExpr); operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Subscript)
        {
            const auto *aliasState = findAlias(operatorCallExpr->getArg(0));
            return aliasState != nullptr && aliasState->writable;
        }

        if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(normalizedExpr))
        {
            const auto *aliasState = findAlias(arraySubscriptExpr->getBase());
            return aliasState != nullptr && aliasState->writable;
        }

        return false;
    }

    std::optional<std::string> HLSLVisitorLocalStorage::tryBuildSelectorExpression(AliasState &aliasState, const clang::Expr *expr)
    {
        const clang::Expr *normalizedExpr = unwrapLocalStorageBufferAliasExpr(expr);
        if (normalizedExpr == nullptr)
        {
            return std::nullopt;
        }

        if (const auto *conditionalExpr = llvm::dyn_cast<clang::ConditionalOperator>(normalizedExpr))
        {
            const auto trueSelector = tryBuildSelectorExpression(aliasState, conditionalExpr->getTrueExpr());
            const auto falseSelector = tryBuildSelectorExpression(aliasState, conditionalExpr->getFalseExpr());
            if (!trueSelector.has_value() || !falseSelector.has_value())
            {
                return std::nullopt;
            }
            return "((" + mVisitor.TranslateExpr(conditionalExpr->getCond()) + ") ? " + *trueSelector + " : " + *falseSelector + ")";
        }

        if (const auto *rhsAliasState = findAlias(normalizedExpr); rhsAliasState != nullptr)
        {
            if (rhsAliasState == &aliasState)
            {
                return aliasState.selectorName;
            }

            if (rhsAliasState->resourceExpressions.empty())
            {
                return rhsAliasState->selectorName;
            }

            std::vector<size_t> remappedIndices;
            remappedIndices.reserve(rhsAliasState->resourceExpressions.size());
            for (const auto &resourceExpression : rhsAliasState->resourceExpressions)
            {
                remappedIndices.push_back(registerAliasCandidate(aliasState, resourceExpression));
            }

            if (remappedIndices.size() == 1)
            {
                return std::to_string(remappedIndices.front()) + "u";
            }

            std::string selectorExpression;
            for (size_t candidateIndex = 0; candidateIndex < remappedIndices.size(); ++candidateIndex)
            {
                if (candidateIndex + 1 < remappedIndices.size())
                {
                    selectorExpression += "((" + rhsAliasState->selectorName + " == " + std::to_string(candidateIndex) + "u) ? " + std::to_string(remappedIndices[candidateIndex]) + "u : ";
                }
                else
                {
                    selectorExpression += std::to_string(remappedIndices[candidateIndex]) + "u";
                }
            }
            selectorExpression.append(remappedIndices.size() - 1, ')');
            return selectorExpression;
        }

        const std::string resourceExpression = tryBuildFlatBindGroupResourceExpression(mVisitor, mFlatBindGroupNames, normalizedExpr)
                                                   .value_or(mVisitor.TranslateExpr(normalizedExpr));
        const size_t candidateIndex = registerAliasCandidate(aliasState, resourceExpression);
        return std::to_string(candidateIndex) + "u";
    }

    std::optional<HLSLVisitorLocalStorage::AtomicTarget> HLSLVisitorLocalStorage::tryResolveAtomicTarget(const clang::Expr *expr)
    {
        const clang::Expr *normalizedExpr = stripTransparentExprWrappers(expr);
        if (normalizedExpr == nullptr)
        {
            return std::nullopt;
        }

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(normalizedExpr))
        {
            auto baseTarget = tryResolveAtomicTarget(memberExpr->getBase());
            if (!baseTarget.has_value())
            {
                return std::nullopt;
            }
            baseTarget->memberAccessSuffix += "." + sanitizeHLSLIdentifier(memberExpr->getMemberNameInfo().getAsString());
            return baseTarget;
        }

        if (const auto *operatorCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(normalizedExpr); operatorCallExpr != nullptr && operatorCallExpr->getOperator() == clang::OO_Subscript)
        {
            if (const auto *aliasState = findAlias(operatorCallExpr->getArg(0)); aliasState != nullptr && aliasState->writable)
            {
                return AtomicTarget{
                    .aliasState = aliasState,
                    .indexExpression = mVisitor.TranslateExpr(operatorCallExpr->getArg(1)),
                    .memberAccessSuffix = {},
                };
            }
        }

        if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(normalizedExpr))
        {
            if (const auto *aliasState = findAlias(arraySubscriptExpr->getBase()); aliasState != nullptr && aliasState->writable)
            {
                return AtomicTarget{
                    .aliasState = aliasState,
                    .indexExpression = mVisitor.TranslateExpr(arraySubscriptExpr->getIdx()),
                    .memberAccessSuffix = {},
                };
            }
        }

        return std::nullopt;
    }

    std::string HLSLVisitorLocalStorage::buildAtomicTargetExpression(const AtomicTarget &atomicTarget, size_t candidateIndex) const
    {
        if (atomicTarget.aliasState == nullptr || candidateIndex >= atomicTarget.aliasState->resourceExpressions.size())
        {
            return mVisitor.makeUnsupportedPlaceholder("storage buffer alias atomic target", atomicTarget.indexExpression);
        }
        return "(" + atomicTarget.aliasState->resourceExpressions[candidateIndex] + ")[" + atomicTarget.indexExpression + "]" + atomicTarget.memberAccessSuffix;
    }

    std::string HLSLVisitorLocalStorage::buildAliasSubscript(const AliasState &aliasState, const std::string &indexExpression) const
    {
        if (aliasState.resourceExpressions.empty())
        {
            return mVisitor.makeUnsupportedPlaceholder("storage buffer alias", aliasState.selectorName);
        }
        return aliasState.helperFunctionName + "(" + aliasState.selectorName + ", " + indexExpression + ")";
    }

    std::string HLSLVisitorLocalStorage::buildAtomicCallStatement(const AtomicTarget &atomicTarget,
                                                                  size_t candidateIndex,
                                                                  std::string_view interlockedFunctionName,
                                                                  const std::string &resultVariableName,
                                                                  const std::vector<std::string> &atomicArgumentExpressions,
                                                                  bool compareExchange) const
    {
        const std::string targetExpr = buildAtomicTargetExpression(atomicTarget, candidateIndex);
        if (compareExchange)
        {
            return std::string(interlockedFunctionName) + "(" + targetExpr + ", " + atomicArgumentExpressions[0] + ", " + atomicArgumentExpressions[1] + ", " + resultVariableName + ")" + mVisitor.EOS();
        }
        return std::string(interlockedFunctionName) + "(" + targetExpr + ", " + atomicArgumentExpressions[0] + ", " + resultVariableName + ")" + mVisitor.EOS();
    }

    std::string HLSLVisitorLocalStorage::makeReadHelperDefinition(const AliasState &aliasState)
    {
        const std::string elementTypeName = generateTypeName(aliasState.elementType);
        const std::string selectorParamName = "__uglc_selector";
        const std::string indexParamName = "__uglc_index";

        std::string result;
        result += elementTypeName + " " + aliasState.helperFunctionName + "(uint " + selectorParamName + ", uint " + indexParamName + ")" + mVisitor.NewLine();
        result += "{" + mVisitor.NewLine();
        for (size_t candidateIndex = 0; candidateIndex < aliasState.resourceExpressions.size(); ++candidateIndex)
        {
            const std::string returnExpr = "(" + aliasState.resourceExpressions[candidateIndex] + ")[" + indexParamName + "]";
            if (candidateIndex + 1 < aliasState.resourceExpressions.size())
            {
                result += "    if (" + selectorParamName + " == " + std::to_string(candidateIndex) + "u)" + mVisitor.NewLine();
                result += "    {" + mVisitor.NewLine();
                result += "        return " + returnExpr + mVisitor.EOS();
                result += "    }" + mVisitor.NewLine();
            }
            else
            {
                result += "    return " + returnExpr + mVisitor.EOS();
            }
        }
        result += "}" + mVisitor.NewLine();
        return result;
    }

    std::string HLSLVisitorLocalStorage::makeWriteHelperDefinition(const AliasState &aliasState)
    {
        const std::string elementTypeName = generateTypeName(aliasState.elementType);
        const std::string selectorParamName = "__uglc_selector";
        const std::string indexParamName = "__uglc_index";
        const std::string valueParamName = "__uglc_value";

        std::string result;
        result += "void " + aliasState.writeHelperFunctionName + "(uint " + selectorParamName + ", uint " + indexParamName + ", " + elementTypeName + " " + valueParamName + ")" + mVisitor.NewLine();
        result += "{" + mVisitor.NewLine();
        for (size_t candidateIndex = 0; candidateIndex < aliasState.resourceExpressions.size(); ++candidateIndex)
        {
            const std::string writeExpr = "(" + aliasState.resourceExpressions[candidateIndex] + ")[" + indexParamName + "] = " + valueParamName + mVisitor.EOS();
            if (candidateIndex + 1 < aliasState.resourceExpressions.size())
            {
                result += "    if (" + selectorParamName + " == " + std::to_string(candidateIndex) + "u)" + mVisitor.NewLine();
                result += "    {" + mVisitor.NewLine();
                result += "        " + writeExpr;
                result += "        return;" + mVisitor.NewLine();
                result += "    }" + mVisitor.NewLine();
            }
            else
            {
                result += "    " + writeExpr;
            }
        }
        result += "}" + mVisitor.NewLine();
        return result;
    }

    bool HLSLVisitorLocalStorage::isImplicitOrDefaultInitializer(const clang::Expr *expr) const
    {
        if (expr == nullptr || mVisitor.isImplicitNode(expr))
        {
            return true;
        }

        expr = stripTransparentExprWrappers(expr);
        if (llvm::isa<clang::ImplicitValueInitExpr>(expr) || llvm::isa<clang::NoInitExpr>(expr))
        {
            return true;
        }
        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr))
        {
            return constructExpr->getNumArgs() == 0;
        }
        return false;
    }

    std::string HLSLVisitorLocalStorage::generateTypeName(clang::QualType type)
    {
        return mVisitor.generateTypeCanonicalName(type, mTypeConvertor);
    }

    size_t HLSLVisitorLocalStorage::registerAliasCandidate(AliasState &aliasState, const std::string &resourceExpression)
    {
        for (size_t index = 0; index < aliasState.resourceExpressions.size(); ++index)
        {
            if (aliasState.resourceExpressions[index] == resourceExpression)
            {
                return index;
            }
        }
        aliasState.resourceExpressions.push_back(resourceExpression);
        return aliasState.resourceExpressions.size() - 1;
    }

    std::string HLSLVisitorLocalStorage::makeGeneratedIdentifier(const std::string &prefix, clang::SourceLocation location)
    {
        return "__uglc_" + prefix + "_" + std::to_string(location.getRawEncoding());
    }

    std::string HLSLVisitorLocalStorage::stripIllegalLocalVariableQualifiers(const std::string &typeName)
    {
        static constexpr std::string_view kGroupSharedQualifier = "groupshared ";
        if (typeName.starts_with(kGroupSharedQualifier))
        {
            return typeName.substr(kGroupSharedQualifier.size());
        }
        return typeName;
    }
} // namespace UGLC::CodeGen::HLSL
