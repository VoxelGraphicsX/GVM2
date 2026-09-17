#pragma once

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "ASTMethodLookup.hpp"
#include "BaseAttributeEmitter.hpp"
#include "BaseDeclQuery.hpp"
#include "BaseExpressionTranslator.hpp"
#include "BaseStatementTranslator.hpp"
#include "BaseTemplateArgumentEvaluator.hpp"
#include "BaseTypeNameEmitter.hpp"
#include "BaseShaderBindingResolver.hpp"
#include "Diagnostics.hpp"
#include "BaseShaderBinding.hpp"
#include "ShaderBindGroupInfo.hpp"
#include "AbstractAttributeConvertor.hpp"
#include "AbstractFunctionConvertor.hpp"
#include "AbstractTypeConvertor.hpp"
#include "SpaceManager.hpp"
#include "clang/Rewrite/Core/Rewriter.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TemplateBase.h>
#include <clang/Frontend/CompilerInstance.h>

namespace clang
{
    class ClassTemplateSpecializationDecl;
    class DeclRefExpr;
    class ValueDecl;
} // namespace clang

namespace UGLC::CodeGen
{
    struct SourceFileInfo
    {
        std::vector<std::string> fileNames;
    };
    std::string stringJoin(const std::vector<std::string> &sources, const std::string &splitStr);
    class BaseASTVisitor
    {
        friend class BaseExpressionTranslator;
        /** Stores the active concrete template specialization used while erasing a DSL template body. */
        struct TemplateSubstitutionContext
        {
            const clang::ClassTemplateSpecializationDecl *specializationDecl = nullptr;
        };
        // std::string mCodeGenResult;
        AbstractTypeConvertor *mDefaultTypeConvertor = nullptr;
        AbstractAttributeConvertor *mDefaultAttributeConvertor = nullptr;
        AbstractFunctionConvertor *mDefaultFunctionConvertor = nullptr;
        mutable std::vector<clang::SourceLocation> mDiagnosticLocationStack;
        std::vector<TemplateSubstitutionContext> mTemplateSubstitutionStack;
        BaseAttributeEmitter mAttributeEmitter;
        BaseDeclQuery mDeclQuery;
        BaseExpressionTranslator mExpressionTranslator;
        BaseStatementTranslator mStatementTranslator;
        ASTMethodLookup mMethodLookup;
        BaseTemplateArgumentEvaluator mTemplateArgumentEvaluator;
        BaseTypeNameEmitter mTypeNameEmitter;
        BaseShaderBindingResolver mShaderBindingResolver;

    protected:
        class DiagnosticLocationScope
        {
        public:
            DiagnosticLocationScope(const BaseASTVisitor *visitor, clang::SourceLocation location);
            DiagnosticLocationScope(const DiagnosticLocationScope &) = delete;
            DiagnosticLocationScope &operator=(const DiagnosticLocationScope &) = delete;
            DiagnosticLocationScope(DiagnosticLocationScope &&other) noexcept;
            DiagnosticLocationScope &operator=(DiagnosticLocationScope &&other) noexcept;
            ~DiagnosticLocationScope();

        private:
            const BaseASTVisitor *mVisitor = nullptr;
            bool mActive = false;
        };

    public:
        explicit BaseASTVisitor(clang::ASTContext *Context, AbstractTypeConvertor *defaultTypeConvertor, AbstractAttributeConvertor *defaultAttributeConvertor, AbstractFunctionConvertor *defaultFunctionConvertor);


        std::string TranslateStmt(const clang::Stmt *S);

        std::string TranslateExpr(const clang::Expr *E);

        clang::ASTContext *Context;
        SpaceManager mSpaceManager;
        bool mEnableLineDirectiveInsertion = false;

        std::string enterScope();

        std::string quitScope();

        std::string endClass();

