#include "PixelLocalFieldAnalysis.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/Support/Casting.h>

#include <string>
#include <unordered_map>
#include <utility>

namespace UGLC::CodeGen::PixelLocalFieldAnalysis
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

    const clang::CXXRecordDecl *getSelfOrPointeeCXXRecordDecl(clang::QualType type)
    {
        clang::QualType currentType = type;
        while (!currentType.isNull())
        {
            currentType = currentType.getCanonicalType().getUnqualifiedType();
            if (const auto *recordDecl = currentType->getAsCXXRecordDecl())
            {
                return recordDecl;
            }
            if (currentType->isPointerType() || currentType->isReferenceType())
            {
                currentType = currentType->getPointeeType();
                continue;
            }
            return nullptr;
        }
        return nullptr;
    }

    namespace
    {
        /** Returns true when a type refers to the target record or one of its concrete derived records. */
        bool isSameOrDerivedRecordType(clang::QualType lhs, const clang::CXXRecordDecl *rhs)
        {
            const clang::CXXRecordDecl *lhsRecord = getSelfOrPointeeCXXRecordDecl(lhs);
            if (lhsRecord == nullptr || rhs == nullptr)
            {
                return false;
            }
            lhsRecord = lhsRecord->getCanonicalDecl();
            rhs = rhs->getCanonicalDecl();
            return lhsRecord == rhs ||
                   lhsRecord->isDerivedFrom(rhs) ||
                   rhs->isDerivedFrom(lhsRecord);
        }

        /** Returns true when a concrete record or its template pattern exposes a field with the requested name. */
        bool recordHasFieldNamed(const clang::CXXRecordDecl *recordDecl, const std::string &fieldName)
        {
            recordDecl = recordDecl == nullptr ? nullptr : recordDecl->getCanonicalDecl();
            if (recordDecl == nullptr || fieldName.empty())
            {
                return false;
            }
            for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
            {
                if (fieldDecl != nullptr && fieldDecl->getNameAsString() == fieldName)
                {
                    return true;
                }
            }
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                return false;
            }
            const clang::CXXRecordDecl *patternDecl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
            patternDecl = patternDecl == nullptr ? nullptr : patternDecl->getDefinition();
            if (patternDecl == nullptr)
            {
                return false;
            }
            for (const clang::FieldDecl *fieldDecl : patternDecl->fields())
            {
                if (fieldDecl != nullptr && fieldDecl->getNameAsString() == fieldName)
                {
                    return true;
                }
            }
            return false;
        }

        bool isDeclRefToParameter(const clang::Expr *expr, const clang::ParmVarDecl *param)
        {
            if (expr == nullptr || param == nullptr)
            {
                return false;
            }

            const clang::Expr *current = stripTransparentExprWrappers(expr);
            if (const auto *declRef = llvm::dyn_cast_or_null<clang::DeclRefExpr>(current))
            {
                return declRef->getDecl() == param;
            }
            return false;
        }

        /** Returns the body statement that should be analyzed for concrete declarations and template instantiations. */
        clang::Stmt *getFunctionBodyForAnalysis(const clang::FunctionDecl *shaderFunc)
        {
            if (shaderFunc == nullptr)
            {
                return nullptr;
            }
            if (clang::Stmt *body = shaderFunc->getBody())
            {
                return body;
            }
            if (const clang::FunctionDecl *definition = shaderFunc->getDefinition())
            {
                if (clang::Stmt *body = definition->getBody())
                {
                    return body;
                }
            }
            if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(shaderFunc))
            {
                if (const clang::FunctionDecl *pattern = methodDecl->getTemplateInstantiationPattern())
                {
                    if (clang::Stmt *body = pattern->getBody())
                    {
                        return body;
                    }
                }
            }
            if (const clang::FunctionDecl *pattern = shaderFunc->getTemplateInstantiationPattern())
            {
                return pattern->getBody();
            }
            return nullptr;
        }

        bool resolveDirectPixelLocalInputFieldRead(const clang::CXXMemberCallExpr *expr, const clang::ParmVarDecl *pixelLocalInputParam, std::string *fieldName)
        {
            const clang::CXXMethodDecl *method = expr == nullptr ? nullptr : expr->getMethodDecl();
            if (method == nullptr || method->getNameAsString() != "read")
            {
                return false;
            }

            const clang::Expr *baseExpr = stripTransparentExprWrappers(expr->getImplicitObjectArgument());
            const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(baseExpr);
            if (memberExpr == nullptr || !isDeclRefToParameter(memberExpr->getBase(), pixelLocalInputParam))
            {
                return false;
            }

            const clang::ValueDecl *memberDecl = memberExpr->getMemberDecl();
            if (memberDecl == nullptr)
            {
                return false;
            }

            if (fieldName != nullptr)
            {
                *fieldName = memberDecl->getNameAsString();
            }
            return true;
        }

        class PixelLocalReadFieldCollector final : public clang::RecursiveASTVisitor<PixelLocalReadFieldCollector>
        {
        public:
            explicit PixelLocalReadFieldCollector(const clang::ParmVarDecl *pixelLocalInputParam)
                : mPixelLocalInputParam(pixelLocalInputParam)
            {
            }

            bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr *expr)
            {
                std::string fieldName;
                if (resolveDirectPixelLocalInputFieldRead(expr, mPixelLocalInputParam, &fieldName))
                {
                    mReadFields.insert(std::move(fieldName));
                }
                return true;
            }

            const std::unordered_set<std::string> &getReadFields() const
            {
                return mReadFields;
            }

        private:
            const clang::ParmVarDecl *mPixelLocalInputParam = nullptr;
            std::unordered_set<std::string> mReadFields;
        };

        class RenderTargetWriteFieldCollector final : public clang::RecursiveASTVisitor<RenderTargetWriteFieldCollector>
        {
        public:
            explicit RenderTargetWriteFieldCollector(const clang::CXXRecordDecl *renderTargetRecord)
                : mRenderTargetRecord(renderTargetRecord == nullptr ? nullptr : renderTargetRecord->getCanonicalDecl())
            {
            }

            bool VisitDeclStmt(clang::DeclStmt *stmt)
            {
                for (const clang::Decl *decl : stmt->decls())
                {
                    const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(decl);
                    if (varDecl != nullptr && isSameOrDerivedRecordType(varDecl->getType(), mRenderTargetRecord))
                    {
                        const clang::VarDecl *canonicalVar = varDecl->getCanonicalDecl();
                        mRenderTargetVariables.insert(canonicalVar);
                        if (!isPreciseDefaultInitialization(varDecl))
                        {
                            mConservativeRenderTargetVariables.insert(canonicalVar);
                        }
                    }
                }
                return true;
            }

            bool VisitBinaryOperator(clang::BinaryOperator *expr)
            {
                if (expr != nullptr && expr->isAssignmentOp())
                {
                    recordAssignment(expr->getLHS());
                }
                return true;
            }

            bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr *expr)
            {
                if (expr != nullptr && expr->getNumArgs() > 0u && isAssignmentOperator(expr->getOperator()))
                {
                    recordAssignment(expr->getArg(0));
                }
                return true;
            }

            bool VisitReturnStmt(clang::ReturnStmt *stmt)
            {
                const clang::Expr *returnValue = stmt == nullptr ? nullptr : stmt->getRetValue();
                const clang::VarDecl *returnedVar = resolveRenderTargetVariable(returnValue);
                if (returnedVar != nullptr)
                {
                    mReturnedRenderTargetVariables.insert(returnedVar);
                }
                else if (returnValue != nullptr && isSameOrDerivedRecordType(returnValue->getType(), mRenderTargetRecord))
                {
                    mSawUnknownRenderTargetReturn = true;
                }
                return true;
            }

            RenderTargetWriteFieldAnalysis getAnalysis() const
            {
                RenderTargetWriteFieldAnalysis analysis;
                analysis.conservativeAllWrites = mSawUnknownRenderTargetReturn;
                analysis.renderTargetVariableCount = mRenderTargetVariables.size();
                analysis.returnedVariableCount = mReturnedRenderTargetVariables.size();
                analysis.writtenVariableCount = mWrittenFieldsByVariable.size();
                analysis.sawUnknownRenderTargetReturn = mSawUnknownRenderTargetReturn;

                std::unordered_set<const clang::VarDecl *> returnedRenderTargetVariables = mReturnedRenderTargetVariables;
                const bool hasSingleRenderTargetVariable = mRenderTargetVariables.size() == 1u;
                if (returnedRenderTargetVariables.empty() && hasSingleRenderTargetVariable)
                {
                    const clang::VarDecl *onlyRenderTargetVariable = *mRenderTargetVariables.begin();
                    if (mWrittenFieldsByVariable.find(onlyRenderTargetVariable) != mWrittenFieldsByVariable.end())
                    {
                        returnedRenderTargetVariables.insert(onlyRenderTargetVariable);
                        analysis.conservativeAllWrites = false;
                    }
                }
                analysis.returnedVariableCount = returnedRenderTargetVariables.size();

                if (returnedRenderTargetVariables.empty() && !mSawUnknownRenderTargetReturn)
                {
                    analysis.conservativeAllWrites = true;
                }

                for (const clang::VarDecl *returnedVar : returnedRenderTargetVariables)
                {
                    if (mConservativeRenderTargetVariables.find(returnedVar) != mConservativeRenderTargetVariables.end())
                    {
                        analysis.conservativeAllWrites = true;
                    }

                    const auto fieldIt = mWrittenFieldsByVariable.find(returnedVar);
                    if (fieldIt == mWrittenFieldsByVariable.end())
                    {
                        continue;
                    }

                    analysis.fields.insert(fieldIt->second.begin(), fieldIt->second.end());
                }

                if (analysis.conservativeAllWrites)
                {
                    analysis.fields.clear();
                }
                return analysis;
            }

        private:
            static bool isAssignmentOperator(clang::OverloadedOperatorKind op)
            {
                switch (op)
                {
                case clang::OO_Equal:
                case clang::OO_PlusEqual:
                case clang::OO_MinusEqual:
                case clang::OO_StarEqual:
                case clang::OO_SlashEqual:
                case clang::OO_PercentEqual:
                case clang::OO_CaretEqual:
                case clang::OO_AmpEqual:
                case clang::OO_PipeEqual:
                case clang::OO_LessLessEqual:
                case clang::OO_GreaterGreaterEqual:
                    return true;
                default:
                    return false;
                }
            }

            static bool isPreciseDefaultInitialization(const clang::VarDecl *varDecl)
            {
                if (varDecl == nullptr || !varDecl->hasInit())
                {
                    return true;
                }

                const clang::Expr *init = stripTransparentExprWrappers(varDecl->getInit());
                return isPreciseDefaultInitializerExpr(init);
            }

            static bool isPreciseDefaultInitializerExpr(const clang::Expr *expr)
            {
                const clang::Expr *init = stripTransparentExprWrappers(expr);
                if (init == nullptr)
                {
                    return true;
                }
                if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(init))
                {
                    return isPreciseDefaultInitializerExpr(constantExpr->getSubExpr());
                }
                if (const auto *defaultArgExpr = llvm::dyn_cast<clang::CXXDefaultArgExpr>(init))
                {
                    return defaultArgExpr->getExpr() == nullptr || isPreciseDefaultInitializerExpr(defaultArgExpr->getExpr());
                }
                if (const auto *constructExpr = llvm::dyn_cast_or_null<clang::CXXConstructExpr>(init))
                {
                    if (constructExpr->getNumArgs() == 0u)
                    {
                        return true;
                    }

                    for (const clang::Expr *arg : constructExpr->arguments())
                    {
                        if (!isPreciseDefaultInitializerExpr(arg))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (const auto *initListExpr = llvm::dyn_cast_or_null<clang::InitListExpr>(init))
                {
                    for (const clang::Expr *childInit : initListExpr->inits())
                    {
                        if (!isPreciseDefaultInitializerExpr(childInit))
                        {
                            return false;
                        }
                    }
                    return true;
                }
                if (llvm::isa_and_nonnull<clang::ImplicitValueInitExpr>(init))
                {
                    return true;
                }
                if (llvm::isa_and_nonnull<clang::CXXScalarValueInitExpr>(init))
                {
                    return true;
                }
                if (llvm::isa_and_nonnull<clang::IntegerLiteral>(init) ||
                    llvm::isa_and_nonnull<clang::FloatingLiteral>(init) ||
                    llvm::isa_and_nonnull<clang::CXXBoolLiteralExpr>(init) ||
                    llvm::isa_and_nonnull<clang::CharacterLiteral>(init) ||
                    llvm::isa_and_nonnull<clang::StringLiteral>(init))
                {
                    return true;
                }
                if (const auto *defaultInitExpr = llvm::dyn_cast_or_null<clang::CXXDefaultInitExpr>(init))
                {
                    return defaultInitExpr->getExpr() == nullptr || isPreciseDefaultInitializerExpr(defaultInitExpr->getExpr());
                }
                if (const auto *unaryOperator = llvm::dyn_cast_or_null<clang::UnaryOperator>(init))
                {
                    return isPreciseDefaultInitializerExpr(unaryOperator->getSubExpr());
                }
                if (const auto *binaryOperator = llvm::dyn_cast_or_null<clang::BinaryOperator>(init))
                {
                    return isPreciseDefaultInitializerExpr(binaryOperator->getLHS()) && isPreciseDefaultInitializerExpr(binaryOperator->getRHS());
                }
                if (const auto *declRef = llvm::dyn_cast_or_null<clang::DeclRefExpr>(init))
                {
                    return llvm::isa<clang::EnumConstantDecl>(declRef->getDecl());
                }
                return false;
            }

            const clang::VarDecl *resolveRenderTargetVariable(const clang::Expr *expr)
            {
                const clang::Expr *stripped = stripTransparentExprWrappers(expr);
                if (const auto *constructExpr = llvm::dyn_cast_or_null<clang::CXXConstructExpr>(stripped);
                    constructExpr != nullptr && constructExpr->getNumArgs() == 1u && isSameOrDerivedRecordType(constructExpr->getType(), mRenderTargetRecord))
                {
                    return resolveRenderTargetVariable(constructExpr->getArg(0));
                }

                const auto *declRef = llvm::dyn_cast_or_null<clang::DeclRefExpr>(stripped);
                const auto *varDecl = declRef == nullptr ? nullptr : llvm::dyn_cast_or_null<clang::VarDecl>(declRef->getDecl());
                if (varDecl == nullptr)
                {
                    return nullptr;
                }

                const clang::VarDecl *canonicalVar = varDecl->getCanonicalDecl();
                if (mRenderTargetVariables.find(canonicalVar) != mRenderTargetVariables.end())
                {
                    return canonicalVar;
                }
                if (!isSameOrDerivedRecordType(varDecl->getType(), mRenderTargetRecord))
                {
                    return nullptr;
                }
                mRenderTargetVariables.insert(canonicalVar);
                if (!isPreciseDefaultInitialization(varDecl))
                {
                    mConservativeRenderTargetVariables.insert(canonicalVar);
                }
                return canonicalVar;
            }

            const clang::VarDecl *resolveRenderTargetFieldBaseVariable(const clang::Expr *expr)
            {
                const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(stripTransparentExprWrappers(expr));
                if (memberExpr == nullptr)
                {
                    return nullptr;
                }
                return resolveRenderTargetVariable(memberExpr->getBase());
            }

            /** Returns the base expression for a concrete or dependent member assignment target. */
            const clang::Expr *fieldAssignmentBaseExpr(const clang::Expr *expr) const
            {
                const clang::Expr *stripped = stripTransparentExprWrappers(expr);
                if (const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(stripped))
                {
                    return memberExpr->getBase();
                }
                if (const auto *dependentMemberExpr = llvm::dyn_cast_or_null<clang::CXXDependentScopeMemberExpr>(stripped))
                {
                    return dependentMemberExpr->getBase();
                }
                return nullptr;
            }

            /** Returns the assigned framebuffer field name when the target matches the analyzed render target record. */
            std::string renderTargetFieldNameForAssignment(const clang::Expr *expr) const
            {
                const clang::Expr *stripped = stripTransparentExprWrappers(expr);
                std::string fieldName;
                if (const auto *memberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(stripped))
                {
                    if (const clang::ValueDecl *memberDecl = memberExpr->getMemberDecl())
                    {
                        fieldName = memberDecl->getNameAsString();
                    }
                }
                else if (const auto *dependentMemberExpr = llvm::dyn_cast_or_null<clang::CXXDependentScopeMemberExpr>(stripped))
                {
                    fieldName = dependentMemberExpr->getMemberNameInfo().getAsString();
                }
                return recordHasFieldNamed(mRenderTargetRecord, fieldName) ? fieldName : std::string();
            }

            /** Resolves a dependent framebuffer local by matching assigned field names against the concrete return record. */
            const clang::VarDecl *resolvePotentialRenderTargetFieldBaseVariable(const clang::Expr *expr, const std::string &fieldName)
            {
                if (!recordHasFieldNamed(mRenderTargetRecord, fieldName))
                {
                    return nullptr;
                }
                const clang::Expr *baseExpr = fieldAssignmentBaseExpr(expr);
                const auto *declRef = llvm::dyn_cast_or_null<clang::DeclRefExpr>(stripTransparentExprWrappers(baseExpr));
                const auto *varDecl = declRef == nullptr ? nullptr : llvm::dyn_cast_or_null<clang::VarDecl>(declRef->getDecl());
                if (varDecl == nullptr)
                {
                    return nullptr;
                }

                const clang::VarDecl *canonicalVar = varDecl->getCanonicalDecl();
                mRenderTargetVariables.insert(canonicalVar);
                if (!isPreciseDefaultInitialization(varDecl))
                {
                    mConservativeRenderTargetVariables.insert(canonicalVar);
                }
                return canonicalVar;
            }

            void recordAssignment(const clang::Expr *lhs)
            {
                const clang::Expr *strippedLHS = stripTransparentExprWrappers(lhs);
                const std::string fieldName = renderTargetFieldNameForAssignment(strippedLHS);
                if (!fieldName.empty())
                {
                    const clang::VarDecl *baseVar = resolveRenderTargetFieldBaseVariable(strippedLHS);
                    if (baseVar == nullptr)
                    {
                        baseVar = resolvePotentialRenderTargetFieldBaseVariable(strippedLHS, fieldName);
                    }
                    if (baseVar == nullptr)
                    {
                        return;
                    }

                    mWrittenFieldsByVariable[baseVar].insert(fieldName);
                    return;
                }

                if (const clang::VarDecl *wholeTargetVar = resolveRenderTargetVariable(strippedLHS))
                {
                    mConservativeRenderTargetVariables.insert(wholeTargetVar);
                }
            }

            const clang::CXXRecordDecl *mRenderTargetRecord = nullptr;
            std::unordered_set<const clang::VarDecl *> mRenderTargetVariables;
            std::unordered_map<const clang::VarDecl *, std::unordered_set<std::string>> mWrittenFieldsByVariable;
            std::unordered_set<const clang::VarDecl *> mReturnedRenderTargetVariables;
            std::unordered_set<const clang::VarDecl *> mConservativeRenderTargetVariables;
            bool mSawUnknownRenderTargetReturn = false;
        };
    } // namespace

    std::unordered_set<std::string> collectPixelLocalReadFields(const clang::FunctionDecl *shaderFunc, const clang::ParmVarDecl *pixelLocalInputParam)
    {
        PixelLocalReadFieldCollector collector(pixelLocalInputParam);
        if (clang::Stmt *body = getFunctionBodyForAnalysis(shaderFunc))
        {
            collector.TraverseStmt(body);
        }
        return collector.getReadFields();
    }

    bool isDirectPixelLocalInputFieldRead(const clang::CXXMemberCallExpr *expr, const clang::ParmVarDecl *pixelLocalInputParam, std::string *fieldName)
    {
        return resolveDirectPixelLocalInputFieldRead(expr, pixelLocalInputParam, fieldName);
    }

    RenderTargetWriteFieldAnalysis collectRenderTargetWriteFieldAnalysis(const clang::FunctionDecl *shaderFunc, const clang::CXXRecordDecl *renderTargetRecord)
    {
        RenderTargetWriteFieldCollector collector(renderTargetRecord);
        if (clang::Stmt *body = getFunctionBodyForAnalysis(shaderFunc))
        {
            collector.TraverseStmt(body);
        }
        return collector.getAnalysis();
    }

} // namespace UGLC::CodeGen::PixelLocalFieldAnalysis
