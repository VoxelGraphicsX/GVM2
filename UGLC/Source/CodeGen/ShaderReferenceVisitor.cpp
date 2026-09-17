#include "ShaderReferenceVisitor.hpp"

#include "UGLC.Constants.hpp"

#include <algorithm>
#include <clang/AST/DeclTemplate.h>

namespace UGLC::CodeGen
{
    ShaderReferenceVisitor::ShaderReferenceVisitor(clang::ASTContext *Context)
        : BaseASTVisitor(Context, nullptr, nullptr, nullptr)
    {
    }

    void ShaderReferenceVisitor::solveReference(const clang::FunctionDecl *func,
                                                const BindGroupInfoMap &bindGroupInfoMap,
                                                const std::vector<clang::Decl *> &extraDecls,
                                                const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        if (templateSpecialization != nullptr)
        {
            pushTemplateSubstitutionContext(templateSpecialization);
        }
        mRefDefs.clear();
        mRefResults.clear();
        mTraversedFunctions.clear();
        mHostResourceTypeCache.clear();
        mHostResourceRecordCache.clear();

        if (!func->getReturnType()->isVoidType())
        {
            solveType(func->getReturnType());
        }

        for (unsigned paramIndex = 0; paramIndex < func->getNumParams(); ++paramIndex)
        {
            const clang::ParmVarDecl *param = func->getParamDecl(paramIndex);
            solveType(param->getType());
        }

        solveFunction(func);
        if (clang::Stmt *body = func->getBody())
        {
            traverseStmt(body);
        }

        for (const auto &entry : bindGroupInfoMap)
        {
            solveRecord(entry.second.typeDecl, false);
            for (const BaseShaderResourceBinding &resourceBinding : entry.second.resourceBindings)
            {
                solveResourceBinding(resourceBinding);
            }
        }

        for (const auto *decl : extraDecls)
        {
            const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
            solveRecord(recordDecl, false);
        }

        mRefDefs.emplace(getFunctionUniqueID(func), func);
        sortReferences();
        if (templateSpecialization != nullptr)
        {
            popTemplateSubstitutionContext();
        }
    }