        std::string EOS() const;
        std::string NewLine(int count = 1) const;
        DiagnosticLocationScope scopeDiagnosticLocation(clang::SourceLocation location) const;
        DiagnosticLocationScope scopeDiagnosticLocation(const clang::Decl *decl) const;
        DiagnosticLocationScope scopeDiagnosticLocation(const clang::Stmt *stmt) const;
        std::string formatCodegenError(const std::string &message) const;
        std::string formatCodegenError(clang::SourceLocation location, const std::string &message) const;
        std::string formatCodegenError(const clang::Decl *decl, const std::string &message) const;
        std::string formatCodegenError(const clang::Stmt *stmt, const std::string &message) const;
        [[noreturn]] void throwCodegenError(const std::string &message) const;
        [[noreturn]] void throwCodegenError(clang::SourceLocation location, const std::string &message) const;
        [[noreturn]] void throwCodegenError(const clang::Decl *decl, const std::string &message) const;
        [[noreturn]] void throwCodegenError(const clang::Stmt *stmt, const std::string &message) const;
        [[noreturn]] void rethrowCodegenError(const std::exception &error, clang::SourceLocation fallbackLocation) const;
        [[noreturn]] void rethrowCodegenError(const std::exception &error, const clang::Decl *decl) const;
        [[noreturn]] void rethrowCodegenError(const std::exception &error, const clang::Stmt *stmt) const;
        virtual bool allowExplicitThisPointerAccess() const
        {
            return false;
        }
        const clang::CXXThisExpr *tryGetCXXThisExpr(const clang::Expr *expr) const;
        bool isUserWrittenThisExpr(const clang::CXXThisExpr *expr) const;
        [[noreturn]] void throwExplicitThisPointerAccessError(const clang::Expr *expr) const;
        bool pushDiagnosticLocation(clang::SourceLocation location) const;
        void popDiagnosticLocation() const;
        std::string currentDiagnosticMessageOrFallback(const std::string &message, clang::SourceLocation location) const;

        bool isFromExcludedFile(clang::SourceLocation loc) const;
        bool isImplicitNode(const clang::Expr *E) const;
        bool isTopLevel(const clang::Decl *D) const;
        bool checkAttibuteByName(const clang::Decl *D, const std::string &name) const;

