#pragma once

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Legacy/HLSL/HLSLAggregateInitializerEmitter.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRecordEmitter.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRenderInterfaceValidator.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRenderSetEmitter.hpp>
#include <CodeGen/Legacy/HLSL/HLSLResourceBindingEmitter.hpp>
#include <CodeGen/Legacy/HLSL/HLSLShaderBuiltinTranslator.hpp>
#include <CodeGen/Legacy/HLSL/HLSLTextureMemberCallLowering.hpp>
#include <CodeGen/Legacy/HLSL/HLSLVisitorLocalStorage.hpp>
#include <CodeGen/Legacy/HLSL/HLSLTypeConvertor.hpp>
#include <CodeGen/RenderSetLayoutInfo.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace UGLC::CodeGen::HLSL
{
    class HLSLVisitor final : public BaseASTVisitor, public IShaderSourceEmitter
    {
    public:
        explicit HLSLVisitor(clang::ASTContext *context);

        EmittedShaderSource generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                           const BindGroupInfoMap &bindGroupInfoMap,
                                           const clang::CXXRecordDecl *shaderClassDecl,
                                           const clang::FunctionDecl *entryFunction = nullptr,
                                           const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) override;

    private:
        HLSLTypeConvertor mTypeConvertor;
        HLSLVisitorLocalStorage mLocalStorage;
        HLSLAggregateInitializerEmitter mAggregateInitializer;
        HLSLResourceBindingEmitter mResourceBindingEmitter;
        HLSLShaderBuiltinTranslator mShaderBuiltinTranslator;
        HLSLTextureMemberCallLowering mTextureMemberCallLowering;
        HLSLRenderSetEmitter mRenderSetEmitter;
        HLSLRenderInterfaceValidator mRenderInterfaceValidator;
        HLSLRecordEmitter mRecordEmitter;
        BindGroupInfoMap mBindGroupInfoMap;
        const clang::FunctionDecl *mMainFunc = nullptr;
        const clang::FunctionDecl *mCurrentFunctionDecl = nullptr;
        std::unordered_map<const clang::ValueDecl *, std::string> mLocalGroupSharedGlobals;
        std::unordered_map<const clang::ValueDecl *, std::string> mRenderSetParameterGlobalAliases;
        std::vector<std::string> mPendingGroupSharedGlobalDeclarations;
        std::unordered_set<std::string> mPendingGroupSharedGlobalNames;
        /** Tracks HLSL record forward declarations already emitted for the current shader artifact. */
        std::unordered_set<std::string> mEmittedRecordForwardDeclarationNames;
        /** Tracks HLSL record definitions already emitted for the current shader artifact. */
        std::unordered_set<std::string> mEmittedRecordDefinitionNames;
        int mPixelLocalInputDescriptorSetIndex = 0;
        const clang::CXXRecordDecl *mShaderClassDecl = nullptr;
        bool mUsePixelLocalFramebufferOutputFilter = false;
        std::unordered_set<std::string> mPixelLocalFramebufferOutputFields;
        /** Describes one generated HLSL helper variant where RenderSet parameters are rebound to global resources. */
        struct RenderSetSpecializedFunctionRequest
        {
            const clang::FunctionDecl *functionDecl = nullptr;
            std::vector<std::string> renderSetAliases;
            std::string key;
            std::string functionName;
            std::string qualifiedCallName;
        };
        std::vector<RenderSetSpecializedFunctionRequest> mPendingRenderSetSpecializedFunctionRequests;
        std::unordered_set<std::string> mQueuedRenderSetSpecializedFunctionKeys;
        std::unordered_set<std::string> mEmittedRenderSetSpecializedFunctionKeys;
        std::vector<std::string> mRenderSetSpecializedFunctionPrototypes;
        std::vector<std::string> mRenderSetSpecializedFunctionDefinitions;

        bool checkIsShaderFunction(const clang::FunctionDecl *shaderFunc) const;
        int resolvePixelLocalInputDescriptorSetIndex() const;
        bool checkCXXRecord(const clang::CXXRecordDecl *decl);
        std::string generateRecordDefinition(const clang::CXXRecordDecl *decl) override;
        std::string generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor = nullptr) override;
        std::string generateFunctionDefinition(const clang::FunctionDecl *func) override;
        std::string generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor = nullptr) override;
        /** Returns true when a helper function has at least one UGL::RenderSet<T> parameter that needs HLSL global-resource rebinding. */
        bool functionHasRenderSetParameter(const clang::FunctionDecl *func) const;
        /** Returns true when a helper function signature needs a generated local BindGroup handle type. */
        bool functionHasBindGroupHandleParameter(const clang::FunctionDecl *func) const;
        /** Validates that a RenderSet helper parameter is input-only before HLSL rebinds it to global resources. */
        void validateRenderSetHelperParameterContract(const clang::ParmVarDecl *param) const;
        std::string translateDeclRefExpr(const clang::DeclRefExpr *E) override;
        std::string translateBinaryOperator(const clang::BinaryOperator *E) override;
        std::string translateReturnStmt(const clang::ReturnStmt *S) override;
        std::string translateMemberExpr(const clang::MemberExpr *E) override;
        /** Lowers dependent member syntax that still refers to concrete bind-group resources after template erasure. */
        std::string translateCXXDependentScopeMemberExpr(const clang::CXXDependentScopeMemberExpr *E) override;
        std::string translateCallExpr(const clang::CallExpr *E) override;
        std::string translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E) override;
        std::string translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E) override;
        std::optional<std::string> tryTranslateMatrixElementSubscriptLValue(const clang::Expr *expr);
        std::string translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E) override;
        std::string translateCXXConstructExprFunction(const clang::CXXConstructExpr *E) override;
        std::string translateCXXThrowExpr(const clang::CXXThrowExpr *E) override;
        std::string translateCXXFunctionalCastExpr(const clang::CXXFunctionalCastExpr *E) override;
        std::string translateVarDecl(const clang::VarDecl *VD) override;
        /** Emits a function prototype that matches HLSL helper signature lowering. */
        std::string generateStandaloneFunctionPrototype(const clang::FunctionDecl *func, const std::string &funcName);
        /** Emits an HLSL record forward declaration so helper prototypes can reference records before full method bodies are emitted. */
        std::string generateRecordForwardDeclaration(const clang::CXXRecordDecl *decl);
        std::string generateMainFunction(const clang::FunctionDecl *shaderFunc);
        std::string generateStandaloneFunctionSignature(const clang::FunctionDecl *func, const std::string &funcName, AbstractTypeConvertor *typeConvertor = nullptr);
        /** Emits a BindGroup handle parameter when the DSL parameter uses UGL::BindGroup<T>, or returns nullopt for other parameter types. */
        std::optional<std::string> tryGenerateBindGroupHandleParameter(const clang::ParmVarDecl *param);
        /** Collects concrete BindGroup records that appear in helper signatures or helper fields. */
        std::vector<const clang::CXXRecordDecl *> collectBindGroupHandleRecords(const std::vector<const clang::Decl *> &shaderDefs);
        /** Adds one concrete BindGroup record to a helper collection if it has not been added yet. */
        void addBindGroupHandleRecord(std::vector<const clang::CXXRecordDecl *> &records,
                                      std::unordered_set<const clang::Decl *> &seenRecords,
                                      const clang::CXXRecordDecl *recordDecl);
        /** Adds one BindGroup<T> type to a helper collection when the type is concrete. */
        void addBindGroupHandleType(std::vector<const clang::CXXRecordDecl *> &records,
                                    std::unordered_set<const clang::Decl *> &seenRecords,
                                    const clang::QualType &type);
        /** Adds every BindGroup<T> field or method parameter used by one record declaration. */
        void collectBindGroupHandleRecordsFromRecord(std::vector<const clang::CXXRecordDecl *> &records,
                                                     std::unordered_set<const clang::Decl *> &seenRecords,
                                                     const clang::CXXRecordDecl *recordDecl);
        /** Emits uniform wrapper structs needed by flat descriptors and local BindGroup handle structs. */
        std::string generateBindGroupUniformWrapperDefinitions(const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls);
        /** Emits HLSL local BindGroup handle struct definitions for this shader artifact. */
        std::string generateBindGroupHandleStructDefinitions(const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls);
        std::string generateBindGroupResourceDeclarations();
        /** Emits local BindGroup handle variables initialized from flat descriptor globals for a shader entry body. */
        std::string generateBindGroupHandleMaterializations();
        struct ResourceFieldAccess
        {
            const clang::FieldDecl *fieldDecl = nullptr;
            const clang::Expr *ownerExpr = nullptr;
        };
        std::optional<ResourceFieldAccess> tryResolveResourceFieldAccess(const clang::MemberExpr *expr, std::string_view expectedOwnerTypeName) const;
        /** Resolves `BindGroup<T>` dependent member syntax to a local HLSL handle member access. */
        std::optional<std::string> tryTranslateDependentBindGroupResourceAccess(const clang::CXXDependentScopeMemberExpr *expr);
        /** Lowers dependent texture member calls whose texture object comes from a BindGroup handle field. */
        std::optional<std::string> tryTranslateDependentTextureMemberCall(const clang::CallExpr *expr, const clang::CXXDependentScopeMemberExpr *calleeExpr);
        /** Translates one dependent texture method argument through the HLSL visitor. */
        std::string translateDependentTextureArgument(const clang::CallExpr *expr, unsigned argIndex);
        /** Translates one dependent texture method argument or returns a fallback when the argument is omitted/defaulted. */
        std::string translateOptionalDependentTextureArgument(const clang::CallExpr *expr, unsigned argIndex, std::string_view fallbackValue);
        /** Appends one optional dependent texture method argument when the DSL call explicitly provides it. */
        void appendOptionalDependentTextureArgument(std::string &result, const clang::CallExpr *expr, unsigned argIndex);
        /** Lowers dependent UniformBuffer member calls whose buffer object comes from a BindGroup handle field. */
        std::optional<std::string> tryTranslateDependentUniformBufferMemberCall(const clang::CallExpr *expr, const clang::CXXDependentScopeMemberExpr *calleeExpr);
        std::optional<std::string> tryTranslateBindGroupResourceAccess(const clang::MemberExpr *E);
        std::optional<std::string> tryTranslateRenderSetResourceAccess(const clang::MemberExpr *E) const;
        std::string translateNestedNameSpecifierToHLSL(const clang::NestedNameSpecifier *nestedNameSpecifier);
        std::string extractBindGroupInstanceName(const clang::Expr *expr) const;
        /** Returns true when a DSL record must be emitted as a shader-resource behavior type. */
        bool isShaderResourceBehaviorRecordDefinition(const clang::CXXRecordDecl *recordDecl) const;
        /** Resolves a RenderSet expression to the global resource alias used by HLSL helper rebinding. */
        std::string extractRenderSetGlobalAliasName(const clang::Expr *expr) const;
        std::array<std::string, 3> getLocalWorkgroupSize(const clang::CXXRecordDecl *shaderClassDecl) const;
        static bool isDefaultArgumentExpr(const clang::Expr *expr);
        static std::string stripMemberCallSuffix(const std::string &callee, const std::string &suffix);
        std::optional<clang::QualType> tryGetCallParameterType(const clang::CallExpr *expr, unsigned argIndex) const;
        std::vector<std::string> translateCallArgument(const clang::CallExpr *expr, unsigned argIndex);
        std::vector<std::string> collectTranslatedCallArguments(const clang::CallExpr *expr);
        /** Rewrites a helper call with RenderSet parameters to a generated global-resource helper variant when required. */
        std::optional<std::string> tryTranslateRenderSetSpecializedCall(const clang::CallExpr *expr);
        /** Builds the stable global-resource rebinding request for one helper call and its concrete RenderSet aliases. */
        RenderSetSpecializedFunctionRequest makeRenderSetSpecializedFunctionRequest(const clang::CallExpr *expr, const clang::FunctionDecl *calleeDecl);
        /** Queues a RenderSet helper variant unless an equivalent rebinding already exists. */
        void queueRenderSetSpecializedFunction(const RenderSetSpecializedFunctionRequest &request);
        /** Emits the signature for a generated RenderSet helper variant with RenderSet parameters omitted. */
        std::string generateRenderSetSpecializedFunctionSignature(const RenderSetSpecializedFunctionRequest &request, bool emitSemicolon);
        /** Materializes queued RenderSet helper variants after call sites have been translated. */
        void processPendingRenderSetSpecializedFunctions();
        std::optional<std::string> tryTranslateUniformBufferArrowCall(const clang::CXXMemberCallExpr *expr, const clang::Expr *strippedBaseObjectExpr);
        std::optional<std::string> tryTranslateUniformBufferDataPackerCall(const clang::CXXMemberCallExpr *expr, const std::string &currentObjectExpr, const std::string &calleeTypeName, const std::string &methodName);
        clang::QualType unwrapArrayElementType(clang::QualType type) const;
        /** Builds the generated HLSL pixel-local input struct name for a framebuffer record. */
        std::string makePixelLocalInputStructName(const clang::CXXRecordDecl *recordDecl) const;
        /** Emits the generated record used to pass SubpassInput values into the user pixel-local entry body. */
        std::string generatePixelLocalInputStructDefinition(const clang::CXXRecordDecl *recordDecl);
        /** Configures framebuffer output narrowing for one pixel-local HLSL entry. */
        void configurePixelLocalFramebufferOutputFilter(const clang::FunctionDecl *shaderFunc);
    };
} // namespace UGLC::CodeGen::HLSL