    bool ShaderReferenceVisitor::shouldTraverseFunctionDefinition(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }
        if (func->isImplicit())
        {
            return false;
        }
        if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(func))
        {
            if (!method->isUserProvided() && !checkAttibuteByName(func, mUGLCTORName))
            {
                return false;
            }
        }
        return true;
    }

    bool ShaderReferenceVisitor::shouldCollectFunctionDefinition(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }
        if (llvm::isa<clang::CXXMethodDecl>(func))
        {
            return false;
        }
        return true;
    }

    bool ShaderReferenceVisitor::shaderTypeContainsHostResourceHandle(const clang::QualType &type)
    {
        if (type.isNull())
        {
            return false;
        }

        const clang::QualType canonicalType = type.getCanonicalType();
        const clang::Type *typeKey = canonicalType.getTypePtrOrNull();
        if (typeKey == nullptr)
        {
            return typeContainsHostResourceHandle(type);
        }

        if (const auto iter = mHostResourceTypeCache.find(typeKey); iter != mHostResourceTypeCache.end())
        {
            return iter->second;
        }

        const bool containsHostResourceHandle = typeContainsHostResourceHandle(type);
        mHostResourceTypeCache.emplace(typeKey, containsHostResourceHandle);
        return containsHostResourceHandle;
    }

    bool ShaderReferenceVisitor::shaderRecordContainsHostResourceHandles(const clang::CXXRecordDecl *decl)
    {
        if (decl == nullptr)
        {
            return false;
        }

        const clang::CXXRecordDecl *recordKey = decl->getCanonicalDecl();
        if (const auto iter = mHostResourceRecordCache.find(recordKey); iter != mHostResourceRecordCache.end())
        {
            return iter->second;
        }

        const bool containsHostResourceHandle = recordContainsHostResourceHandles(decl);
        mHostResourceRecordCache.emplace(recordKey, containsHostResourceHandle);
        return containsHostResourceHandle;
    }

    void ShaderReferenceVisitor::solveType(const clang::QualType &type, bool diagnoseHostOnlyUse)
    {
        if (type.isNull())
        {
            return;
        }

        if (diagnoseHostOnlyUse && shaderTypeContainsHostResourceHandle(type))
        {
            throwCodegenError("host-only resource handles cannot be used in shader code.");
        }

        const clang::QualType resolvedType = getUnqualifiedType(type);
        if (resolvedType.isNull())
        {
            return;
        }

        for (const clang::TemplateArgument &templateArgument : getTemplateArgumentsFromType(resolvedType))
        {
            solveTemplateArgumentRecords(templateArgument);
        }

        solveRecord(resolvedType->getAsCXXRecordDecl(), diagnoseHostOnlyUse);
    }

    void ShaderReferenceVisitor::solveRecord(const clang::CXXRecordDecl *decl, bool diagnoseHostOnlyUse)
    {
        if (decl == nullptr || isFromExcludedFile(decl->getLocation()))
        {
            return;
        }

        const clang::CXXRecordDecl *storedDecl = decl;
        const clang::CXXRecordDecl *walkDecl = decl;
        bool pushedTemplateContext = false;
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            storedDecl = specializationDecl;
            walkDecl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
            pushTemplateSubstitutionContext(specializationDecl);
            pushedTemplateContext = true;
        }

        const std::string qualifiedName = llvm::isa<clang::ClassTemplateSpecializationDecl>(storedDecl)
                                              ? generateTypeCanonicalName(Context->getRecordType(storedDecl))
                                              : storedDecl->getQualifiedNameAsString();
        if (mRefDefs.contains(qualifiedName))
        {
            if (pushedTemplateContext)
            {
                popTemplateSubstitutionContext();
            }
            return;
        }

        if (shaderRecordContainsHostResourceHandles(walkDecl))
        {
            if (diagnoseHostOnlyUse)
            {
                throwCodegenError(walkDecl, "host-only resource handles cannot be used in shader code.");
            }
            if (pushedTemplateContext)
            {
                popTemplateSubstitutionContext();
            }
            return;
        }

        mRefDefs.emplace(qualifiedName, storedDecl);

        for (const auto *field : getAllFieldFromRecord(walkDecl))
        {
            solveType(field->getType());
            traverseExpr(field->getInClassInitializer());
        }

        for (auto *method : walkDecl->methods())
        {
            if (!shouldTraverseFunctionDefinition(method))
            {
                continue;
            }
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                if (!method->getReturnType()->isVoidType())
                {
                    solveType(method->getReturnType());
                }
                for (const clang::ParmVarDecl *param : method->parameters())
                {
                    solveType(param->getType());
                }
                traverseStmt(method->getBody());
            }
        }

        if (pushedTemplateContext)
        {
            popTemplateSubstitutionContext();
        }
    }

    void ShaderReferenceVisitor::solveFunctionTemplate(const clang::FunctionTemplateDecl *decl)
    {
        if (decl == nullptr || isFromExcludedFile(decl->getLocation()))
        {
            return;
        }

        const clang::FunctionDecl *templatedDecl = decl->getTemplatedDecl();
        if (!shouldTraverseFunctionDefinition(templatedDecl))
        {
            return;
        }

        const std::string uniqueName = getFunctionUniqueID(templatedDecl) + ":template";
        if (mTraversedFunctions.contains(uniqueName))
        {
            return;
        }
        mTraversedFunctions.insert(uniqueName);
        if (mRefDefs.contains(uniqueName))
        {
            return;
        }

        mRefDefs.emplace(uniqueName, decl);
        for (const clang::ParmVarDecl *param : templatedDecl->parameters())
        {
            solveType(param->getType());
        }
        if (!templatedDecl->getReturnType()->isVoidType())
        {
            solveType(templatedDecl->getReturnType());
        }
        if (clang::Stmt *body = templatedDecl->getBody())
        {
            traverseStmt(body);
        }
    }

    void ShaderReferenceVisitor::solveTemplateArgumentRecords(const clang::TemplateArgument &arg)
    {
        if (arg.getKind() == clang::TemplateArgument::ArgKind::Type)
        {
            solveType(arg.getAsType());
            return;
        }

        if (arg.getKind() == clang::TemplateArgument::ArgKind::Pack)
        {
            for (const clang::TemplateArgument &packArg : arg.pack_elements())
            {
                solveTemplateArgumentRecords(packArg);
            }
        }
    }

    void ShaderReferenceVisitor::solveResourceBinding(const BaseShaderResourceBinding &resourceBinding)
    {
        if (!resourceBinding.resourceType.isNull())
        {
            solveType(resourceBinding.resourceType);
        }

        if (!resourceBinding.elementType.isNull())
        {
            solveType(resourceBinding.elementType);
        }
    }

    void ShaderReferenceVisitor::solveFunction(const clang::FunctionDecl *func)
    {
        if (func == nullptr || isFromExcludedFile(func->getLocation()))
        {
            return;
        }
        if (!shouldTraverseFunctionDefinition(func))
        {
            return;
        }

        if (const clang::FunctionTemplateDecl *primaryTemplate = func->getPrimaryTemplate())
        {
            solveFunctionTemplate(primaryTemplate);
            if (const clang::TemplateArgumentList *templateArgs = func->getTemplateSpecializationArgs())
            {
                for (unsigned i = 0; i < templateArgs->size(); ++i)
                {
                    solveTemplateArgumentRecords(templateArgs->get(i));
                }
            }
            return;
        }

        const clang::FunctionDecl *canonicalFunc = func->getCanonicalDecl();
        const std::string uniqueName = getFunctionUniqueID(canonicalFunc);
        if (mTraversedFunctions.contains(uniqueName))
        {
            return;
        }
        mTraversedFunctions.insert(uniqueName);

        const clang::FunctionDecl *storedFunc = func->hasBody() ? func : func->getDefinition();
        if (storedFunc == nullptr)
        {
            storedFunc = func;
        }

        if (mRefDefs.contains(uniqueName))
        {
            return;
        }

        if (shouldCollectFunctionDefinition(canonicalFunc))
        {
            mRefDefs.emplace(uniqueName, storedFunc);
        }

        for (auto *param : func->parameters())
        {
            solveType(param->getType());
        }
        if (!func->getReturnType()->isVoidType())
        {
            solveType(func->getReturnType());
        }
        if (clang::Stmt *body = storedFunc->getBody())
        {
            traverseStmt(body);
        }
    }

    void ShaderReferenceVisitor::sortReferences()
    {
        for (const auto &entry : mRefDefs)
        {
            mRefResults.emplace_back(entry.second);
        }

        const clang::SourceManager &sourceManager = Context->getSourceManager();
        std::sort(mRefResults.begin(), mRefResults.end(), [&sourceManager](const clang::Decl *left, const clang::Decl *right) {
            return sourceManager.isBeforeInTranslationUnit(left->getBeginLoc(), right->getBeginLoc());
        });
    }

    void ShaderReferenceVisitor::traverseStmt(const clang::Stmt *stmt)
    {
        if (stmt == nullptr)
        {
            return;
        }

        if (const auto *expr = llvm::dyn_cast<clang::Expr>(stmt))
        {
            traverseExpr(expr);
            return;
        }

        if (const auto *compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt))
        {
            for (auto *child : compoundStmt->body())
            {
                traverseStmt(child);
            }
        }
        else if (const auto *ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt))
        {
            if (ifStmt->isConstexpr())
            {
                bool conditionValue = false;
                if (!ifStmt->getCond()->EvaluateAsBooleanCondition(conditionValue, *Context))
                {
                    if (const auto substitutedCondition = tryEvaluateTemplateSubstitutionBooleanCondition(ifStmt->getCond()); substitutedCondition.has_value())
                    {
                        conditionValue = *substitutedCondition;
                    }
                    else
                    {
                        throwCodegenError(ifStmt->getCond(), "UGLC if constexpr condition must be a compile-time boolean expression.");
                    }
                }
                traverseStmt(conditionValue ? ifStmt->getThen() : ifStmt->getElse());
            }
            else
            {
                traverseStmt(ifStmt->getCond());
                traverseStmt(ifStmt->getThen());
                traverseStmt(ifStmt->getElse());
            }
        }
        else if (const auto *returnStmt = llvm::dyn_cast<clang::ReturnStmt>(stmt))
        {
            traverseStmt(returnStmt->getRetValue());
        }
        else if (const auto *declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt))
        {
            for (auto *decl : declStmt->decls())
            {
                if (auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
                {
                    traverseVarDecl(varDecl);
                }
            }
        }
        else if (const auto *whileStmt = llvm::dyn_cast<clang::WhileStmt>(stmt))
        {
            traverseExpr(whileStmt->getCond());
            traverseStmt(whileStmt->getBody());
        }
        else if (const auto *forStmt = llvm::dyn_cast<clang::ForStmt>(stmt))
        {
            if (forStmt->getInit())
            {
                traverseStmt(forStmt->getInit());
            }
            if (forStmt->getCond())
            {
                traverseExpr(forStmt->getCond());
            }
            if (forStmt->getInc())
            {
                traverseExpr(forStmt->getInc());
            }
            if (forStmt->getBody())
            {
                traverseStmt(forStmt->getBody());
            }
        }
        else
        {
            for (const clang::Stmt *child : stmt->children())
            {
                traverseStmt(child);
            }
        }
    }

    void ShaderReferenceVisitor::traverseVarDecl(const clang::VarDecl *decl)
    {
        solveType(decl->getType());

        const std::string qualifiedName = decl->getQualifiedNameAsString();
        if (isTopLevel(decl))
        {
            if (isAnyShaderResourceHandleType(decl->getType()))
            {
                throwCodegenError(decl, "shader resource handles cannot be stored as namespace-scope variables; pass BindGroup<T>, RenderSet<T>, textures, samplers, or buffers through shader entry or helper parameters.");
            }
            mRefDefs.emplace(qualifiedName, decl);
        }

        traverseStmt(decl->getInit());
        if (decl->hasInit())
        {
            traverseExpr(decl->getInit());
        }
    }

    void ShaderReferenceVisitor::traverseExpr(const clang::Expr *expr)
    {
        if (expr == nullptr)
        {
            return;
        }

        expr = expr->IgnoreParenImpCasts();
        solveType(expr->getType());

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(expr))
        {
            if (const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(declRefExpr->getDecl()); varDecl != nullptr && isTopLevel(varDecl))
            {
                traverseVarDecl(varDecl);
            }
        }
        else if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(expr))
        {
            traverseExpr(memberExpr->getBase());
        }
        else if (const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(expr))
        {
            traverseExpr(callExpr->getCallee());

            const clang::FunctionDecl *calleeFunctionDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl());
            if (calleeFunctionDecl == nullptr)
            {
                if (const auto *memberCall = llvm::dyn_cast<clang::CXXMemberCallExpr>(callExpr))
                {
                    calleeFunctionDecl = memberCall->getMethodDecl();
                }
            }
            if (calleeFunctionDecl != nullptr)
            {
                solveFunction(calleeFunctionDecl);
                if (const clang::TemplateArgumentList *templateArgs = calleeFunctionDecl->getTemplateSpecializationArgs())
                {
                    for (unsigned i = 0; i < templateArgs->size(); ++i)
                    {
                        solveTemplateArgumentRecords(templateArgs->get(i));
                    }
                }
            }
            for (auto *arg : callExpr->arguments())
            {
                traverseExpr(arg);
            }
        }
        else if (const auto *binaryOperator = llvm::dyn_cast<clang::BinaryOperator>(expr))
        {
            traverseExpr(binaryOperator->getLHS());
            traverseExpr(binaryOperator->getRHS());
        }
        else if (const auto *conditionalOperator = llvm::dyn_cast<clang::ConditionalOperator>(expr))
        {
            traverseExpr(conditionalOperator->getCond());
            traverseExpr(conditionalOperator->getTrueExpr());
            traverseExpr(conditionalOperator->getFalseExpr());
        }
        else if (const auto *unaryOperator = llvm::dyn_cast<clang::UnaryOperator>(expr))
        {
            traverseExpr(unaryOperator->getSubExpr());
        }
        else if (const auto *castExpr = llvm::dyn_cast<clang::CastExpr>(expr))
        {
            solveType(castExpr->getType());
            traverseExpr(castExpr->getSubExpr());
        }
        else if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr))
        {
            solveType(constructExpr->getType());
            for (auto *arg : constructExpr->arguments())
            {
                traverseExpr(arg);
            }
        }
        else if (const auto *initListExpr = llvm::dyn_cast<clang::InitListExpr>(expr))
        {
            for (const clang::Expr *initExpr : initListExpr->inits())
            {
                traverseExpr(initExpr);
            }
        }
        else if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr))
        {
            traverseExpr(arraySubscriptExpr->getBase());
            traverseExpr(arraySubscriptExpr->getIdx());
        }
        else
        {
            for (const clang::Stmt *child : expr->children())
            {
                traverseStmt(child);
            }
        }
    }

} // namespace UGLC::CodeGen
