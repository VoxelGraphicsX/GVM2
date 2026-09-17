#pragma once
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/RenderSetLayoutInfo.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <CodeGen/Legacy/MSL/MSLAttributeConvertor.hpp>
#include <CodeGen/Legacy/MSL/MSLShaderBuiltinTranslator.hpp>
#include <CodeGen/Legacy/MSL/MSLTypeConvertor.hpp>
#include <CodeGen/Legacy/MSL/MSLWaveBuiltinAnalyzer.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <CodeGen/ShaderBindGroupInfo.hpp>
#include <CodeGen/UGLC.Constants.hpp>
namespace UGLC::CodeGen::MSL
{

    class MSLVisitor final : public BaseASTVisitor, public IShaderSourceEmitter
    {
        MSLTypeConvertor mTypeConvertor;
        MSLAttributeConvertor mAttriConv;
        BindGroupInfoMap mBindGroupInfoMap;
        std::unordered_set<const clang::CXXRecordDecl *> mRenderVaryingRecordDecls;
        const clang::FunctionDecl *mMainFunc = nullptr;
        // Tracks the function body currently being emitted so call lowering can
        // resolve which hidden wave builtin values are available in scope.
        const clang::FunctionDecl *mCurrentCodegenFunction = nullptr;
        // Computes and stores transitive hidden Metal wave-builtin requirements.
        MSLWaveBuiltinAnalyzer mWaveBuiltinAnalyzer;
        MSLShaderBuiltinTranslator mShaderBuiltinTranslator;
        std::unordered_set<const clang::FieldDecl *> mAtomicPlainFields;
        std::unordered_set<const clang::FieldDecl *> mAtomicStructuredBufferFields;
        std::unordered_set<const clang::VarDecl *> mAtomicStructuredBufferVariables;
        std::unordered_set<std::string> mAtomicStructuredBufferParameterIDs;
        std::unordered_set<const clang::VarDecl *> mAtomicGroupSharedVariables;
        bool mTranslateAtomicLValueRaw = false;

    public:
        explicit MSLVisitor(clang::ASTContext *Context);
        EmittedShaderSource generateShader(const std::vector<const clang::Decl *> &shaderDefs,
                                           const BindGroupInfoMap &bindGroupInfoMap,
                                           const clang::CXXRecordDecl *shaderClassDecl,
                                           const clang::FunctionDecl *entryFunction = nullptr,
                                           const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr) override;
        bool checkIsShaderFunction(const clang::FunctionDecl *shaderFunc) const;
        std::string generateMainFunction(const clang::FunctionDecl *shaderFunc);


    private:
        int mBindgroupOffset = 0;
        const std::string mUGLRenderSetComponentListVariableNamePostfix = "ComponentList";
        const std::string mUGLRenderSetAccessBoundDataVariableName = "RenderSetAccessBoundData";
        const std::string mUGLRenderSetRenderEntityInfoVariableName = "RenderEntityInfo";
        const std::string mUGLRenderSetRenderEntityCMDParamsVariableName = "RenderEntityCMDParams";

