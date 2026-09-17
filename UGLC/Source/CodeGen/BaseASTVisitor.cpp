#include "BaseASTVisitor.hpp"
#include "ShaderBufferLayoutValidator.hpp"
#include "UGLC.Constants.hpp"
#include <clang/AST/DeclTemplate.h>
#include "clang/Lex/Lexer.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
namespace UGLC::CodeGen
{
    namespace
    {
        const clang::Expr *stripTransparentExprWrappers(const clang::Expr *expr)
        {
            const clang::Expr *current = expr;
            while (current != nullptr)
            {
                current = current->IgnoreParenImpCasts();
                if (const auto *cleanupsExpr = llvm::dyn_cast<clang::ExprWithCleanups>(current))
                {
                    current = cleanupsExpr->getSubExpr();
                    continue;
                }
                if (const auto *materializeExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
                {
                    current = materializeExpr->getSubExpr();
                    continue;
                }
                if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
                {
                    current = bindTemporaryExpr->getSubExpr();
                    continue;
                }
                if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(current))
                {
                    current = constantExpr->getSubExpr();
                    continue;
                }
                return current;
            }
            return expr;
        }

        /** Returns a type after removing pointer, reference, array, and cv wrappers without consulting visitor substitution. */
        clang::QualType stripTypeWrappersForTemplateSubstitution(clang::ASTContext &context, clang::QualType type)
        {
            clang::QualType result = type;
            if (result->isPointerType())
            {
                result = result->getPointeeType();
            }
            if (result->isReferenceType())
            {
                result = result.getNonReferenceType();
            }
            while (result->isArrayType())
            {
                const clang::ArrayType *arrayType = context.getAsArrayType(result);
                result = arrayType->getElementType();
            }
            result = result.getUnqualifiedType();
            bool strippedSugar = true;
            while (strippedSugar)
            {
                strippedSugar = false;
                if (const auto *elaboratedType = llvm::dyn_cast<clang::ElaboratedType>(result.getTypePtr()))
                {
                    result = elaboratedType->getNamedType().getUnqualifiedType();
                    strippedSugar = true;
                    continue;
                }
                if (const auto *parenType = llvm::dyn_cast<clang::ParenType>(result.getTypePtr()))
                {
                    result = parenType->getInnerType().getUnqualifiedType();
                    strippedSugar = true;
                    continue;
                }
            }
            return result;
        }

        /** Finds a nested type alias on a concrete record by unqualified name. */
        std::optional<clang::QualType> findNestedTypeAlias(const clang::CXXRecordDecl *recordDecl, const std::string &aliasName)
        {
            if (recordDecl == nullptr)
            {
                return std::nullopt;
            }
            if (const auto *definition = recordDecl->getDefinition())
            {
                recordDecl = definition;
            }
            for (const auto *decl : recordDecl->decls())
            {
                if (const auto *typedefDecl = llvm::dyn_cast<clang::TypedefNameDecl>(decl))
                {
                    if (typedefDecl->getNameAsString() == aliasName)
                    {
                        return typedefDecl->getUnderlyingType();
                    }
                }
            }
            return std::nullopt;
        }

        /** Converts a template argument spelling into a stable identifier-safe erased specialization component. */
        std::string sanitizeErasedTemplateSpecializationComponent(const std::string &input)
        {
            std::string result;
            result.reserve(input.size());
            bool lastWasSeparator = false;
            for (const unsigned char c : input)
            {
                if (std::isalnum(c) != 0)
                {
                    result.push_back(static_cast<char>(c));
                    lastWasSeparator = false;
                    continue;
                }

                if (!lastWasSeparator)
                {
                    result.push_back('_');
                    lastWasSeparator = true;
                }
            }

            while (!result.empty() && result.front() == '_')
            {
                result.erase(result.begin());
            }
            while (!result.empty() && result.back() == '_')
            {
                result.pop_back();
            }
            return result.empty() ? "Value" : result;
        }

    } // namespace


    BaseASTVisitor::BaseASTVisitor(clang::ASTContext *Context, AbstractTypeConvertor *defaultTypeConvertor, AbstractAttributeConvertor *defaultAttributeConvertor, AbstractFunctionConvertor *defaultFunctionConvertor)
        : mDefaultTypeConvertor(defaultTypeConvertor)
        , mDefaultAttributeConvertor(defaultAttributeConvertor)
        , mDefaultFunctionConvertor(defaultFunctionConvertor)
        , mDeclQuery(*this)
        , mExpressionTranslator(*this)
        , mStatementTranslator(*this)
        , mMethodLookup(*this)
        , mTemplateArgumentEvaluator(*this)
        , mTypeNameEmitter(*this)
        , mShaderBindingResolver(*this)
        , Context(Context)
    {
    }

    BaseASTVisitor::DiagnosticLocationScope::DiagnosticLocationScope(const BaseASTVisitor *visitor, clang::SourceLocation location)
        : mVisitor(visitor)
    {
        if (mVisitor != nullptr)
        {
            mActive = mVisitor->pushDiagnosticLocation(location);
        }
    }

    BaseASTVisitor::DiagnosticLocationScope::DiagnosticLocationScope(DiagnosticLocationScope &&other) noexcept
        : mVisitor(other.mVisitor)
        , mActive(other.mActive)
    {
        other.mVisitor = nullptr;
        other.mActive = false;
    }

    BaseASTVisitor::DiagnosticLocationScope &BaseASTVisitor::DiagnosticLocationScope::operator=(DiagnosticLocationScope &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        if (mActive && mVisitor != nullptr)
        {
            mVisitor->popDiagnosticLocation();
        }

        mVisitor = other.mVisitor;
        mActive = other.mActive;
        other.mVisitor = nullptr;
        other.mActive = false;
        return *this;
    }

    BaseASTVisitor::DiagnosticLocationScope::~DiagnosticLocationScope()
    {
        if (mActive && mVisitor != nullptr)
        {
            mVisitor->popDiagnosticLocation();
        }
    }

    bool BaseASTVisitor::pushDiagnosticLocation(clang::SourceLocation location) const
    {
        if (Context == nullptr)
        {
            return false;
        }

        location = normalizeDiagnosticLocation(Context->getSourceManager(), location);
        if (location.isInvalid())
        {
            return false;
        }

        mDiagnosticLocationStack.push_back(location);
        return true;
    }

    void BaseASTVisitor::popDiagnosticLocation() const
    {
        if (!mDiagnosticLocationStack.empty())
        {
            mDiagnosticLocationStack.pop_back();
        }
    }

    BaseASTVisitor::DiagnosticLocationScope BaseASTVisitor::scopeDiagnosticLocation(clang::SourceLocation location) const
    {
        return DiagnosticLocationScope(this, location);
    }

    BaseASTVisitor::DiagnosticLocationScope BaseASTVisitor::scopeDiagnosticLocation(const clang::Decl *decl) const
    {
        return DiagnosticLocationScope(this, decl == nullptr ? clang::SourceLocation() : decl->getLocation());
    }

    BaseASTVisitor::DiagnosticLocationScope BaseASTVisitor::scopeDiagnosticLocation(const clang::Stmt *stmt) const
    {
        return DiagnosticLocationScope(this, stmt == nullptr ? clang::SourceLocation() : stmt->getBeginLoc());
    }

    std::string BaseASTVisitor::currentDiagnosticMessageOrFallback(const std::string &message, clang::SourceLocation location) const
    {
        if (isFormattedClangStyleDiagnostic(message))
        {
            return message;
        }

        if (Context == nullptr)
        {
            return formatUnlocatedDiagnostic(message);
        }

        const clang::SourceManager &sourceManager = Context->getSourceManager();
        location = normalizeDiagnosticLocation(sourceManager, location);
        if (location.isInvalid() && !mDiagnosticLocationStack.empty())
        {
            location = mDiagnosticLocationStack.back();
        }
        return formatClangStyleDiagnostic(sourceManager, location, message);
    }

    std::string BaseASTVisitor::formatCodegenError(const std::string &message) const
    {
        return currentDiagnosticMessageOrFallback(message, clang::SourceLocation());
    }

    std::string BaseASTVisitor::formatCodegenError(clang::SourceLocation location, const std::string &message) const
    {
        return currentDiagnosticMessageOrFallback(message, location);
    }

    std::string BaseASTVisitor::formatCodegenError(const clang::Decl *decl, const std::string &message) const
    {
        return currentDiagnosticMessageOrFallback(message, decl == nullptr ? clang::SourceLocation() : decl->getLocation());
    }

    std::string BaseASTVisitor::formatCodegenError(const clang::Stmt *stmt, const std::string &message) const
    {
        return currentDiagnosticMessageOrFallback(message, stmt == nullptr ? clang::SourceLocation() : stmt->getBeginLoc());
    }

    void BaseASTVisitor::throwCodegenError(const std::string &message) const
    {
        throw std::runtime_error(formatCodegenError(message));
    }

    void BaseASTVisitor::throwCodegenError(clang::SourceLocation location, const std::string &message) const
    {
        throw std::runtime_error(formatCodegenError(location, message));
    }

    void BaseASTVisitor::throwCodegenError(const clang::Decl *decl, const std::string &message) const
    {
        throw std::runtime_error(formatCodegenError(decl, message));
    }

    void BaseASTVisitor::throwCodegenError(const clang::Stmt *stmt, const std::string &message) const
    {
        throw std::runtime_error(formatCodegenError(stmt, message));
    }

    void BaseASTVisitor::rethrowCodegenError(const std::exception &error, clang::SourceLocation fallbackLocation) const
    {
        throw std::runtime_error(formatCodegenError(fallbackLocation, error.what()));
    }

    void BaseASTVisitor::rethrowCodegenError(const std::exception &error, const clang::Decl *decl) const
    {
        throw std::runtime_error(formatCodegenError(decl, error.what()));
    }

    void BaseASTVisitor::rethrowCodegenError(const std::exception &error, const clang::Stmt *stmt) const
    {
        throw std::runtime_error(formatCodegenError(stmt, error.what()));
    }

    const clang::CXXThisExpr *BaseASTVisitor::tryGetCXXThisExpr(const clang::Expr *expr) const
    {
        if (expr == nullptr)
        {
            return nullptr;
        }
        return llvm::dyn_cast<clang::CXXThisExpr>(expr->IgnoreParenImpCasts());
    }