        bool checkDerivedClassByName(const clang::CXXRecordDecl *decl, const std::string &name) const;
        bool checkTypeCanonicalName(const clang::QualType &qt, const std::string &name) const;
        /** Pushes a concrete class template specialization used to erase dependent template names during codegen. */
        void pushTemplateSubstitutionContext(const clang::ClassTemplateSpecializationDecl *specializationDecl);
        /** Pops the most recent concrete template specialization context. */
        void popTemplateSubstitutionContext();
        /** Returns the current concrete template specialization context, or null when no template is being erased. */
        const clang::ClassTemplateSpecializationDecl *getCurrentTemplateSubstitutionContext() const;
        /** Resolves a template type parameter or dependent nested type through the active substitution context. */
        std::optional<clang::QualType> tryResolveTemplateSubstitutionType(const clang::QualType &type) const;
        /** Finds the concrete template argument bound to a template parameter in the active substitution context. */
        std::optional<clang::TemplateArgument> findTemplateSubstitutionArgument(const clang::NamedDecl *parameterDecl) const;
        /** Resolves a non-type template parameter reference through the active substitution context. */
        std::optional<std::string> tryResolveTemplateSubstitutionValue(const clang::ValueDecl *decl) const;
        /** Translates a DeclRefExpr that names a substituted non-type template parameter in the active shader specialization. */
        std::optional<std::string> tryTranslateTemplateSubstitutedDeclRefExpr(const clang::DeclRefExpr *expr) const;
        /** Resolves a non-type template parameter by source name through the active substitution context. */
        std::optional<std::string> tryResolveTemplateSubstitutionValueByName(const std::string &parameterName) const;
        /** Evaluates a boolean expression after replacing supported non-type template parameters. */
        std::optional<bool> tryEvaluateTemplateSubstitutionBooleanCondition(const clang::Expr *expr) const;
        bool checkFunctionName(const clang::FunctionDecl *decl, const std::string &name) const;
        bool shouldEmitNestedRecordDefinition(const clang::CXXRecordDecl *parentDecl, const clang::CXXRecordDecl *nestedDecl) const;
        bool isLegacyStorageBufferType(const clang::QualType &qt) const;
        [[noreturn]] void throwLegacyStorageBufferMigrationError(const std::string &usageContext) const;
        /** Returns true when the type is `UGL::BindGroup<T>` and can be used as a shader handle. */
        bool isBindGroupHandleType(const clang::QualType &qt) const;
        /** Returns true when the type is `UGL::RenderSet<T>` and can be used as a shader handle. */
        bool isRenderSetHandleType(const clang::QualType &qt) const;
        /** Returns true when the type is a direct shader resource handle such as a buffer, texture, or sampler. */
        bool isShaderResourceHandleType(const clang::QualType &qt) const;
        /** Returns true when the type is a sampled texture or sampler handle that must be passed by input only. */
        bool isSampledTextureOrSamplerHandleType(const clang::QualType &qt) const;
        /** Returns true when a shader handle parameter must not use output-style attributes. */
        bool isInputOnlyShaderHandleParameterType(const clang::QualType &qt) const;
        /** Rejects output-qualified shader handle parameters using backend-specific diagnostic text. */
        void validateInputOnlyShaderHandleParameterOrThrow(const clang::ParmVarDecl *param, const std::string &backendName) const;
        /** Returns true when the type is any shader-side resource handle, including bind groups and render sets. */
        bool isAnyShaderResourceHandleType(const clang::QualType &qt) const;
        /** Returns true when the type is a host-only resource handle that must not be emitted into shader code. */
        bool isHostResourceHandleType(const clang::QualType &qt) const;
        /** Returns true when the type itself or a nested template argument contains a host-only resource handle. */
        bool typeContainsHostResourceHandle(const clang::QualType &qt) const;
        /** Returns true when a function signature mentions a shader resource handle type. */
        bool functionSignatureUsesShaderResourceHandles(const clang::FunctionDecl *func) const;
        /** Returns true when a record should be treated as shader-only resource-handle behavior. */
        bool recordUsesShaderResourceHandles(const clang::CXXRecordDecl *decl) const;
        /** Recursively checks whether a record contains fields with host-only resource handles. */
        bool recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl) const;
        bool isShaderResourceOrBindingType(const clang::QualType &qt) const;
        bool recordContainsShaderResourceOrBindingFields(const clang::CXXRecordDecl *decl) const;
        // bool checkMethodFromClass() const;
        // std::string getTypeCanonicalName(const clang::VarDecl *decl) const;

        std::vector<clang::TemplateArgument> getTemplateArgumentsFromType(const clang::QualType &qt) const;
        void collectFlattenedTemplateArguments(const clang::TemplateArgument &arg, std::vector<clang::TemplateArgument> &outArgs) const;
        int64_t getIntValueFromTemplateArgument(const clang::TemplateArgument &arg) const;