        bool checkCXXRecord(const clang::CXXRecordDecl *decl);
        virtual std::string generateRecordDefinition(const clang::CXXRecordDecl *decl) override;
        /** Emits a Metal forward declaration for a selected shader record so helper prototypes can appear before full method bodies. */
        std::string generateRecordForwardDeclaration(const clang::CXXRecordDecl *decl);
        std::string generateRecordDefinitionDetailed(const clang::CXXRecordDecl *decl);
        std::string generateUGLFramebufferClass(const clang::CXXRecordDecl *decl);
        std::string generateUGLRenderSetClass(const clang::CXXRecordDecl *decl);
        std::string generateUGLBindGroupClass(const clang::CXXRecordDecl *decl);
        /** Emits bounds-hardened Metal accessors for one RenderSet buffer component. */
        std::string generateRenderSetBufferComponentMethods(const std::string &varName,
                                                            const std::string &returnTypeName,
                                                            const std::string &accessBoundExpr,
                                                            const std::string &addressQualifier);
        std::string generateRenderSetTextureComponentMethods(const std::string &varName,
                                                             const std::string &elementTypeName,
                                                             const std::string &accessBoundExpr,
                                                             const std::string &addressQualifier);
        std::string generateRenderSetEntityMethods(const std::string &addressQualifier);
        /** Scans shader code for atomic builtin targets so Metal-only atomic layout can be inferred from plain DSL types. */
        void analyzeAtomicResourceUsage(const std::vector<const clang::Decl *> &shaderDefs, const clang::CXXRecordDecl *shaderClassDecl);
        /** Recursively scans a declaration for atomic builtin calls that target resources or groupshared variables. */
        void analyzeAtomicDecl(const clang::Decl *decl);
        /** Recursively scans a statement for atomic builtin calls that target resources or groupshared variables. */
        void analyzeAtomicStmt(const clang::Stmt *stmt);
        /** Records the resource, struct field, or groupshared variable reached by an atomic builtin target expression. */
        void recordAtomicTargetExpression(const clang::Expr *expr);
        /** Records a structured-buffer field or variable whose scalar elements require Metal atomic pointer storage. */
        void recordAtomicStructuredBufferBaseExpression(const clang::Expr *expr);
        /** Propagates callee atomic structured-buffer parameter requirements back to helper call arguments. */
        void propagateAtomicStructuredBufferCallArguments(const clang::CallExpr *callExpr);
        /** Returns true when a field was inferred as requiring Metal atomic storage. */
        bool isAtomicPlainField(const clang::FieldDecl *field) const;
        /** Returns true when a bind-group structured-buffer field stores scalar elements used by atomic operations. */
        bool isAtomicStructuredBufferField(const clang::FieldDecl *field) const;
        /** Returns true when a structured-buffer variable or parameter stores scalar elements used by atomic operations. */
        bool isAtomicStructuredBufferVariable(const clang::VarDecl *decl) const;
        /** Builds a stable function-parameter key for atomic structured-buffer propagation across Clang redeclaration objects. */
        std::optional<std::string> makeAtomicStructuredBufferParameterID(const clang::ParmVarDecl *param) const;
        /** Marks a structured-buffer variable or parameter as requiring Metal atomic pointer storage when its element type supports atomic operations. */
        bool recordAtomicStructuredBufferVariableIfSupported(const clang::VarDecl *decl);
        /** Returns true when a local groupshared variable stores scalar elements used by atomic operations. */
        bool isAtomicGroupSharedVariable(const clang::VarDecl *decl) const;
        /** Marks a scalar groupshared variable as requiring Metal atomic storage when its element type supports atomic operations. */
        bool recordAtomicGroupSharedVariableIfSupported(const clang::VarDecl *decl);
        /** Builds a Metal atomic scalar type name from a DSL scalar type when the target type is supported. */
        std::optional<std::string> makeAtomicScalarTypeName(const clang::QualType &type);
        /** Builds the Metal pointer type for a scalar structured-buffer type whose elements are used atomically. */
        std::optional<std::string> makeAtomicStructuredBufferPointerTypeName(const clang::QualType &type);
        /** Builds the Metal pointer type for a scalar structured-buffer binding whose elements are used atomically. */
        std::optional<std::string> makeAtomicStructuredBufferPointerTypeName(const clang::FieldDecl *field);
        /** Builds the Metal pointer type for a scalar structured-buffer variable whose elements are used atomically. */
        std::optional<std::string> makeAtomicStructuredBufferPointerTypeName(const clang::VarDecl *decl);
        /** Builds the Metal declaration type for a groupshared scalar variable whose elements are used atomically. */
        std::optional<std::string> makeAtomicGroupSharedVariableTypeName(const clang::VarDecl *decl);
        /** Builds the plain scalar type used when ordinary operations access backend atomic storage. */
        std::optional<std::string> makeAtomicPlainValueTypeName(const clang::QualType &type);
        /** Builds the Metal pointer type for a backend atomic lvalue expression. */
        std::optional<std::string> makeAtomicBackedLValuePointerTypeName(const clang::Expr *expr);
        /** Returns true when an expression names storage that was lowered to a Metal atomic type. */
        bool isAtomicBackedLValue(const clang::Expr *expr);
        /** Translates an atomic-backed expression without converting it to an ordinary loaded value. */
        std::string translateAtomicBackedLValueRaw(const clang::Expr *expr);
        /** Translates an atomic-backed expression as an ordinary relaxed load. */
        std::string translateAtomicBackedLValueLoad(const clang::Expr *expr);
        /** Translates a nested expression while selecting raw or ordinary atomic-backed lvalue behavior. */
        std::string translateExprWithAtomicLValueMode(const clang::Expr *expr, bool rawAtomicLValue);
        /** Lowers ordinary compound writes to backend atomic storage as relaxed load, operation, and store. */
        std::string translateAtomicBackedCompoundAssignment(const clang::Expr *lhsExpr, const clang::Expr *rhsExpr, clang::SourceLocation location, std::string_view operationSpelling);
        /** Lowers ordinary binary compound assignments to backend atomic storage as relaxed load, operation, and store. */
        std::string translateAtomicBackedCompoundAssignment(const clang::BinaryOperator *expr, std::string_view operationSpelling);
        /** Lowers ordinary increment and decrement on backend atomic storage as relaxed load, operation, and store. */
        std::string translateAtomicBackedIncrement(const clang::UnaryOperator *expr, std::string_view operationSpelling);
        void collectRenderVaryingRecordDecls(const clang::CXXRecordDecl *shaderClassDecl, const clang::FunctionDecl *entryFunction);
        void registerRenderVaryingRecordDecl(const clang::CXXRecordDecl *recordDecl);
        bool isRenderVaryingRecordDecl(const clang::CXXRecordDecl *recordDecl) const;
        std::string getRenderVaryingFieldAttributes(const clang::FieldDecl *field) const;
        /** Lowers ordinary assignments to inferred Metal atomic storage without changing DSL atomic semantics. */
        virtual std::string translateBinaryOperator(const clang::BinaryOperator *E) override;
        /** Lowers ordinary increment and address-of diagnostics for inferred Metal atomic storage. */
        virtual std::string translateUnaryOperator(const clang::UnaryOperator *E) override;
        /** Handles overloaded assignment operators used by DSL wrapper types that are backed by Metal atomics. */
        virtual std::string translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E) override;
        /** Converts ordinary reads from inferred Metal atomic fields into relaxed atomic loads. */
        virtual std::string translateMemberExpr(const clang::MemberExpr *E) override;
        /** Converts ordinary reads from inferred Metal atomic variables into relaxed atomic loads. */
        virtual std::string translateDeclRefExpr(const clang::DeclRefExpr *E) override;
        virtual std::string translateCallExpr(const clang::CallExpr *E) override;
        std::string getShaderResourceAddressSpaceQualifier(const clang::QualType &qt) const;
        virtual std::string generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor) override;
        virtual std::string generateFunctionDefinition(const clang::FunctionDecl *func) override;


        virtual std::string generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor = nullptr) override;

        virtual std::string translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E) override;
        virtual std::string translateReturnStmt(const clang::ReturnStmt *S) override;
        /** Converts ordinary reads from inferred Metal atomic native array elements into relaxed atomic loads. */
        virtual std::string translateArraySubscriptExpr(const clang::ArraySubscriptExpr *E) override;
        /** Converts ordinary reads from inferred Metal atomic structured-buffer elements into relaxed atomic loads. */
        virtual std::string translateCXXOperatorCallExprArraySubScript(const clang::CXXOperatorCallExpr *E) override;
        virtual std::string translateCXXThrowExpr(const clang::CXXThrowExpr *E) override;
        std::optional<std::string> tryTranslateTexture2DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                   const std::string &textureExpr,
                                                                   const std::string &methodName);
        std::optional<std::string> tryTranslateTexture2DArrayMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                        const std::string &textureExpr,
                                                                        const std::string &methodName);
        /** Lowers methods on `UGL::Texture3D<T>` and `UGL::RWTexture3D<T>` to Metal texture3d calls. */
        std::optional<std::string> tryTranslateTexture3DMemberCall(const clang::CXXMemberCallExpr *expr,
                                                                   const std::string &textureExpr,
                                                                   const std::string &methodName,
                                                                   bool isReadWriteTexture);
        /** Translates one Metal texture method argument through the owning visitor. */
        std::string translateTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex);
        /** Translates one Metal texture method argument or returns a fallback when the argument is omitted/defaulted. */
        std::string translateOptionalTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex, std::string_view fallbackValue);


        virtual std::string translateVarDecl(const clang::VarDecl *VD) override;
        void validateBindGroupBufferBindings(const clang::CXXRecordDecl *shaderClassDecl) const;
        int getValidatedVertexInputAttributeLocation(const clang::FieldDecl *field,
                                                     const std::string &renderClassName,
                                                     const std::string &vertexInputTypeName) const;
        void validateVertexInputRecordOrThrow(const clang::CXXRecordDecl *recordDecl,
                                              const std::string &renderClassName) const;
        void validateRenderVaryingRecordOrThrow(const clang::CXXRecordDecl *recordDecl,
                                                const std::string &renderClassName,
                                                const std::string &recordRole,
                                                bool requirePosition = false) const;
        void validateShaderEntryBuiltinParametersOrThrow(const clang::FunctionDecl *shaderFunc) const;
        void validateRenderEntityBuiltinContractOrThrow(const clang::FunctionDecl *shaderFunc) const;
        std::string generateFunctionSignatureWithWaveBuiltins(const clang::FunctionDecl *func, const std::string &funcName, const std::string &constSpecifierValue);
        /** Builds a forward declaration that matches the generated Metal helper signature, including hidden wave builtin parameters. */
        std::string generateFunctionPrototypeWithWaveBuiltins(const clang::FunctionDecl *func);
        /** Builds the generated pixel-local input struct name for a framebuffer record. */
        std::string makePixelLocalInputStructName(const clang::CXXRecordDecl *recordDecl) const;
        /** Emits a plain struct used to pass current-pixel attachment values to the user pixel() body. */
        std::string generatePixelLocalInputStructDefinition(const clang::CXXRecordDecl *recordDecl);
    };

} // namespace UGLC::CodeGen::MSL
