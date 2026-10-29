#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>
#include <CodeGen/RenderSetMemberCallUtils.hpp>
#include <CodeGen/IntegerConstantExpressionUtils.hpp>
#include <CodeGen/PixelLocalFieldAnalysis.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>
#include <unordered_map>

#include <optional>
#include <string>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    /** Type-name spelling, parsing, and classification helpers shared by the UGLIR lowering passes.
        None of them read lowering state; every function is a pure string/AST-type utility. */
    namespace TypeSpelling
    {
        /** Classifies a call on a type-erased buffer, texture, or draw-data component so lowering can expand it against the selected resource. */
        enum class ErasedResourceCallKind
        {
            None,
            BufferGetRaw,
            BufferCheckValid,
            BufferGet,
            TextureSelect,
            DrawInfoStoreFields,
            DrawCommandParamsStoreFields,
            DrawInfoIndexCount,
            DrawInfoInstanceCount,
            DrawInfoFirstIndex,
            DrawInfoVertexOffset,
            DrawInfoGlobalInstanceBase,
            DrawInfoVersion,
            DrawInfoCheckValid
        };

        /** Returns true when the current UGLIR entry can legally perform fragment-kill operations. */
        bool isFragmentKillCapableEntryKind(ShaderEntryKind entryKind);
        /** Returns a diagnostic-facing description for a UGLIR shader entry kind. */
        const char *describeEntryKindForDiagnostic(ShaderEntryKind entryKind);
        /** Returns the source spelling used in diagnostics for fragment-kill intrinsics. */
        const char *getFragmentKillIntrinsicDiagnosticName(IntrinsicCallKind intrinsicCallKind);
        /** Returns the stable builtin stem encoded in a lowered UGL call name. */
        std::string makeUGLIntrinsicStem(const std::string &functionName);
        /** Converts a public texture DSL method name into a structured UGLIR intrinsic tag. */
        IntrinsicCallKind classifyTextureIntrinsicCallKind(const std::string &methodName);
        /** Returns the structured intrinsic tag for a lowered call name when one is known. */
        IntrinsicCallKind classifyCallIntrinsicCallKind(const std::string &functionName);
        /** Returns an initializer expression with syntactic wrappers removed for framebuffer default-initializer checks. */
        const clang::Expr *stripDefaultInitializerWrappers(const clang::Expr *expr);
        /** Returns true when an initializer only contains compiler-expanded default construction for a framebuffer record. */
        bool isOmissibleFramebufferDefaultInitializer(const clang::Expr *expr);
        /** Returns the record definition when Clang has one available. */
        const clang::CXXRecordDecl *recordDefinition(const clang::CXXRecordDecl *recordDecl);
        /** Removes statement wrappers with no execution scope while preserving compound statement boundaries. */
        const clang::Stmt *stripNonSemanticStatementWrappers(const clang::Stmt *stmt);
        /** Returns concrete record fields or the template pattern fields for implicit specializations without instantiated fields. */
        std::vector<const clang::FieldDecl *> collectRecordFields(const clang::CXXRecordDecl *recordDecl);
        /** Returns true when a record instance carries no shader-visible state and can be omitted from helper calls. */
        bool isStatelessRecordType(clang::QualType type);
        /** Returns true when a string starts with the requested prefix. */
        bool startsWith(const std::string &value, const std::string &prefix);
        /** Returns true when a string contains the requested path fragment. */
        bool containsPathFragment(const std::string &value, const std::string &fragment);
        /** Returns a copy of the string with leading and trailing whitespace removed. */
        std::string trimCopy(const std::string &input);
        /** Returns true when an annotation spells the requested function-style attribute. */
        bool isFunctionStyleAttributeName(const std::string &rawAttribute, const std::string &attributeName);
        /** Splits a function-style annotation argument list into trimmed parameter strings. */
        std::vector<std::string> splitAttributeParameters(const std::string &rawAttribute);
        /** Returns all clang annotate strings attached to a declaration. */
        std::vector<std::string> getAnnotationStrings(const clang::Decl *decl);
        /** Returns true when a raw annotation is exactly an indexed attribute such as Slot0. */
        bool isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName);
        /** Returns the integer suffix from an exact indexed attribute or -1 when it is not present. */
        int getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName);
        /** Returns a compact type spelling by stripping common C++ elaboration prefixes. */
        std::string stripElaboratedPrefix(std::string name);
        /** Returns a stable type spelling suitable for UGLIR names and diagnostics. */
        std::string typeSpelling(const clang::QualType &type);
        /** Returns a stable qualified name for a named declaration. */
        std::string qualifiedName(const clang::NamedDecl *decl);
        /** Normalizes libc++ inline namespace spellings into stable std:: names. */
        std::string normalizeStdName(std::string name);
        /** Returns true when a type spelling names a supported UGL vector alias. */
        bool parseVectorAlias(const std::string &name, std::string &elementType, uint32_t &width);
        /** Scalar kind, element type, dimensions, and canonical IR name parsed from a UGL matrix alias or template spelling. */
        struct ParsedMatrixAlias
        {
            ScalarKind scalarKind = ScalarKind::None;
            std::string elementType;
            uint32_t rows = 0;
            uint32_t columns = 0;
            std::string canonicalName;
        };
        /** Returns true when a type spelling names a supported compact UGL matrix alias. */
        bool parseMatrixAlias(const std::string &name, ParsedMatrixAlias &result);
        /** Splits a simple template argument list while preserving nested template arguments. */
        std::vector<std::string> splitTemplateArgumentList(const std::string &argumentText);
        /** Returns the scalar category represented by a public UGL scalar template argument spelling. */
        ScalarKind scalarKindFromUGLTemplateScalarSpelling(std::string scalarName);
        /** Returns the UGLIR scalar element spelling for a parsed compact shader scalar kind. */
        std::string parsedScalarIRTypeName(ScalarKind kind);
        /** Returns the compact public scalar alias spelling for a parsed compact shader scalar kind. */
        std::string parsedScalarAliasName(ScalarKind kind);
        /** Returns true when a type spelling names a supported UGL Matrix<T, R, C> specialization. */
        bool parseMatrixTemplateSpelling(const std::string &name, ParsedMatrixAlias &result);
        /** Returns the UGLIR type record with the requested name when it is already present in a module. */
        const Type *findLoweredTypeByName(const Module &module, const std::string &typeName);
        /** Returns true when a lowered expression type is the UGL workgroup-storage wrapper. */
        bool isWorkgroupWrapperTypeName(const Module &module, const std::string &typeName);
        /** Returns the bit width used for one scalar value category in UGLIR type metadata. */
        uint32_t bitWidthForScalarKind(ScalarKind kind);
        /** Returns the scalar UGLIR element type name used inside vector and matrix type records. */
        std::string scalarIRTypeName(ScalarKind kind);
        /** Describes a scalar or vector value type as a scalar component type and component count. */
        bool describeConstructValueShape(const Module &module,
                                         const std::string &typeName,
                                         ScalarKind &scalarKind,
                                         uint32_t &componentCount);
        /** Returns true when a member name is a scalar/vector component swizzle. */
        bool isVectorSwizzleName(const std::string &memberName);
        /** Creates a UGLIR unsigned integer literal expression. */
        Expression makeUIntLiteralExpression(uint32_t value, SourceLocation location);
        /** Creates a reflected resource declaration reference expression. */
        Expression makeResourceDeclRefExpression(const ResourceBinding &resource, std::string typeName = "resource_selector");
        /** Creates a UGLIR member reference expression. */
        Expression makeMemberRefExpression(Expression base, std::string memberName, std::string typeName, SourceLocation location);
        /** Creates a UGLIR subscript expression. */
        Expression makeSubscriptExpression(Expression base, Expression index, std::string typeName, SourceLocation location);
        /** Creates a UGLIR binary expression. */
        Expression makeBinaryExpression(std::string operatorName, Expression lhs, Expression rhs, std::string typeName, SourceLocation location);
        /** Creates a UGLIR conditional expression. */
        Expression makeConditionalExpression(Expression condition, Expression trueValue, Expression falseValue, std::string typeName, SourceLocation location);
        /** Creates a UGLIR store expression. */
        Expression makeStoreExpression(Expression destination, Expression value, SourceLocation location);
        /** Creates a UGLIR call expression for a generic math helper. */
        Expression makeGenericCallExpression(std::string name, std::string typeName, std::vector<Expression> operands, SourceLocation location);
        /** Creates a UGLIR expression that clamps an unsigned index to one less than an exclusive bound. */
        Expression makeSafeUnsignedIndexExpression(Expression index, Expression exclusiveBound, SourceLocation location);
        /** Creates a UGLIR expression that combines two boolean expressions with logical and. */
        Expression makeBoolAndExpression(Expression lhs, Expression rhs, SourceLocation location);
        /** Creates a UGLIR expression statement that evaluates one expression. */
        Statement makeExpressionStatement(Expression expression);
        /** Creates an explicit scalar conversion expression used by lowered internal index arithmetic. */
        Expression makeScalarConvertExpression(Expression value, std::string targetType, ScalarKind targetScalarKind, SourceLocation location);
        /** Creates an explicit unsigned conversion expression for internal index arithmetic. */
        Expression makeUIntExpression(Expression value, SourceLocation location);
        /** Finds one reflected resource by its UGLIR resource name. */
        const ResourceBinding *findResourceByName(const Module &module, const std::string &name);
        /** Finds one reflected resource by bind-group index, role, and optional component index. */
        const ResourceBinding *findResourceByRole(const Module &module,
                                                  uint32_t bindGroupIndex,
                                                  ResourceRole role,
                                                  std::optional<uint32_t> resourceIndex = std::nullopt);
        /** Returns the first payload operand after an optional resource selector operand. */
        size_t erasedResourcePayloadOffset(const Expression &expression);
        /** Returns the reflected resource named by an erased resource selector expression. */
        const ResourceBinding *findSelectedResource(const Module &module, const Expression &expression);
        /** Finds the bind group referenced by an erased resource selector path. */
        std::optional<uint32_t> findSelectedBindGroupIndex(const Module &module, const Expression &expression);
        /** Returns the requested template argument spelling from a simple template-id string. */
        std::string extractTemplateArgumentSpelling(const std::string &typeName, unsigned argumentIndex);
        /** Collects concrete type template arguments from a class template specialization. */
        std::unordered_map<std::string, clang::QualType> collectClassTypeTemplateArguments(const clang::CXXRecordDecl *recordDecl);
        /** Resolves a dependent nested type alias spelling such as typename Policy::TexCoordType. */
        clang::QualType resolveDependentTypeAliasSpelling(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                         std::string typeName);
        /** Resolves a direct template type parameter spelling to the concrete type used by a shader specialization. */
        clang::QualType resolveClassTypeTemplateArgument(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                        const clang::QualType &type);
        /** Extracts a UGL texture-format token from a canonical type spelling. */
        std::string extractTextureFormatToken(const std::string &spelledType);
        /** Scalar kind and component count decoded from an encoded UGL vector specialization name. */
        struct EncodedUGLVectorSpecialization
        {
            ScalarKind scalarKind = ScalarKind::None;
            uint32_t width = 0;
        };
        /** Scalar kind, row count, and column count decoded from an encoded UGL matrix specialization name. */
        struct EncodedUGLMatrixSpecialization
        {
            ScalarKind scalarKind = ScalarKind::None;
            uint32_t rows = 0;
            uint32_t columns = 0;
        };
        /** Returns a scalar category from the scalar token embedded in a lowered UGL template specialization name. */
        ScalarKind scalarKindFromEncodedUGLTemplateToken(std::string token);
        /** Decodes a UGL Vector specialization name produced by the local stable-name utilities. */
        std::optional<EncodedUGLVectorSpecialization> parseEncodedUGLVectorSpecialization(const std::string &canonicalName);
        /** Decodes a UGL Matrix specialization name produced by the local stable-name utilities. */
        std::optional<EncodedUGLMatrixSpecialization> parseEncodedUGLMatrixSpecialization(const std::string &canonicalName);
        /** Maps a framebuffer attachment format token to the DSL payload type carried by UGL::TextureFormat. */
        std::string makeFramebufferTextureFormatValueTypeName(const std::string &textureFormat);
        /** Returns true when a canonical type name is a Phase-3 bind-group handle. */
        bool isBindGroupHandleName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a render-set handle. */
        bool isRenderSetHandleName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a Phase-3 supported buffer wrapper. */
        bool isSupportedBufferResourceName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a sampled texture wrapper supported by Phase 8. */
        bool isSampledTextureResourceName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a storage texture wrapper supported by Phase 8. */
        bool isStorageTextureResourceName(const std::string &canonicalName);
        /** Returns true when a canonical type name is one of the UGL private texture access packers. */
        bool isTextureAccessPackerName(const std::string &canonicalName);
        /** Returns true when a canonical type name is one of the UGL private uniform-buffer data packers. */
        bool isUniformBufferDataPackerName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a DSL buffer-component data pack handled by AST erasure. */
        bool isBufferComponentDataPackName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a DSL texture-component data pack handled by AST erasure. */
        bool isTextureComponentDataPackName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a DSL draw-data pack handled by AST erasure. */
        bool isDrawDataPackName(const std::string &canonicalName);
        /** Classifies a buffer-component data-pack method before it is erased into ordinary UGLIR. */
        ErasedResourceCallKind classifyErasedBufferComponentCall(std::string_view methodName);
        /** Classifies a texture-component data-pack method before it is erased into ordinary UGLIR. */
        ErasedResourceCallKind classifyErasedTextureComponentCall(std::string_view methodName);
        /** Classifies a draw-data method before it is erased into ordinary UGLIR. */
        ErasedResourceCallKind classifyErasedDrawDataCall(std::string_view methodName);
        /** Returns true when a RenderSet-erased call writes values through out parameters. */
        bool isErasedDrawDataStoreCall(ErasedResourceCallKind callKind);
        /** Returns true when a canonical type name is a sampler resource wrapper supported by Phase 8. */
        bool isSamplerResourceName(const std::string &canonicalName);
        /** Returns true when a canonical type name is the UGL workgroup-storage wrapper. */
        bool isGroupSharedName(const std::string &canonicalName);
        /** Returns true when a canonical type name is a framebuffer attachment wrapper. */
        bool isFramebufferAttachmentName(const std::string &canonicalName);
        /** Classifies source-level UGL helper types that should not be emitted as shader payload ABI. */
        TypeRole typeRoleFromSourceTypeName(const std::string &canonicalName, const std::string &spelledType, const std::string &recordSymbolName = {});
        /** Resolves a framebuffer attachment wrapper name from a dependent template-id spelling. */
        std::string framebufferAttachmentNameFromDependentSpelling(const std::string &typeName);
        /** Returns a resource texture dimension token for a canonical UGL texture wrapper name. */
        TextureDimension textureDimensionForResourceName(const std::string &canonicalName);
        /** Returns true when a source location belongs to system or UGL implementation headers. */
        bool isIgnoredLocation(const clang::SourceManager &sourceManager, clang::SourceLocation location);
        /** Returns the canonical record name for a UGL type spelling. */
        std::string canonicalRecordName(const clang::QualType &type);
        /** Returns the type template argument at the requested index, or an empty QualType. */
        clang::QualType getTemplateTypeArgument(const clang::QualType &type, unsigned index);
        /** Returns the unsigned-integer template argument at the requested index, or nullopt. */
        std::optional<uint32_t> getTemplateUnsignedIntegerArgument(const clang::QualType &type, unsigned index);
        /** Returns true when decl is explicit (not compiler-implicit) and located outside system headers and the UGL headers, i.e. in user DSL source. */
        bool isUserAuthoredDecl(const clang::SourceManager &sourceManager, const clang::Decl *decl);
        /** Returns true when recordDecl, or the class template it specializes, is declared in user DSL source. */
        bool isUserAuthoredRecordDecl(const clang::SourceManager &sourceManager, const clang::CXXRecordDecl *recordDecl);
        /** Returns true when functionDecl is a non-null function declared in user DSL source. */
        bool isUserAuthoredFunction(const clang::SourceManager &sourceManager, const clang::FunctionDecl *functionDecl);
        /** Returns true when decl's definition derives, directly or transitively, from the UGL base class with the given qualified name. */
        bool isDerivedFromUGLBase(const clang::SourceManager &sourceManager, const clang::CXXRecordDecl *decl, const std::string &baseName);
    } // namespace TypeSpelling
} // namespace UGLC::CodeGen::UGLIR