        std::vector<clang::FieldDecl *> getAllFieldFromRecord(const clang::CXXRecordDecl *decl) const;
        clang::FieldDecl *getFieldFromClassWithAttribute(const clang::CXXRecordDecl *decl, const std::string &name) const;
        const clang::ParmVarDecl *getParamFromFunctionWithAttribute(const clang::FunctionDecl *decl, const std::string &name) const;
        const clang::ParmVarDecl *getParamFromFunctionByName(const clang::FunctionDecl *decl, const std::string &name) const;
        std::string getUGLAttributeSlotNameByIndex(int index) const;
        std::string getUGLAttributeVertexInputNameByIndex(int index) const;
        std::string getFullNamespace(const clang::Decl *D) const;
        /** Returns the lexical namespace chain that should wrap a selected shader declaration. */
        std::vector<std::string> getLexicalNamespaceParts(const clang::Decl *decl) const;
        /** Returns the lexical namespace chain plus the shader class scope for nested shader declarations. */
        std::vector<std::string> getShaderClassLexicalScopeParts(const clang::CXXRecordDecl *shaderClassDecl) const;
        /** Emits opening namespace scopes for one selected declaration and updates indentation state. */
        std::string beginLexicalNamespaceScopes(const std::vector<std::string> &namespaceParts);
        /** Emits closing namespace scopes for one selected declaration and restores indentation state. */
        std::string endLexicalNamespaceScopes(const std::vector<std::string> &namespaceParts);
        /** Wraps generated declaration text in the provided namespace scopes. */
        std::string wrapInNamespaceScopes(const std::vector<std::string> &namespaceParts, const std::string &body);
        /** Wraps generated declaration text in the declaration's lexical namespace scopes. */
        std::string wrapInLexicalNamespaceScopes(const clang::Decl *decl, const std::string &body);
        bool isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName) const;
        int getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName) const;
        std::optional<clang::QualType> resolveRecordNestedTrueType(const clang::QualType &qt) const;
        void validateHLSLShaderBufferLayoutsOrThrow(const BindGroupInfoMap &bindGroupInfoMap) const;
        void validateMSLShaderBufferLayoutsOrThrow(const BindGroupInfoMap &bindGroupInfoMap) const;
        std::vector<BindGroupFieldBindingInfo> resolveBindGroupFieldBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc = nullptr);
        std::vector<BaseShaderResourceBinding> resolveBaseShaderResourceBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc = nullptr);

        /** Builds a #line directive for generated text that preserves source diagnostics. */
        std::string getLineDirective(clang::SourceLocation loc) const;
        /** Inserts a #line directive before the current rewriter insertion point. */
        void insertLineDirective(clang::SourceLocation loc) const;
        std::string getClassCanonicalName(const clang::CXXRecordDecl *decl, const AbstractTypeConvertor *typeConvertor = nullptr) const;
        std::string getFunctionUniqueID(const clang::FunctionDecl *decl) const;
        std::string getNamespaceUniqueID(const clang::NamespaceDecl *decl) const;
        clang::QualType getUnqualifiedType(const clang::QualType &qt) const;
        clang::CXXMethodDecl *getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const;
        clang::CXXMethodDecl *getMethodFromClass(const clang::CXXRecordDecl *decl, const std::string &name, const MethodLookupOptions &options) const;
        std::vector<clang::CXXMethodDecl *> getMethodsFromClass(const clang::CXXRecordDecl *decl, const std::string &name) const;
        MethodLookupOptions makeCreateMethodLookupOptions() const;
        MethodLookupOptions makeVertexShaderMethodLookupOptions() const;
        MethodLookupOptions makeFragmentShaderMethodLookupOptions(const std::optional<clang::QualType> &preferredFirstParamType = std::nullopt) const;
        MethodLookupOptions makeComputeShaderMethodLookupOptions() const;
        MethodLookupOptions makeDomainShaderMethodLookupOptions() const;
        MethodLookupOptions makeHullShaderMethodLookupOptions() const;

        /** Emits a complete namespace body for host C++ generation; shader backends must emit only selected declarations wrapped by lexical namespace scopes. */
        std::string generateNamespaceDefinition(const clang::NamespaceDecl *decl);
        std::string manualTraverseDecl(const clang::Decl *D);


        std::string generateBaseClassInRecordDefinition(const clang::CXXRecordDecl *decl);
        virtual std::string generateFunctionDefinition(const clang::FunctionDecl *func);
        std::string generateFunctionSignature(const clang::FunctionDecl *func, const std::string &funcName, const std::string &constSpecifier = "const", AbstractTypeConvertor *typeConvertor = nullptr);
        virtual std::string generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor = nullptr);

        std::string generateFunctionBody(const clang::FunctionDecl *func);
        std::string generateUGLCTORFunctionBody(const clang::FunctionDecl *func);
        // std::string generateMethodDefinitions(const clang::CXXMethodDecl *decl);
        std::string generateTemplateParameters(const clang::TemplateParameterList *params);
        virtual std::string generateRecordDefinition(const clang::CXXRecordDecl *decl);
        std::string generateTemplateClassDecl(const clang::ClassTemplateDecl *decl);
        std::string generateTemplateFunctionDecl(const clang::FunctionTemplateDecl *decl);
        std::string generateRecordDataMembers(const clang::CXXRecordDecl *decl, AbstractTypeConvertor *typeConvertor = nullptr);
        virtual std::string generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor = nullptr);
        std::string generateDeclArraySpecifier(const clang::QualType &qt);

        // --- 【新增】一个递归辅助函数，用于将嵌套名称限定符翻译成字符串 ---
        std::string translateNestedNameSpecifier(const clang::NestedNameSpecifier *NNS);

        // --- 【核心修正】重写此部分以使用正确的 Clang API ---
        std::string translateTemplateArgument(const clang::TemplateArgument &arg, const AbstractTypeConvertor *typeConvertor = nullptr);

        std::string generateTypeTemplateArgs(const clang::QualType qt);

        std::string generateTypeCanonicalName(const clang::QualType &qt, const AbstractTypeConvertor *typeConvertor = nullptr);
        /** Returns an erased ordinary type name for a concrete template specialization when a backend owns that mapping. */
        virtual std::optional<std::string> getErasedTemplateSpecializationName(const clang::ClassTemplateSpecializationDecl *decl, const AbstractTypeConvertor *typeConvertor = nullptr) const;
        /** Returns the unqualified record name used at a concrete backend definition site. */
        std::string generateRecordDefinitionName(const clang::CXXRecordDecl *decl, const AbstractTypeConvertor *typeConvertor = nullptr);
        // 新增：翻译属性 [[...]]
        std::string generateAttributes(const clang::Decl *D, const AbstractAttributeConvertor *conv = nullptr);
        std::vector<const clang::AnnotateAttr *> getAllAttributes(const clang::Decl *D) const;
        std::string generateAttribute(const clang::AnnotateAttr *A, const AbstractAttributeConvertor *conv = nullptr);
        std::string generateRawAttribute(const clang::AnnotateAttr *A, const AbstractAttributeConvertor *conv = nullptr);
        std::vector<std::string> getAttributeParams(const clang::Decl *D, const std::string &funcName, const AbstractAttributeConvertor *conv = nullptr);


        // --- 【新增】专门处理函数调用时显式模板参数的辅助函数 ---
        std::string generateTemplateCallArguments(const clang::Expr *CalleeExpr);
        std::vector<std::string> generateTemplateCallArgumentsStr(const clang::Expr *CalleeExpr, AbstractTypeConvertor *typeConv = nullptr);

        // --- 语句翻译的具体实现 ---
        std::string translateCompoundStmt(const clang::CompoundStmt *S);

        std::string translateDeclStmt(const clang::DeclStmt *S);

        virtual std::string translateReturnStmt(const clang::ReturnStmt *S);

        // --- 表达式翻译的具体实现 ---
        virtual std::string translateBinaryOperator(const clang::BinaryOperator *E);
        std::string translateExprAsGroupedInfixOperand(const clang::Expr *E);

        virtual std::string translateMemberExpr(const clang::MemberExpr *E);

        virtual std::string translateCallExpr(const clang::CallExpr *E);
        // std::string translateMemberCallExpr(clang::CXXMemberCallExpr *E);
        //  新增：翻译 if-else 语句
        virtual std::string translateIfStmt(const clang::IfStmt *S);

        // 新增：翻译 {} 初始化列表
        std::string translateInitListExpr(const clang::InitListExpr *E);

        // 新增：翻译构造函数调用 (例如 float4(...) )
        std::string translateCXXConstructExpr(const clang::CXXConstructExpr *E);
        std::string translateCXXConstructExprImplicit(const clang::CXXConstructExpr *E);

        std::string translateCXXConstructExprList(const clang::CXXConstructExpr *E);

        virtual std::string translateCXXConstructExprFunction(const clang::CXXConstructExpr *E);
        virtual std::string translateCXXThrowExpr(const clang::CXXThrowExpr *E);

        // --- 【新增】专门处理 C++ 操作符调用的函数 ---
        std::string translateCXXOperatorCallExpr(const clang::CXXOperatorCallExpr *E);
        virtual std::string translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E);
        std::string translateCXXOperatorCallExprArrow(const clang::CXXOperatorCallExpr *E);
        virtual std::string translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *E);
        virtual std::string translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E);


        virtual std::string translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E);
        std::string generateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E, const std::string &callee);

        // --- 新增：处理一元操作符 ---
        virtual std::string translateUnaryOperator(const clang::UnaryOperator *E);
        std::string translateCXXBoolLiteralExpr(const clang::CXXBoolLiteralExpr *E);

        // 【新版本】翻译浮点数字面量
        std::string translateFloatingLiteral(const clang::FloatingLiteral *L);

        // 【新版本】翻译整数字面量
        std::string translateIntegerLiteral(const clang::IntegerLiteral *L);
        // --- 【新增】专门翻译 DeclRefExpr 的辅助函数 ---
        virtual std::string translateDeclRefExpr(const clang::DeclRefExpr *E);
        std::string translateUnresolvedLookupExpr(const clang::UnresolvedLookupExpr *E);
        /** Emits a dependent member expression using the same surface syntax as the template pattern. */
        virtual std::string translateCXXDependentScopeMemberExpr(const clang::CXXDependentScopeMemberExpr *E);
        /** Emits a dependent qualified declaration reference after applying supported template substitutions. */
        virtual std::string translateDependentScopeDeclRefExpr(const clang::DependentScopeDeclRefExpr *E);
        virtual std::string translateVarDecl(const clang::VarDecl *VD);
        std::string translateCXXMemberAccessSpecifier(const clang::AccessSpecifier &as);
        virtual std::string translateCStyleCastExpr(const clang::CStyleCastExpr *E);
        virtual std::string translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *E);

        // --- 【新增】专门翻译字符串字面量的辅助函数 ---
        std::string translateStringLiteral(const clang::StringLiteral *L);

        // --- 【新增】专门翻译 sizeof, alignof 等表达式的辅助函数 ---
        std::string translateUnaryExprOrTypeTraitExpr(const clang::UnaryExprOrTypeTraitExpr *E);

        // --- 【新增】专门翻译函数式类型转换的辅助函数 ---
        virtual std::string translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *E);

        // --- 【新增】专门翻译三元条件运算符的辅助函数 ---
        std::string translateConditionalOperator(const clang::ConditionalOperator *E);

        // --- 【新增】专门翻译 for 循环的辅助函数 ---
        std::string translateForStmt(const clang::ForStmt *S);
        // --- 【新增】专门翻译原生数组下标操作的辅助函数 ---
        virtual std::string translateArraySubscriptExpr(const clang::ArraySubscriptExpr *E);
        // --- 【新增】专门翻译 break 语句的辅助函数 ---
        std::string translateBreakStmt(const clang::BreakStmt *S);
        std::string translateContinueStmt(const clang::ContinueStmt *S);
        std::string translateWhileStmt(const clang::WhileStmt *S);
        std::string translateDoStmt(const clang::DoStmt *S);

        // --- 【新增】专门翻译 offsetof 表达式的辅助函数 ---
        std::string translateOffsetOfExpr(const clang::OffsetOfExpr *E);

        // --- 【新增】统一处理 typedef 和 using 的 Visit 方法 ---
        std::string VisitTypedefNameDecl(const clang::TypedefNameDecl *decl);

        std::string translateSwitchStmt(const clang::SwitchStmt *S);
        std::string translateCaseStmt(const clang::CaseStmt *S);
        std::string translateDefaultStmt(const clang::DefaultStmt *S);

        std::string GetAlignasSourceString(const clang::Decl *D);
        std::string makeUnsupportedPlaceholder(const std::string &category, const std::string &kindName) const;
        std::optional<std::string> tryBuildStructuredBufferWriteDiagnostic(const clang::Expr *expr) const;
        std::optional<std::string> tryBuildRecoveryExprDiagnostic(const clang::RecoveryExpr *expr) const;
        bool methodHasAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const;
        bool methodHasParameterAttribute(const clang::CXXMethodDecl *method, const std::string &attributeName) const;
        bool methodMatchesLookupOptions(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const;
        int scoreMethodLookupCandidate(const clang::CXXMethodDecl *method, const MethodLookupOptions &options) const;
        std::string describeMethodOverload(const clang::CXXMethodDecl *method) const;
    };


} // namespace UGLC::CodeGen