    bool BaseASTVisitor::isUserWrittenThisExpr(const clang::CXXThisExpr *expr) const
    {
        if (expr == nullptr || Context == nullptr)
        {
            return false;
        }

        const clang::SourceManager &sourceManager = Context->getSourceManager();
        clang::SourceLocation location = normalizeDiagnosticLocation(sourceManager, expr->getExprLoc());
        if (location.isInvalid())
        {
            return false;
        }

        const auto sourceText = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(expr->getSourceRange()),
                                                            sourceManager,
                                                            Context->getLangOpts());
        return sourceText.trim() == "this";
    }

    void BaseASTVisitor::throwExplicitThisPointerAccessError(const clang::Expr *expr) const
    {
        throwCodegenError(expr, "UGL shader code does not allow explicit this-pointer access. Access members directly and omit \"this->\".");
    }

    std::string stringJoin(const std::vector<std::string> &sources, const std::string &splitStr)
    {
        std::string result;
        for (size_t i = 0; i < sources.size(); ++i)
        {
            result += sources[i];
            if (i + 1 < sources.size())
            {
                result += splitStr;
            }
        }
        return result;
    }
    std::string BaseASTVisitor::makeUnsupportedPlaceholder(const std::string &category, const std::string &kindName) const
    {
        throwCodegenError("UGLC does not support " + category + " \"" + kindName + "\" yet. Please rewrite this DSL construct or extend the compiler lowering for it.");
    }
    std::optional<std::string> BaseASTVisitor::tryBuildStructuredBufferWriteDiagnostic(const clang::Expr *expr) const
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        const clang::Expr *strippedExpr = stripTransparentExprWrappers(expr);
        const clang::Expr *bufferObjectExpr = nullptr;
        if (const auto *subscriptCallExpr = llvm::dyn_cast<clang::CXXOperatorCallExpr>(strippedExpr);
            subscriptCallExpr != nullptr && subscriptCallExpr->getOperator() == clang::OO_Subscript && subscriptCallExpr->getNumArgs() >= 2)
        {
            bufferObjectExpr = subscriptCallExpr->getArg(0);
        }
        else if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(strippedExpr))
        {
            bufferObjectExpr = arraySubscriptExpr->getBase();
        }

        if (bufferObjectExpr == nullptr)
        {
            return std::nullopt;
        }

        bufferObjectExpr = stripTransparentExprWrappers(bufferObjectExpr);

        auto makeMessage = [&](const std::string &ownerKind,
                               const std::string &ownerName,
                               const std::string &fieldName,
                               const clang::QualType &fieldType) -> std::string
        {
            return ownerKind + " \"" + ownerName + "\" field \"" + fieldName
                   + "\" is declared as \""
                   + const_cast<BaseASTVisitor *>(this)->generateTypeCanonicalName(fieldType)
                   + "\", which is read-only. Writes through UGL::StructuredBuffer<T>::operator[] are not allowed. Use UGL::RWStructuredBuffer<T> for read-write access.";
        };

        if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(bufferObjectExpr))
        {
            const auto *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(memberExpr->getMemberDecl());
            if (fieldDecl == nullptr)
            {
                return std::nullopt;
            }

            const clang::QualType fieldType = getUnqualifiedType(fieldDecl->getType());
            if (!checkTypeCanonicalName(fieldType, mUGLShaderStructuredBufferName) ||
                checkTypeCanonicalName(fieldType, mUGLShaderRWStructuredBufferName))
            {
                return std::nullopt;
            }

            const auto *parentRecord = fieldDecl->getParent();
            const std::string ownerName = parentRecord == nullptr ? "<unknown>" : parentRecord->getQualifiedNameAsString();
            return makeMessage("BindGroup", ownerName, fieldDecl->getNameAsString(), fieldType);
        }

        if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(bufferObjectExpr))
        {
            const auto *valueDecl = llvm::dyn_cast<clang::ValueDecl>(declRefExpr->getDecl());
            if (valueDecl == nullptr)
            {
                return std::nullopt;
            }

            const clang::QualType valueType = getUnqualifiedType(valueDecl->getType());
            if (!checkTypeCanonicalName(valueType, mUGLShaderStructuredBufferName) ||
                checkTypeCanonicalName(valueType, mUGLShaderRWStructuredBufferName))
            {
                return std::nullopt;
            }

            return "Variable \"" + valueDecl->getNameAsString()
                   + "\" is declared as \""
                   + const_cast<BaseASTVisitor *>(this)->generateTypeCanonicalName(valueType)
                   + "\", which is read-only. Writes through UGL::StructuredBuffer<T>::operator[] are not allowed. Use UGL::RWStructuredBuffer<T> for read-write access.";
        }

        return std::nullopt;
    }
    std::optional<std::string> BaseASTVisitor::tryBuildRecoveryExprDiagnostic(const clang::RecoveryExpr *expr) const
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        for (const clang::Expr *subExpr : expr->subExpressions())
        {
            if (auto diagnostic = tryBuildStructuredBufferWriteDiagnostic(subExpr); diagnostic.has_value())
            {
                return diagnostic;
            }
        }

        return std::nullopt;
    }
    bool BaseASTVisitor::methodHasAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const
    {
        return mMethodLookup.methodHasAttribute(method, attributeName);
    }
    bool BaseASTVisitor::methodHasParameterAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const
    {
        return mMethodLookup.methodHasParameterAttribute(method, attributeName);
    }
    bool BaseASTVisitor::methodMatchesLookupOptions(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const
    {
        return mMethodLookup.methodMatchesLookupOptions(method, options);
    }
    int BaseASTVisitor::scoreMethodLookupCandidate(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const
    {
        return mMethodLookup.scoreMethodLookupCandidate(method, options);
    }
    std::string BaseASTVisitor::describeMethodOverload(const clang::CXXMethodDecl *method) const
    {
        return mMethodLookup.describeMethodOverload(method);
    }
    bool BaseASTVisitor::isFromExcludedFile(clang::SourceLocation loc) const
    {
        clang::SourceManager &SM = Context->getSourceManager();
        const clang::FileEntry *FE = SM.getFileEntryForID(SM.getFileID(loc));
        if (FE == nullptr || SM.isInSystemHeader(loc))
        {
            return true;
        }

        llvm::StringRef fileName = llvm::sys::path::filename(FE->tryGetRealPathName());
        return fileName.ends_with(".hpp") == false;
    }
    bool BaseASTVisitor::isImplicitNode(const clang::Expr *E) const
    {
        // 1) GetBeginLoc / GetExprLoc 若无效，通常代表隐式
        if (E == nullptr || E->getBeginLoc().isInvalid())
            return true;

        // 2) 不是出现在主文件（或宏展开结果）中的，也视为隐式
        //    - 这可以剔除模板实例化、系统头、隐式生成代码等
        // if (!Context->getSourceManager().isWrittenInMainFile(Context->getSourceManager().getExpansionLoc(E->getBeginLoc())))
        //    return true;

        // 3) 特判几种典型隐式节点
        if (isa<clang::ImplicitValueInitExpr>(E) || // 标量零初始化
            isa<clang::NoInitExpr>(E))              // 完全未初始化 (C struct)
            return true;

        if (const auto *CCE = dyn_cast<clang::CXXConstructExpr>(E))
        {
            // paren/brace Range 无效且 0 个实参 → 默认构造被编译器插入
            if (CCE->getNumArgs() == 0 && CCE->getParenOrBraceRange().isInvalid())
                return true;
        }

        return false; // 以上都不满足 → 显式
    }
    std::string BaseASTVisitor::generateFunctionSignature(const clang::FunctionDecl *func, const std::string &funcName, const std::string &constSpecifierValue, AbstractTypeConvertor *typeConvertor)
    {
        std::string accessSpecifier = translateCXXMemberAccessSpecifier(func->getAccess());
        if (accessSpecifier.empty() == false)
        {
            accessSpecifier += ": ";
        }

        std::string returnType = generateTypeCanonicalName(func->getReturnType(), mDefaultTypeConvertor);

        std::string constSpecifier;
        if (auto *cxxMethod = llvm::dyn_cast<clang::CXXMethodDecl>(func))
        {
            if (cxxMethod->isConst())
            {
                constSpecifier = " " + constSpecifierValue;
            }
        }


        std::string result;
        result += getLineDirective(func->getBeginLoc());
        result += mSpaceManager.getSpace() + accessSpecifier + returnType + " " + funcName + "(";


        if (typeConvertor == nullptr)
        {
            typeConvertor = mDefaultTypeConvertor;
        }

        for (unsigned i = 0; i < func->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = func->getParamDecl(i);
            /*
             std::string attrs = generateAttributes(param, mDefaultAttributeConvertor);
             result += generateTypeCanonicalName(param->getType(), typeConvertor) + " " + param->getNameAsString() + attrs; */
            result += generateFunctionSignatureParam(param, typeConvertor);
            if (i < func->getNumParams() - 1)
            {
                result += ", ";
            }
        }
        result += ")" + constSpecifier + NewLine();
        return result;
    }
    std::string BaseASTVisitor::generateFunctionBody(const clang::FunctionDecl *func)
    {
        std::string result;
        if (clang::Stmt *body = func->getBody())
        {
            result += TranslateStmt(body);
        }
        else
        {
            result += EOS();
        }
        return result;
    }
    std::string BaseASTVisitor::generateUGLCTORFunctionBody(const clang::FunctionDecl *func)
    {
        std::string result;
        result += enterScope();

        for (unsigned i = 0; i < func->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = func->getParamDecl(i);

            result += mSpaceManager.getSpace() + "this->" + param->getNameAsString() + " = " + param->getNameAsString() + EOS();
        }
        result += generateFunctionBody(func);
        result += quitScope();
        return result;
    }
    /* std::string BaseASTVisitor::generateMethodDefinitions(const clang::CXXMethodDecl *decl)
    {
        std::string result;
        if (!decl->isUserProvided() || decl->isCopyAssignmentOperator() || decl->isMoveAssignmentOperator() || llvm::isa<clang::CXXConstructorDecl>(decl) || llvm::isa<clang::CXXDestructorDecl>(decl))
        {
            return result;
        }


        if (decl && decl->hasBody())
        {
            result += generateFunctionSignature(decl, decl->getNameAsString()); // 无需前缀
            result += generateFunctionBody(decl);
        }
        return result;
    } */
    std::string BaseASTVisitor::generateTemplateParameters(const clang::TemplateParameterList *params)
    {
        if (!params)
        {
            return "";
        }
        std::string result = "template<";
        for (unsigned i = 0; i < params->size(); ++i)
        {
            const clang::NamedDecl *param = params->getParam(i);
            if (const auto *typeParam = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param))
            {
                result += typeParam->wasDeclaredWithTypename() ? "typename" : "class";

                result += " " + param->getNameAsString();
            }
            else if (const auto *nonTypeParam = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param))
            {
                result += nonTypeParam->getType().getAsString();

                result += " " + param->getNameAsString();
            }
            if (i < params->size() - 1)
            {
                result += ", ";
            }
        }
        result += ">" + NewLine();
        return result;
    }


    std::string BaseASTVisitor::TranslateStmt(const clang::Stmt *S)
    {
        auto diagnosticScope = scopeDiagnosticLocation(S);
        try
        {
            return mStatementTranslator.translateStatement(S);
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, S);
        }
    }
    std::string BaseASTVisitor::TranslateExpr(const clang::Expr *E)
    {
        auto diagnosticScope = scopeDiagnosticLocation(E);
        try
        {
            return mExpressionTranslator.translateExpression(E);
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, E);
        }
    }
    std::string BaseASTVisitor::translateCompoundStmt(const clang::CompoundStmt *S)
    {
        return mStatementTranslator.translateCompoundStmt(S);
    }
    std::string BaseASTVisitor::translateDeclStmt(const clang::DeclStmt *S)
    {
        return mStatementTranslator.translateDeclStmt(S);
    }
    std::string BaseASTVisitor::translateReturnStmt(const clang::ReturnStmt *S)
    {
        return mStatementTranslator.translateReturnStmt(S);
    }
    std::string BaseASTVisitor::translateBinaryOperator(const clang::BinaryOperator *E)
    {
        return mExpressionTranslator.translateBinaryOperator(E);
    }
    std::string BaseASTVisitor::translateExprAsGroupedInfixOperand(const clang::Expr *E)
    {
        return mExpressionTranslator.translateExprAsGroupedInfixOperand(E);
    }
    std::string BaseASTVisitor::translateMemberExpr(const clang::MemberExpr *E)
    {
        return mExpressionTranslator.translateMemberExpr(E);
    }
    std::string BaseASTVisitor::translateCallExpr(const clang::CallExpr *E)
    {
        return mExpressionTranslator.translateCallExpr(E);
    }
    /* std::string BaseASTVisitor::translateMemberCallExpr(clang::CXXMemberCallExpr *E)
    {

        std::string Object = TranslateExpr(E->getImplicitObjectArgument());
        std::string FuncName = E->getMethodDecl()->getNameAsString();

        std::string Accessor = "."; // 默认为 .
        // 检查实际用于调用的表达式，看它是不是一个箭头操作
        if (auto *CalleeExpr = llvm::dyn_cast<clang::MemberExpr>(E->getCallee()->IgnoreParenCasts()))
        {
            if (CalleeExpr->isArrow())
            {
                Accessor = "->";
            }
        }

        std::string result = Object + Accessor + FuncName + "(";
        std::string Args;
        for (unsigned i = 0; i < E->getNumArgs(); ++i)
        {
            Args += TranslateExpr(E->getArg(i));
            if (i < E->getNumArgs() - 1)
            {
                Args += ", ";
            }
        }
        result += Args + ")";
        return result;
    } */
    std::string BaseASTVisitor::translateIfStmt(const clang::IfStmt *S)
    {
        return mStatementTranslator.translateIfStmt(S);
    }
    std::string BaseASTVisitor::translateInitListExpr(const clang::InitListExpr *E)
    {
        return mExpressionTranslator.translateInitListExpr(E);
    }
    std::string BaseASTVisitor::translateCXXConstructExpr(const clang::CXXConstructExpr *E)
    {
        return mExpressionTranslator.translateCXXConstructExpr(E);
    }
    std::string BaseASTVisitor::translateCXXConstructExprImplicit(const clang::CXXConstructExpr *E)
    {
        return mExpressionTranslator.translateCXXConstructExprImplicit(E);
    }
    std::string BaseASTVisitor::translateCXXConstructExprList(const clang::CXXConstructExpr *E)
    {
        return mExpressionTranslator.translateCXXConstructExprList(E);
    }
    std::string BaseASTVisitor::translateCXXConstructExprFunction(const clang::CXXConstructExpr *E)
    {
        return mExpressionTranslator.translateCXXConstructExprFunction(E);
    }
    std::string BaseASTVisitor::translateCXXThrowExpr(const clang::CXXThrowExpr *E)
    {
        return mExpressionTranslator.translateCXXThrowExpr(E);
    }
    std::string BaseASTVisitor::generateAttributes(const clang::Decl *D, const AbstractAttributeConvertor *conv)
    {
        return mAttributeEmitter.generateAttributes(D, conv);
    }
    std::vector<const clang::AnnotateAttr *> BaseASTVisitor::getAllAttributes(const clang::Decl *D) const
    {
        return mAttributeEmitter.getAllAttributes(D);
    }
    std::string BaseASTVisitor::generateAttribute(const clang::AnnotateAttr *A, const AbstractAttributeConvertor *conv)
    {
        return mAttributeEmitter.generateAttribute(A, conv);
    }
    std::string BaseASTVisitor::generateRawAttribute(const clang::AnnotateAttr *A, const AbstractAttributeConvertor *conv)
    {
        return mAttributeEmitter.generateRawAttribute(A, conv);
    }
    std::string BaseASTVisitor::translateCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *E)
    {
        return mExpressionTranslator.translateCXXOperatorCallExpr(E);
    }
    std::string BaseASTVisitor::translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E)
    {
        return mExpressionTranslator.translateCXXMemberCallExpr(E);
    }
    std::string BaseASTVisitor::translateUnaryOperator(const clang::UnaryOperator *E)
    {
        return mExpressionTranslator.translateUnaryOperator(E);
    }
    // --- 【新增】专门翻译布尔字面量的辅助函数 ---
    std::string BaseASTVisitor::translateCXXBoolLiteralExpr(const clang::CXXBoolLiteralExpr *E)
    {
        return mExpressionTranslator.translateCXXBoolLiteralExpr(E);
    }
    std::string BaseASTVisitor::translateVarDecl(const clang::VarDecl *VD)
    {
        auto diagnosticScope = scopeDiagnosticLocation(VD);
        try
        {
            std::string staticSpecifier = VD->getStorageClass() == clang::SC_Static ? "static " : "";
            std::string constSpecifier = VD->getType().isConstQualified() ? "const " : "";
            std::string constexprSpecifier = VD->isConstexpr() ? "constexpr " : "";
            std::string result = staticSpecifier + constexprSpecifier + constSpecifier + generateTypeCanonicalName(VD->getType(), mDefaultTypeConvertor) + " " + VD->getNameAsString() + generateDeclArraySpecifier(VD->getType());


            if (VD->hasInit() && (isImplicitNode(VD->getInit()) == false))
            {
                result += " = " + TranslateExpr(VD->getInit());
            }
            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, VD);
        }
    }
    std::string BaseASTVisitor::generateRecordDefinition(const clang::CXXRecordDecl *decl)
    {
        if (decl == nullptr || decl->isImplicit())
        {
            return {};
        }
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            std::string result;
            std::string keyword = decl->isClass() ? "class" : "struct";
            std::string name = decl->getNameAsString();

            if (auto *templateDecl = decl->getDescribedClassTemplate())
            {
                result += getLineDirective(templateDecl->getBeginLoc());
                result += generateTemplateParameters(templateDecl->getTemplateParameters());
            }


            const std::string alignasString = GetAlignasSourceString(decl);
            result += mSpaceManager.getSpace() + keyword + " " + alignasString + " " + name + generateBaseClassInRecordDefinition(decl) + NewLine();
            result += enterScope();

            for (auto *innerDecl : decl->decls())
            {
                if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
                {
                    if (shouldEmitNestedRecordDefinition(decl, nestedRecord))
                    {
                        result += getLineDirective(nestedRecord->getBeginLoc());
                        result += generateRecordDefinition(nestedRecord);
                    }
                }
            }


            result += generateRecordDataMembers(decl);
            // 我们还需要处理成员函数
            for (auto *method : decl->methods())
            {
                if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
                {
                    result += generateFunctionDefinition(method);
                }
            }
            result += endClass();

            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    bool BaseASTVisitor::isTopLevel(const clang::Decl *D) const
    {
        return mDeclQuery.isTopLevel(D);
    }
    bool BaseASTVisitor::checkAttibuteByName(const clang::Decl *D, const std::string &name) const
    {
        return mAttributeEmitter.hasAttribute(D, name);
    }

    bool BaseASTVisitor::checkDerivedClassByName(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        return mDeclQuery.checkDerivedClassByName(decl, name);
    }
    bool BaseASTVisitor::checkTypeCanonicalName(const clang::QualType &qt, const std::string &name) const
    {
        return mDeclQuery.checkTypeCanonicalName(qt, name);
    }
    void BaseASTVisitor::pushTemplateSubstitutionContext(const clang::ClassTemplateSpecializationDecl *specializationDecl)
    {
        if (specializationDecl != nullptr)
        {
            mTemplateSubstitutionStack.push_back({.specializationDecl = specializationDecl});
        }
    }
    void BaseASTVisitor::popTemplateSubstitutionContext()
    {
        if (!mTemplateSubstitutionStack.empty())
        {
            mTemplateSubstitutionStack.pop_back();
        }
    }
    const clang::ClassTemplateSpecializationDecl *BaseASTVisitor::getCurrentTemplateSubstitutionContext() const
    {
        if (mTemplateSubstitutionStack.empty())
        {
            return nullptr;
        }
        return mTemplateSubstitutionStack.back().specializationDecl;
    }
    std::optional<clang::TemplateArgument> BaseASTVisitor::findTemplateSubstitutionArgument(const clang::NamedDecl *parameterDecl) const
    {
        if (parameterDecl == nullptr)
        {
            return std::nullopt;
        }

        for (auto contextIt = mTemplateSubstitutionStack.rbegin(); contextIt != mTemplateSubstitutionStack.rend(); ++contextIt)
        {
            const clang::ClassTemplateSpecializationDecl *specializationDecl = contextIt->specializationDecl;
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                continue;
            }

            const clang::TemplateParameterList *parameterList = specializationDecl->getSpecializedTemplate()->getTemplateParameters();
            const clang::TemplateArgumentList &argumentList = specializationDecl->getTemplateArgs();
            const unsigned count = std::min(parameterList->size(), argumentList.size());
            for (unsigned index = 0; index < count; ++index)
            {
                const clang::NamedDecl *candidateDecl = llvm::dyn_cast_or_null<clang::NamedDecl>(parameterList->getParam(index));
                if (candidateDecl == nullptr)
                {
                    continue;
                }
                if (candidateDecl->getCanonicalDecl() == parameterDecl->getCanonicalDecl() ||
                    (!candidateDecl->getName().empty() && candidateDecl->getName() == parameterDecl->getName()))
                {
                    return argumentList.get(index);
                }
            }
        }

        return std::nullopt;
    }
    std::optional<clang::QualType> BaseASTVisitor::tryResolveTemplateSubstitutionType(const clang::QualType &type) const
    {
        if (Context == nullptr || mTemplateSubstitutionStack.empty() || type.isNull())
        {
            return std::nullopt;
        }

        const clang::QualType strippedType = stripTypeWrappersForTemplateSubstitution(*Context, type);
        const clang::Type *typePtr = strippedType.getTypePtrOrNull();
        if (typePtr == nullptr)
        {
            return std::nullopt;
        }

        if (const auto *templateType = llvm::dyn_cast<clang::TemplateTypeParmType>(typePtr))
        {
            if (const auto argument = findTemplateSubstitutionArgument(templateType->getDecl());
                argument.has_value() && argument->getKind() == clang::TemplateArgument::Type)
            {
                return argument->getAsType();
            }
        }

        if (const auto *substType = llvm::dyn_cast<clang::SubstTemplateTypeParmType>(typePtr))
        {
            return substType->getReplacementType();
        }

        if (const auto *dependentNameType = llvm::dyn_cast<clang::DependentNameType>(typePtr))
        {
            const clang::NestedNameSpecifier *qualifier = dependentNameType->getQualifier();
            clang::QualType qualifierType;
            if (qualifier != nullptr)
            {
                if (qualifier->getKind() == clang::NestedNameSpecifier::TypeSpec ||
                    qualifier->getKind() == clang::NestedNameSpecifier::TypeSpecWithTemplate)
                {
                    qualifierType = clang::QualType(qualifier->getAsType(), 0);
                }
                else if (qualifier->getKind() == clang::NestedNameSpecifier::Identifier && qualifier->getPrefix() == nullptr)
                {
                    const std::string parameterName = qualifier->getAsIdentifier()->getName().str();
                    for (auto contextIt = mTemplateSubstitutionStack.rbegin(); contextIt != mTemplateSubstitutionStack.rend(); ++contextIt)
                    {
                        const clang::ClassTemplateSpecializationDecl *specializationDecl = contextIt->specializationDecl;
                        if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
                        {
                            continue;
                        }
                        const clang::TemplateParameterList *parameterList = specializationDecl->getSpecializedTemplate()->getTemplateParameters();
                        const clang::TemplateArgumentList &argumentList = specializationDecl->getTemplateArgs();
                        const unsigned count = std::min(parameterList->size(), argumentList.size());
                        for (unsigned index = 0; index < count; ++index)
                        {
                            const clang::NamedDecl *parameterDecl = llvm::dyn_cast_or_null<clang::NamedDecl>(parameterList->getParam(index));
                            if (parameterDecl == nullptr || parameterDecl->getNameAsString() != parameterName)
                            {
                                continue;
                            }
                            const clang::TemplateArgument &argument = argumentList.get(index);
                            if (argument.getKind() == clang::TemplateArgument::Type)
                            {
                                qualifierType = argument.getAsType();
                            }
                            break;
                        }
                        if (!qualifierType.isNull())
                        {
                            break;
                        }
                    }
                }
            }

            if (!qualifierType.isNull())
            {
                if (const auto resolvedQualifier = tryResolveTemplateSubstitutionType(qualifierType); resolvedQualifier.has_value())
                {
                    qualifierType = *resolvedQualifier;
                }
                const auto *recordDecl = stripTypeWrappersForTemplateSubstitution(*Context, qualifierType)->getAsCXXRecordDecl();
                if (const auto aliasType = findNestedTypeAlias(recordDecl, dependentNameType->getIdentifier()->getName().str()))
                {
                    return *aliasType;
                }
            }
        }

        return std::nullopt;
    }
    std::optional<std::string> BaseASTVisitor::tryResolveTemplateSubstitutionValue(const clang::ValueDecl *decl) const
    {
        const auto *nonTypeParam = llvm::dyn_cast_or_null<clang::NonTypeTemplateParmDecl>(decl);
        if (nonTypeParam == nullptr)
        {
            return std::nullopt;
        }

        const auto argument = findTemplateSubstitutionArgument(nonTypeParam);
        if (!argument.has_value())
        {
            return std::nullopt;
        }
        if (argument->getKind() == clang::TemplateArgument::Integral)
        {
            return std::to_string(argument->getAsIntegral().getSExtValue());
        }
        if (argument->getKind() == clang::TemplateArgument::Expression && argument->getAsExpr() != nullptr)
        {
            clang::Expr::EvalResult result;
            if (argument->getAsExpr()->EvaluateAsInt(result, *Context))
            {
                return std::to_string(result.Val.getInt().getExtValue());
            }
        }
        return std::nullopt;
    }
    std::optional<std::string> BaseASTVisitor::tryTranslateTemplateSubstitutedDeclRefExpr(const clang::DeclRefExpr *expr) const
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        return tryResolveTemplateSubstitutionValue(expr->getDecl());
    }
    std::optional<std::string> BaseASTVisitor::tryResolveTemplateSubstitutionValueByName(const std::string &parameterName) const
    {
        for (auto contextIt = mTemplateSubstitutionStack.rbegin(); contextIt != mTemplateSubstitutionStack.rend(); ++contextIt)
        {
            const clang::ClassTemplateSpecializationDecl *specializationDecl = contextIt->specializationDecl;
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                continue;
            }
            const clang::TemplateParameterList *parameterList = specializationDecl->getSpecializedTemplate()->getTemplateParameters();
            const clang::TemplateArgumentList &argumentList = specializationDecl->getTemplateArgs();
            const unsigned count = std::min(parameterList->size(), argumentList.size());
            for (unsigned index = 0; index < count; ++index)
            {
                const auto *parameterDecl = llvm::dyn_cast_or_null<clang::NonTypeTemplateParmDecl>(parameterList->getParam(index));
                if (parameterDecl == nullptr || parameterDecl->getNameAsString() != parameterName)
                {
                    continue;
                }
                const clang::TemplateArgument &argument = argumentList.get(index);
                if (argument.getKind() == clang::TemplateArgument::Integral)
                {
                    return std::to_string(argument.getAsIntegral().getSExtValue());
                }
                if (argument.getKind() == clang::TemplateArgument::Expression && argument.getAsExpr() != nullptr)
                {
                    clang::Expr::EvalResult result;
                    if (argument.getAsExpr()->EvaluateAsInt(result, *Context))
                    {
                        return std::to_string(result.Val.getInt().getExtValue());
                    }
                }
            }
        }
        return std::nullopt;
    }
    std::optional<bool> BaseASTVisitor::tryEvaluateTemplateSubstitutionBooleanCondition(const clang::Expr *expr) const
    {
        std::function<std::optional<int64_t>(const clang::Expr *)> evalInt = [&](const clang::Expr *current) -> std::optional<int64_t> {
            current = stripTransparentExprWrappers(current);
            if (current == nullptr)
            {
                return std::nullopt;
            }
            if (const auto *integerLiteral = llvm::dyn_cast<clang::IntegerLiteral>(current))
            {
                return integerLiteral->getValue().getSExtValue();
            }
            if (const auto *boolLiteral = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(current))
            {
                return boolLiteral->getValue() ? 1 : 0;
            }
            if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(current))
            {
                if (const auto value = tryResolveTemplateSubstitutionValue(declRefExpr->getDecl()))
                {
                    return std::stoll(*value);
                }
            }
            if (const auto *unaryOperator = llvm::dyn_cast<clang::UnaryOperator>(current))
            {
                const auto value = evalInt(unaryOperator->getSubExpr());
                if (!value.has_value())
                {
                    return std::nullopt;
                }
                switch (unaryOperator->getOpcode())
                {
                case clang::UO_Plus:
                    return *value;
                case clang::UO_Minus:
                    return -*value;
                case clang::UO_LNot:
                    return *value == 0 ? 1 : 0;
                default:
                    return std::nullopt;
                }
            }
            if (const auto *binaryOperator = llvm::dyn_cast<clang::BinaryOperator>(current))
            {
                const auto left = evalInt(binaryOperator->getLHS());
                const auto right = evalInt(binaryOperator->getRHS());
                if (!left.has_value() || !right.has_value())
                {
                    return std::nullopt;
                }
                switch (binaryOperator->getOpcode())
                {
                case clang::BO_Add:
                    return *left + *right;
                case clang::BO_Sub:
                    return *left - *right;
                case clang::BO_Mul:
                    return *left * *right;
                case clang::BO_Div:
                    return *right == 0 ? std::nullopt : std::optional<int64_t>(*left / *right);
                case clang::BO_EQ:
                    return *left == *right ? 1 : 0;
                case clang::BO_NE:
                    return *left != *right ? 1 : 0;
                case clang::BO_LT:
                    return *left < *right ? 1 : 0;
                case clang::BO_LE:
                    return *left <= *right ? 1 : 0;
                case clang::BO_GT:
                    return *left > *right ? 1 : 0;
                case clang::BO_GE:
                    return *left >= *right ? 1 : 0;
                case clang::BO_LAnd:
                    return (*left != 0 && *right != 0) ? 1 : 0;
                case clang::BO_LOr:
                    return (*left != 0 || *right != 0) ? 1 : 0;
                default:
                    return std::nullopt;
                }
            }
            return std::nullopt;
        };

        if (const auto value = evalInt(expr))
        {
            return *value != 0;
        }
        return std::nullopt;
    }
    bool BaseASTVisitor::isLegacyStorageBufferType(const clang::QualType &qt) const
    {
        return mDeclQuery.isLegacyStorageBufferType(qt);
    }
    [[noreturn]] void BaseASTVisitor::throwLegacyStorageBufferMigrationError(const std::string &usageContext) const
    {
        mDeclQuery.throwLegacyStorageBufferMigrationError(usageContext);
    }
    bool BaseASTVisitor::isBindGroupHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isBindGroupHandleType(qt);
    }
    bool BaseASTVisitor::isRenderSetHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isRenderSetHandleType(qt);
    }
    bool BaseASTVisitor::isShaderResourceHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isShaderResourceHandleType(qt);
    }
    bool BaseASTVisitor::isSampledTextureOrSamplerHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isSampledTextureOrSamplerHandleType(qt);
    }
    bool BaseASTVisitor::isInputOnlyShaderHandleParameterType(const clang::QualType &qt) const
    {
        return mDeclQuery.isInputOnlyShaderHandleParameterType(qt);
    }
    void BaseASTVisitor::validateInputOnlyShaderHandleParameterOrThrow(const clang::ParmVarDecl *param, const std::string &backendName) const
    {
        if (param == nullptr || !isInputOnlyShaderHandleParameterType(param->getType()))
        {
            return;
        }
        if (!checkAttibuteByName(param, mUGLAttributeOUTName) && !checkAttibuteByName(param, mUGLAttributeINOUTName))
        {
            return;
        }
        if (isSampledTextureOrSamplerHandleType(param->getType()))
        {
            throw std::runtime_error(backendName + " shader helper parameter \"" + param->getNameAsString()
                                     + "\" uses a sampled texture/sampler handle with [[OUT]] or [[INOUT]], but sampled texture/sampler helper parameters can only be input parameters.");
        }
        throw std::runtime_error(backendName + " shader helper parameter \"" + param->getNameAsString()
                                 + "\" uses a shader resource handle with [[OUT]] or [[INOUT]], but shader resource helper parameters can only be input parameters.");
    }
    bool BaseASTVisitor::isAnyShaderResourceHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isAnyShaderResourceHandleType(qt);
    }
    bool BaseASTVisitor::isHostResourceHandleType(const clang::QualType &qt) const
    {
        return mDeclQuery.isHostResourceHandleType(qt);
    }
    bool BaseASTVisitor::typeContainsHostResourceHandle(const clang::QualType &qt) const
    {
        return mDeclQuery.typeContainsHostResourceHandle(qt);
    }
    bool BaseASTVisitor::functionSignatureUsesShaderResourceHandles(const clang::FunctionDecl *func) const
    {
        return mDeclQuery.functionSignatureUsesShaderResourceHandles(func);
    }
    bool BaseASTVisitor::recordUsesShaderResourceHandles(const clang::CXXRecordDecl *decl) const
    {
        return mDeclQuery.recordUsesShaderResourceHandles(decl);
    }
    bool BaseASTVisitor::recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl) const
    {
        return mDeclQuery.recordContainsHostResourceHandles(decl);
    }
    bool BaseASTVisitor::isShaderResourceOrBindingType(const clang::QualType &qt) const
    {
        return mDeclQuery.isShaderResourceOrBindingType(qt);
    }
    bool BaseASTVisitor::recordContainsShaderResourceOrBindingFields(const clang::CXXRecordDecl *decl) const
    {
        return mDeclQuery.recordContainsShaderResourceOrBindingFields(decl);
    }
    bool BaseASTVisitor::checkFunctionName(const clang::FunctionDecl *decl, const std::string &name) const
    {
        return mDeclQuery.checkFunctionName(decl, name);
    }
    bool BaseASTVisitor::shouldEmitNestedRecordDefinition(const clang::CXXRecordDecl *parentDecl, const clang::CXXRecordDecl *nestedDecl) const
    {
        return mDeclQuery.shouldEmitNestedRecordDefinition(parentDecl, nestedDecl);
    }
    /*     std::string BaseASTVisitor::getTypeCanonicalName(const clang::VarDecl *decl) const
        {
            std::string className = getClassCanonicalName(decl->getType()->getAsCXXRecordDecl());
            className += getTypeTemplateArgs(decl->getType());
            return className;
        } */
    std::string BaseASTVisitor::generateTypeCanonicalName(const clang::QualType &qt, const AbstractTypeConvertor *typeConvertor)
    {
        return mTypeNameEmitter.generateTypeCanonicalName(qt, typeConvertor);
    }
    std::optional<std::string> BaseASTVisitor::getErasedTemplateSpecializationName(const clang::ClassTemplateSpecializationDecl *decl, const AbstractTypeConvertor *typeConvertor) const
    {
        if (decl == nullptr || decl->getSpecializedTemplate() == nullptr)
        {
            return std::nullopt;
        }
        if (isFromExcludedFile(decl->getSpecializedTemplate()->getTemplatedDecl()->getLocation()))
        {
            return std::nullopt;
        }

        std::string result = decl->getSpecializedTemplate()->getTemplatedDecl()->getQualifiedNameAsString();
        if (typeConvertor != nullptr)
        {
            result = typeConvertor->convertType(result, {});
        }
        const clang::TemplateArgumentList &templateArgs = decl->getTemplateArgs();
        for (unsigned i = 0; i < templateArgs.size(); ++i)
        {
            result += "__";
            result += sanitizeErasedTemplateSpecializationComponent(const_cast<BaseASTVisitor *>(this)->translateTemplateArgument(templateArgs.get(i), typeConvertor));
        }
        return result;
    }
    std::string BaseASTVisitor::generateRecordDefinitionName(const clang::CXXRecordDecl *decl, const AbstractTypeConvertor *typeConvertor)
    {
        if (decl == nullptr)
        {
            return "";
        }

        std::string name;
        if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl))
        {
            name = generateTypeCanonicalName(Context->getRecordType(decl), typeConvertor);
        }
        else if (const auto *currentSpecialization = getCurrentTemplateSubstitutionContext();
                 currentSpecialization != nullptr &&
                 currentSpecialization->getSpecializedTemplate() != nullptr &&
                 currentSpecialization->getSpecializedTemplate()->getTemplatedDecl()->getCanonicalDecl() == decl->getCanonicalDecl())
        {
            name = generateTypeCanonicalName(Context->getRecordType(currentSpecialization), typeConvertor);
        }
        else
        {
            name = decl->getNameAsString();
        }
        const size_t qualifierPos = name.rfind("::");
        if (qualifierPos != std::string::npos)
        {
            name.erase(0, qualifierPos + 2);
        }
        return name;
    }
    std::vector<clang::TemplateArgument> BaseASTVisitor::getTemplateArgumentsFromType(const clang::QualType &qt) const
    {
        return mTemplateArgumentEvaluator.getArgumentsFromType(qt);
    }
    void BaseASTVisitor::collectFlattenedTemplateArguments(const clang::TemplateArgument &arg, std::vector<clang::TemplateArgument> &outArgs) const
    {
        mTemplateArgumentEvaluator.collectFlattenedArguments(arg, outArgs);
    }
    int64_t BaseASTVisitor::getIntValueFromTemplateArgument(const clang::TemplateArgument &arg) const
    {
        return mTemplateArgumentEvaluator.evaluateIntegerArgument(arg);
    }
    std::vector<clang::FieldDecl *> BaseASTVisitor::getAllFieldFromRecord(const clang::CXXRecordDecl *decl) const
    {
        return mDeclQuery.getAllFieldsFromRecord(decl);
    }
    clang::FieldDecl *BaseASTVisitor::getFieldFromClassWithAttribute(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        return mDeclQuery.getFieldFromClassWithAttribute(decl, name);
    }
    const clang::ParmVarDecl *BaseASTVisitor::getParamFromFunctionWithAttribute(const clang::FunctionDecl *decl, const std::string &name) const
    {
        return mDeclQuery.getParamFromFunctionWithAttribute(decl, name);
    }
    const clang::ParmVarDecl *BaseASTVisitor::getParamFromFunctionByName(const clang::FunctionDecl *decl, const std::string &name) const
    {
        return mDeclQuery.getParamFromFunctionByName(decl, name);
    }
    bool BaseASTVisitor::isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName) const
    {
        return mAttributeEmitter.isExactIndexedAttribute(rawAttribute, attributeName);
    }
    int BaseASTVisitor::getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName) const
    {
        return mAttributeEmitter.getIndexedAttributeNumber(rawAttribute, attributeName);
    }
    std::optional<clang::QualType> BaseASTVisitor::resolveRecordNestedTrueType(const clang::QualType &qt) const
    {
        return mDeclQuery.resolveRecordNestedTrueType(qt);
    }
    void BaseASTVisitor::validateHLSLShaderBufferLayoutsOrThrow(const BindGroupInfoMap &bindGroupInfoMap) const
    {
        validateHLSLShaderBufferLayouts(*this, bindGroupInfoMap);
    }
    void BaseASTVisitor::validateMSLShaderBufferLayoutsOrThrow(const BindGroupInfoMap &bindGroupInfoMap) const
    {
        validateMSLShaderBufferLayouts(*this, bindGroupInfoMap);
    }
    std::vector<BindGroupFieldBindingInfo> BaseASTVisitor::resolveBindGroupFieldBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc)
    {
        return mShaderBindingResolver.resolveFieldBindings(bindGroupDecl, createFunc);
    }
    std::vector<BaseShaderResourceBinding> BaseASTVisitor::resolveBaseShaderResourceBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc)
    {
        return mShaderBindingResolver.resolveResourceBindings(bindGroupDecl, createFunc);
    }
    std::string BaseASTVisitor::translateFloatingLiteral(const clang::FloatingLiteral *L)
    {
        // 1. 获取 SourceManager 和 LangOptions，这是 Lexer 需要的上下文
        clang::SourceManager &SM = Context->getSourceManager();
        const clang::LangOptions &LO = Context->getLangOpts();

        // 2. 获取字面量在源码中的精确范围
        clang::SourceRange range = L->getSourceRange();

        // 3. 使用 Lexer 从源码中“剪取”出这段文本
        llvm::StringRef sourceText = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range), SM, LO);

        // 4. 将结果转换为 std::string 并返回
        return sourceText.str();
    }
    std::string BaseASTVisitor::translateIntegerLiteral(const clang::IntegerLiteral *L)
    {
        // 逻辑与上面完全相同
        clang::SourceManager &SM = Context->getSourceManager();
        const clang::LangOptions &LO = Context->getLangOpts();

        clang::SourceRange range = L->getSourceRange();

        llvm::StringRef sourceText = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range), SM, LO);

        if (sourceText.empty())
        {
            llvm::APSInt intVal(L->getValue());
            return std::to_string(intVal.getExtValue());
        }

        return sourceText.str();
    }
    std::string BaseASTVisitor::enterScope()
    {
        std::string result = mSpaceManager.getSpace();
        mSpaceManager.enter();
        return result + "{" + NewLine();
    }
    std::string BaseASTVisitor::quitScope()
    {
        mSpaceManager.quit();
        std::string result = mSpaceManager.getSpace();

        return result + "}" + NewLine();
    }
    std::string BaseASTVisitor::endClass()
    {
        mSpaceManager.quit();
        std::string result = mSpaceManager.getSpace();

        return result + "};";
    }
    std::string BaseASTVisitor::getLineDirective(clang::SourceLocation loc) const
    {
        if (mEnableLineDirectiveInsertion == false)
        {
            return NewLine();
        }
        //  检查位置是否有效且不位于宏中
        if (!loc.isValid() || loc.isMacroID())
        {
            return "";
        }

        const auto &sm = Context->getSourceManager();

        // 分解位置信息，获取原始文件名和行号
        auto decomposedLoc = sm.getDecomposedLoc(loc);
        llvm::StringRef filename = sm.getFilename(loc);
        int line = sm.getLineNumber(decomposedLoc.first, decomposedLoc.second);

        if (filename.empty())
        {
            return "";
        }

        // 对Windows路径进行转义处理
        std::string escapedFilename = filename.str(); // escapeStringForLineDirective(filename.str());

        // 构建 #line 指令字符串。注意前后的换行符，确保指令独占一行。
        std::string lineDirective = NewLine() + "#line " + std::to_string(line) + " \"" + escapedFilename + "\"" + NewLine();

        return lineDirective;
    }

    std::string BaseASTVisitor::getClassCanonicalName(const clang::CXXRecordDecl *decl, const AbstractTypeConvertor *typeConvertor) const
    {
        if (decl == nullptr)
        {
            return "";
        }

        std::string result = decl->getQualifiedNameAsString();
        if (typeConvertor)
        {
            result = typeConvertor->convertType(result, {});
        }
        return result;
    }

    std::string BaseASTVisitor::getFunctionUniqueID(const clang::FunctionDecl *decl) const
    {
        // 1. 获取完整的函数名
        std::string qualifiedName = decl->getQualifiedNameAsString();

        // 2. 获取函数的类型字符串（包含返回类型和参数）
        std::string typeString = decl->getType().getAsString();

        // 3. 将它们组合成一个唯一的、可读的标识符
        return qualifiedName + ":" + typeString;
    }

    std::string BaseASTVisitor::getNamespaceUniqueID(const clang::NamespaceDecl *decl) const
    {
        // 1. 获取完整的函数名
        std::string qualifiedName = decl->getQualifiedNameAsString();

        // 2. 获取函数的类型字符串（包含返回类型和参数）
        std::string typeString = std::to_string(decl->getBeginLoc().getHashValue());

        // 3. 将它们组合成一个唯一的、可读的标识符
        return qualifiedName + ":" + typeString;
    }

    clang::QualType BaseASTVisitor::getUnqualifiedType(const clang::QualType &qt) const
    {
        return mDeclQuery.getUnqualifiedType(qt);
    }


    std::string BaseASTVisitor::EOS() const
    {
        return ";\n";
    }
    std::string BaseASTVisitor::NewLine(int count) const
    {
        std::string result;
        int loopCount = std::max(1, count);
        for (int i = 0; i < loopCount; ++i)
        {
            result += "\n";
        }
        return result;
    }
    std::string BaseASTVisitor::generateBaseClassInRecordDefinition(const clang::CXXRecordDecl *decl)
    {
        std::string result = decl->getNumBases() > 0 ? ": " : "";
        unsigned counter = 0;
        for (const auto &baseSpec : decl->bases())
        {
            if (baseSpec.getAccessSpecifier() == clang::AS_public)
            {
                result += "public ";
            }
            // 获取基类的 CXXRecordDecl 定义
            std::string baseClassName = generateTypeCanonicalName(baseSpec.getType(), mDefaultTypeConvertor); //.getBaseTypeIdentifier()->getName().str();

            result += baseClassName;
            if (counter + 1 < decl->getNumBases())
            {
                result += ", ";
            }
            counter++;
        }

        return result;
    }

    std::string BaseASTVisitor::generateFunctionDefinition(const clang::FunctionDecl *func)
    {
        auto diagnosticScope = scopeDiagnosticLocation(func);
        try
        {
            std::string result;
            if (func == nullptr || func->isImplicit())
            {
                return result;
            }
            if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
                method != nullptr && !method->isUserProvided() && !checkAttibuteByName(func, mUGLCTORName))
            {
                return result;
            }

            std::string attrs = generateAttributes(func, mDefaultAttributeConvertor);
            if (mDefaultAttributeConvertor == nullptr)
            {
                attrs = "";
            }

            result += NewLine() + mSpaceManager.getSpace() + attrs + NewLine();
            std::string funcName = func->getNameAsString();


            result += generateFunctionSignature(func, funcName);
            if (checkAttibuteByName(func, mUGLCTORName))
            {
                result += generateUGLCTORFunctionBody(func);
            }
            else
            {
                result += generateFunctionBody(func);
            }
            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, func);
        }
    }

    std::vector<clang::CXXMethodDecl *> BaseASTVisitor::getMethodsFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        return mMethodLookup.getMethodsFromClass(decl, name);
    }
    MethodLookupOptions BaseASTVisitor::makeCreateMethodLookupOptions() const
    {
        return mMethodLookup.makeCreateMethodLookupOptions();
    }
    MethodLookupOptions BaseASTVisitor::makeVertexShaderMethodLookupOptions() const
    {
        return mMethodLookup.makeVertexShaderMethodLookupOptions();
    }
    MethodLookupOptions BaseASTVisitor::makeFragmentShaderMethodLookupOptions(const std::optional<clang::QualType> &preferredFirstParamType) const
    {
        return mMethodLookup.makeFragmentShaderMethodLookupOptions(preferredFirstParamType);
    }
    MethodLookupOptions BaseASTVisitor::makeComputeShaderMethodLookupOptions() const
    {
        return mMethodLookup.makeComputeShaderMethodLookupOptions();
    }
    MethodLookupOptions BaseASTVisitor::makeDomainShaderMethodLookupOptions() const
    {
        return mMethodLookup.makeDomainShaderMethodLookupOptions();
    }
    MethodLookupOptions BaseASTVisitor::makeHullShaderMethodLookupOptions() const
    {
        return mMethodLookup.makeHullShaderMethodLookupOptions();
    }
    std::string BaseASTVisitor::generateNamespaceDefinition(const clang::NamespaceDecl *decl)
    {
        std::string result;
        const std::string &nsName = decl->getNameAsString();

        result += mSpaceManager.getSpace() + "namespace " + nsName + NewLine();
        result += mSpaceManager.getSpace() + "{" + NewLine();
        mSpaceManager.enter();

        result += manualTraverseDecl(decl);
        mSpaceManager.quit();
        result += NewLine() + mSpaceManager.getSpace() + "} // namespace " + nsName + NewLine(2);
        return result;
    }
    std::string BaseASTVisitor::manualTraverseDecl(const clang::Decl *D)
    {
        std::string result;
        const clang::DeclContext *DC = llvm::dyn_cast<clang::DeclContext>(D);
        if (!DC)
        {
            return result;
        }

        for (const auto *SubDecl : DC->decls())
        {
            if (auto *Record = llvm::dyn_cast<clang::CXXRecordDecl>(SubDecl))
            {
                result += generateRecordDefinition(Record);
            }
            else if (auto *Function = llvm::dyn_cast<clang::FunctionDecl>(SubDecl))
            {
                // if (!llvm::dyn_cast<clang::CXXMethodDecl>(Function))
                {
                    result += generateFunctionDefinition(Function);
                }
            }
            else if (auto *Namespace = llvm::dyn_cast<clang::NamespaceDecl>(SubDecl))
            {
                result += generateNamespaceDefinition(Namespace);
            }
            else if (auto *Var = llvm::dyn_cast<clang::VarDecl>(SubDecl))
            {
                // this->VisitVarDecl(Var);
                result += translateVarDecl(Var) + EOS();
                // throw;
            }
            else if (auto *UsingDirective = llvm::dyn_cast<clang::UsingDirectiveDecl>(SubDecl))
            {
                // Keep namespace-scoped using-directives intact when we emit a
                // namespace body manually. This avoids turning traversal-only
                // cleanups into new unsupported-decl failures.
                result += getLineDirective(UsingDirective->getBeginLoc());
                result += mSpaceManager.getSpace() + "using namespace " + UsingDirective->getNominatedNamespace()->getQualifiedNameAsString() + EOS();
            }
            else if (auto *Typedef = llvm::dyn_cast<clang::TypedefNameDecl>(SubDecl))
            {
                // 如果是一个类型别名 (可以添加 VisitTypedefNameDecl 来处理)
                result += this->VisitTypedefNameDecl(Typedef);
                // throw;
            }
            else if (auto *templateClassDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(SubDecl))
            {
                result += generateTemplateClassDecl(templateClassDecl);
            }
            else if (auto *fnTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(SubDecl))
            {
                result += generateTemplateFunctionDecl(fnTemplateDecl);
            }
            else
            {
                throwCodegenError(SubDecl, "Unsupported decl: " + std::string(SubDecl->getDeclKindName()));
            }


            // ... 在这里可以继续添加对 EnumDecl, ClassTemplateDecl 等其他声明类型的处理
        }
        return result;
    }
    clang::CXXMethodDecl *BaseASTVisitor::getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        return mMethodLookup.getMethodFromClass(decl, name);
    }
    clang::CXXMethodDecl *BaseASTVisitor::getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name, const MethodLookupOptions &options) const
    {
        return mMethodLookup.getMethodFromClass(decl, name, options);
    }
    std::string BaseASTVisitor::generateRecordDataMembers(const clang::CXXRecordDecl *decl, AbstractTypeConvertor *typeConvertor)
    {
        std::string result;
        if (typeConvertor == nullptr)
        {
            typeConvertor = mDefaultTypeConvertor;
        }
        // 遍历所有成员
        for (auto *field : decl->fields())
        {
            result += generateRecordFieldDecl(field, typeConvertor);
        }
        return result;
    }
    std::string BaseASTVisitor::generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor)
    {
        std::string result;
        std::string attrs = generateAttributes(decl, mDefaultAttributeConvertor);
        if (mDefaultAttributeConvertor == nullptr)
        {
            attrs = "";
        }

        std::string constSpecifier;
        if (decl->getType().isConstQualified())
        {
            constSpecifier = "const ";
        }

        std::string typeName = generateTypeCanonicalName(decl->getType(), typeConvertor);

        std::string varName = decl->getNameAsString();
        result += getLineDirective(decl->getBeginLoc());

        result += mSpaceManager.getSpace() + constSpecifier + typeName + " " + varName + generateDeclArraySpecifier(decl->getType()) + attrs; // + EOS();
        if (decl->hasInClassInitializer() && (isImplicitNode(decl->getInClassInitializer()) == false))
        {
            result += " = " + TranslateExpr(decl->getInClassInitializer());
        }
        result += EOS();
        return result;
    }
    std::string BaseASTVisitor::getUGLAttributeSlotNameByIndex(int index) const
    {
        return mAttributeEmitter.getSlotNameByIndex(index);
    }

    std::string BaseASTVisitor::getUGLAttributeVertexInputNameByIndex(int index) const
    {
        return mAttributeEmitter.getVertexInputNameByIndex(index);
    }

    std::string BaseASTVisitor::generateTemplateCallArguments(const clang::Expr *CalleeExpr)
    {
        return mTypeNameEmitter.generateTemplateCallArguments(CalleeExpr, mDefaultTypeConvertor);
    }
    std::vector<std::string> BaseASTVisitor::generateTemplateCallArgumentsStr(const clang::Expr *CalleeExpr, AbstractTypeConvertor *typeConv)
    {
        return mTypeNameEmitter.generateTemplateCallArgumentsStr(CalleeExpr, typeConv);
    }
    std::string BaseASTVisitor::translateNestedNameSpecifier(const clang::NestedNameSpecifier *NNS)
    {
        return mTypeNameEmitter.translateNestedNameSpecifier(NNS, mDefaultTypeConvertor);
    }
    std::string BaseASTVisitor::translateTemplateArgument(const clang::TemplateArgument &arg, const AbstractTypeConvertor *typeConvertor)
    {
        return mTypeNameEmitter.translateTemplateArgument(arg, typeConvertor);
    }
    std::string BaseASTVisitor::generateTypeTemplateArgs(const clang::QualType qt)
    {
        return mTypeNameEmitter.generateTypeTemplateArgs(qt, mDefaultTypeConvertor);
    }
    std::string BaseASTVisitor::translateDeclRefExpr(const clang::DeclRefExpr *E)
    {
        if (const auto value = tryTranslateTemplateSubstitutedDeclRefExpr(E))
        {
            return *value;
        }

        // 1. 使用新的辅助函数来语义化地翻译限定符
        std::string qualifierStr = translateNestedNameSpecifier(E->getQualifier());

        // 2. 将限定符和名称拼接起来
        return qualifierStr + E->getNameInfo().getName().getAsString();
    }
    std::string BaseASTVisitor::translateUnresolvedLookupExpr(const clang::UnresolvedLookupExpr *E)
    {
        return translateNestedNameSpecifier(E->getQualifier()) + E->getNameInfo().getName().getAsString();
    }
    std::string BaseASTVisitor::translateCXXDependentScopeMemberExpr(const clang::CXXDependentScopeMemberExpr *E)
    {
        const std::string memberName = E->getMemberNameInfo().getName().getAsString();
        if (E->isImplicitAccess())
        {
            return memberName;
        }

        std::string baseText = TranslateExpr(E->getBase());
        if (!baseText.empty() && (baseText.back() == '.' || (baseText.length() > 1 && baseText.substr(baseText.length() - 2) == "->")))
        {
            return baseText + memberName;
        }

        return baseText + (E->isArrow() ? "->" : ".") + memberName;
    }
    std::string BaseASTVisitor::translateDependentScopeDeclRefExpr(const clang::DependentScopeDeclRefExpr *E)
    {
        return translateNestedNameSpecifier(E->getQualifier()) + E->getNameInfo().getName().getAsString();
    }
    std::string BaseASTVisitor::translateCXXMemberAccessSpecifier(const clang::AccessSpecifier &as)
    {
        std::string result;
        switch (as)
        {
        case clang::AS_public:
            result = "public";
            break;
        case clang::AS_protected:
            result = "protected";
            break;
        case clang::AS_private:
            result = "private";
            break;
        default:
            break;
        }
        return result;
    }
    // --- 【新增】专门翻译 C 风格类型转换的辅助函数 ---
    std::string BaseASTVisitor::translateCStyleCastExpr(const clang::CStyleCastExpr *E)
    {
        return mExpressionTranslator.translateCStyleCastExpr(E);
    }
    std::string BaseASTVisitor::translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *E)
    {
        return mExpressionTranslator.translateCXXStaticCastExpr(E);
    }
    std::string BaseASTVisitor::translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E)
    {
        return mExpressionTranslator.translateCXXOperatorCallExprBinaryOp(E);
    }
    std::string BaseASTVisitor::translateCXXOperatorCallExprArrow(const clang::CXXOperatorCallExpr *E)
    {
        return mExpressionTranslator.translateCXXOperatorCallExprArrow(E);
    }
    std::string BaseASTVisitor::translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *E)
    {
        return mExpressionTranslator.translateCXXOperatorCallExprFuncCall(E);
    }
    std::string BaseASTVisitor::translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E)
    {
        return mExpressionTranslator.translateCXXOperatorCallExprArraySubScript(E);
    }
    std::string BaseASTVisitor::generateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E, const std::string &callee)
    {
        return mExpressionTranslator.generateCXXMemberCallExpr(E, callee);
    }
    std::string BaseASTVisitor::translateStringLiteral(const clang::StringLiteral *L)
    {
        return mExpressionTranslator.translateStringLiteral(L);
    }
    std::string BaseASTVisitor::translateUnaryExprOrTypeTraitExpr(const clang::UnaryExprOrTypeTraitExpr *E)
    {
        return mExpressionTranslator.translateUnaryExprOrTypeTraitExpr(E);
    }
    std::string BaseASTVisitor::translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *E)
    {
        return mExpressionTranslator.translateCXXFunctionalCastExpr(E);
    }
    std::string BaseASTVisitor::generateDeclArraySpecifier(const clang::QualType &qt)
    {
        std::string arraySpecifiers;
        clang::QualType tempqt = qt;

        // 循环处理，以支持多维数组 (例如 T a[1][2])
        while (tempqt->isArrayType())
        {
            // 我们从最外层的数组维度开始剥离
            const clang::ArrayType *arrayType = Context->getAsArrayType(tempqt);
            if (const auto *constArrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType))
            {
                // 将 [6] 这样的维度信息加到 arraySpecifiers 的前面
                arraySpecifiers = "[" + std::to_string(constArrayType->getSize().getZExtValue()) + "]" + arraySpecifiers;
            }
            else if (const auto *dependentArrayType = llvm::dyn_cast<clang::DependentSizedArrayType>(arrayType))
            {
                // 场景2: 数组大小依赖于模板参数 (例如 T arr[N])
                clang::Expr *sizeExpr = dependentArrayType->getSizeExpr();
                if (sizeExpr)
                {
                    std::string sizeStr = TranslateExpr(sizeExpr);
                    if (const auto *declRefExpr = llvm::dyn_cast<clang::DeclRefExpr>(sizeExpr->IgnoreParenImpCasts()))
                    {
                        if (const auto resolvedValue = tryResolveTemplateSubstitutionValue(declRefExpr->getDecl()))
                        {
                            sizeStr = *resolvedValue;
                        }
                    }
                    if (const auto resolvedValue = tryResolveTemplateSubstitutionValueByName(sizeStr))
                    {
                        sizeStr = *resolvedValue;
                    }
                    if (sizeStr.empty())
                    {
                        // 我们获取代表大小的表达式，并将其打印为字符串
                        llvm::raw_string_ostream os(sizeStr);
                        // 使用 printPretty 可以将表达式（例如这里的 'N'）转换为字符串
                        sizeExpr->printPretty(os, nullptr, Context->getPrintingPolicy());
                    }

                    arraySpecifiers = "[" + sizeStr + "]" + arraySpecifiers;
                }
                else
                {
                    // 理论上，依赖大小数组应该总是有sizeExpr，但以防万一
                    arraySpecifiers = "[]" + arraySpecifiers;
                }
            }
            else
            {
                // 处理不完整数组类型 T[]
                arraySpecifiers = "[]" + arraySpecifiers;
            }
            // 更新 qt 为其内部的元素类型，以进行下一次循环
            tempqt = arrayType->getElementType();
        }
        return arraySpecifiers;
    }
    std::string BaseASTVisitor::generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor)
    {
        std::string attrs = generateAttributes(param, mDefaultAttributeConvertor);
        std::string defaultArg;
        std::string arraySpecifiers = generateDeclArraySpecifier(param->getOriginalType());
        // 【新增】检查参数是否有默认值
        if (param->hasDefaultArg())
        {
            // 获取默认参数的表达式
            const clang::Expr *defaultArgExpr = param->getDefaultArg();

            // 翻译该表达式
            std::string defaultArgStr = TranslateExpr(defaultArgExpr);

            // 拼接 " = " 和翻译好的默认值
            if (!defaultArgStr.empty())
            {
                defaultArg = " = " + defaultArgStr;
            }
        }
        return generateTypeCanonicalName(param->getType(), typeConvertor) + " " + param->getNameAsString() + arraySpecifiers + defaultArg + attrs;
    }
    std::string BaseASTVisitor::translateForStmt(const clang::ForStmt *S)
    {
        return mStatementTranslator.translateForStmt(S);
    }
    std::string BaseASTVisitor::translateConditionalOperator(const clang::ConditionalOperator *E)
    {
        return mExpressionTranslator.translateConditionalOperator(E);
    }
    std::string BaseASTVisitor::translateArraySubscriptExpr(const clang::ArraySubscriptExpr *E)
    {
        return mExpressionTranslator.translateArraySubscriptExpr(E);
    }
    std::string BaseASTVisitor::translateBreakStmt(const clang::BreakStmt *S)
    {
        return mStatementTranslator.translateBreakStmt(S);
    }
    std::string BaseASTVisitor::getFullNamespace(const clang::Decl *D) const
    {
        return mDeclQuery.getFullNamespace(D);
    }
    std::vector<std::string> BaseASTVisitor::getLexicalNamespaceParts(const clang::Decl *decl) const
    {
        std::vector<std::string> namespaceParts;
        const clang::DeclContext *declContext = decl == nullptr ? nullptr : decl->getDeclContext();
        while (declContext != nullptr && !declContext->isTranslationUnit())
        {
            if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext))
            {
                if (!namespaceDecl->isAnonymousNamespace() && !namespaceDecl->getNameAsString().empty())
                {
                    namespaceParts.push_back(namespaceDecl->getNameAsString());
                }
            }
            declContext = declContext->getParent();
        }
        std::reverse(namespaceParts.begin(), namespaceParts.end());
        return namespaceParts;
    }
    std::vector<std::string> BaseASTVisitor::getShaderClassLexicalScopeParts(const clang::CXXRecordDecl *shaderClassDecl) const
    {
        std::vector<std::string> scopeParts = getLexicalNamespaceParts(shaderClassDecl);
        if (shaderClassDecl != nullptr && !shaderClassDecl->getNameAsString().empty())
        {
            scopeParts.push_back(shaderClassDecl->getNameAsString());
        }
        return scopeParts;
    }
    std::string BaseASTVisitor::beginLexicalNamespaceScopes(const std::vector<std::string> &namespaceParts)
    {
        std::string result;
        for (const std::string &namespacePart : namespaceParts)
        {
            result += mSpaceManager.getSpace() + "namespace " + namespacePart + NewLine();
            result += enterScope();
        }
        return result;
    }
    std::string BaseASTVisitor::endLexicalNamespaceScopes(const std::vector<std::string> &namespaceParts)
    {
        std::string result;
        for (auto it = namespaceParts.rbegin(); it != namespaceParts.rend(); ++it)
        {
            result += quitScope();
        }
        return result;
    }
    std::string BaseASTVisitor::wrapInNamespaceScopes(const std::vector<std::string> &namespaceParts, const std::string &body)
    {
        if (body.empty())
        {
            return {};
        }
        if (namespaceParts.empty())
        {
            return body;
        }
        return beginLexicalNamespaceScopes(namespaceParts) + body + endLexicalNamespaceScopes(namespaceParts) + NewLine();
    }
    std::string BaseASTVisitor::wrapInLexicalNamespaceScopes(const clang::Decl *decl, const std::string &body)
    {
        const std::vector<std::string> namespaceParts = getLexicalNamespaceParts(decl);
        return wrapInNamespaceScopes(namespaceParts, body);
    }
    std::vector<std::string> BaseASTVisitor::getAttributeParams(const clang::Decl *D, const std::string &funcName, const AbstractAttributeConvertor *conv)
    {
        return mAttributeEmitter.getAttributeParams(D, funcName, conv);
    }
    std::string BaseASTVisitor::translateContinueStmt(const clang::ContinueStmt *S)
    {
        return mStatementTranslator.translateContinueStmt(S);
    }
    std::string BaseASTVisitor::translateWhileStmt(const clang::WhileStmt *S)
    {
        return mStatementTranslator.translateWhileStmt(S);
    }
    std::string BaseASTVisitor::translateDoStmt(const clang::DoStmt *S)
    {
        return mStatementTranslator.translateDoStmt(S);
    }
    std::string BaseASTVisitor::translateSwitchStmt(const clang::SwitchStmt *S)
    {
        return mStatementTranslator.translateSwitchStmt(S);
    }
    std::string BaseASTVisitor::translateCaseStmt(const clang::CaseStmt *S)
    {
        return mStatementTranslator.translateCaseStmt(S);
    }
    std::string BaseASTVisitor::translateDefaultStmt(const clang::DefaultStmt *S)
    {
        return mStatementTranslator.translateDefaultStmt(S);
    }
    std::string BaseASTVisitor::translateOffsetOfExpr(const clang::OffsetOfExpr *E)
    {
        return mExpressionTranslator.translateOffsetOfExpr(E);
    }

    std::string BaseASTVisitor::generateTemplateClassDecl(const clang::ClassTemplateDecl *decl)
    {
        std::string result;
        const clang::CXXRecordDecl *templatedDecl = decl->getTemplatedDecl();
        const std::string canonicalName = getClassCanonicalName(templatedDecl);
        if (mDefaultTypeConvertor != nullptr && mDefaultTypeConvertor->shouldMaterializeTemplateSpecializationName(canonicalName))
        {
            for (auto *specializationDecl : decl->specializations())
            {
                const clang::CXXRecordDecl *specializationDefinition = specializationDecl->getDefinition();
                if (specializationDefinition == nullptr || isFromExcludedFile(specializationDefinition->getLocation()))
                {
                    continue;
                }

                result += getLineDirective(specializationDefinition->getBeginLoc());
                result += generateRecordDefinition(specializationDefinition);
            }
            return result;
        }

        // 2. 生成模板头部，例如 "template<class T>"
        result += mSpaceManager.getSpace() + generateTemplateParameters(decl->getTemplateParameters());

        // 3. 获取模板内部的类/结构体“模式”，并委托给核心生成函数处理
        //    decl->getTemplatedDecl() 返回的是那个 CXXRecordDecl*
        result += generateRecordDefinition(templatedDecl);

        // 4. 返回 false，因为我们已经完整地处理了这个模板及其内部定义，
        //    不希望 Visitor 再对内部的 CXXRecordDecl 单独进行 Visit。

        return result;
    }

    std::string BaseASTVisitor::generateTemplateFunctionDecl(const clang::FunctionTemplateDecl *decl)
    {
        std::string result;
        // 2. 生成模板头部，例如 "template<class T>"
        result += mSpaceManager.getSpace() + generateTemplateParameters(decl->getTemplateParameters());

        // 3. 获取模板内部的类/结构体“模式”，并委托给核心生成函数处理
        //    decl->getTemplatedDecl() 返回的是那个 CXXRecordDecl*
        result += generateFunctionDefinition(decl->getTemplatedDecl());

        // 4. 返回 false，因为我们已经完整地处理了这个模板及其内部定义，
        //    不希望 Visitor 再对内部的 CXXRecordDecl 单独进行 Visit。

        return result;
    }
    std::string BaseASTVisitor::VisitTypedefNameDecl(const clang::TypedefNameDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            std::string result;
            // 1. 入口检查：我们只处理顶层的、非排除文件中的别名
            /*  if (!isTopLevel(decl) || isFromExcludedFile(decl->getLocation()))
             {
                 return "";
             } */

            // 2. 获取别名指向的真实类型
            clang::QualType underlyingType = decl->getUnderlyingType();

            if (mDefaultTypeConvertor && mDefaultTypeConvertor->checkShouldIgnoreUsingDecl(generateTypeCanonicalName(underlyingType)))
            {
                result += "//";
            }
            std::string typeStr = generateTypeCanonicalName(underlyingType, mDefaultTypeConvertor);

            // 3. 【关键】通过 dyn_cast 判断这究竟是 using 还是 typedef
            if (llvm::dyn_cast<clang::TypeAliasDecl>(decl))
            {
                // --- a. 这是 using 别名的情况 ---
                // 例如: using Vector2 = float2;
                // 生成 `using Name = Type;`
                result += "using " + decl->getNameAsString() + " = " + typeStr + ";\n\n";
            }
            else
            {
                // --- b. 这是传统 typedef 的情况 ---
                // 例如: typedef float4 Color;
                // 生成 `typedef Type Name;`
                result += "typedef " + typeStr + " " + decl->getNameAsString() + ";\n\n";
            }

            // 4. 返回 true，允许遍历继续
            return result;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }

    std::string BaseASTVisitor::GetAlignasSourceString(const clang::Decl *D)
    {
        if (!D)
        {
            return "";
        }

        // 从 ASTContext 获取必要的工具
        const clang::SourceManager &SM = Context->getSourceManager();
        const clang::LangOptions &LangOpts = Context->getLangOpts();

        for (const auto *Attr : D->attrs())
        {
            // 1. 转换为 AlignedAttr
            const auto *Aligned = clang::dyn_cast<clang::AlignedAttr>(Attr);
            if (!Aligned)
                continue;

            // 2. 必须是 C++11 的 alignas (排除 __attribute__)
            if (!Aligned->isAlignas())
                continue;

            // 3. 获取 alignas 关键字的起始位置
            clang::SourceLocation StartLoc = Aligned->getLocation();
            if (StartLoc.isInvalid())
                continue;

            // 4. 开始手动 Token 扫描
            // 我们需要找到匹配的右括号 ')'
            clang::SourceLocation EndLoc = StartLoc;
            int ParenCount = 0;
            bool FoundFirstParen = false;

            // 这里的 Token 循环逻辑：
            // 从 alignas 开始往后读，直到括号闭合
            clang::Token Tok;
            // 初始化一个“生”词法分析器 (Raw Lexer)
            // 注意：这里使用 StartLoc 开始读取
            bool Invalid = false;
            SM.getCharacterData(StartLoc, &Invalid);
            if (Invalid)
                continue;

            // 简单的字符流扫描可能会被宏干扰，最稳妥的是使用 Lexer::getRawToken
            // 但为了简单且处理连续性，我们使用循环 getRawToken

            clang::SourceLocation CurrentLoc = StartLoc;

            while (true)
            {
                // 获取当前位置的一个 Token（不预处理宏，直接看源码）
                if (clang::Lexer::getRawToken(CurrentLoc, Tok, SM, LangOpts, true))
                {
                    // 如果获取失败（比如到了文件末尾），停止
                    break;
                }

                if (Tok.is(clang::tok::l_paren))
                {
                    ParenCount++;
                    FoundFirstParen = true;
                }
                else if (Tok.is(clang::tok::r_paren))
                {
                    ParenCount--;
                }

                // 更新结束位置为当前 Token 的结束位置
                EndLoc = Tok.getEndLoc();

                // 如果找到了第一个括号，且计数归零，说明 alignas(...) 结束了
                if (FoundFirstParen && ParenCount == 0)
                {
                    break;
                }

                // 移动到下一个 Token 的位置
                CurrentLoc = Tok.getEndLoc();

                // 安全保护：防止死循环（比如代码写错了，只有 alignas 没有括号）
                // 如果扫描超过一定距离或者遇到分号/大括号，强制退出
                if (Tok.is(clang::tok::semi) || Tok.is(clang::tok::l_brace) || Tok.is(clang::tok::eof))
                {
                    break;
                }
            }

            // 5. 提取最终范围的文本
            // 构造一个从 StartLoc 到 EndLoc 的范围
            clang::SourceRange FullRange(StartLoc, EndLoc);

            // 注意：EndLoc 已经是 Token 的结束位置了，所以不需要 getTokenRange
            // 但为了保险，我们使用 CharSourceRange::getCharRange
            clang::CharSourceRange CSR = clang::CharSourceRange::getCharRange(FullRange);

            return clang::Lexer::getSourceText(CSR, SM, LangOpts).str();
        }

        return "";
    }
} // namespace UGLC::CodeGen
