#include "ConstructorArgumentContract.hpp"

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLC.Constants.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Sema/Initialization.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace UGLC::CodeGen
{
    namespace
    {
        /** Parses Args annotations as native C++ attributes without polluting library identifiers. */
        class ConstructorArgumentAttribute final : public clang::ParsedAttrInfo
        {
        public:
            /** Registers the logical positions supported by the existing 64-parameter constructor macro. */
            ConstructorArgumentAttribute()
            {
                static constexpr Spelling Names[] = {
                    {clang::AttributeCommonInfo::AS_CXX11, "Args0"}, {clang::AttributeCommonInfo::AS_CXX11, "Args1"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args2"}, {clang::AttributeCommonInfo::AS_CXX11, "Args3"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args4"}, {clang::AttributeCommonInfo::AS_CXX11, "Args5"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args6"}, {clang::AttributeCommonInfo::AS_CXX11, "Args7"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args8"}, {clang::AttributeCommonInfo::AS_CXX11, "Args9"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args10"}, {clang::AttributeCommonInfo::AS_CXX11, "Args11"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args12"}, {clang::AttributeCommonInfo::AS_CXX11, "Args13"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args14"}, {clang::AttributeCommonInfo::AS_CXX11, "Args15"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args16"}, {clang::AttributeCommonInfo::AS_CXX11, "Args17"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args18"}, {clang::AttributeCommonInfo::AS_CXX11, "Args19"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args20"}, {clang::AttributeCommonInfo::AS_CXX11, "Args21"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args22"}, {clang::AttributeCommonInfo::AS_CXX11, "Args23"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args24"}, {clang::AttributeCommonInfo::AS_CXX11, "Args25"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args26"}, {clang::AttributeCommonInfo::AS_CXX11, "Args27"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args28"}, {clang::AttributeCommonInfo::AS_CXX11, "Args29"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args30"}, {clang::AttributeCommonInfo::AS_CXX11, "Args31"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args32"}, {clang::AttributeCommonInfo::AS_CXX11, "Args33"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args34"}, {clang::AttributeCommonInfo::AS_CXX11, "Args35"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args36"}, {clang::AttributeCommonInfo::AS_CXX11, "Args37"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args38"}, {clang::AttributeCommonInfo::AS_CXX11, "Args39"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args40"}, {clang::AttributeCommonInfo::AS_CXX11, "Args41"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args42"}, {clang::AttributeCommonInfo::AS_CXX11, "Args43"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args44"}, {clang::AttributeCommonInfo::AS_CXX11, "Args45"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args46"}, {clang::AttributeCommonInfo::AS_CXX11, "Args47"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args48"}, {clang::AttributeCommonInfo::AS_CXX11, "Args49"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args50"}, {clang::AttributeCommonInfo::AS_CXX11, "Args51"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args52"}, {clang::AttributeCommonInfo::AS_CXX11, "Args53"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args54"}, {clang::AttributeCommonInfo::AS_CXX11, "Args55"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args56"}, {clang::AttributeCommonInfo::AS_CXX11, "Args57"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args58"}, {clang::AttributeCommonInfo::AS_CXX11, "Args59"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args60"}, {clang::AttributeCommonInfo::AS_CXX11, "Args61"},
                    {clang::AttributeCommonInfo::AS_CXX11, "Args62"}, {clang::AttributeCommonInfo::AS_CXX11, "Args63"}};
                Spellings = Names;
            }

            /** Restricts Args attributes to parameter and field declarations before contract validation. */
            bool diagAppertainsToDecl(clang::Sema &sema, const clang::ParsedAttr &attribute,
                                      const clang::Decl *declaration) const override
            {
                if (llvm::isa<clang::ParmVarDecl, clang::FieldDecl>(declaration))
                {
                    return true;
                }
                sema.Diag(attribute.getLoc(), sema.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "Args attributes only apply to constructor parameters and generated fields"));
                return false;
            }

            /** Marks Args fields const before shader bodies are checked, preserving mutable host declarations. */
            AttrHandling handleDeclAttribute(clang::Sema &sema, clang::Decl *declaration,
                                             const clang::ParsedAttr &attribute) const override
            {
                declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, attribute.getAttrName()->getName(),
                    nullptr, 0, attribute.getRange(), clang::AnnotateAttr::CXX11_clang_annotate));
                if (auto *field = llvm::dyn_cast<clang::FieldDecl>(declaration);
                    field != nullptr && sema.getPreprocessor().isMacroDefined(mUGLShaderArgsReadonlyMacro))
                {
                    const auto readonlyType = field->getType().withConst();
                    const auto *sourceInfo = field->getTypeSourceInfo();
                    const auto typeLocation = sourceInfo == nullptr ? field->getLocation() : sourceInfo->getTypeLoc().getBeginLoc();
                    field->setType(readonlyType);
                    field->setTypeSourceInfo(sema.Context.getTrivialTypeSourceInfo(readonlyType, typeLocation));
                }
                return AttributeApplied;
            }
        };

        static clang::ParsedAttrInfoRegistry::Add<ConstructorArgumentAttribute> ArgumentAttributeRegistration(
            "ugl-constructor-arguments", "UGL constructor argument positions");

        /** Identifies an operation control separately from its constructor argument prefix. */
        enum class ArgumentControlKind { Unsigned, Signed, IndexFormat, IndexBuffer, IndirectBuffer };

        /** Defines one control shared by signature export and source-call validation. */
        struct ArgumentControl
        {
            const char *name;
            ArgumentControlKind kind;
        };

        /** Returns the fixed operation suffix for a supported shader-class execution entry. */
        llvm::ArrayRef<ArgumentControl> operationControls(UGLIR::ShaderClassKind kind, llvm::StringRef operation)
        {
            static constexpr ArgumentControl Draw[] = {
                {"vertexCount", ArgumentControlKind::Unsigned}, {"instanceCount", ArgumentControlKind::Unsigned},
                {"firstVertex", ArgumentControlKind::Unsigned}, {"firstInstance", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl Indexed[] = {
                {"indexBuffer", ArgumentControlKind::IndexBuffer}, {"indexFormat", ArgumentControlKind::IndexFormat},
                {"indexCount", ArgumentControlKind::Unsigned}, {"instanceCount", ArgumentControlKind::Unsigned},
                {"firstIndex", ArgumentControlKind::Unsigned}, {"baseVertex", ArgumentControlKind::Signed},
                {"firstInstance", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl Indirect[] = {
                {"indirectBuffer", ArgumentControlKind::IndirectBuffer}, {"indirectCommandCount", ArgumentControlKind::Unsigned},
                {"stride", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl IndexedIndirect[] = {
                {"indexBuffer", ArgumentControlKind::IndexBuffer}, {"indexFormat", ArgumentControlKind::IndexFormat},
                {"indirectBuffer", ArgumentControlKind::IndirectBuffer}, {"indirectCommandCount", ArgumentControlKind::Unsigned},
                {"stride", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl IndirectCount[] = {
                {"indirectBuffer", ArgumentControlKind::IndirectBuffer}, {"indirectCountBuffer", ArgumentControlKind::IndirectBuffer},
                {"maxDrawCount", ArgumentControlKind::Unsigned}, {"stride", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl IndexedIndirectCount[] = {
                {"indexBuffer", ArgumentControlKind::IndexBuffer}, {"indexFormat", ArgumentControlKind::IndexFormat},
                {"indirectBuffer", ArgumentControlKind::IndirectBuffer}, {"indirectCountBuffer", ArgumentControlKind::IndirectBuffer},
                {"maxDrawCount", ArgumentControlKind::Unsigned}, {"stride", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl Dispatch[] = {
                {"x", ArgumentControlKind::Unsigned}, {"y", ArgumentControlKind::Unsigned}, {"z", ArgumentControlKind::Unsigned}};
            static constexpr ArgumentControl DispatchIndirect[] = {{"indirectBuffer", ArgumentControlKind::IndirectBuffer}};
            if (kind == UGLIR::ShaderClassKind::PixelLocal && operation == "draw")
            {
                return {};
            }
            if (kind == UGLIR::ShaderClassKind::Render)
            {
                if (operation == "draw")
                {
                    return Draw;
                }
                if (operation == "drawIndexed")
                {
                    return Indexed;
                }
                if (operation == "drawIndirect")
                {
                    return Indirect;
                }
                if (operation == "drawIndexedIndirect")
                {
                    return IndexedIndirect;
                }
                if (operation == "drawIndirectCount")
                {
                    return IndirectCount;
                }
                if (operation == "drawIndexedIndirectCount")
                {
                    return IndexedIndirectCount;
                }
            }
            if (kind == UGLIR::ShaderClassKind::Compute)
            {
                if (operation == "dispatchThreads")
                {
                    return Dispatch;
                }
                if (operation == "dispatchWorkgroupsIndirect")
                {
                    return DispatchIndirect;
                }
            }
            throw std::runtime_error("Unsupported constructor-argument execution operation: " + operation.str());
        }

        /** Returns a public DSL spelling for an operation control, including constrained buffer types. */
        const char *controlTypeName(ArgumentControlKind kind)
        {
            switch (kind)
            {
            case ArgumentControlKind::Unsigned: return "uint32_t";
            case ArgumentControlKind::Signed: return "int32_t";
            case ArgumentControlKind::IndexFormat: return "UGL::IndexFormat";
            case ArgumentControlKind::IndexBuffer: return "IsIndexBuffer";
            case ArgumentControlKind::IndirectBuffer: return "IsIndirectBuffer";
            }
            throw std::runtime_error("Unknown argument control kind.");
        }

        /** Returns a stable source coordinate without retaining Clang source objects. */
        UGLIR::SourceLocation argumentLocation(const clang::ASTContext &context, clang::SourceLocation location)
        {
            const auto &sources = context.getSourceManager();
            const auto presumed = sources.getPresumedLoc(sources.getSpellingLoc(location));
            if (presumed.isInvalid())
            {
                return {};
            }
            return {presumed.getFilename(), presumed.getLine(), presumed.getColumn()};
        }

        /** Carries one Args contract failure from a validator to the nearest analysis-unit boundary. */
        struct ArgumentContractError final : public std::runtime_error
        {
            /** Captures the diagnostic location and raw message for one analysis-unit failure. */
            ArgumentContractError(clang::SourceLocation location, std::string message, const std::string &formatted)
                : std::runtime_error(formatted), location(location), message(std::move(message))
            {
            }

            clang::SourceLocation location;
            std::string message;
        };

        /** Stops the current Args analysis unit; the analyzer reports it through Clang diagnostics. */
        [[noreturn]] void argumentError(const clang::ASTContext &context, clang::SourceLocation location,
                                       const std::string &message)
        {
            const auto source = argumentLocation(context, location);
            throw ArgumentContractError(location, message, formatClangStyleDiagnostic(source.file, source.line,
                source.column, "UGLC Args: " + message));
        }

        /** Returns true when an annotation with the requested exact spelling is present. */
        bool hasAnnotation(const clang::Decl &declaration, llvm::StringRef name)
        {
            for (const auto *attribute : declaration.specific_attrs<clang::AnnotateAttr>())
            {
                if (attribute->getAnnotation() == name)
                {
                    return true;
                }
            }
            return false;
        }

        /** Returns true for any annotation in an indexed family, including malformed indices. */
        bool hasAnnotationPrefix(const clang::Decl &declaration, llvm::StringRef prefix)
        {
            for (const auto *attribute : declaration.specific_attrs<clang::AnnotateAttr>())
            {
                if (attribute->getAnnotation().starts_with(prefix))
                {
                    return true;
                }
            }
            return false;
        }

        /** Validates one unique indexed annotation and returns its index, or minus one when absent. */
        int32_t indexedAnnotation(const clang::ASTContext &context, const clang::Decl &declaration,
                                  llvm::StringRef prefix, uint32_t limit)
        {
            int32_t result = -1;
            for (const auto *attribute : declaration.specific_attrs<clang::AnnotateAttr>())
            {
                const auto spelling = attribute->getAnnotation();
                if (!spelling.starts_with(prefix))
                {
                    continue;
                }
                uint32_t index = 0;
                if (spelling.drop_front(prefix.size()).getAsInteger(10, index) || index >= limit)
                {
                    argumentError(context, declaration.getLocation(), "invalid " + prefix.str() + " annotation: " + spelling.str());
                }
                if (result >= 0)
                {
                    argumentError(context, declaration.getLocation(), "multiple " + prefix.str() + " annotations on one declaration.");
                }
                result = static_cast<int32_t>(index);
            }
            return result;
        }

        /** Finds the sole concrete constructor macro expansion used by a shader class. */
        clang::CXXMethodDecl &argumentConstructor(clang::ASTContext &context, const clang::CXXRecordDecl &record)
        {
            clang::CXXMethodDecl *result = nullptr;
            for (auto *method : record.methods())
            {
                if (method->getNameAsString() != mUGLCTORFunctionName)
                {
                    continue;
                }
                if (result != nullptr)
                {
                    argumentError(context, method->getLocation(), record.getNameAsString() + " requires one unambiguous constructor.");
                }
                result = method;
            }
            if (result == nullptr || !hasAnnotation(*result, mUGLCTORName) || result->getDescribedFunctionTemplate() != nullptr)
            {
                argumentError(context, record.getLocation(), record.getNameAsString() + " requires its own non-templated constructor declaration.");
            }
            return *result;
        }

        /** Detects a class that opts into Args, including annotations copied onto macro-generated fields. */
        bool classDeclaresArguments(const clang::CXXRecordDecl &record)
        {
            for (const auto *field : record.fields())
            {
                if (hasAnnotationPrefix(*field, "Args"))
                {
                    return true;
                }
            }
            for (const auto *method : record.methods())
            {
                if (method->getNameAsString() == mUGLCTORFunctionName)
                {
                    for (const auto *parameter : method->parameters())
                    {
                        if (hasAnnotationPrefix(*parameter, "Args"))
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        /** Returns the canonical record reached through a value, reference, or implicit object pointer. */
        const clang::CXXRecordDecl *argumentRecord(clang::QualType type)
        {
            if (type.isNull())
            {
                return nullptr;
            }
            type = type.getNonReferenceType();
            if (type->isPointerType())
            {
                type = type->getPointeeType();
            }
            const auto *record = type->getAsCXXRecordDecl();
            return record == nullptr ? nullptr : record->getCanonicalDecl();
        }

        /** Returns true when a buffer type inherits the existing UGL usage tag, regardless of access. */
        bool hasBufferUsage(const clang::CXXRecordDecl &record, llvm::StringRef usage)
        {
            if (record.getQualifiedNameAsString() == usage)
            {
                return true;
            }
            for (const auto &base : record.bases())
            {
                if (const auto *baseRecord = base.getType()->getAsCXXRecordDecl())
                {
                    if (hasBufferUsage(*baseRecord, usage))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /** Serializes a recursive constructor argument with its diagnostic source location. */
        nlohmann::json fieldJson(const ConstructorArgumentField &field)
        {
            nlohmann::json result = {{"name", field.name},
                                     {"path", field.path},
                                     {"type", field.canonicalType},
                                     {"kind", static_cast<uint32_t>(field.kind)},
                                     {"scalar", UGLIR::toString(field.scalarKind)},
                                     {"bits", field.bitWidth},
                                     {"vectorWidth", field.vectorWidth},
                                     {"matrixRows", field.matrixRows},
                                     {"matrixColumns", field.matrixColumns},
                                     {"arrayCount", field.arrayCount},
                                     {"resourceKind", static_cast<uint32_t>(field.resourceKind)},
                                     {"access", field.accessMode == UGLIR::AccessMode::ReadWrite ? "read_write" : "read"},
                                     {"textureDimension", UGLIR::toString(field.textureDimension)},
                                     {"textureFormat", UGLIR::toString(field.textureFormat)},
                                     {"fields", nlohmann::json::array()}};
            for (const auto &child : field.fields)
            {
                result["fields"].push_back(fieldJson(child));
            }
            result["source"] = {{"file", field.sourceLocation.file}, {"line", field.sourceLocation.line}, {"column", field.sourceLocation.column}};
            return result;
        }

        /** Collects entry use and rejects shader reads of creation-only state on an Args class. */
        class ConstructorArgumentUsage final : public clang::RecursiveASTVisitor<ConstructorArgumentUsage>
        {
        public:
            /** Borrows one shader class and one entry-use result during shader semantic preparation. */
            ConstructorArgumentUsage(clang::ASTContext &context, clang::Sema &sema,
                                     const clang::CXXRecordDecl &record, const ConstructorArgumentContract &contract,
                                     ConstructorArgumentEntryUsage &usage)
                : mContext(context), mSema(sema), mRecord(record), mContract(contract), mUsage(usage) {}

            /** Records an Args read or diagnoses a runtime member that cannot follow per-call replacement. */
            bool VisitMemberExpr(clang::MemberExpr *expression)
            {
                const auto *field = llvm::dyn_cast<clang::FieldDecl>(expression->getMemberDecl());
                if (field == nullptr || field->getParent()->getCanonicalDecl() != mRecord.getCanonicalDecl())
                {
                    return true;
                }
                for (uint32_t index = 0; index < mContract.parameters.size(); ++index)
                {
                    if (mContract.parameters[index].name == field->getName())
                    {
                        if (std::find(mUsage.argumentIndices.begin(), mUsage.argumentIndices.end(), index) == mUsage.argumentIndices.end())
                        {
                            mUsage.argumentIndices.push_back(index);
                        }
                        return true;
                    }
                }
                clang::Expr::EvalResult constant;
                if (field->getType().isConstQualified() && field->hasInClassInitializer() && field->getInClassInitializer()->EvaluateAsInt(constant, mContext))
                {
                    return true;
                }
                argumentError(mContext, expression->getExprLoc(), "shader reads creation-only member " + field->getNameAsString() + "; declare it as a constructor Args parameter or use a supported compile-time constant.");
            }

            /** Checks output destinations and follows user helpers for entry-use metadata; Clang checks const writes. */
            bool VisitCallExpr(clang::CallExpr *expression)
            {
                auto *callee = expression->getDirectCallee();
                if (callee != nullptr)
                {
                    const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
                    const uint32_t argumentOffset = llvm::isa<clang::CXXOperatorCallExpr>(expression) && method != nullptr && !method->isStatic() ? 1 : 0;
                    for (uint32_t index = 0; index < callee->getNumParams() && index + argumentOffset < expression->getNumArgs(); ++index)
                    {
                        const auto &parameter = *callee->getParamDecl(index);
                        if (!hasAnnotation(parameter, "OUT") && !hasAnnotation(parameter, "INOUT"))
                        {
                            continue;
                        }
                        const auto *destination = expression->getArg(index + argumentOffset)->IgnoreParenImpCasts();
                        if (destination->isModifiableLvalue(mContext) != clang::Expr::MLV_Valid)
                        {
                            argumentError(mContext, destination->getExprLoc(), "OUT and INOUT require a modifiable lvalue; Args values and resource handles are readonly.");
                        }
                    }
                }
                if (callee == nullptr || callee->isDependentContext() || mContext.getSourceManager().isInSystemHeader(callee->getLocation()) || llvm::StringRef(callee->getQualifiedNameAsString()).starts_with("UGL::"))
                {
                    return true;
                }
                if (!mVisited.insert(callee->getCanonicalDecl()).second)
                {
                    return true;
                }
                if (!callee->hasBody() && callee->getTemplateInstantiationPattern() != nullptr)
                {
                    mSema.InstantiateFunctionDefinition(callee->getLocation(), callee, true, true);
                }
                if (callee->hasBody())
                {
                    TraverseStmt(callee->getBody());
                }
                return true;
            }

        private:
            clang::ASTContext &mContext;
            clang::Sema &mSema;
            const clang::CXXRecordDecl &mRecord;
            const ConstructorArgumentContract &mContract;
            ConstructorArgumentEntryUsage &mUsage;
            llvm::SmallPtrSet<const clang::FunctionDecl *, 16> mVisited;
        };

        /** Builds owned logical fields and validates source calls while the shader Sema remains alive. */
        class ConstructorArgumentAnalyzer final : public clang::RecursiveASTVisitor<ConstructorArgumentAnalyzer>
        {
        public:
            /** Borrows frontend state only for the duration of one contract preparation pass. */
            ConstructorArgumentAnalyzer(clang::ASTContext &context, clang::Sema &sema,
                                        llvm::ArrayRef<UGLIR::ShaderClassRoot> roots)
                : mContext(context), mSema(sema), mQueries(&context, nullptr, nullptr, nullptr)
            {
                for (const auto &root : roots)
                {
                    mRoots[root.recordDecl->getCanonicalDecl()] = root;
                }
            }

            /** Includes concrete template calls so template instantiation cannot bypass the Args contract. */
            bool shouldVisitTemplateInstantiations() const { return true; }

            /** Validates C++ initialization in the actual caller's access context. */
            bool TraverseDecl(clang::Decl *declaration)
            {
                if (auto *function = llvm::dyn_cast_or_null<clang::FunctionDecl>(declaration))
                {
                    clang::Sema::ContextRAII context(mSema, function);
                    return clang::RecursiveASTVisitor<ConstructorArgumentAnalyzer>::TraverseDecl(declaration);
                }
                return clang::RecursiveASTVisitor<ConstructorArgumentAnalyzer>::TraverseDecl(declaration);
            }

            /** Rejects Args on parameters outside a concrete shader constructor. */
            bool VisitParmVarDecl(clang::ParmVarDecl *parameter)
            {
                try
                {
                    checkParameterDeclaration(parameter);
                }
                catch (const ArgumentContractError &error)
                {
                    reportArgumentError(error);
                }
                return true;
            }

            /** Rejects Args fields that do not correspond to a constructor parameter. */
            bool VisitFieldDecl(clang::FieldDecl *field)
            {
                try
                {
                    checkFieldDeclaration(field);
                }
                catch (const ArgumentContractError &error)
                {
                    reportArgumentError(error);
                }
                return true;
            }

        private:
            /** Validates one parameter declaration as an independent Args analysis unit. */
            void checkParameterDeclaration(clang::ParmVarDecl *parameter)
            {
                if (!hasAnnotationPrefix(*parameter, "Args") || parameter->getDeclContext()->isDependentContext())
                {
                    return;
                }
                const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(parameter->getDeclContext());
                if (method == nullptr || !mRoots.count(method->getParent()->getCanonicalDecl()) || !hasAnnotation(*method, mUGLCTORName))
                {
                    argumentError(mContext, parameter->getLocation(), "Args annotations are only valid on shader constructor parameters.");
                }
            }

            /** Validates one field declaration as an independent Args analysis unit. */
            void checkFieldDeclaration(clang::FieldDecl *field)
            {
                if (!hasAnnotationPrefix(*field, "Args") || field->getDeclContext()->isDependentContext())
                {
                    return;
                }
                const auto *record = llvm::dyn_cast<clang::CXXRecordDecl>(field->getDeclContext());
                if (record == nullptr || !mIndices.count(record->getCanonicalDecl()))
                {
                    argumentError(mContext, field->getLocation(), "Args fields must be produced by a shader constructor.");
                }
                auto &constructor = argumentConstructor(mContext, *record);
                bool matched = false;
                for (const auto *parameter : constructor.parameters())
                {
                    if (parameter->getName() == field->getName())
                    {
                        matched = true;
                    }
                }
                if (!matched)
                {
                    argumentError(mContext, field->getLocation(), "Args field has no matching constructor parameter.");
                }
            }

        public:
            /** Splits a concrete execution call into constructor arguments and operation controls. */
            bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr *call)
            {
                try
                {
                    checkMemberCallExpr(call);
                }
                catch (const ArgumentContractError &error)
                {
                    reportArgumentError(error);
                }
                return true;
            }

        private:
            /** Validates one execution call site as an independent Args analysis unit. */
            void checkMemberCallExpr(clang::CXXMemberCallExpr *call)
            {
                auto *method = call->getMethodDecl();
                if (method == nullptr || call->isTypeDependent() || call->getImplicitObjectArgument() == nullptr)
                {
                    return;
                }
                const auto *record = argumentRecord(call->getImplicitObjectArgument()->IgnoreParenImpCasts()->getType());
                const auto root = mRoots.find(record);
                const auto operation = method->getNameAsString();
                const bool placeholder = hasAnnotation(*method, "UGLARGUMENTSCALL");
                if (root == mRoots.end())
                {
                    const auto receiver = method->getParent()->getQualifiedNameAsString();
                    const bool interfaceExecution =
                        (receiver == "UGL::IComputeClass" && operation == "dispatchWorkgroupsIndirect") ||
                        (receiver == "UGL::IRenderClass" && operation == "drawIndirect");
                    if (placeholder || interfaceExecution)
                    {
                        argumentError(mContext, call->getExprLoc(), "execution arguments require a concrete shader-class receiver with a constructor contract.");
                    }
                    return;
                }
                const bool counted = operation == "drawIndirectCount" || operation == "drawIndexedIndirectCount";
                if (!mIndices.count(record))
                {
                    if (!placeholder)
                    {
                        return;
                    }
                    // Slot indirect calls can select the placeholder when controls need C++ conversions.
                    if (!counted && operation != "drawIndirect" && operation != "dispatchWorkgroupsIndirect")
                    {
                        validateClassContract(root->second);
                    }
                }
                if (operation == "create")
                {
                    return; // Clang already initializes the concrete constructor parameters.
                }
                if (operation == "setBindGroup")
                {
                    argumentError(mContext, call->getExprLoc(), "Args classes receive bindings through their complete execution argument prefix.");
                }
                if (!placeholder && operation != "drawIndirect" && operation != "dispatchWorkgroupsIndirect")
                {
                    return;
                }
                auto &constructor = argumentConstructor(mContext, *root->second.recordDecl);
                const auto controls = operationControls(root->second.kind, operation);
                const uint32_t prefixCount = mIndices.count(record) ? constructor.getNumParams() : 0;
                const size_t expectedCount = prefixCount + controls.size();
                if (call->getNumArgs() != expectedCount && (!counted || call->getNumArgs() + 1 != expectedCount))
                {
                    argumentError(mContext, call->getExprLoc(), root->second.recordDecl->getNameAsString() + "." + operation +
                        " requires " + std::to_string(prefixCount) + " constructor arguments followed by " +
                        std::to_string(controls.size()) + " operation controls" + (counted ? " (stride may be omitted)" : "") +
                        "; got " + std::to_string(call->getNumArgs()) + " arguments.");
                }
                for (uint32_t index = 0; index < prefixCount; ++index)
                {
                    validateInitialization(*call->getArg(index), constructor.getParamDecl(index)->getType(),
                        root->second.recordDecl->getNameAsString() + "." + operation + " argument " + std::to_string(index) +
                        " must initialize constructor Args" + std::to_string(index));
                }
                for (uint32_t index = 0; index < controls.size() && prefixCount + index < call->getNumArgs(); ++index)
                {
                    validateControl(*call->getArg(prefixCount + index), controls[index]);
                }
                return;
            }

        public:
            /** Prevents legacy wrapper calls from omitting Args on a class using the new contract. */
            bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr *call)
            {
                try
                {
                    checkOperatorCallExpr(call);
                }
                catch (const ArgumentContractError &error)
                {
                    reportArgumentError(error);
                }
                return true;
            }

        private:
            /** Validates one legacy wrapper call site as an independent Args analysis unit. */
            void checkOperatorCallExpr(clang::CXXOperatorCallExpr *call)
            {
                if (call->getOperator() != clang::OO_Call || call->getNumArgs() == 0 || call->isTypeDependent())
                {
                    return;
                }
                const auto type = call->getArg(0)->getType().getNonReferenceType();
                if (!mQueries.checkTypeCanonicalName(type, "UGL::RenderClass") && !mQueries.checkTypeCanonicalName(type, "UGL::ComputeClass"))
                {
                    return;
                }
                const auto arguments = mQueries.getTemplateArgumentsFromType(type);
                if (arguments.empty() || arguments[0].getKind() != clang::TemplateArgument::Type)
                {
                    return;
                }
                const auto *record = argumentRecord(arguments[0].getAsType());
                if (mIndices.count(record))
                {
                    argumentError(mContext, call->getExprLoc(), "Args classes require an explicit draw or dispatch with the complete constructor argument prefix.");
                }
            }

        public:
            /** Exports all declared Args contracts, including unused fields and optional zero-argument classes. */
            std::vector<ConstructorArgumentContract> analyze(llvm::ArrayRef<UGLIR::ShaderClassRoot> roots,
                                                            bool includeZeroArgumentClasses)
            {
                for (const auto &root : roots)
                {
                    if (classDeclaresArguments(*root.recordDecl))
                    {
                        validateClassContract(root);
                    }
                    else if (includeZeroArgumentClasses)
                    {
                        for (const auto *method : root.recordDecl->methods())
                        {
                            if (method->getNameAsString() == mUGLCTORFunctionName && method->getNumParams() == 0 && hasAnnotation(*method, mUGLCTORName))
                            {
                                validateClassContract(root);
                                break;
                            }
                        }
                    }
                }
                TraverseDecl(mContext.getTranslationUnitDecl());
                return std::move(mContracts);
            }

        private:
            /** Emits one Args contract failure as a Clang error diagnostic so later analysis units still run. */
            void reportArgumentError(const ArgumentContractError &error)
            {
                mSema.Diag(error.location,
                    mSema.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "UGLC Args: %0"))
                    << error.message;
            }

            /** Validates one shader-class contract; a failed unit reports its error and contributes no contract. */
            void validateClassContract(const UGLIR::ShaderClassRoot &root)
            {
                try
                {
                    ensureContract(root);
                }
                catch (const ArgumentContractError &error)
                {
                    reportArgumentError(error);
                    mIndices.erase(root.recordDecl->getCanonicalDecl());
                    mFailedContracts.insert(root.recordDecl->getCanonicalDecl());
                }
            }

            /** Prints a complete canonical C++ type, retaining qualifiers needed for contract diagnostics. */
            std::string canonicalType(clang::QualType type) const
            {
                clang::PrintingPolicy policy(mContext.getLangOpts());
                policy.SuppressTagKeyword = true;
                policy.FullyQualifiedName = true;
                return type.getCanonicalType().getAsString(policy);
            }

            /** Completes one record or array before examining its fields, including unused template types. */
            void completeType(clang::QualType type, clang::SourceLocation location)
            {
                if (type->isDependentType() || mSema.RequireCompleteType(location, type, clang::diag::err_incomplete_type))
                {
                    argumentError(mContext, location, "constructor arguments require a complete concrete type.");
                }
            }

            /** Resolves the first type template argument of a supported DSL resource wrapper. */
            clang::QualType elementType(clang::QualType type, clang::SourceLocation location)
            {
                const auto arguments = mQueries.getTemplateArgumentsFromType(type);
                if (arguments.empty() || arguments[0].getKind() != clang::TemplateArgument::Type)
                {
                    argumentError(mContext, location, "resource requires a concrete element type.");
                }
                return arguments[0].getAsType();
            }

            /** Reads a fixed positive template dimension without truncating a large constant. */
            uint32_t templateDimension(const clang::TemplateArgument &argument, clang::SourceLocation location)
            {
                if (argument.getKind() != clang::TemplateArgument::Integral || argument.getAsIntegral().isNegative() || argument.getAsIntegral().getActiveBits() > 32 || argument.getAsIntegral().getZExtValue() == 0)
                {
                    argumentError(mContext, location, "argument arrays and math dimensions require a positive 32-bit constant.");
                }
                return static_cast<uint32_t>(argument.getAsIntegral().getZExtValue());
            }

            /** Classifies the existing UGL math types before inspecting their emulation-only fields. */
            bool exportMathType(clang::QualType type, ConstructorArgumentField &field)
            {
                const auto *record = type->getAsCXXRecordDecl();
                if (record == nullptr)
                {
                    return false;
                }
                const auto name = mQueries.getClassCanonicalName(record);
                const auto arguments = mQueries.getTemplateArgumentsFromType(type);
                clang::QualType scalar;
                uint32_t rows = 0, columns = 0, width = 0;
                if (name == "UGL::Vector" && arguments.size() == 2 && arguments[0].getKind() == clang::TemplateArgument::Type)
                {
                    scalar = arguments[0].getAsType();
                    width = templateDimension(arguments[1], record->getLocation());
                }
                else if (name == "UGL::Matrix" && arguments.size() == 3 && arguments[0].getKind() == clang::TemplateArgument::Type)
                {
                    scalar = arguments[0].getAsType();
                    rows = templateDimension(arguments[1], record->getLocation());
                    columns = templateDimension(arguments[2], record->getLocation());
                }
                else
                {
                    for (const char *prefix : {"float", "half", "int", "uint", "bool"})
                    {
                        const std::string qualified = std::string("UGL::") + prefix;
                        if (name.size() != qualified.size() + 1 || name.compare(0, qualified.size(), qualified) != 0)
                        {
                            continue;
                        }
                        width = static_cast<uint32_t>(name.back() - '0');
                        if (width < 2 || width > 4)
                        {
                            continue;
                        }
                        field.scalarKind = UGLIR::scalarKindFromTypeName(prefix);
                        field.bitWidth = field.scalarKind == UGLIR::ScalarKind::Half ? 16 : field.scalarKind == UGLIR::ScalarKind::Bool ? 1 : 32;
                        break;
                    }
                    if (field.scalarKind == UGLIR::ScalarKind::None)
                    {
                        return false;
                    }
                }
                if (!scalar.isNull())
                {
                    auto component = exportField(scalar, "$scalar", field.path + ".$scalar", record->getLocation());
                    if (component.kind != UGLIR::TypeKind::Int && component.kind != UGLIR::TypeKind::UInt && component.kind != UGLIR::TypeKind::Float && component.kind != UGLIR::TypeKind::Half && component.kind != UGLIR::TypeKind::Bool)
                    {
                        argumentError(mContext, record->getLocation(), field.path + " has an unsupported math scalar.");
                    }
                    field.scalarKind = component.scalarKind;
                    field.bitWidth = component.bitWidth;
                }
                if ((rows == 0 && (width < 2 || width > 4)) || (rows != 0 && (rows < 2 || rows > 4 || columns < 2 || columns > 4)))
                {
                    argumentError(mContext, record->getLocation(), field.path + " has an unsupported math dimension.");
                }
                field.kind = rows == 0 ? UGLIR::TypeKind::Vector : UGLIR::TypeKind::Matrix;
                field.vectorWidth = width;
                field.matrixRows = rows;
                field.matrixColumns = columns;
                return true;
            }

            /** Recursively exports values and resource leaves without copying CPU wrapper representations. */
            ConstructorArgumentField exportField(clang::QualType type, const std::string &name,
                                                  const std::string &path, clang::SourceLocation location)
            {
                ConstructorArgumentField field;
                field.name = name;
                field.path = path;
                field.canonicalType = canonicalType(type);
                field.sourceLocation = argumentLocation(mContext, location);
                if (type->isPointerType() || type->isMemberPointerType() || type->isReferenceType() || type.isVolatileQualified())
                {
                    argumentError(mContext, location, path + " contains an unsupported pointer, reference, or volatile value.");
                }
                completeType(type, location);
                type = type.getUnqualifiedType();
                if (type->isBooleanType())
                { field.kind = UGLIR::TypeKind::Bool; field.scalarKind = UGLIR::ScalarKind::Bool; field.bitWidth = 1; return field; }
                if (type->isIntegerType() || type->isEnumeralType())
                {
                    field.scalarKind = type->isSignedIntegerOrEnumerationType() ? UGLIR::ScalarKind::Int : UGLIR::ScalarKind::UInt;
                    field.kind = field.scalarKind == UGLIR::ScalarKind::Int ? UGLIR::TypeKind::Int : UGLIR::TypeKind::UInt;
                    field.bitWidth = static_cast<uint32_t>(mContext.getTypeSize(type));
                    if (field.bitWidth > 64)
                    {
                        argumentError(mContext, location, path + " has an unsupported integer width.");
                    }
                    return field;
                }
                if (type->isFloatingType() || mQueries.checkTypeCanonicalName(type, "UGL::half"))
                {
                    field.bitWidth = mQueries.checkTypeCanonicalName(type, "UGL::half") ? 16 : static_cast<uint32_t>(mContext.getTypeSize(type));
                    if (field.bitWidth != 16 && field.bitWidth != 32)
                    {
                        argumentError(mContext, location, path + " requires a supported half or 32-bit floating-point type.");
                    }
                    field.kind = field.bitWidth == 16 ? UGLIR::TypeKind::Half : UGLIR::TypeKind::Float;
                    field.scalarKind = field.bitWidth == 16 ? UGLIR::ScalarKind::Half : UGLIR::ScalarKind::Float;
                    return field;
                }
                if (const auto *array = mContext.getAsConstantArrayType(type))
                {
                    if (array->getSize().getActiveBits() > 32 || array->getSize().isZero())
                    {
                        argumentError(mContext, location, path + " requires a positive 32-bit array length.");
                    }
                    field.kind = UGLIR::TypeKind::Array;
                    field.arrayCount = static_cast<uint32_t>(array->getSize().getZExtValue());
                    field.fields.push_back(exportField(array->getElementType(), "$element", path + "[]", location));
                    return field;
                }
                if (type->isArrayType())
                {
                    argumentError(mContext, location, path + " requires a fixed-size array.");
                }
                if (exportMathType(type, field))
                {
                    return field;
                }
                const auto *record = type->getAsCXXRecordDecl();
                if (record == nullptr)
                {
                    argumentError(mContext, location, path + " has no supported shader argument layout.");
                }
                record = record->getDefinition();
                const auto primary = mQueries.getClassCanonicalName(record);
                if (mQueries.isShaderResourceHandleType(type))
                {
                    if (primary == mUGLShaderLegacyStorageBufferName)
                    {
                        argumentError(mContext, location, path + " uses deprecated StorageBuffer; use StructuredBuffer or RWStructuredBuffer.");
                    }
                    if (primary == mUGLShaderSamplerName)
                    { field.kind = UGLIR::TypeKind::Sampler; field.resourceKind = UGLIR::ResourceKind::Sampler; return field; }
                    const bool buffer = primary == mUGLShaderUniformBufferName || primary == mUGLShaderStructuredBufferName || primary == mUGLShaderRWStructuredBufferName;
                    const bool writable = primary == mUGLShaderRWStructuredBufferName || primary == mUGLShaderRWTexture2DName ||
                        primary == mUGLShaderRWTexture2DArrayName || primary == mUGLShaderRWTexture3DName;
                    field.kind = buffer ? UGLIR::TypeKind::Buffer : UGLIR::TypeKind::Texture;
                    field.accessMode = writable ? UGLIR::AccessMode::ReadWrite : UGLIR::AccessMode::Read;
                    field.resourceKind = buffer ? (primary == mUGLShaderUniformBufferName ? UGLIR::ResourceKind::UniformBuffer : UGLIR::ResourceKind::StorageBuffer)
                                                : (writable ? UGLIR::ResourceKind::StorageTexture : UGLIR::ResourceKind::Texture);
                    auto payload = elementType(type, location);
                    if (!buffer)
                    {
                        field.textureDimension = primary == mUGLShaderTexture3DName || primary == mUGLShaderRWTexture3DName ? UGLIR::TextureDimension::Texture3D :
                            primary == mUGLShaderTexture2DArrayName || primary == mUGLShaderRWTexture2DArrayName ? UGLIR::TextureDimension::Texture2DArray : UGLIR::TextureDimension::Texture2D;
                        const auto payloadName = mQueries.generateTypeCanonicalName(payload);
                        const auto separator = payloadName.rfind("::");
                        field.textureFormat = UGLIR::textureFormatFromToken(separator == std::string::npos ? payloadName : payloadName.substr(separator + 2));
                        if (const auto underlying = mQueries.resolveRecordNestedTrueType(payload))
                        {
                            payload = *underlying;
                        }
                    }
                    field.fields.push_back(exportField(payload, "$element", path + ".$element", location));
                    if (!buffer && field.fields.back().kind != UGLIR::TypeKind::Bool && field.fields.back().kind != UGLIR::TypeKind::Int && field.fields.back().kind != UGLIR::TypeKind::UInt && field.fields.back().kind != UGLIR::TypeKind::Float && field.fields.back().kind != UGLIR::TypeKind::Half && field.fields.back().kind != UGLIR::TypeKind::Vector)
                    {
                        argumentError(mContext, location, path + " requires a scalar or vector texture element type.");
                    }
                    if (containsResource(field.fields.back()))
                    {
                        argumentError(mContext, location, path + " cannot contain GPU-side resource pointer graphs.");
                    }
                    return field;
                }
                if (mQueries.isBindGroupHandleType(type))
                {
                    argumentError(mContext, location, path + " cannot be a BindGroup; Args carry only values and direct resources. Declare bind groups on a Slot class with [[SlotN]] parameters and bind them with setBindGroup.");
                }
                if (primary == "eastl::array" || primary == "std::array")
                {
                    const auto arguments = mQueries.getTemplateArgumentsFromType(type);
                    if (arguments.size() != 2)
                    {
                        argumentError(mContext, location, path + " has an unsupported array declaration.");
                    }
                    field.kind = UGLIR::TypeKind::Array;
                    field.arrayCount = templateDimension(arguments[1], location);
                    field.fields.push_back(exportField(elementType(type, location), "$element", path + "[]", location));
                    return field;
                }
                if (record->isUnion() || record->isPolymorphic() || record->getNumBases() != 0 || !record->isTriviallyCopyable())
                {
                    argumentError(mContext, location, path + " requires a trivially copyable record without unions, base classes, or virtual state.");
                }
                if (!mActiveRecords.insert(record).second)
                {
                    argumentError(mContext, location, path + " has a recursive argument layout.");
                }
                try
                {
                    for (const auto *member : record->fields())
                    {
                        if (member->isMutable())
                        {
                            argumentError(mContext, member->getLocation(), path + "." + member->getNameAsString() + " cannot be a mutable Args field.");
                        }
                        if (member->isBitField() || member->isAnonymousStructOrUnion() || member->getAccess() == clang::AS_private || member->getAccess() == clang::AS_protected)
                        {
                            argumentError(mContext, member->getLocation(), path + "." + member->getNameAsString() + " requires an ordinary public field.");
                        }
                        field.fields.push_back(exportField(member->getType(), member->getNameAsString(), path + "." + member->getNameAsString(), member->getLocation()));
                    }
                }
                catch (...)
                {
                    mActiveRecords.erase(record);
                    throw;
                }
                mActiveRecords.erase(record);
                return field;
            }

            /** Returns whether a recursive field includes a resource leaf. */
            bool containsResource(const ConstructorArgumentField &field) const
            {
                if (field.resourceKind != UGLIR::ResourceKind::Unknown)
                {
                    return true;
                }
                for (const auto &child : field.fields)
                {
                    if (containsResource(child))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Validates the macro's fields and parameters as one class-wide ordered contract. */
            void ensureContract(const UGLIR::ShaderClassRoot &root)
            {
                const auto *record = root.recordDecl;
                if (mIndices.count(record->getCanonicalDecl()) || mFailedContracts.count(record->getCanonicalDecl()))
                {
                    return;
                }
                auto &constructor = argumentConstructor(mContext, *record);
                ConstructorArgumentContract contract;
                contract.className = mQueries.generateTypeCanonicalName(mContext.getRecordType(record));
                contract.classKind = root.kind;
                contract.sourceLocation = argumentLocation(mContext, record->getLocation());
                for (uint32_t index = 0; index < constructor.getNumParams(); ++index)
                {
                    const auto *parameter = constructor.getParamDecl(index);
                    const auto annotation = indexedAnnotation(mContext, *parameter, "Args", 64);
                    if (annotation != static_cast<int32_t>(index))
                    {
                        argumentError(mContext, parameter->getLocation(), contract.className + " constructor argument " + std::to_string(index) + " must declare Args" + std::to_string(index) + (annotation < 0 ? "; annotation is missing." : "; found Args" + std::to_string(annotation) + "."));
                    }
                    if (hasAnnotationPrefix(*parameter, "Slot") || parameter->hasDefaultArg())
                    {
                        argumentError(mContext, parameter->getLocation(), "Args constructor parameters cannot use Slot annotations or default arguments.");
                    }
                    if (parameter->getType()->isReferenceType())
                    {
                        argumentError(mContext, parameter->getLocation(), "Args constructor parameters cannot use reference types.");
                    }
                    const clang::FieldDecl *member = nullptr;
                    for (const auto *field : record->fields())
                    {
                        if (field->getName() == parameter->getName())
                        {
                            member = field;
                        }
                    }
                    if (member == nullptr || !mContext.hasSameType(member->getType().getUnqualifiedType(), parameter->getType().getUnqualifiedType()) || indexedAnnotation(mContext, *member, "Args", 64) != annotation || hasAnnotationPrefix(*member, "Slot"))
                    {
                        argumentError(mContext, parameter->getLocation(), "Args parameter requires a matching constructor-generated member.");
                    }
                    if (!member->getType().isConstQualified() || member->isMutable())
                    {
                        argumentError(mContext, member->getLocation(), "Args members must be readonly in the shader semantic frontend.");
                    }
                    auto field = exportField(parameter->getType(), parameter->getNameAsString(), parameter->getNameAsString(), parameter->getLocation());
                    field.canonicalType = canonicalType(parameter->getType().getUnqualifiedType());
                    contract.parameters.push_back(std::move(field));
                }
                mIndices[record->getCanonicalDecl()] = mContracts.size();
                for (auto *method : record->methods())
                {
                    UGLIR::ShaderEntryKind entryKind = UGLIR::ShaderEntryKind::None;
                    const auto name = method->getNameAsString();
                    if (root.kind == UGLIR::ShaderClassKind::Compute && name == mUGLComputeShaderFunctionName)
                    {
                        entryKind = UGLIR::ShaderEntryKind::Compute;
                    }
                    if (root.kind == UGLIR::ShaderClassKind::Render && name == mUGLVertexShaderFunctionName)
                    {
                        entryKind = UGLIR::ShaderEntryKind::Vertex;
                    }
                    if (root.kind == UGLIR::ShaderClassKind::Render && name == mUGLFragmentShaderFunctionName)
                    {
                        entryKind = UGLIR::ShaderEntryKind::Fragment;
                    }
                    if (root.kind == UGLIR::ShaderClassKind::PixelLocal && name == mUGLPixelShaderFunctionName)
                    {
                        entryKind = UGLIR::ShaderEntryKind::PixelLocal;
                    }
                    if (entryKind == UGLIR::ShaderEntryKind::None || !method->hasBody())
                    {
                        continue;
                    }
                    ConstructorArgumentEntryUsage usage;
                    usage.name = name;
                    usage.entryKind = entryKind;
                    ConstructorArgumentUsage visitor(mContext, mSema, *record, contract, usage);
                    visitor.TraverseStmt(method->getBody());
                    std::sort(usage.argumentIndices.begin(), usage.argumentIndices.end());
                    contract.entries.push_back(std::move(usage));
                }
                mContracts.push_back(std::move(contract));
            }

            /** Uses Clang parameter copy-initialization, preserving user conversions and resource concepts. */
            void validateInitialization(clang::Expr &expression, clang::QualType expected, const std::string &description)
            {
                const auto entity = clang::InitializedEntity::InitializeParameter(mContext, expected, false);
                const auto kind = clang::InitializationKind::CreateCopy(expression.getExprLoc(), expression.getExprLoc());
                clang::Expr *argument = &expression;
                const clang::MultiExprArg arguments(&argument, 1);
                clang::InitializationSequence sequence(mSema, entity, kind, arguments);
                if (sequence.Failed())
                {
                    argumentError(mContext, expression.getExprLoc(), description + ": expected " + canonicalType(expected) + ", got " + canonicalType(expression.getType()) + ".");
                }
                if (sequence.Perform(mSema, entity, kind, arguments).isInvalid())
                {
                    argumentError(mContext, expression.getExprLoc(), description + ": C++ initialization is invalid.");
                }
            }

            /** Checks scalar initialization or the existing usage-tag constraint for one operation control. */
            void validateControl(clang::Expr &expression, const ArgumentControl &control)
            {
                if (control.kind == ArgumentControlKind::IndexBuffer || control.kind == ArgumentControlKind::IndirectBuffer)
                {
                    const auto *record = expression.getType().getNonReferenceType()->getAsCXXRecordDecl();
                    const auto usage = control.kind == ArgumentControlKind::IndexBuffer ? "UGL::Index" : "UGL::Indirect";
                    if (record == nullptr || !hasBufferUsage(*record, usage))
                    {
                        argumentError(mContext, expression.getExprLoc(), std::string(control.name) + " must satisfy " + controlTypeName(control.kind) + ".");
                    }
                    return;
                }
                clang::QualType expected = control.kind == ArgumentControlKind::Signed ? mContext.IntTy : mContext.UnsignedIntTy;
                if (control.kind == ArgumentControlKind::IndexFormat)
                {
                    expected = findIndexFormat();
                }
                validateInitialization(expression, expected, std::string("operation control ") + control.name);
            }

            /** Resolves the existing UGL index-format enum instead of comparing an actual type's spelling. */
            clang::QualType findIndexFormat()
            {
                const clang::DeclarationName uglName(&mContext.Idents.get("UGL"));
                for (const auto *declaration : mContext.getTranslationUnitDecl()->lookup(uglName))
                {
                    if (const auto *space = llvm::dyn_cast<clang::NamespaceDecl>(declaration))
                    {
                        for (const auto *named : space->lookup(clang::DeclarationName(&mContext.Idents.get("IndexFormat"))))
                        {
                            if (const auto *enumeration = llvm::dyn_cast<clang::EnumDecl>(named))
                            {
                                return mContext.getTypeDeclType(enumeration);
                            }
                        }
                    }
                }
                throw std::runtime_error("UGLC Args: the UGL IndexFormat declaration is missing.");
            }

            clang::ASTContext &mContext;
            clang::Sema &mSema;
            BaseASTVisitor mQueries;
            llvm::DenseMap<const clang::CXXRecordDecl *, UGLIR::ShaderClassRoot> mRoots;
            llvm::DenseMap<const clang::CXXRecordDecl *, size_t> mIndices;
            llvm::SmallPtrSet<const clang::CXXRecordDecl *, 16> mFailedContracts;
            llvm::SmallPtrSet<const clang::CXXRecordDecl *, 16> mActiveRecords;
            std::vector<ConstructorArgumentContract> mContracts;
        };
    }

    std::vector<ConstructorArgumentContract> prepareConstructorArgumentContracts(
        clang::ASTContext &context, clang::Sema &sema,
        llvm::ArrayRef<UGLIR::ShaderClassRoot> roots, bool includeZeroArgumentClasses)
    {
        ConstructorArgumentAnalyzer analyzer(context, sema, roots);
        return analyzer.analyze(roots, includeZeroArgumentClasses);
    }

    std::string dumpConstructorArgumentContracts(const std::vector<ConstructorArgumentContract> &contracts)
    {
        nlohmann::json result = {{"version", 1}, {"nativeAbiImplemented", false}, {"classes", nlohmann::json::array()}};
        for (const auto &contract : contracts)
        {
            nlohmann::json entry = {{"class", contract.className}, {"classKind", static_cast<uint32_t>(contract.classKind)},
                {"version", contract.version}, {"parameters", nlohmann::json::array()},
                {"entries", nlohmann::json::array()}, {"operations", nlohmann::json::array()}};
            for (uint32_t index = 0; index < contract.parameters.size(); ++index)
            {
                auto parameter = fieldJson(contract.parameters[index]);
                parameter["index"] = index;
                entry["parameters"].push_back(std::move(parameter));
            }
            for (const auto &usage : contract.entries)
            {
                entry["entries"].push_back({{"name", usage.name}, {"entryKind", static_cast<uint32_t>(usage.entryKind)}, {"arguments", usage.argumentIndices}});
            }
            const std::vector<std::string> operations = contract.classKind == UGLIR::ShaderClassKind::Compute ? std::vector<std::string>{"dispatchThreads", "dispatchWorkgroupsIndirect"} : contract.classKind == UGLIR::ShaderClassKind::Render ? std::vector<std::string>{"draw", "drawIndexed", "drawIndirect", "drawIndexedIndirect", "drawIndirectCount", "drawIndexedIndirectCount"} : std::vector<std::string>{"draw"};
            for (const auto &operation : operations)
            {
                nlohmann::json parameters = nlohmann::json::array();
                for (const auto &parameter : contract.parameters)
                {
                    parameters.push_back({{"name", parameter.name}, {"type", parameter.canonicalType}});
                }
                for (const auto &control : operationControls(contract.classKind, operation))
                {
                    parameters.push_back({{"name", control.name}, {"type", controlTypeName(control.kind)}});
                    if ((operation == "drawIndirectCount" || operation == "drawIndexedIndirectCount") &&
                        llvm::StringRef(control.name) == "stride")
                    {
                        parameters.back()["default"] = 0;
                    }
                }
                entry["operations"].push_back({{"name", operation}, {"argumentCount", contract.parameters.size()}, {"returnType", contract.classKind == UGLIR::ShaderClassKind::Compute ? "ComputePassTaskDescriptor" : "RenderPassTaskDescriptor"}, {"parameters", std::move(parameters)}});
            }
            result["classes"].push_back(std::move(entry));
        }
        return result.dump(2) + "\n";
    }
}
