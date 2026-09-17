#include "CPPVisitor.hpp"
#include <CodeGen/ShaderEmitter/PreparedShaderTranslationUnit.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include "../UGLC.Constants.hpp"

#include "CPPHostClassValidator.hpp"
#include "CPPPixelLocalAnalysis.hpp"
#include "CPPStaticShaderVariantCollector.hpp"
#include "CPPVertexFormatTypeConvertor.hpp"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/FixIt.h"
#include <CodeGen/CodeWriter.hpp>
#include <CodeGen/PixelLocalFieldAnalysis.hpp>
#include <CodeGen/ShaderBackendRegistry.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        bool shouldSkipNamespaceVisit(const clang::NamespaceDecl *decl, const clang::SourceManager &sourceManager)
        {
            if (decl == nullptr)
            {
                return true;
            }

            return decl->isAnonymousNamespace() || decl->isInline() || sourceManager.isInSystemHeader(decl->getLocation());
        }

        /** Converts a template argument spelling into a stable identifier-safe variant label component. */
        std::string sanitizeVariantIdentifierComponent(const std::string &input)
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

        /** Stores generated host descriptor expressions for one bind-group resource field. */
        struct BindGroupFieldCodegenInfo
        {
            BaseShaderResourceKind kind = BaseShaderResourceKind::UniformBuffer;
            std::string bufferTypeExpr;
            std::string bufferAccessExpr;
            std::string textureSampleTypeExpr;
            std::string viewDimensionExpr;
            std::string storageTextureAccessExpr;
            std::string storageTextureFormatExpr;
            std::string samplerTypeExpr;
            std::string bindGroupEntryTarget;
        };

        /** Tracks which shader stages reference a bind group while inferring its visibility mask. */
        struct BindGroupStageUsage
        {
            bool usesVertex = false;
            bool usesFragment = false;
            bool usesCompute = false;
            bool usesHull = false;
            bool usesDomain = false;
        };

        /** Returns the component count of a DSL vector alias or its generated packed GLM representation. */
        std::optional<int> getUGLVectorComponentCount(const std::string &typeName)
        {
            const size_t packedVector = typeName.find("glm::vec<");
            if (packedVector != std::string::npos && typeName.size() > packedVector + 10 &&
                typeName[packedVector + 10] == ',' && typeName[packedVector + 9] >= '2' && typeName[packedVector + 9] <= '4')
            {
                return typeName[packedVector + 9] - '0';
            }
            const size_t namespacePos = typeName.rfind("::");
            const std::string localName = namespacePos == std::string::npos ? typeName : typeName.substr(namespacePos + 2);
            if (localName.size() < 4)
            {
                return std::nullopt;
            }

            const char componentCountChar = localName.back();
            if (componentCountChar < '2' || componentCountChar > '4')
            {
                return std::nullopt;
            }

            const std::string scalarName = localName.substr(0, localName.size() - 1);
            static constexpr std::array<const char *, 6> vectorScalarNames = {
                "half",
                "float",
                "double",
                "int",
                "uint",
                "bool",
            };

            if (std::find(vectorScalarNames.begin(), vectorScalarNames.end(), scalarName) == vectorScalarNames.end())
            {
                return std::nullopt;
            }

            return componentCountChar - '0';
        }

        bool isSupportedSwizzleCharacter(char c)
        {
            switch (c)
            {
            case 'x':
            case 'y':
            case 'z':
            case 'w':
            case 'r':
            case 'g':
            case 'b':
            case 'a':
                return true;
            default:
                return false;
            }
        }

        bool shouldIgnoreShaderClassMethodForEntryValidation(const clang::CXXMethodDecl *method)
        {
            return method == nullptr ||
                   method->isImplicit() ||
                   llvm::isa<clang::CXXConstructorDecl>(method) ||
                   llvm::isa<clang::CXXDestructorDecl>(method) ||
                   method->isCopyAssignmentOperator() ||
                   method->isMoveAssignmentOperator();
        }

        void validateShaderClassEntryMethodListOrThrow(CPPVisitor &visitor,
                                                       const clang::CXXRecordDecl *decl,
                                                       const std::string &ownerKind,
                                                       const std::vector<std::string> &allowedEntryNames,
                                                       const std::vector<const clang::CXXMethodDecl *> &selectedEntryMethods,
                                                       const std::string &rewriteTarget)
        {
            if (decl == nullptr)
            {
                return;
            }

            std::vector<const clang::CXXMethodDecl *> canonicalSelectedEntryMethods;
            for (const auto *selectedEntryMethod : selectedEntryMethods)
            {
                if (selectedEntryMethod != nullptr)
                {
                    canonicalSelectedEntryMethods.emplace_back(selectedEntryMethod->getCanonicalDecl());
                }
            }

            for (const clang::CXXMethodDecl *method : decl->methods())
            {
                if (shouldIgnoreShaderClassMethodForEntryValidation(method) ||
                    visitor.checkAttibuteByName(method, mUGLCTORName))
                {
                    continue;
                }

                const clang::CXXMethodDecl *canonicalMethod = method->getCanonicalDecl();
                if (std::find(canonicalSelectedEntryMethods.begin(), canonicalSelectedEntryMethods.end(), canonicalMethod) != canonicalSelectedEntryMethods.end())
                {
                    continue;
                }

                const std::string methodName = method->getNameAsString();
                const bool methodLooksLikeShaderEntry = std::find(allowedEntryNames.begin(), allowedEntryNames.end(), methodName) != allowedEntryNames.end();
                const std::string methodRole = methodLooksLikeShaderEntry ? "helper overload" : "helper method";
                visitor.throwCodegenError(method,
                                          ownerKind + " \"" + decl->getQualifiedNameAsString()
                                              + "\" cannot declare " + methodRole + " \"" + methodName
                                              + "\". Only the selected constructor and shader entry methods are allowed. Move helper logic outside the shader class or inline it into "
                                          + rewriteTarget + "().");
            }
        }

        bool classUsesRenderEntityBuiltins(CPPVisitor &visitor, const clang::CXXRecordDecl *decl)
        {
            if (decl == nullptr)
            {
                return false;
            }

            for (const auto *method : decl->methods())
            {
                for (const auto *param : method->parameters())
                {
                    if (visitor.checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) ||
                        visitor.checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        /** Materializes a GLM swizzle once before converting its scalar type. */
        std::optional<std::string> materializeSingleSwizzleConstructorArgument(const std::string &typeName,
                                                                          const std::string &argument,
                                                                          int expectedComponentCount)
        {
            const size_t firstNonSpace = argument.find_first_not_of(" \t\n\r");
            if (firstNonSpace == std::string::npos)
            {
                return std::nullopt;
            }

            const size_t lastNonSpace = argument.find_last_not_of(" \t\n\r");
            const std::string trimmedArgument = argument.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
            const size_t memberSeparator = trimmedArgument.rfind('.');
            if (memberSeparator == std::string::npos || memberSeparator == 0)
            {
                return std::nullopt;
            }

            const std::string swizzle = trimmedArgument.substr(memberSeparator + 1);
            if (static_cast<int>(swizzle.size()) != expectedComponentCount)
            {
                return std::nullopt;
            }

            if (!std::all_of(swizzle.begin(), swizzle.end(), isSupportedSwizzleCharacter))
            {
                return std::nullopt;
            }

            return "GVM::Core::Math::convertShaderSwizzle<" + typeName + ">(" + trimmedArgument + ")";
        }

    } // namespace


    CPPVisitor::CPPVisitor(clang::Rewriter *Rewriter, clang::ASTContext *Context, ShaderSourcePipelineOptions shaderSourcePipelineOptions)
        : BaseASTVisitor(Context, &mTypeConvertor, nullptr, &mFuncConvertor)
        , mHostShaderOnlyGuard(*this)
        , mRendererEmitter(*this)
        , mShaderArtifactEmitter(*this)
        , mHostClassValidator(std::make_unique<CPPHostClassValidator>(*this))
        , mShaderSourcePipelineOptions(shaderSourcePipelineOptions)
        , Rewriter(Rewriter)
    {
        mEnableLineDirectiveInsertion = false;
    }
    CPPVisitor::~CPPVisitor() = default;

    std::optional<std::string> CPPVisitor::getErasedTemplateSpecializationName(const clang::ClassTemplateSpecializationDecl *decl, const AbstractTypeConvertor *typeConvertor) const
    {
        (void)typeConvertor;
        if (!shouldMaterializeTemplateSpecialization(decl))
        {
            return std::nullopt;
        }
        return const_cast<CPPVisitor *>(this)->getRecordVariantLabel(decl);
    }

    void CPPVisitor::collectStaticShaderVariants(clang::TranslationUnitDecl *translationUnitDecl)
    {
        CPPStaticShaderVariantCollector collector(*this);
        collector.TraverseDecl(translationUnitDecl);
        mMaterializedTemplateSpecializations = collector.getMaterializedTemplateSpecializations();
        mShaderVariantRootSpecializations = collector.getShaderVariantRootSpecializations();
    }

    bool CPPVisitor::functionRequiresDeletedHostDefinition(const clang::FunctionDecl *func)
    {
        return mHostShaderOnlyGuard.functionRequiresDeletedDefinition(func);
    }
    std::string CPPVisitor::generateDeletedFunctionDeclaration(const clang::FunctionDecl *func, const std::string &funcNameOverride)
    {
        return mHostShaderOnlyGuard.generateDeletedFunctionDeclaration(func, funcNameOverride);
    }
    std::array<std::string, 3> CPPVisitor::getValidatedLocalWorkGroupSize(const clang::CXXRecordDecl *decl)
    {
        return mHostClassValidator->getValidatedLocalWorkGroupSize(decl);
    }
    bool CPPVisitor::isShaderClassBindGroupParamType(const clang::QualType &qt) const
    {
        return mHostClassValidator->isShaderClassBindGroupParamType(qt);
    }
    void CPPVisitor::validateShaderClassBindGroupSlots(const clang::CXXRecordDecl *decl, const clang::FunctionDecl *createFunc, const std::string &ownerKind, int bindgroupBufferOffset)
    {
        mHostClassValidator->validateShaderClassBindGroupSlots(decl, createFunc, ownerKind, bindgroupBufferOffset);
    }
    std::string CPPVisitor::buildEmbeddedShaderArtifactMember(const std::string &memberName, const std::string &shaderHeaderVariableName, const clang::CXXRecordDecl *shaderClassDecl, const clang::FunctionDecl *entryFunction, const BindGroupInfoMap &bindGroupInfoMap, const std::vector<clang::Decl *> &extraDecls, UGLC::CodeGen::IShaderSourceEmitter &shaderSourceEmitter)
    {
        return mShaderArtifactEmitter.buildEmbeddedShaderArtifactMember(memberName,
                                                                        shaderHeaderVariableName,
                                                                        shaderClassDecl,
                                                                        entryFunction,
                                                                        bindGroupInfoMap,
                                                                        extraDecls,
                                                                        shaderSourceEmitter,
                                                                        getCurrentTemplateSubstitutionContext());
    }
    int CPPVisitor::getValidatedVertexAttributeLocation(const clang::FieldDecl *fieldDecl, const std::string &renderClassName, const std::string &vertexInputTypeName)
    {
        return mHostClassValidator->getValidatedVertexAttributeLocation(fieldDecl, renderClassName, vertexInputTypeName);
    }
    std::vector<CPPVisitor::VertexAttributeLayoutInfo> CPPVisitor::getValidatedVertexAttributeLayout(const clang::ParmVarDecl *vertexInputParam, const std::string &renderClassName)
    {
        return mHostClassValidator->getValidatedVertexAttributeLayout(vertexInputParam, renderClassName);
    }
    bool CPPVisitor::isRenderVaryingSystemSemanticField(const clang::FieldDecl *fieldDecl) const
    {
        return mHostClassValidator->isRenderVaryingSystemSemanticField(fieldDecl);
    }
    int CPPVisitor::getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *fieldDecl, const std::string &renderClassName, const std::string &recordRole, const std::string &recordTypeName) const
    {
        return mHostClassValidator->getValidatedRenderVaryingAttributeLocation(fieldDecl, renderClassName, recordRole, recordTypeName);
    }
    std::unordered_map<int, CPPVisitor::RenderVaryingLayoutInfo> CPPVisitor::collectValidatedRenderVaryingLayoutOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName, const std::string &recordRole, bool requirePosition) const
    {
        return mHostClassValidator->collectValidatedRenderVaryingLayout(recordDecl, renderClassName, recordRole, requirePosition);
    }
    void CPPVisitor::validateVertexFragmentVaryingContractOrThrow(const clang::CXXRecordDecl *vertexOutputRecord, const clang::CXXRecordDecl *fragmentInputRecord, const std::string &renderClassName) const
    {
        mHostClassValidator->validateVertexFragmentVaryingContract(vertexOutputRecord, fragmentInputRecord, renderClassName);
    }
    const clang::FunctionDecl *CPPVisitor::requireCreateMethodOrThrow(const clang::CXXRecordDecl *decl, const std::string &ownerKind) const
    {
        return mHostClassValidator->requireCreateMethod(decl, ownerKind);
    }
    const clang::FunctionDecl *CPPVisitor::requireRendererRenderMethodOrThrow(const clang::CXXRecordDecl *decl) const
    {
        return mHostClassValidator->requireRendererRenderMethod(decl);
    }
    void CPPVisitor::validateFrameBufferFieldsOrThrow(const clang::CXXRecordDecl *decl, const std::string &framebufferName) const
    {
        mHostClassValidator->validateFrameBufferFields(decl, framebufferName);
    }
    clang::QualType CPPVisitor::requireAttachmentTemplateTypeOrThrow(const clang::FieldDecl *fieldDecl, const std::string &ownerKind, const std::string &ownerName) const
    {
        return mHostClassValidator->requireAttachmentTemplateType(fieldDecl, ownerKind, ownerName);
    }
    void CPPVisitor::validateRenderTargetRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const
    {
        mHostClassValidator->validateRenderTargetRecord(recordDecl, renderClassName);
    }
    void CPPVisitor::validateRenderSetComponentFieldAttributesOrThrow(const clang::FieldDecl *fieldDecl, const std::string &renderSetName) const
    {
        mHostClassValidator->validateRenderSetComponentFieldAttributes(fieldDecl, renderSetName);
    }
    void CPPVisitor::validateRenderSetTextureResourceCountOrThrow(const clang::FieldDecl *fieldDecl, const std::string &renderSetName, int maxResourceCount) const
    {
        mHostClassValidator->validateRenderSetTextureResourceCount(fieldDecl, renderSetName, maxResourceCount);
    }
    bool CPPVisitor::VisitNamespaceDecl(clang::NamespaceDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (isFromExcludedFile(decl->getLocation()) || shouldSkipNamespaceVisit(decl, Context->getSourceManager()))
            {
                return true;
            }
            // Namespace emission is handled by the surrounding declaration walkers.
            // Keep RecursiveASTVisitor traversing children here so namespace shape
            // never depends on a confusing boolean shortcut in this callback.
            return true;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    bool CPPVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (isFromExcludedFile(decl->getLocation()) || isTopLevel(decl) == false)
            {
                return true;
            }
            if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl))
            {
                return true;
            }
            if (decl->getDescribedClassTemplate() != nullptr)
            {
                return true;
            }
            if (checkUGLDerivedClass(decl) || recordUsesDirectShaderResourceHandles(decl))
            {
                auto result = getLineDirective(decl->getBeginLoc()) + replaceUGLClass(decl); // generateRecordDefinition(decl);

                clang::SourceRange bodyRange = decl->getSourceRange();
                Rewriter->ReplaceText(bodyRange, result);
            }

            return true;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    bool CPPVisitor::TraverseClassTemplateDecl(clang::ClassTemplateDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (decl == nullptr || isFromExcludedFile(decl->getLocation()) || isTopLevel(decl) == false)
            {
                return clang::RecursiveASTVisitor<CPPVisitor>::TraverseClassTemplateDecl(decl);
            }

            clang::CXXRecordDecl *templatedDecl = decl->getTemplatedDecl();
            if (shouldEmitUGLTemplateSpecializationBundle(templatedDecl))
            {
                auto result = getLineDirective(decl->getBeginLoc()) + replaceUGLTemplateSpecializationBundle(decl, templatedDecl);
                Rewriter->ReplaceText(decl->getSourceRange(), result);
                return true;
            }
            for (const auto *specializationDecl : decl->specializations())
            {
                if (shouldMaterializeTemplateSpecialization(specializationDecl))
                {
                    auto result = getLineDirective(decl->getBeginLoc()) + replacePlainTemplateSpecializationBundle(decl, templatedDecl);
                    Rewriter->ReplaceText(decl->getSourceRange(), result);
                    return true;
                }
            }

            return clang::RecursiveASTVisitor<CPPVisitor>::TraverseClassTemplateDecl(decl);
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    bool CPPVisitor::VisitFunctionDecl(clang::FunctionDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (isFromExcludedFile(decl->getLocation()) || isTopLevel(decl) == false)
            {
                return true;
            }

            if (llvm::isa<clang::CXXMethodDecl>(decl) || llvm::isa<clang::CXXConstructorDecl>(decl) || llvm::isa<clang::CXXDestructorDecl>(decl))
            {
                return true;
            }
            auto result = generateFunctionDefinition(decl);
            clang::SourceRange bodyRange = decl->getSourceRange();
            Rewriter->ReplaceText(bodyRange, result);

            return true;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }

    bool CPPVisitor::VisitVarDecl(clang::VarDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (isFromExcludedFile(decl->getLocation()))
            {
                return true;
            }
            CPPPixelLocalAnalysis(*this).validateAttachmentValueObjectVarOrThrow(decl);
            // 我们只关心文件作用域或命名空间作用域的变量 (即全局变量)
            if (decl->isFileVarDecl() || decl->isStaticDataMember())
            {
                auto result = translateVarDecl(decl); // + EOS();
                clang::SourceRange bodyRange = decl->getSourceRange();
                Rewriter->ReplaceText(bodyRange, result);
            }
            return true;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }
    bool CPPVisitor::VisitTypedefNameDecl(clang::TypedefNameDecl *decl)
    {
        auto diagnosticScope = scopeDiagnosticLocation(decl);
        try
        {
            if (isFromExcludedFile(decl->getLocation()))
            {
                return true;
            }
            /* std::string newName = decl->getNameAsString();
            std::string oldName = decl->getUnderlyingType().getAsString();

            std::string result;

            if (llvm::isa<clang::TypeAliasDecl>(decl))
            {
                result += "using " + newName + " = " + oldName; //+ EOS();
            }
            else
            {

                result += "typedef " + oldName + " " + newName; //+ EOS();
            } */
            std::string result = BaseASTVisitor::VisitTypedefNameDecl(decl);
            clang::SourceRange bodyRange = decl->getSourceRange();
            Rewriter->ReplaceText(bodyRange, result);
            return true;
        }
        catch (const std::exception &error)
        {
            rethrowCodegenError(error, decl);
        }
    }


    void CPPVisitor::insertLineDirective(clang::SourceLocation loc) const
    {
        // 检查位置是否有效且不位于宏中
        if (!loc.isValid() || loc.isMacroID())
        {
            return;
        }
        auto lineDirective = getLineDirective(loc);
        if (lineDirective.empty() == false)
        {
            // 使用 Rewriter 在指定位置前插入指令
            Rewriter->InsertText(loc, lineDirective, /*InsertAfter=*/false);
        }
    }

    std::string CPPVisitor::replaceUGLFrameBufferClass(const clang::CXXRecordDecl *decl)
    {
        // 在 HLSL 中，class 和 struct 几乎没有区别，我们统一生成为 struct
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string emittedName = getRecordEmissionName(decl);
        std::string name = getRecordVariantLabel(decl);
        validateFrameBufferFieldsOrThrow(decl, name);

        std::string result;
        result += mSpaceManager.getSpace() + keyword + " " + emittedName + NewLine();
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

        {
            auto fields = getAllFieldFromRecord(decl);
            std::vector<clang::FieldDecl *> colorAttachmentFields;
            clang::FieldDecl *depthAttachmentField = nullptr;
            for (size_t i = 0; i < fields.size(); ++i)
            {
                auto *f = fields[i];
                if (checkTypeCanonicalName(f->getType(), mUGLColorAttachmentName) ||
                    checkTypeCanonicalName(f->getType(), mUGLPixelLocalColorAttachmentName))
                {
                    colorAttachmentFields.emplace_back(f);
                }
                else if (checkTypeCanonicalName(f->getType(), mUGLDepthAttachmentName) ||
                         checkTypeCanonicalName(f->getType(), mUGLPixelLocalDepthAttachmentName))
                {
                    if (depthAttachmentField != nullptr)
                    {
                        throwCodegenError("FrameBuffer \"" + name + "\" can not contain more than one depth attachment.");
                    }
                    depthAttachmentField = f;
                }
            }
            result += mSpaceManager.getSpace() + "GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const" + NewLine();
            result += enterScope();
            result += mSpaceManager.getSpace() + "GVM::RHI::RenderPassDescriptor descriptor" + EOS();
            result += mSpaceManager.getSpace() + "descriptor.colorAttachments = {";
            for (size_t i = 0; i < colorAttachmentFields.size(); ++i)
            {
                const std::string color = colorAttachmentFields[i]->getNameAsString();
                result += "(GVM::RHI::RenderPassColorAttachment)this->" + color;
                if (i + 1 < colorAttachmentFields.size())
                {
                    result += ", ";
                }
            }
            result += "}" + EOS();
            for (size_t i = 0; i < colorAttachmentFields.size(); ++i)
            {
                auto *colorField = colorAttachmentFields[i];
                if (checkTypeCanonicalName(colorField->getType(), mUGLPixelLocalColorAttachmentName))
                {
                    result += mSpaceManager.getSpace() + "descriptor.colorAttachments[" + std::to_string(i) + "].loadOp = " + CPPPixelLocalAnalysis(*this).resolveLoadOp(colorField) + EOS();
                    result += mSpaceManager.getSpace() + "descriptor.colorAttachments[" + std::to_string(i) + "].storeOp = " + CPPPixelLocalAnalysis(*this).resolveStoreOp(colorField) + EOS();
                }
            }
            if (depthAttachmentField != nullptr)
            {
                result += mSpaceManager.getSpace() + "descriptor.depthStencilAttachment = (GVM::RHI::RenderPassDepthStencilAttachment)this->" + depthAttachmentField->getNameAsString() + EOS();
                if (checkTypeCanonicalName(depthAttachmentField->getType(), mUGLPixelLocalDepthAttachmentName))
                {
                    result += mSpaceManager.getSpace() + "descriptor.depthStencilAttachment.depthLoadOp = " + CPPPixelLocalAnalysis(*this).resolveLoadOp(depthAttachmentField) + EOS();
                    result += mSpaceManager.getSpace() + "descriptor.depthStencilAttachment.depthStoreOp = " + CPPPixelLocalAnalysis(*this).resolveStoreOp(depthAttachmentField) + EOS();
                }
            }
            result += mSpaceManager.getSpace() + "return descriptor" + EOS();
            result += quitScope();
        }


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
    std::string CPPVisitor::replaceUGLBindGroupClass(const clang::CXXRecordDecl *decl)
    {
        CPPBindGroupTypeConvertor hbgTypeConvertor;
        // 在 HLSL 中，class 和 struct 几乎没有区别，我们统一生成为 struct
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string emittedName = getRecordEmissionName(decl);
        std::string name = getRecordVariantLabel(decl);

        std::string allStaticAsserts;
        std::string result;
        result += mSpaceManager.getSpace() + keyword + " " + emittedName + ": public GVM::RHI::RefCountedObject" + NewLine();
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

        result += generateRecordDataMembers(decl, &hbgTypeConvertor);
        {

            result += mSpaceManager.getSpace() + "GVM::RHI::Device mDevice" + EOS();
            result += mSpaceManager.getSpace() + "GVM::RHI::BindGroupLayout mBindGroupLayout" + EOS();
            result += mSpaceManager.getSpace() + "GVM::RHI::BindGroup mBindGroup" + EOS();
        }
        {

            auto createFunc = requireCreateMethodOrThrow(decl, "BindGroup");
            auto baseBindings = resolveBaseShaderResourceBindings(decl, createFunc);
            applyPreparedBindGroupBindings(*decl, baseBindings);
            auto textureSampleTypeExpr = [](BaseShaderTextureSampleType sampleType) -> std::string {
                switch (sampleType)
                {
                case BaseShaderTextureSampleType::Float:
                    return "GVM::RHI::TextureSampleType::Float";
                case BaseShaderTextureSampleType::Sint:
                    return "GVM::RHI::TextureSampleType::Sint";
                case BaseShaderTextureSampleType::Uint:
                    return "GVM::RHI::TextureSampleType::Uint";
                case BaseShaderTextureSampleType::Depth:
                    return "GVM::RHI::TextureSampleType::Depth";
                case BaseShaderTextureSampleType::None:
                    break;
                }
                return {};
            };
            auto textureViewDimensionExpr = [](BaseShaderTextureDimension dimension) -> std::string {
                switch (dimension)
                {
                case BaseShaderTextureDimension::Texture2D:
                    return "GVM::RHI::TextureViewDimension::e2D";
                case BaseShaderTextureDimension::Texture2DArray:
                    return "GVM::RHI::TextureViewDimension::e2DArray";
                case BaseShaderTextureDimension::Texture3D:
                    return "GVM::RHI::TextureViewDimension::e3D";
                case BaseShaderTextureDimension::None:
                    break;
                }
                return {};
            };
            auto inferBindGroupFieldCodegenInfo = [&](const BaseShaderResourceBinding &baseBinding) -> BindGroupFieldCodegenInfo {
                BindGroupFieldCodegenInfo info{};
                info.kind = baseBinding.kind;

                if (baseBinding.kind == BaseShaderResourceKind::UniformBuffer)
                {
                    info.bufferTypeExpr = "GVM::RHI::BufferBindingType::Uniform";
                    info.bufferAccessExpr = "GVM::RHI::StorageBufferAccess::ReadOnly";
                    info.bindGroupEntryTarget = "buffer";
                    return info;
                }

                if (baseBinding.kind == BaseShaderResourceKind::StorageBuffer)
                {
                    info.bufferTypeExpr = "GVM::RHI::BufferBindingType::Storage";
                    info.bufferAccessExpr = baseBinding.access == BaseShaderResourceAccess::ReadOnly ? "GVM::RHI::StorageBufferAccess::ReadOnly" : "GVM::RHI::StorageBufferAccess::ReadWrite";
                    info.bindGroupEntryTarget = "buffer";
                    return info;
                }

                if (baseBinding.kind == BaseShaderResourceKind::SampledTexture)
                {
                    info.viewDimensionExpr = textureViewDimensionExpr(baseBinding.dimension);
                    info.textureSampleTypeExpr = textureSampleTypeExpr(baseBinding.sampleType);
                    info.bindGroupEntryTarget = "textureView";
                    return info;
                }

                if (baseBinding.kind == BaseShaderResourceKind::StorageTexture)
                {
                    info.viewDimensionExpr = textureViewDimensionExpr(baseBinding.dimension);
                    info.storageTextureAccessExpr = "GVM::RHI::StorageTextureAccess::ReadWrite";
                    info.storageTextureFormatExpr = generateTypeCanonicalName(baseBinding.elementType, &hbgTypeConvertor);
                    info.bindGroupEntryTarget = "textureView";
                    return info;
                }

                if (baseBinding.kind == BaseShaderResourceKind::Sampler)
                {
                    info.samplerTypeExpr = "GVM::RHI::SamplerBindingType::Filtering";
                    info.bindGroupEntryTarget = "sampler";
                    return info;
                }

                throwCodegenError("BindGroup \"" + name + "\" contains unsupported field type \"" + baseBinding.resourceTypeName + "\" on field \"" + baseBinding.fieldDecl->getNameAsString() + "\".");
            };

            std::vector<BindGroupFieldCodegenInfo> fieldInfos;
            fieldInfos.reserve(baseBindings.size());
            for (const auto &baseBinding : baseBindings)
            {
                fieldInfos.emplace_back(inferBindGroupFieldCodegenInfo(baseBinding));
            }

            if (functionRequiresDeletedHostDefinition(createFunc))
            {
                result += generateDeletedFunctionDeclaration(createFunc, "create");
            }
            else
            {
                result += generateFunctionSignature(createFunc, "create", "const", &hbgTypeConvertor);
                result += enterScope();
                for (unsigned i = 0; i < createFunc->getNumParams(); ++i)
                {
                    const clang::ParmVarDecl *param = createFunc->getParamDecl(i);

                    result += mSpaceManager.getSpace() + "this->" + param->getNameAsString() + " = " + param->getNameAsString() + EOS();
                }
                result += generateFunctionBody(createFunc);


                result += mSpaceManager.getSpace() + "GVM::RHI::BindGroupLayoutDescriptor bindGroupLayoutDescriptor" + EOS();
                result += mSpaceManager.getSpace() + "bindGroupLayoutDescriptor.label = " + "\"" + name + "BindGroupLayout\"" + EOS();
                result += mSpaceManager.getSpace() + "eastl::vector<GVM::RHI::BindGroupLayoutEntry> layoutEntry(" + std::to_string(baseBindings.size()) + ")" + EOS();
                for (size_t entryIndex = 0; entryIndex < baseBindings.size(); ++entryIndex)
                {
                    const auto *param = baseBindings[entryIndex].fieldDecl;
                    const auto &fieldInfo = fieldInfos[entryIndex];
                    result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].binding = " + std::to_string(baseBindings[entryIndex].bindingIndex) + EOS();
                    result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].visibility = " + inferBindGroupVisibilityExpr(decl) + EOS();
                    if (fieldInfo.kind == BaseShaderResourceKind::UniformBuffer || fieldInfo.kind == BaseShaderResourceKind::StorageBuffer)
                    {
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].buffer.type = " + fieldInfo.bufferTypeExpr + EOS();
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].buffer.access = " + fieldInfo.bufferAccessExpr + EOS();
                    }
                    else if (fieldInfo.kind == BaseShaderResourceKind::SampledTexture)
                    {
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].texture.sampleType = " + fieldInfo.textureSampleTypeExpr + EOS();
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].texture.viewDimension = " + fieldInfo.viewDimensionExpr + EOS();
                    }
                    else if (fieldInfo.kind == BaseShaderResourceKind::StorageTexture)
                    {
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].storageTexture.access = " + fieldInfo.storageTextureAccessExpr + EOS();
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].storageTexture.viewDimension = " + fieldInfo.viewDimensionExpr + EOS();
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].storageTexture.format = " + fieldInfo.storageTextureFormatExpr + EOS();
                    }
                    else if (fieldInfo.kind == BaseShaderResourceKind::Sampler)
                    {
                        result += mSpaceManager.getSpace() + "layoutEntry[" + std::to_string(entryIndex) + "].sampler.type = " + fieldInfo.samplerTypeExpr + EOS();
                    }
                    else
                    {
                        throwCodegenError("BindGroup \"" + name + "\" contains unsupported field type \"" + generateTypeCanonicalName(param->getType()) + "\" on field \"" + param->getNameAsString() + "\".");
                    }
                }
                result += mSpaceManager.getSpace() + "bindGroupLayoutDescriptor.entries = layoutEntry" + EOS();
                result += mSpaceManager.getSpace() + "this->mBindGroupLayout = this->mDevice->createBindGroupLayout(bindGroupLayoutDescriptor)" + EOS();
                result += mSpaceManager.getSpace() + "GVM::RHI::BindGroupDescriptor bindGroupDescriptor" + EOS();
                result += mSpaceManager.getSpace() + "bindGroupDescriptor.label = " + "\"" + name + "BindGroup\"" + EOS();
                result += mSpaceManager.getSpace() + "bindGroupDescriptor.layout = this->mBindGroupLayout" + EOS();
                result += mSpaceManager.getSpace() + "eastl::vector<GVM::RHI::BindGroupEntry> bindgroupEntry(" + std::to_string(baseBindings.size()) + ")" + EOS();
                for (size_t entryIndex = 0; entryIndex < baseBindings.size(); ++entryIndex)
                {
                    const auto *param = baseBindings[entryIndex].fieldDecl;
                    const auto &fieldInfo = fieldInfos[entryIndex];
                    result += mSpaceManager.getSpace() + "bindgroupEntry[" + std::to_string(entryIndex) + "].binding = " + std::to_string(baseBindings[entryIndex].bindingIndex) + EOS();
                    if (fieldInfo.bindGroupEntryTarget.empty() == false)
                    {
                        result += mSpaceManager.getSpace() + "bindgroupEntry[" + std::to_string(entryIndex) + "]." + fieldInfo.bindGroupEntryTarget + " = " + param->getNameAsString() + EOS();
                    }
                    else
                    {
                        throwCodegenError("BindGroup \"" + name + "\" field \"" + param->getNameAsString() + "\" did not resolve to a bind group entry target.");
                    }
                }
                result += mSpaceManager.getSpace() + "bindGroupDescriptor.entries = bindgroupEntry" + EOS();
                result += mSpaceManager.getSpace() + "this->mBindGroup = this->mDevice->createBindGroup(bindGroupDescriptor)" + EOS();
                result += quitScope();
            }
        }


        // 我们还需要处理成员函数
        for (auto *method : decl->methods())
        {
            if (checkAttibuteByName(method, mUGLCTORName) == false && method->hasBody())
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += allStaticAsserts;
        result += endClass();
        return result;
    }
    std::string CPPVisitor::replaceUGLRenderClass(const clang::CXXRecordDecl *decl)
    {
        CPPTypeConvertor hTypeConvertor;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string emittedName = getRecordEmissionName(decl);
        std::string name = getRecordVariantLabel(decl);
        const bool isPixelLocalRenderClass = checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName);
        std::string result;
        result += mSpaceManager.getSpace() + keyword + " " + emittedName + " : public " + std::string(isPixelLocalRenderClass ? "GVM::Core::IPixelLocalRenderClass" : "GVM::Core::IRenderClass") + ", public GVM::RHI::RefCountedObject" + NewLine();
        result += enterScope();


        std::vector<clang::Decl *> extraDecls;

        for (auto *innerDecl : decl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (shouldEmitNestedRecordDefinition(decl, nestedRecord))
                {
                    result += getLineDirective(nestedRecord->getBeginLoc());
                    result += generateRecordDefinition(nestedRecord);
                    extraDecls.emplace_back(innerDecl);
                }
            }
        }

        result += generateRecordDataMembers(decl);
        BindGroupInfoMap bindGroupInfoMap;
        const ShaderBackendCapabilities &primaryBackendCapabilities = UGLC::CodeGen::getPrimaryShaderBackendCapabilities();
        auto shaderVertexFunc = getMethodFromClass(decl, mUGLVertexShaderFunctionName, makeVertexShaderMethodLookupOptions());
        auto shaderHullFunc = getMethodFromClass(decl, mUGLHullShaderFunctionName, makeHullShaderMethodLookupOptions());
        auto shaderDomainFunc = getMethodFromClass(decl, mUGLDomainShaderFunctionName, makeDomainShaderMethodLookupOptions());
        auto pixelMethod = getMethodFromClass(decl, mUGLPixelShaderFunctionName, makeFragmentShaderMethodLookupOptions(std::nullopt));
        auto rasterFragmentMethod = getMethodFromClass(decl, mUGLFragmentShaderFunctionName, makeFragmentShaderMethodLookupOptions(shaderVertexFunc != nullptr ? std::optional<clang::QualType>(shaderVertexFunc->getReturnType()) : std::nullopt));
        auto createFunc = requireCreateMethodOrThrow(decl, "RenderClass");
        validateShaderClassEntryMethodListOrThrow(*this,
                                                  decl,
                                                  isPixelLocalRenderClass ? "PixelLocalRenderClass" : "RenderClass",
                                                  isPixelLocalRenderClass ? std::vector<std::string>{mUGLPixelShaderFunctionName}
                                                                          : std::vector<std::string>{mUGLVertexShaderFunctionName,
                                                                                                     mUGLFragmentShaderFunctionName,
                                                                                                     mUGLHullShaderFunctionName,
                                                                                                     mUGLDomainShaderFunctionName,
                                                                                                     mUGLConstantsHullShaderFunctionName},
                                                  isPixelLocalRenderClass ? std::vector<const clang::CXXMethodDecl *>{pixelMethod}
                                                                          : std::vector<const clang::CXXMethodDecl *>{shaderVertexFunc,
                                                                                                                       rasterFragmentMethod,
                                                                                                                       shaderHullFunc,
                                                                                                                       shaderDomainFunc},
                                                  isPixelLocalRenderClass ? mUGLPixelShaderFunctionName : mUGLFragmentShaderFunctionName);

        {
            if (isPixelLocalRenderClass)
            {
                if (shaderVertexFunc != nullptr || rasterFragmentMethod != nullptr || shaderHullFunc != nullptr || shaderDomainFunc != nullptr)
                {
                    throwCodegenError("PixelLocalRenderClass \"" + name + "\" is pixel-only. Use IRenderClass for raster vertex()/fragment() producers in pixelLocalPass(...).");
                }
                if (pixelMethod == nullptr)
                {
                    throwCodegenError("PixelLocalRenderClass \"" + name + "\" must declare pixel(...).");
                }
            }
            else
            {
                if (shaderVertexFunc == nullptr)
                {
                    throwCodegenError("RenderClass \"" + name + "\" is missing required shader entry method \"" + mUGLVertexShaderFunctionName + "(...)\".");
                }
                if (pixelMethod != nullptr)
                {
                    throwCodegenError("RenderClass \"" + name + "\" declares pixel-local entry \"pixel(...)\" but does not derive from UGL::IPixelLocalRenderClass.");
                }
            }
            CPPPixelLocalAnalysis(*this).validateInputAndReadUsage(decl, isPixelLocalRenderClass ? pixelMethod : nullptr);

            validateRenderStageSupportOrThrow(primaryBackendCapabilities, decl, shaderHullFunc, shaderDomainFunc);

            const bool usesVertexInput = shaderVertexFunc != nullptr && getParamFromFunctionWithAttribute(shaderVertexFunc, getUGLAttributeVertexInputNameByIndex(0)) != nullptr;
            int bindgroupBufferOffset = usesVertexInput ? primaryBackendCapabilities.renderVertexInputReservedBufferSlots : 0;
            if (mShaderSourcePipelineOptions.preparedShaders != nullptr)
                bindgroupBufferOffset = static_cast<int>(requirePreparedClassInterface(*decl).metalBindGroupBufferOffset);
            // Reject unsupported or overflowing SlotN declarations before any
            // bind-group map or shader text is emitted. This keeps invalid
            // render signatures from degrading into a later Metal compile error.
            validateShaderClassBindGroupSlots(decl, createFunc, "RenderClass", bindgroupBufferOffset);
            bindGroupInfoMap = createBindGroupInfoMap(decl, 0);
        }
        if (shaderVertexFunc != nullptr)
        {
            auto shaderEmitter = UGLC::CodeGen::createPrimaryShaderEmitter(this->Context, mShaderSourcePipelineOptions);
            result += buildEmbeddedShaderArtifactMember("vertexShaderArtifact", shaderEmitter.preludeVariableName, decl, shaderVertexFunc, bindGroupInfoMap, extraDecls, *shaderEmitter.emitter);
        }
        auto fragmentMethod = rasterFragmentMethod;
        if (fragmentMethod == nullptr)
        {
            fragmentMethod = pixelMethod;
        }
        const bool isPixelOnlyOperation = isPixelLocalRenderClass && pixelMethod != nullptr && fragmentMethod == pixelMethod;
        if (fragmentMethod != nullptr)
        {
            auto shaderEmitter = UGLC::CodeGen::createPrimaryShaderEmitter(this->Context, mShaderSourcePipelineOptions);
            result += buildEmbeddedShaderArtifactMember("fragmentShaderArtifact", shaderEmitter.preludeVariableName, decl, fragmentMethod, bindGroupInfoMap, extraDecls, *shaderEmitter.emitter);
        }
        if (isPixelLocalRenderClass && fragmentMethod == nullptr)
        {
            throwCodegenError("PixelLocalRenderClass \"" + name + "\" must declare pixel(...).");
        }

        {

            auto fields = getAllFieldFromRecord(decl);
            if (functionRequiresDeletedHostDefinition(createFunc))
            {
                result += NewLine() + generateDeletedFunctionDeclaration(createFunc, "create");
            }
            else
            {
                // Preserve the user-authored constructor body so it can run after
                // the generated pipeline descriptor setup, but before pipeline creation.
                const std::string userCreateBody = generateFunctionBody(createFunc);


                result += NewLine() + generateFunctionSignature(createFunc, "create");
                result += enterScope();
                for (unsigned i = 0; i < createFunc->getNumParams(); ++i)
                {
                    const clang::ParmVarDecl *param = createFunc->getParamDecl(i);
                    if (checkTypeCanonicalName(param->getType(), mUGLRenderSetName))
                    {
                        result += mSpaceManager.getSpace() + "this->mRenderSet = " + param->getNameAsString() + EOS();
                    }

                    result += mSpaceManager.getSpace() + "this->" + param->getNameAsString() + " = " + param->getNameAsString() + EOS();
                }

                result += generateBindGoupAssign(bindGroupInfoMap);

                int bindGroupCount = getBindGroupCountFromInfoMap(bindGroupInfoMap, 0);
                if (shaderVertexFunc != nullptr)
                {
                    result += mSpaceManager.getSpace() + "this->vertexShader = this->mDevice->createShaderModule(UGLC::Generated::MakeShaderModuleDescriptor(\"" + name + "VertexShader\", vertexShaderArtifact))" + EOS();
                }
                // Fragment shader generation is intentionally optional so vertex-only
                // render passes do not reference a nonexistent fragment module.
                if (fragmentMethod != nullptr)
                {
                    result += mSpaceManager.getSpace() + "this->fragmentShader = this->mDevice->createShaderModule(UGLC::Generated::MakeShaderModuleDescriptor(\"" + name + "FragmentShader\", fragmentShaderArtifact))" + EOS();
                }
                result += mSpaceManager.getSpace() + "eastl::vector<GVM::RHI::BindGroupLayout> bindGroupLayouts" + EOS();
                ;
                if (bindGroupCount > 0)
                {
                    result += mSpaceManager.getSpace() + "bindGroupLayouts.resize(" + std::to_string(bindGroupCount) + ")" + EOS();
                }
                for (const auto &[index, b] : bindGroupInfoMap)
                {
                    result += mSpaceManager.getSpace() + "bindGroupLayouts[" + std::to_string(index) + "] = " + b.name + "->mBindGroupLayout" + EOS();
                }
                result += mSpaceManager.getSpace() + "GVM::RHI::PipelineLayout pipelineLayout = this->mDevice->createPipelineLayout({.label = \"" + name + "PipelineLayout\", .bindGroupLayouts = bindGroupLayouts})" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.label = \"" + name + "RenderPipeline\"" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.layout = pipelineLayout" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.fragment = {}" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.depthStencil = {}" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.primitive.frontFace = GVM::RHI::FrontFace::CW" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.primitive.cullMode = GVM::RHI::CullMode::None" + EOS();
                if (shaderVertexFunc != nullptr)
                {
                    result += mSpaceManager.getSpace() + "GVM::RHI::VertexState vertexState = {.module = this->vertexShader, .entryPoint = \"vertexMain\"}" + EOS();

                    auto vertexFunction = getMethodFromClass(decl, mUGLVertexShaderFunctionName, makeVertexShaderMethodLookupOptions());
                    if (auto param = getParamFromFunctionWithAttribute(vertexFunction, getUGLAttributeVertexInputNameByIndex(0)))
                    {
                        CPPVertexFormatTypeConvertor vTypeConvertor;
                        bool pushedVertexInputContext = false;
                        if (const auto *vertexInputRecord = getUnqualifiedType(param->getType())->getAsCXXRecordDecl())
                        {
                            if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(vertexInputRecord);
                                specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
                            {
                                pushTemplateSubstitutionContext(specializationDecl);
                                pushedVertexInputContext = true;
                            }
                        }
                        auto vertexLayoutInfos = getValidatedVertexAttributeLayout(param, name);
                        int vertexArrayStride = 0;
                        for (const auto &vertexLayoutInfo : vertexLayoutInfos)
                        {
                            auto vertexField = vertexLayoutInfo.fieldDecl;
                            vertexArrayStride += convertVertexFormatToStorageBytes(generateTypeCanonicalName(vertexField->getType()));
                        }
                        result += mSpaceManager.getSpace() + "vertexState.buffers.resize(1)" + EOS();
                        result += mSpaceManager.getSpace() + "vertexState.buffers[0].arrayStride = " + std::to_string(vertexArrayStride) + EOS();
                        result += mSpaceManager.getSpace() + "vertexState.buffers[0].attributes.resize(" + std::to_string(vertexLayoutInfos.size()) + ")" + EOS();

                        for (size_t i = 0; i < vertexLayoutInfos.size(); ++i)
                        {
                            const auto &vertexLayoutInfo = vertexLayoutInfos[i];
                            auto vertexField = vertexLayoutInfo.fieldDecl;
                            result += mSpaceManager.getSpace() + "vertexState.buffers[0].attributes[" + std::to_string(i) + "].format = " + generateTypeCanonicalName(vertexField->getType(), &vTypeConvertor) + EOS();
                            result += mSpaceManager.getSpace() + "vertexState.buffers[0].attributes[" + std::to_string(i) + "].offset = offsetof(" + generateTypeCanonicalName(param->getType(), &hTypeConvertor) + ", " + vertexField->getNameAsString() + ")" + EOS();
                            result += mSpaceManager.getSpace() + "vertexState.buffers[0].attributes[" + std::to_string(i) + "].shaderLocation = " + std::to_string(vertexLayoutInfo.shaderLocation) + EOS();
                        }
                        if (pushedVertexInputContext)
                        {
                            popTemplateSubstitutionContext();
                        }
                    }
                    result += mSpaceManager.getSpace() + "this->pipelineDescriptor.vertex = vertexState" + EOS();
                }

                if (fragmentMethod != nullptr)
                {
                    const auto *vertexFunction = shaderVertexFunc;
                    const auto *vertexOutputRecord = vertexFunction == nullptr ? nullptr : getUnqualifiedType(vertexFunction->getReturnType())->getAsCXXRecordDecl();
                    if (vertexOutputRecord != nullptr)
                    {
                        for (unsigned paramIndex = 0; paramIndex < fragmentMethod->getNumParams(); ++paramIndex)
                        {
                            const auto *fragmentParam = fragmentMethod->getParamDecl(paramIndex);
                            if (!getAllAttributes(fragmentParam).empty())
                            {
                                continue;
                            }

                            const auto *fragmentInputRecord = getUnqualifiedType(fragmentParam->getType())->getAsCXXRecordDecl();
                            if (fragmentInputRecord == nullptr || checkDerivedClassByName(fragmentInputRecord, mUGLFrameBufferBaseName))
                            {
                                continue;
                            }

                            validateVertexFragmentVaryingContractOrThrow(vertexOutputRecord, fragmentInputRecord, name);
                        }
                    }

                    std::vector<clang::FieldDecl *> allFields;
                    const auto *rtRecord = getUnqualifiedType(fragmentMethod->getReturnType())->getAsCXXRecordDecl();
                    const clang::CXXRecordDecl *rtLayoutRecord = rtRecord;
                    bool pushedRenderTargetContext = false;
                    if (rtRecord != nullptr)
                    {
                        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rtRecord);
                            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
                        {
                            pushTemplateSubstitutionContext(specializationDecl);
                            rtLayoutRecord = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
                            pushedRenderTargetContext = true;
                        }
                        validateRenderTargetRecordOrThrow(rtLayoutRecord, name);
                        allFields = getAllFieldFromRecord(rtLayoutRecord);
                    }
                    else
                    {
                        throwCodegenError("RenderClass \"" + name + "\"'s RT is not described in a structure.");
                    }
                    CPPPixelLocalAnalysis pixelLocalAnalysis(*this);
                    const bool renderTargetUsesPixelLocalAttachments = pixelLocalAnalysis.renderTargetHasPixelLocalAttachment(rtLayoutRecord);
                    const bool shouldAnalyzePixelLocalWrites = isPixelLocalRenderClass || renderTargetUsesPixelLocalAttachments;
                    const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis renderTargetWriteAnalysis =
                        shouldAnalyzePixelLocalWrites ? PixelLocalFieldAnalysis::collectRenderTargetWriteFieldAnalysis(fragmentMethod, rtLayoutRecord) : PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis{};
                    const bool shouldConstrainPixelLocalColorWrites =
                        shouldAnalyzePixelLocalWrites && !renderTargetWriteAnalysis.conservativeAllWrites;
                    PixelLocalAttachmentAccessMask pixelLocalAttachmentAccess = {};
                    if (isPixelLocalRenderClass)
                    {
                        pixelLocalAnalysis.accumulateReadAccess(fragmentMethod, pixelLocalAttachmentAccess);
                        pixelLocalAnalysis.accumulateWriteAccess(rtLayoutRecord, renderTargetWriteAnalysis, isPixelOnlyOperation, pixelLocalAttachmentAccess);
                        pixelLocalAnalysis.validateEntryAccessOrThrow(fragmentMethod, pixelLocalAttachmentAccess, renderTargetWriteAnalysis);
                    }
                    else if (renderTargetUsesPixelLocalAttachments)
                    {
                        pixelLocalAnalysis.accumulateWriteAccess(rtLayoutRecord, renderTargetWriteAnalysis, false, pixelLocalAttachmentAccess);
                    }
                    result += mSpaceManager.getSpace() + "GVM::RHI::FragmentState fragmentState" + EOS();
                    result += mSpaceManager.getSpace() + "fragmentState.module = this->fragmentShader" + EOS();
                    result += mSpaceManager.getSpace() + "fragmentState.entryPoint = \"fragmentMain\"" + EOS();


                    int colorTargetCount = 0;
                    const clang::FieldDecl *depthAttachmentField = nullptr;
                    for (size_t i = 0; i < allFields.size(); ++i)
                    {
                        auto field = allFields[i];
                        if (checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName) || checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName))
                        {
                            if (depthAttachmentField != nullptr)
                            {
                                throwCodegenError("RenderClass \"" + name + "\" has more than one depth attachment in render target \"" + rtLayoutRecord->getQualifiedNameAsString() + "\".");
                            }
                            if (i + 1 != allFields.size())
                            {
                                throwCodegenError("RenderClass \"" + name + "\" requires depth attachment to be the last field in render target \"" + rtLayoutRecord->getQualifiedNameAsString() + "\".");
                            }
                            depthAttachmentField = field;
                            continue;
                        }

                        colorTargetCount++;
                    }

                    result += mSpaceManager.getSpace() + "fragmentState.targets.resize(" + std::to_string(colorTargetCount) + ")" + EOS();
                    int colorTargetIndex = 0;
                    for (size_t i = 0; i < allFields.size(); ++i)
                    {
                        auto field = allFields[i];
                        if (checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName) || checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName))
                        {
                            continue;
                        }

                        result += mSpaceManager.getSpace() + "fragmentState.targets[" + std::to_string(colorTargetIndex) + "].format = " + generateTypeCanonicalName(requireAttachmentTemplateTypeOrThrow(field, "RenderClass \"" + name + "\" render target", rtLayoutRecord->getQualifiedNameAsString()), &hTypeConvertor) + EOS();
                        if (checkTypeCanonicalName(field->getType(), mUGLPixelLocalColorAttachmentName))
                        {
                            result += mSpaceManager.getSpace() + "fragmentState.targets[" + std::to_string(colorTargetIndex) + "].pixelLocal = true" + EOS();
                        }
                        if (shouldConstrainPixelLocalColorWrites && renderTargetWriteAnalysis.fields.find(field->getNameAsString()) == renderTargetWriteAnalysis.fields.end())
                        {
                            result += mSpaceManager.getSpace() + "fragmentState.targets[" + std::to_string(colorTargetIndex) + "].writeMask = GVM::RHI::ColorWriteMask::None" + EOS();
                        }
                        colorTargetIndex++;
                    }
                    result += mSpaceManager.getSpace() + "GVM::RHI::DepthStencilState depthStencilState = {}" + EOS();
                    if (depthAttachmentField != nullptr)
                    {
                        const bool isPixelLocalDepthAttachment = checkTypeCanonicalName(depthAttachmentField->getType(), mUGLPixelLocalDepthAttachmentName);
                        const bool disableDepthStateForPixelOnlyPass = isPixelOnlyOperation;
                        const std::string depthFormat = generateTypeCanonicalName(requireAttachmentTemplateTypeOrThrow(depthAttachmentField, "RenderClass \"" + name + "\" render target", rtLayoutRecord->getQualifiedNameAsString()), &hTypeConvertor);
                        if (disableDepthStateForPixelOnlyPass)
                        {
                            result += mSpaceManager.getSpace() + "depthStencilState = {.format = " + depthFormat + "}" + EOS();
                        }
                        else
                        {
                            result += mSpaceManager.getSpace() + "depthStencilState = {.format = " + depthFormat + ", .depthTestEnabled = true, .depthWriteEnabled = true}" + EOS();
                        }
                        if (isPixelLocalDepthAttachment)
                        {
                            result += mSpaceManager.getSpace() + "depthStencilState.pixelLocal = true" + EOS();
                        }
                    }
                    result += mSpaceManager.getSpace() + "this->pipelineDescriptor.fragment = fragmentState" + EOS();
                    result += mSpaceManager.getSpace() + "this->pipelineDescriptor.depthStencil = depthStencilState" + EOS();
                    if (isPixelLocalRenderClass)
                    {
                        result += mSpaceManager.getSpace() + "this->pipelineDescriptor.pixelLocalAttachmentAccess.colorReadMask = " + std::to_string(pixelLocalAttachmentAccess.colorReadMask) + "ull" + EOS();
                        result += mSpaceManager.getSpace() + "this->pipelineDescriptor.pixelLocalAttachmentAccess.colorWriteMask = " + std::to_string(pixelLocalAttachmentAccess.colorWriteMask) + "ull" + EOS();
                        result += mSpaceManager.getSpace() + "this->pipelineDescriptor.pixelLocalAttachmentAccess.depthWrite = " + std::string(pixelLocalAttachmentAccess.depthWrite ? "true" : "false") + EOS();
                    }
                    if (pushedRenderTargetContext)
                    {
                        popTemplateSubstitutionContext();
                    }
                }
                if (getMethodFromClass(decl, mUGLDomainShaderFunctionName, makeDomainShaderMethodLookupOptions()) != nullptr)
                {
                    result += mSpaceManager.getSpace() + "GVM::RHI::TessellationState tessState = {}" + EOS();
                    result += mSpaceManager.getSpace() + "tessState.maxTessellationFactor = 8" + EOS();
                    result += mSpaceManager.getSpace() + "this->pipelineDescriptor.tessellation = tessState" + EOS();
                }
                result += userCreateBody;
                if (shaderVertexFunc != nullptr || isPixelLocalRenderClass)
                {
                    result += mSpaceManager.getSpace() + "this->pipeline = this->mDevice->createRenderPipeline(this->pipelineDescriptor)" + EOS();
                }
                result += quitScope();
            }
        }


        // 我们还需要处理成员函数
        for (auto *method : decl->methods())
        {
            if (checkAttibuteByName(method, mUGLCTORName) == false && method->hasBody() && checkFunctionName(method, mUGLFragmentShaderFunctionName) == false && checkFunctionName(method, mUGLPixelShaderFunctionName) == false && checkFunctionName(method, mUGLVertexShaderFunctionName) == false && checkFunctionName(method, mUGLHullShaderFunctionName) == false && checkFunctionName(method, mUGLDomainShaderFunctionName) == false && checkFunctionName(method, mUGLConstantsHullShaderFunctionName) == false)
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();
        return result;
    }
    std::string CPPVisitor::replaceUGLComputeClass(const clang::CXXRecordDecl *decl)
    {
        auto workGroupSize = getValidatedLocalWorkGroupSize(decl);
        std::string workgroupX = workGroupSize[0];
        std::string workgroupY = workGroupSize[1];
        std::string workgroupZ = workGroupSize[2];
        CPPTypeConvertor hTypeConvertor;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string emittedName = getRecordEmissionName(decl);
        std::string name = getRecordVariantLabel(decl);
        std::string result;
        result += mSpaceManager.getSpace() + keyword + " " + emittedName + " : public GVM::Core::IComputeClass, public GVM::RHI::RefCountedObject" + NewLine();
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
        auto createFunc = requireCreateMethodOrThrow(decl, "ComputeClass");
        auto compFunc = getMethodFromClass(decl, mUGLComputeShaderFunctionName, makeComputeShaderMethodLookupOptions());
        validateShaderClassEntryMethodListOrThrow(*this,
                                                  decl,
                                                  "ComputeClass",
                                                  std::vector<std::string>{mUGLComputeShaderFunctionName},
                                                  std::vector<const clang::CXXMethodDecl *>{compFunc},
                                                  mUGLComputeShaderFunctionName);
        // Keep the missing-entry diagnostic at the front-end boundary so
        // compute classes never reach backend codegen with null state.
        if (compFunc == nullptr)
        {
            throwCodegenError("ComputeClass \"" + name + "\" is missing required shader entry method \"" + mUGLComputeShaderFunctionName + "(...)\".");
        }
        // Compute entries do not reserve extra Metal buffer slots ahead of bind
        // groups, so the slot range maps directly to buffer(0) through buffer(7).
        validateShaderClassBindGroupSlots(decl, createFunc, "ComputeClass", 0);
        BindGroupInfoMap bindGroupInfoMap = createBindGroupInfoMap(decl, 0);

        {
            auto shaderEmitter = UGLC::CodeGen::createPrimaryShaderEmitter(this->Context, mShaderSourcePipelineOptions);
            result += buildEmbeddedShaderArtifactMember("computeShaderArtifact", shaderEmitter.preludeVariableName, decl, compFunc, bindGroupInfoMap, {}, *shaderEmitter.emitter);
        }


        {

            auto fields = getAllFieldFromRecord(decl);
            if (functionRequiresDeletedHostDefinition(createFunc))
            {
                result += NewLine() + generateDeletedFunctionDeclaration(createFunc, "create");
            }
            else
            {
                // Preserve the user-authored constructor body so it can run after
                // the generated pipeline descriptor setup, but before pipeline creation.
                const std::string userCreateBody = generateFunctionBody(createFunc);


                result += NewLine() + generateFunctionSignature(createFunc, "create");
                result += enterScope();
                for (unsigned i = 0; i < createFunc->getNumParams(); ++i)
                {
                    const clang::ParmVarDecl *param = createFunc->getParamDecl(i);
                    if (checkTypeCanonicalName(param->getType(), mUGLRenderSetName))
                    {
                        result += mSpaceManager.getSpace() + "this->mRenderSet = " + param->getNameAsString() + EOS();
                    }

                    result += mSpaceManager.getSpace() + "this->" + param->getNameAsString() + " = " + param->getNameAsString() + EOS();
                }
                result += generateBindGoupAssign(bindGroupInfoMap);
                result += mSpaceManager.getSpace() + "this->computeShader = this->mDevice->createShaderModule(UGLC::Generated::MakeShaderModuleDescriptor(\"" + name + "ComputeShader\", computeShaderArtifact))" + EOS();

                int bindGroupCount = getBindGroupCountFromInfoMap(bindGroupInfoMap, 0);
                result += mSpaceManager.getSpace() + "eastl::vector<GVM::RHI::BindGroupLayout> bindGroupLayouts(" + std::to_string(bindGroupCount) + ")" + EOS();
                for (const auto &[index, b] : bindGroupInfoMap)
                {
                    result += mSpaceManager.getSpace() + "bindGroupLayouts[" + std::to_string(index) + "] = " + b.name + "->mBindGroupLayout" + EOS();
                }
                result += mSpaceManager.getSpace() + "GVM::RHI::PipelineLayout pipelineLayout = this->mDevice->createPipelineLayout({.label = \"" + name + "PipelineLayout\", .bindGroupLayouts = bindGroupLayouts})" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.label = \"" + name + "RenderPipeline\"" + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.layout = pipelineLayout" + EOS();

                result += mSpaceManager.getSpace() + "GVM::RHI::ComputeStageDescriptor computeDesp{}" + EOS();
                result += mSpaceManager.getSpace() + "computeDesp.module = this->computeShader" + EOS();
                result += mSpaceManager.getSpace() + "computeDesp.entryPoint = \"" + ComputeShaderEntryName + "\"" + EOS();
                result += mSpaceManager.getSpace() + "computeDesp.workgroupX = " + workgroupX + EOS();
                result += mSpaceManager.getSpace() + "computeDesp.workgroupY = " + workgroupY + EOS();
                result += mSpaceManager.getSpace() + "computeDesp.workgroupZ = " + workgroupZ + EOS();
                result += mSpaceManager.getSpace() + "this->pipelineDescriptor.compute = computeDesp" + EOS();

                result += mSpaceManager.getSpace() + "this->workGroupX = " + workgroupX + EOS();
                result += mSpaceManager.getSpace() + "this->workGroupY = " + workgroupY + EOS();
                result += mSpaceManager.getSpace() + "this->workGroupZ = " + workgroupZ + EOS();


                result += userCreateBody;
                result += mSpaceManager.getSpace() + "this->pipeline = this->mDevice->createComputePipeline(this->pipelineDescriptor)" + EOS();
                result += quitScope();
            }
        }


        // 我们还需要处理成员函数
        for (auto *method : decl->methods())
        {
            if (checkAttibuteByName(method, mUGLCTORName) == false && method->hasBody() && checkFunctionName(method, mUGLComputeShaderFunctionName) == false)
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();
        return result;
    }
    std::string CPPVisitor::replaceUGLRenderSetClass(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string emittedName = getRecordEmissionName(decl);
        std::string name = getRecordVariantLabel(decl);

        if (auto *templateDecl = decl->getDescribedClassTemplate())
        {
            result += getLineDirective(templateDecl->getBeginLoc());
            result += generateTemplateParameters(templateDecl->getTemplateParameters());
        }


        // 在 HLSL 中，class 和 struct 几乎没有区别，我们统一生成为 struct
        result += mSpaceManager.getSpace() + keyword + " " + emittedName + ": public GVM::Core::RenderSet" + NewLine();
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

        auto fields = getAllFieldFromRecord(decl);
        applyPreparedRenderSetFields(*decl, fields);

        auto formatFieldList = [&](const std::vector<clang::FieldDecl *> &fieldDecls) -> std::string {
            std::string formatted;
            for (size_t i = 0; i < fieldDecls.size(); ++i)
            {
                if (i > 0)
                {
                    formatted += ", ";
                }
                formatted += "\"" + fieldDecls[i]->getNameAsString() + "\"";
            }
            return formatted;
        };

        auto collectFieldsWithAttribute = [&](const std::string &attributeName) -> std::vector<clang::FieldDecl *> {
            std::vector<clang::FieldDecl *> matches;
            for (auto *field : fields)
            {
                if (checkAttibuteByName(field, attributeName))
                {
                    matches.emplace_back(field);
                }
            }
            return matches;
        };

        auto requireSingleRenderSetBufferField = [&](const std::string &attributeName, const std::string &friendlyName) -> clang::FieldDecl * {
            auto matches = collectFieldsWithAttribute(attributeName);
            if (matches.empty())
            {
                throwCodegenError("RenderSet \"" + name + "\" requires exactly one [[" + attributeName + "]] field for its " + friendlyName + ".");
            }
            if (matches.size() > 1)
            {
                throwCodegenError("RenderSet \"" + name + "\" requires exactly one [[" + attributeName + "]] field for its " + friendlyName + ", but found " + std::to_string(matches.size()) + " candidates: " + formatFieldList(matches) + ".");
            }

            clang::FieldDecl *fieldDecl = matches.front();
            if (!checkTypeCanonicalName(fieldDecl->getType(), mUGLRenderSetBufferComponentClassName))
            {
                throwCodegenError("RenderSet \"" + name + "\" field \"" + fieldDecl->getNameAsString() + "\" annotated with [[" + attributeName + "]] must use UGL::BufferComponent<...>, but found \"" + generateTypeCanonicalName(fieldDecl->getType()) + "\".");
            }

            return fieldDecl;
        };

        auto vertexBufferDecl = requireSingleRenderSetBufferField(mUGLRenderSetVertexBuffer, "vertex buffer");
        auto indexBufferDecl = requireSingleRenderSetBufferField(mUGLRenderSetIndexBuffer, "index buffer");
        if (vertexBufferDecl == indexBufferDecl)
        {
            throwCodegenError("RenderSet \"" + name + "\" field \"" + vertexBufferDecl->getNameAsString() + "\" cannot declare both [[" + mUGLRenderSetVertexBuffer + "]] and [[" + mUGLRenderSetIndexBuffer + "]].");
        }


        result += mSpaceManager.getSpace() + "public: GVM::RHI::BindGroupLayout mBindGroupLayout" + EOS();
        result += mSpaceManager.getSpace() + "void create(GVM::RHI::Device device)" + NewLine();
        result += enterScope();

        result += mSpaceManager.getSpace() + "GVM::Core::RenderSetCreateInfo createInfo = {}" + EOS();
        result += enterScope();

        result += mSpaceManager.getSpace() + "createInfo.renderSetName = \"" + name + "\"" + EOS();
        result += mSpaceManager.getSpace() + "createInfo.vertexComponentName = \"" + vertexBufferDecl->getNameAsString() + "\"" + EOS();
        result += mSpaceManager.getSpace() + "createInfo.indexComponentName = \"" + indexBufferDecl->getNameAsString() + "\"" + EOS();

        std::vector<RenderComponentCreateInfo> renderComponentCreateInfos;
        renderComponentCreateInfos.reserve(fields.size());

        for (const auto *field : fields)
        {
            validateRenderSetComponentFieldAttributesOrThrow(field, name);
            RenderComponentCreateInfo info;
            if (checkTypeCanonicalName(field->getType(), mUGLRenderSetBufferComponentClassName))
            {
                auto templateArgs = getTemplateArgumentsFromType(field->getType());
                if (templateArgs.empty() || templateArgs.front().getAsType().isNull())
                {
                    throwCodegenError("RenderSet \"" + name + "\" buffer component field \"" + field->getNameAsString() + "\" requires UGL::BufferComponent<ElementType>.");
                }
                info.type = RenderComponentType::BufferComponent;
                info.typeName = translateTemplateArgument(templateArgs.front(), &mTypeConvertor);
                info.typeString = mUGLRenderSetBufferComponentName;
            }
            else if (checkTypeCanonicalName(field->getType(), mUGLRenderSetTextureComponentClassName))
            {
                info.type = RenderComponentType::TextureComponent;
                auto templateArgs = getTemplateArgumentsFromType(field->getType());
                if (templateArgs.size() < 2 || templateArgs.front().getAsType().isNull())
                {
                    throwCodegenError("RenderSet \"" + name + "\" texture component field \"" + field->getNameAsString() + "\" requires UGL::TextureComponent<ElementType, MaxResourceCount>.");
                }
                try
                {
                    info.numElement = static_cast<int>(getIntValueFromTemplateArgument(templateArgs.at(1)));
                }
                catch (const std::exception &ex)
                {
                    throwCodegenError("RenderSet \"" + name + "\" texture component field \"" + field->getNameAsString() + "\" has an invalid MaxResourceCount template argument: " + std::string(ex.what()));
                }
                validateRenderSetTextureResourceCountOrThrow(field, name, info.numElement);
                info.typeName = "float4";
                info.typeString = mUGLRenderSetTextureComponentName;
            }
            else
            {
                throwCodegenError("RenderSet \"" + name + "\" contains unsupported component field \"" + field->getNameAsString() + "\" with type \"" + generateTypeCanonicalName(field->getType()) + "\". Supported component types are UGL::BufferComponent<T> and UGL::TextureComponent<T, MaxResourceCount>.");
            }
            info.fieldDecl = field;
            info.varName = field->getNameAsString();
            renderComponentCreateInfos.emplace_back(info);
        }

        for (size_t i = 0; i < renderComponentCreateInfos.size(); ++i)
        {
            const auto &renderComponentCreateInfo = renderComponentCreateInfos[i];
            result += mSpaceManager.getSpace() + "createInfo.componentInfos.emplace(" + std::to_string(i) + ", GVM::Core::RenderComponentCreateInfo{.type = GVM::Core::RenderComponentType::" + renderComponentCreateInfo.typeString + ", .dataElementStorageSize = sizeof(" + renderComponentCreateInfo.typeName + "), .maxResourceCount = " + std::to_string(renderComponentCreateInfo.numElement) + ", .componentName = \"" + renderComponentCreateInfo.varName + "\"})" + EOS();
            result += mSpaceManager.getSpace() + "createInfo.componentNameList.push_back(\"" + renderComponentCreateInfo.varName + "\")" + EOS();
        }

        result += quitScope();

        result += mSpaceManager.getSpace() + "GVM::Core::RenderSet::create(device, createInfo)" + EOS();

        result += enterScope();
        for (const auto &renderComponentCreateInfo : renderComponentCreateInfos)
        {
            if (renderComponentCreateInfo.type == RenderComponentType::BufferComponent)
            {
                result += mSpaceManager.getSpace() + "this->" + renderComponentCreateInfo.varName + " = getBufferComponentByName(\"" + renderComponentCreateInfo.varName + "\")" + EOS();
            }
            else if (renderComponentCreateInfo.type == RenderComponentType::TextureComponent)
            {
                result += mSpaceManager.getSpace() + "this->" + renderComponentCreateInfo.varName + " = getTextureComponentByName(\"" + renderComponentCreateInfo.varName + "\")" + EOS();
            }
            else
            {
                throwCodegenError("RenderSet \"" + name + "\" failed to resolve generated component handle for field \"" + renderComponentCreateInfo.varName + "\".");
            }
        }
        result += mSpaceManager.getSpace() + "this->mBindGroupLayout = this->getBindGroupLayout()" + EOS();
        result += quitScope();
        result += quitScope();


        mRenderSetComponentInfos.emplace(generateTypeCanonicalName(Context->getRecordType(decl), &mTypeConvertor), renderComponentCreateInfos);


        // 我们还需要处理成员函数
        for (auto *method : decl->methods())
        {
            if (checkAttibuteByName(method, mUGLCTORName) == false && !method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();

        return result;
    }
    std::string CPPVisitor::replaceUGLRendererClass(const clang::CXXRecordDecl *decl)
    {
        return mRendererEmitter.replaceRendererClass(decl);
    }

    std::string CPPVisitor::getPreparedRecordSymbolName(const clang::CXXRecordDecl &record) const
    {
        const auto *concrete = &record;
        if (const auto *specialization = getCurrentTemplateSubstitutionContext();
            specialization != nullptr && specialization->getSpecializedTemplate() != nullptr &&
            specialization->getSpecializedTemplate()->getTemplatedDecl()->getCanonicalDecl() == record.getCanonicalDecl())
            concrete = specialization;
        return UGLIR::makeUGLIRRecordSymbolName(*concrete);
    }

    const UGLIR::Reflection &CPPVisitor::requirePreparedClassInterface(const clang::CXXRecordDecl &record) const
    {
        const auto *prepared = mShaderSourcePipelineOptions.preparedShaders;
        const auto name = getPreparedRecordSymbolName(record);
        if (prepared != nullptr)
            for (const auto &shader : prepared->shaders)
                if (shader.module.reflection.shaderClassName == name)
                    return shader.module.reflection;
        throwCodegenError("UGLIR host interface has no prepared shader class \"" + name + "\".");
    }

    void CPPVisitor::applyPreparedBindGroupBindings(const clang::CXXRecordDecl &record, std::vector<BaseShaderResourceBinding> &bindings)
    {
        const auto *prepared = mShaderSourcePipelineOptions.preparedShaders;
        if (prepared == nullptr)
            return;
        const auto typeName = getPreparedRecordSymbolName(record);
        for (const auto &shader : prepared->shaders)
            for (const auto &group : shader.module.reflection.bindGroups)
            {
                if (group.typeName != typeName || group.isRenderSet)
                    continue;
                size_t matched = 0;
                size_t reflectedCount = 0;
                for (const auto &resource : shader.module.reflection.resources)
                {
                    if (resource.bindGroupIndex != group.bindGroupIndex)
                        continue;
                    ++reflectedCount;
                    for (auto &binding : bindings)
                    {
                        if (resource.name != group.name + "." + binding.fieldDecl->getNameAsString())
                            continue;
                        UGLIR::ResourceKind expectedKind = UGLIR::ResourceKind::Unknown;
                        switch (binding.kind)
                        {
                        case BaseShaderResourceKind::UniformBuffer: expectedKind = UGLIR::ResourceKind::UniformBuffer; break;
                        case BaseShaderResourceKind::StorageBuffer: expectedKind = UGLIR::ResourceKind::StorageBuffer; break;
                        case BaseShaderResourceKind::SampledTexture: expectedKind = UGLIR::ResourceKind::Texture; break;
                        case BaseShaderResourceKind::StorageTexture: expectedKind = UGLIR::ResourceKind::StorageTexture; break;
                        case BaseShaderResourceKind::Sampler: expectedKind = UGLIR::ResourceKind::Sampler; break;
                        default: break;
                        }
                        if (expectedKind != resource.kind || static_cast<uint32_t>(binding.bindingIndex) != resource.bindingIndex)
                            throwCodegenError("UGLIR host resource interface differs for \"" + resource.name + "\".");
                        if ((binding.access == BaseShaderResourceAccess::ReadOnly && resource.accessMode != UGLIR::AccessMode::Read) ||
                            (binding.access == BaseShaderResourceAccess::ReadWrite && resource.accessMode != UGLIR::AccessMode::ReadWrite))
                            throwCodegenError("UGLIR host resource access differs for \"" + resource.name + "\".");
                        if ((binding.dimension == BaseShaderTextureDimension::Texture2D && resource.textureDimension != UGLIR::TextureDimension::Texture2D) ||
                            (binding.dimension == BaseShaderTextureDimension::Texture2DArray && resource.textureDimension != UGLIR::TextureDimension::Texture2DArray) ||
                            (binding.dimension == BaseShaderTextureDimension::Texture3D && resource.textureDimension != UGLIR::TextureDimension::Texture3D))
                            throwCodegenError("UGLIR host texture dimension differs for \"" + resource.name + "\".");
                        binding.bindingIndex = static_cast<int>(resource.bindingIndex);
                        ++matched;
                        break;
                    }
                }
                if (matched != bindings.size() || reflectedCount != bindings.size())
                    throwCodegenError("UGLIR host resource count differs for \"" + typeName + "\".");
                return;
            }
        // Unreferenced host bind-group types do not participate in a shader ABI.
    }

    void CPPVisitor::applyPreparedRenderSetFields(const clang::CXXRecordDecl &record, std::vector<clang::FieldDecl *> &fields)
    {
        const auto *prepared = mShaderSourcePipelineOptions.preparedShaders;
        if (prepared == nullptr)
            return;
        const auto typeName = getPreparedRecordSymbolName(record);
        for (const auto &shader : prepared->shaders)
            for (const auto &group : shader.module.reflection.bindGroups)
            {
                if (group.typeName != typeName || !group.isRenderSet)
                    continue;
                std::vector<clang::FieldDecl *> ordered;
                for (const auto &resource : shader.module.reflection.resources)
                {
                    if (resource.bindGroupIndex != group.bindGroupIndex ||
                        (resource.resourceRole != UGLIR::ResourceRole::BufferValue &&
                         resource.resourceRole != UGLIR::ResourceRole::TextureValue))
                        continue;
                    for (auto *field : fields)
                        if (resource.name == group.name + "." + field->getNameAsString())
                        {
                            if (resource.resourceIndex != ordered.size() + 1u)
                                throwCodegenError("UGLIR host RenderSet component order differs for \"" + typeName + "\".");
                            ordered.push_back(field);
                            break;
                        }
                }
                if (ordered.size() != fields.size())
                    throwCodegenError("UGLIR host RenderSet component count differs for \"" + typeName + "\".");
                fields = std::move(ordered);
                return;
            }
    }

    BindGroupInfoMap CPPVisitor::createBindGroupInfoMap(const clang::CXXRecordDecl *decl, int bindgroupStartIndex)
    {
        BindGroupInfoMap bindGroupInfoMap;


        auto fields = getAllFieldFromRecord(decl);
        const clang::FieldDecl *renderSetField = nullptr;
        const bool usesRenderEntityBuiltins = classUsesRenderEntityBuiltins(*this, decl);
        std::unordered_map<int, const clang::FieldDecl *> usedSlotFields;
        for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            auto field = fields[fieldIndex]; // getFieldFromClassWithAttribute(decl, getUGLAttributeSlotNameByIndex(bindgroupIndex));

            int bindGroupIndex = -1;
            std::vector<std::string> matchedSlotAttributes;
            for (const auto *attr : getAllAttributes(field))
            {
                std::string rawAttribute = generateRawAttribute(attr);
                if (isExactIndexedAttribute(rawAttribute, mUGLAttributeSlotName))
                {
                    matchedSlotAttributes.emplace_back(rawAttribute);
                }
            }

            if (matchedSlotAttributes.size() > 1)
            {
                throwCodegenError("BindGroup field \"" + field->getQualifiedNameAsString() + "\" declares multiple [[SlotN]] attributes.");
            }

            if (!matchedSlotAttributes.empty())
            {
                bindGroupIndex = getAttributeNumber(matchedSlotAttributes.front(), mUGLAttributeSlotName);
            }

            if (bindGroupIndex >= 0)
            {
                const bool isBindGroupSlot = checkTypeCanonicalName(field->getType(), mUGLBindGroupName);
                const bool isRenderSetSlot = checkTypeCanonicalName(field->getType(), mUGLRenderSetName);
                if (!isBindGroupSlot && !isRenderSetSlot)
                {
                    throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" field \"" + field->getNameAsString() + "\" uses [[Slot" + std::to_string(bindGroupIndex) + "]], but only UGL::BindGroup<T> and UGL::RenderSet<T> fields may declare [[SlotN]]. Found \"" + generateTypeCanonicalName(field->getType()) + "\".");
                }

                if (auto iter = usedSlotFields.find(bindGroupIndex); iter != usedSlotFields.end())
                {
                    throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" reuses [[Slot" + std::to_string(bindGroupIndex) + "]] on both parameter \"" + iter->second->getNameAsString() + "\" and parameter \"" + field->getNameAsString() + "\".");
                }

                const auto templateArgs = getTemplateArgumentsFromType(field->getType());
                if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
                {
                    throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" field \"" + field->getNameAsString() + "\" requires a concrete type argument in \"" + generateTypeCanonicalName(field->getType()) + "\".");
                }

                const auto boundType = getUnqualifiedType(templateArgs.front().getAsType());
                auto *boundTypeDecl = boundType->getAsCXXRecordDecl();
                if (boundTypeDecl == nullptr)
                {
                    throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" field \"" + field->getNameAsString() + "\" requires a record type argument, but resolved \"" + generateTypeCanonicalName(boundType) + "\".");
                }

                ShaderBindGroupInfo bindGroupInfo;
                bindGroupInfo.bindingIndex = bindGroupIndex + bindgroupStartIndex;
                bindGroupInfo.name = field->getNameAsString();
                bindGroupInfo.type = translateTemplateArgument(templateArgs.front(), &mTypeConvertor);
                bindGroupInfo.typeDecl = boundTypeDecl;

                if (isRenderSetSlot)
                {
                    bindGroupInfo.isRenderSet = true;
                    if (renderSetField != nullptr)
                    {
                        if (usesRenderEntityBuiltins)
                        {
                            throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" binds multiple UGL::RenderSet<T> fields with [[SlotN]] (\"" + renderSetField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\"), but RenderEntity builtins require exactly one UGL::RenderSet<T> binding because InstanceID must be decoded through that RenderSet's RenderEntityCMDParams.");
                        }
                        throwCodegenError("Shader class \"" + decl->getQualifiedNameAsString() + "\" cannot declare more than one RenderSet field with [[SlotN]]. Found both \"" + renderSetField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                    }
                    renderSetField = field;
                }
                else if (bindGroupInfo.typeDecl != nullptr)
                {
                    // Capture the bind-group's base resource view once at
                    // the shader-class boundary so every backend can consume
                    // the same ABI metadata instead of re-inferring it later.
                    bindGroupInfo.resourceBindings = resolveBaseShaderResourceBindings(bindGroupInfo.typeDecl);
                    applyPreparedBindGroupBindings(*bindGroupInfo.typeDecl, bindGroupInfo.resourceBindings);
                }

                if (mShaderSourcePipelineOptions.preparedShaders != nullptr)
                {
                    const auto &interface = requirePreparedClassInterface(*decl);
                    bool matched = false;
                    for (const auto &group : interface.bindGroups)
                    {
                        if (group.name != bindGroupInfo.name)
                            continue;
                        if (group.typeName != getPreparedRecordSymbolName(*boundTypeDecl) || group.isRenderSet != bindGroupInfo.isRenderSet ||
                            group.bindGroupIndex != static_cast<uint32_t>(bindGroupIndex))
                            throwCodegenError("UGLIR host interface differs for bind group \"" + bindGroupInfo.name + "\".");
                        bindGroupInfo.bindingIndex = static_cast<int>(group.bindGroupIndex) + bindgroupStartIndex;
                        matched = true;
                        break;
                    }
                    if (!matched)
                        throwCodegenError("UGLIR host interface has no prepared bind group \"" + bindGroupInfo.name + "\".");
                }
                bindGroupInfoMap.emplace(bindGroupInfo.bindingIndex, bindGroupInfo);
                usedSlotFields.emplace(bindGroupIndex, field);
            }
        }

        return bindGroupInfoMap;
    }

    int CPPVisitor::getBindGroupCountFromInfoMap(const BindGroupInfoMap &infoMap, int bindgroupStartIndex)
    {
        (void)bindgroupStartIndex;
        int count = -1;
        for (const auto &[index, bg] : infoMap)
        {
            count = std::max(count, index + 1);
        }
        return count;
    }

    std::string CPPVisitor::generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor)
    {
        bool isRef = param->getType()->isReferenceType();
        std::string constSpecificer;
        std::string refSpecificer = isRef ? "&" : "";

        std::string arraySpecifiers = generateDeclArraySpecifier(param->getOriginalType());
        std::string pointerSpecifier;

        std::string attrs = generateAttributes(param, nullptr);
        if (checkAttibuteByName(param, mUGLAttributeINName))
        {
            // constSpecificer = "const ";
            attrs = "";
        }
        if (/* checkAttibuteByName(param, mUGLAttributeINName) ||  */ checkAttibuteByName(param, mUGLAttributeOUTName) || checkAttibuteByName(param, mUGLAttributeINOUTName))
        {
            refSpecificer = "&";
            attrs = "";
            if (arraySpecifiers.empty() == false)
            {
                arraySpecifiers.clear();
                pointerSpecifier = "* ";
                refSpecificer.clear();
            }
        }


        std::string defaultArg;
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

        return constSpecificer + generateTypeCanonicalName(param->getType(), typeConvertor) + refSpecificer + pointerSpecifier + " " + param->getNameAsString() + arraySpecifiers + defaultArg + attrs;
    }

    std::string CPPVisitor::generateFunctionDefinition(const clang::FunctionDecl *func)
    {
        if (functionRequiresDeletedHostDefinition(func))
        {
            return generateDeletedFunctionDeclaration(func);
        }

        // Keep host-visible bodies intact unless they depend on shader-only
        // operations. In that case we emit a deleted declaration instead of a
        // fake body so mere inclusion still succeeds, but real host calls fail
        // at compile time.
        return BaseASTVisitor::generateFunctionDefinition(func);
    }

    std::string CPPVisitor::translateIfStmt(const clang::IfStmt *stmt)
    {
        if (stmt != nullptr && stmt->isConstexpr() && stmt->getCond() != nullptr && stmt->getCond()->isInstantiationDependent() &&
            getCurrentTemplateSubstitutionContext() == nullptr)
        {
            const clang::SourceManager &sourceManager = Context->getSourceManager();
            std::string condition = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(stmt->getCond()->getSourceRange()),
                                                                sourceManager,
                                                                Context->getLangOpts())
                                         .str();
            if (condition.empty())
            {
                condition = TranslateExpr(stmt->getCond());
            }

            std::string result = "if constexpr (";
            if (const clang::Stmt *init = stmt->getInit())
            {
                result += TranslateStmt(init) + "; ";
            }
            if (const clang::DeclStmt *conditionVariable = stmt->getConditionVariableDeclStmt())
            {
                result += TranslateStmt(conditionVariable);
            }
            else
            {
                result += condition;
            }
            result += ")" + NewLine();
            if (!llvm::isa<clang::CompoundStmt>(stmt->getThen()))
                result += "{" + NewLine();
            result += TranslateStmt(stmt->getThen());
            if (!llvm::isa<clang::CompoundStmt>(stmt->getThen()))
                result += EOS() + "}" + NewLine();
            if (stmt->getElse() != nullptr)
            {
                result += mSpaceManager.getSpace() + "else ";
                if (!llvm::isa<clang::CompoundStmt>(stmt->getElse()))
                    result += "{" + NewLine();
                result += TranslateStmt(stmt->getElse());
                if (!llvm::isa<clang::CompoundStmt>(stmt->getElse()))
                    result += EOS() + "}" + NewLine();
            }
            return result;
        }

        return BaseASTVisitor::translateIfStmt(stmt);
    }

    std::string CPPVisitor::replaceUGLClass(const clang::CXXRecordDecl *decl)
    {
        if (checkDerivedClassByName(decl, mUGLFrameBufferBaseName))
        {
            return replaceUGLFrameBufferClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLBindGroupBaseName))
        {
            return replaceUGLBindGroupClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLPixelLocalRenderClassBaseName))
        {
            return replaceUGLRenderClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLRenderClassBaseName))
        {
            return replaceUGLRenderClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLComputeClassBaseName))
        {
            return replaceUGLComputeClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLAbstractRendererClassName))
        {
            return replaceUGLRendererClass(decl);
        }
        else if (checkDerivedClassByName(decl, mUGLRenderSetBaseName))
        {
            return replaceUGLRenderSetClass(decl);
        }
        else if (recordUsesDirectShaderResourceHandles(decl))
        {
            return replaceShaderResourceBehaviorRecordShell(decl);
        }
        else
        {

            std::string res = generateRecordDefinition(decl);
            std::string name = decl->getNameAsString();
            if ((res.find("float") != res.npos || res.find("int") != res.npos || decl->getNumBases() == 0) && decl->isClass() == false)
            {
                // res += "static_assert(sizeof(" + name + ") % 16 == 0, \"" + name + " struct must be 16-byte aligned!\")";
            }
            return res;
        }
    }

    std::string CPPVisitor::translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *E)
    {
        // 第一个参数是被调用的对象
        std::string Callee = TranslateExpr(E->getArg(0));
        auto UGLRunClassDecl = getTemplateArgumentsFromType(E->getArg(0)->getType()).front().getAsType()->getAsCXXRecordDecl();
        if (checkDerivedClassByName(UGLRunClassDecl, mUGLRenderClassBaseName) || checkDerivedClassByName(UGLRunClassDecl, mUGLPixelLocalRenderClassBaseName) || checkDerivedClassByName(UGLRunClassDecl, mUGLComputeClassBaseName))
        {
            Callee += "->run";
        }


        // 拼接剩下的参数
        std::string result = Callee + "(";
        std::string Args;
        for (unsigned i = 1; i < E->getNumArgs(); ++i)
        { // 参数从 1 开始
            Args += TranslateExpr(E->getArg(i));
            if (i < E->getNumArgs() - 1)
            {
                Args += ", ";
            }
        }
        result += Args + ")";
        return result;
    }
    std::string convertBufferUsage(const std::string &canonicalName)
    {
        std::string result;
        if (canonicalName == "UGL::Vertex")
        {
            result = "GVM::RHI::BufferUsage::Vertex";
        }
        else if (canonicalName == "UGL::Index")
        {
            result = "GVM::RHI::BufferUsage::Index";
        }
        else if (canonicalName == "UGL::Uniform")
        {
            result = "GVM::RHI::BufferUsage::Uniform";
        }
        else if (canonicalName == "UGL::Storage")
        {
            result = "GVM::RHI::BufferUsage::Storage";
        }
        else if (canonicalName == "UGL::Indirect")
        {
            result = "GVM::RHI::BufferUsage::Indirect";
        }
        else if (canonicalName == "UGL::CopyDst")
        {
            result = "GVM::RHI::BufferUsage::CopyDst";
        }
        else if (canonicalName == "UGL::MapWrite")
        {
            result = "GVM::RHI::BufferUsage::MapWrite";
        }
        else if (canonicalName == "UGL::MapRead")
        {
            result = "GVM::RHI::BufferUsage::MapRead";
        }
        else if (canonicalName == "UGL::CopySrc")
        {
            result = "GVM::RHI::BufferUsage::CopySrc";
        }
        else if (canonicalName == "UGL::QueryResolve")
        {
            result = "GVM::RHI::BufferUsage::QueryResolve";
        }
        else
        {
            throw std::runtime_error("Unknown buffer usage: " + canonicalName);
        }
        return result;
    }
    std::string convertTextureUsage(const std::string &canonicalName)
    {
        std::string result;
        if (canonicalName == "UGL::CopySrc")
        {
            result = "GVM::RHI::TextureUsage::CopySrc";
        }
        else if (canonicalName == "UGL::CopyDst")
        {
            result = "GVM::RHI::TextureUsage::CopyDst";
        }
        else if (canonicalName == "UGL::TextureBinding")
        {
            result = "GVM::RHI::TextureUsage::TextureBinding";
        }
        else if (canonicalName == "UGL::StorageBinding")
        {
            result = "GVM::RHI::TextureUsage::StorageBinding";
        }
        else if (canonicalName == "UGL::RenderAttachment")
        {
            result = "GVM::RHI::TextureUsage::RenderAttachment";
        }
        else if (canonicalName == "UGL::PixelLocalAttachment")
        {
            result = "GVM::RHI::TextureUsage::PixelLocalAttachment";
        }
        else
        {
            throw std::runtime_error("Unknown texture usage: " + canonicalName);
        }
        return result;
    }
    std::string convertTextureDimension(const std::string &canonicalName)
    {
        std::string result;
        if (canonicalName == "UGL::TextureDimension::e1D")
        {
            throw std::runtime_error(
                "UGLC C++ codegen portable texture contract does not support UGL::TextureDimension::e1D. "
                "GVM shader/resource binding currently only legalizes portable 2D/2DArray/3D shapes, so an e1D texture here indicates an unexpected type deduction or upstream descriptor bug.");
        }
        else if (canonicalName == "UGL::TextureDimension::e2D")
        {
            result = "GVM::RHI::TextureDimension::e2D";
        }
        else if (canonicalName == "UGL::TextureDimension::e3D")
        {
            result = "GVM::RHI::TextureDimension::e3D";
        }
        else
        {
            throw std::runtime_error("Unknown texture dimension: " + canonicalName);
        }
        return result;
    }
    std::string convertTextureFormat(const std::string &canonicalName)
    {
        return "GVM::RHI" + canonicalName.substr(3);
    }
    int convertVertexFormatToStorageBytes(const std::string &canonicalName)
    {
        return getVertexFormatStorageBytes(canonicalName);
    }

    std::string CPPVisitor::translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E)
    {
        std::string result;
        auto op = E->getOperator();
        // std::string typeName = generateTypeCanonicalName(E->getArg(0)->getType());

        setResourceUseInfo(E->getArg(0)->getType());
        result = translateExprAsGroupedInfixOperand(E->getArg(0)) + " "
                 + clang::getOperatorSpelling(op) + " "
                 + translateExprAsGroupedInfixOperand(E->getArg(1));
        // Either side of the translated operator may consume these scratch
        // descriptors, so clear both after emitting the expression.
        mLastBufferInfo = {};
        mLastTextureInfo = {};
        return result;
    }
    std::string CPPVisitor::translateCallExpr(const clang::CallExpr *E)
    {
        CPPPixelLocalAnalysis(*this).validatePassExpressionOrThrow(E);
        const auto *callee = E->getDirectCallee();
        const auto resultType = generateTypeCanonicalName(E->getType(), &mTypeConvertor);
        // GLM's scalar math callbacks require native floating-point types. Preserve
        // binary16 storage and round the intrinsic result at the DSL return boundary.
        bool hasHalfOperand = resultType.find("GVM::Core::Math::ShaderHalf") != std::string::npos;
        for (const auto *argument : E->arguments())
        {
            hasHalfOperand = hasHalfOperand ||
                generateTypeCanonicalName(argument->getType(), &mTypeConvertor).find("GVM::Core::Math::ShaderHalf") != std::string::npos;
        }
        if (callee != nullptr && hasHalfOperand)
        {
            const std::string name = callee->getQualifiedNameAsString();
            static constexpr std::array mathNames = {
                "UGL::abs", "UGL::acos", "UGL::asin", "UGL::atan", "UGL::ceil", "UGL::cos",
                "UGL::cosh", "UGL::exp", "UGL::exp2", "UGL::floor", "UGL::frac", "UGL::log",
                "UGL::log2", "UGL::log10", "UGL::round", "UGL::rsqrt", "UGL::saturate",
                "UGL::sign", "UGL::sin", "UGL::sinh", "UGL::sqrt", "UGL::tan", "UGL::tanh",
                "UGL::normalize", "UGL::radians", "UGL::atan2", "UGL::fmod", "UGL::min",
                "UGL::max", "UGL::step", "UGL::reflect", "UGL::dot", "UGL::lerp", "UGL::pow"
            };
            if (std::find(mathNames.begin(), mathNames.end(), name) != mathNames.end())
            {
                std::string result = resultType + "(" + mFuncConvertor.convertFunc(name, {}) + "(";
                for (unsigned index = 0; index < E->getNumArgs(); ++index)
                {
                    if (index != 0) { result += ", "; }
                    const auto *argument = E->getArg(index);
                    const auto hostType = generateTypeCanonicalName(argument->IgnoreParenImpCasts()->getType(), &mTypeConvertor);
                    std::string value = TranslateExpr(argument);
                    if (hostType.find("GVM::Core::Math::ShaderHalf") != std::string::npos)
                    {
                        auto width = getUGLVectorComponentCount(hostType);
                        if (!width)
                        {
                            const auto *vector = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                                argument->getType()->getAsCXXRecordDecl());
                            if (vector != nullptr && vector->getQualifiedNameAsString() == "UGL::Vector" &&
                                vector->getTemplateArgs().size() == 2u &&
                                vector->getTemplateArgs()[1].getKind() == clang::TemplateArgument::Integral)
                            {
                                width = static_cast<int>(vector->getTemplateArgs()[1].getAsIntegral().getZExtValue());
                            }
                        }
                        value = (width ? "float" + std::to_string(*width) : "float") + "(" + value + ")";
                    }
                    result += value;
                }
                return result + "))";
            }
        }
        return BaseASTVisitor::translateCallExpr(E);
    }
    std::string CPPVisitor::translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E)
    {
        if (llvm::isa<clang::CXXConversionDecl>(E->getMethodDecl()))
        {
            // 如果是，说明这是一个由编译器为实现类型转换而插入的隐式调用。
            // 我们不应该翻译 .operator Type()，而是直接翻译调用它的对象本身，
            // 以还原源码的样貌。
            return TranslateExpr(E->getImplicitObjectArgument());
        }

        // 直接翻译“被调用者”的完整表达式
        std::string Callee = TranslateExpr(E->getCallee());
        clang::Expr *baseObjectExpr = E->getImplicitObjectArgument();
        const std::string methodName = E->getMethodDecl() != nullptr ? E->getMethodDecl()->getNameAsString() : "";
        const std::string implicitObjectTypeName = baseObjectExpr != nullptr ? generateTypeCanonicalName(baseObjectExpr->getType()) : "";
        std::string result;
        result += generateCXXMemberCallExpr(E, Callee);
        if (methodName == "createBuffer")
        {
            if (implicitObjectTypeName == mUGLDeviceImplName)
            {
                if (this->mLastBufferInfo.typeName.empty())
                {
                    throw std::runtime_error("cannot find bufferUsageInfo");
                }
                return Callee + "({.label=" + TranslateExpr(E->getArg(0)) + ", .usage = " + stringJoin(mLastBufferInfo.usageInfos, "|") + ", .size = uint64_t(" + TranslateExpr(E->getArg(1)) + ") * sizeof(" + mLastBufferInfo.typeName + ")})";
            }
        }
        else if (methodName == "createTexture")
        {
            // 处理 createTexture 的情况
            if (E->getNumArgs() < 3)
            {
                throw std::runtime_error("createTexture requires at least 3 arguments");
            }
            std::string debugLabel = TranslateExpr(E->getArg(0));
            if (implicitObjectTypeName == mUGLDeviceImplName)
            {
                if (this->mLastTextureInfo.typeName.empty())
                {
                    throw std::runtime_error("cannot find LastTextureInfo");
                }
                std::string result;

                std::string depthExpr = "1u";
                if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(E->getArg(3)) == nullptr)
                {
                    depthExpr = TranslateExpr(E->getArg(3));
                }

                result = Callee + "({.label=" + debugLabel + ", .usage = " + stringJoin(mLastTextureInfo.usageInfos, "|") + ", .dimension = " + mLastTextureInfo.dimension + ", .size = {.width = (uint32_t)" + TranslateExpr(E->getArg(1)) + ", .height = (uint32_t)" + TranslateExpr(E->getArg(2)) + ", .depth = (uint32_t)" + depthExpr + "}, .format = " + mLastTextureInfo.typeName;
                if (llvm::dyn_cast<clang::CXXDefaultArgExpr>(E->getArg(4)) == nullptr)
                {
                    result += ", .mipLevelCount = " + TranslateExpr(E->getArg(4));
                }
                if (auto argStr = TranslateExpr(E->getArg(5)); argStr.empty() == false)
                {
                    result += ", .arrayLayerCount = " + argStr;
                }
                if (mLastTextureInfo.usesPixelLocalAttachment && !mLastTextureInfo.usesPersistentTextureAccess)
                {
                    result += ", .storageMode = GVM::RHI::TextureStorageMode::TransientAttachment";
                }
                result += "})";
                mLastTextureInfo = {};
                return result;
            }
        }
        else if (auto shaderOnlyOperation = mHostShaderOnlyGuard.describeTextureOperation(implicitObjectTypeName, methodName))
        {
            // These member calls only make sense inside generated GPU shaders.
            // Keep the host header buildable, but route any attempted host-side
            // execution into a single explicit fail-fast stub.
            if (E->getType()->isVoidType())
            {
                return mHostShaderOnlyGuard.makeVoidCallExpr(*shaderOnlyOperation);
            }

            if (baseObjectExpr == nullptr)
            {
                throwCodegenError("Failed to lower shader-only operation \"" + *shaderOnlyOperation + "\" because its implicit object argument is missing.");
            }

            auto hostReturnType = mHostShaderOnlyGuard.inferTextureReturnType(baseObjectExpr->getType(), methodName);
            if (!hostReturnType.has_value())
            {
                throwCodegenError("Failed to infer a host shader-only stub return type for \"" + *shaderOnlyOperation + "\" on object type \"" + implicitObjectTypeName + "\".");
            }

            return mHostShaderOnlyGuard.makeCallExpr(*hostReturnType, *shaderOnlyOperation);
        }


        return result;
    }
    bool CPPVisitor::checkUGLDerivedClass(const clang::CXXRecordDecl *decl) const
    {
        if (decl->hasDefinition() == false)
        {
            return false;
        }

        for (auto baseClass : decl->bases())
        {
            auto cxxDecl = getUnqualifiedType(baseClass.getType())->getAsCXXRecordDecl();
            if (cxxDecl == nullptr)
            {
                continue;
            }
            auto baseClassName = getClassCanonicalName(cxxDecl);
            if (baseClassName == mUGLBindGroupBaseName || baseClassName == mUGLFrameBufferBaseName || baseClassName == mUGLRenderClassBaseName || baseClassName == mUGLComputeClassBaseName || baseClassName == mUGLAbstractRendererClassName || baseClassName.starts_with("UGL::"))
            {
                return true;
            }
        }
        for (const auto &f : getAllFieldFromRecord(decl))
        {
            if (auto recordDecl = getUnqualifiedType(f->getType())->getAsCXXRecordDecl(); recordDecl != nullptr && getClassCanonicalName(recordDecl).starts_with("UGL::"))
            {
                return true;
            }
        }
        return false;
    }

    bool CPPVisitor::shouldEmitUGLTemplateSpecializationBundle(const clang::CXXRecordDecl *decl) const
    {
        return decl != nullptr && decl->getDescribedClassTemplate() != nullptr && checkUGLDerivedClass(decl);
    }

    std::string CPPVisitor::generateUGLTemplateForwardDeclaration(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl)
    {
        std::string result;
        result += generateTemplateParameters(templateDecl->getTemplateParameters());
        result += mSpaceManager.getSpace() + std::string(primaryDecl->isClass() ? "class" : "struct") + " " + primaryDecl->getNameAsString() + EOS();
        return result;
    }

    std::string CPPVisitor::replaceUGLTemplateSpecializationBundle(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl)
    {
        std::string result = generateUGLTemplateForwardDeclaration(templateDecl, primaryDecl);
        std::unordered_set<std::string> emittedVariantLabels;

        for (auto *specialization : templateDecl->specializations())
        {
            if (specialization == nullptr || !shouldMaterializeTemplateSpecialization(specialization))
            {
                continue;
            }
            if ((checkDerivedClassByName(primaryDecl, mUGLRenderClassBaseName) ||
                 checkDerivedClassByName(primaryDecl, mUGLPixelLocalRenderClassBaseName) ||
                 checkDerivedClassByName(primaryDecl, mUGLComputeClassBaseName)) &&
                !isShaderVariantRootSpecialization(specialization))
            {
                continue;
            }
            const std::string variantLabel = getRecordVariantLabel(specialization);
            if (!emittedVariantLabels.emplace(variantLabel).second)
            {
                throwCodegenError(specialization, "UGL template \"" + primaryDecl->getQualifiedNameAsString() + "\" has multiple specializations that map to generated label \"" + variantLabel + "\".");
            }

            result += NewLine();
            result += getLineDirective(specialization->getBeginLoc());
            pushTemplateSubstitutionContext(specialization);
            result += replaceUGLClass(primaryDecl);
            popTemplateSubstitutionContext();
        }

        return result;
    }

    std::string CPPVisitor::replacePlainTemplateSpecializationBundle(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl)
    {
        const clang::SourceManager &sourceManager = Context->getSourceManager();
        const llvm::StringRef originalSource = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(templateDecl->getSourceRange()),
                                                                           sourceManager,
                                                                           Context->getLangOpts());
        std::string result = originalSource.str();
        std::unordered_set<std::string> emittedVariantLabels;

        for (auto *specialization : templateDecl->specializations())
        {
            if (specialization == nullptr || !shouldMaterializeTemplateSpecialization(specialization))
            {
                continue;
            }

            const std::string variantLabel = getRecordVariantLabel(specialization);
            if (!emittedVariantLabels.emplace(variantLabel).second)
            {
                throwCodegenError(specialization, "Template \"" + primaryDecl->getQualifiedNameAsString() + "\" has multiple specializations that map to generated label \"" + variantLabel + "\".");
            }

            result += NewLine();
            result += getLineDirective(specialization->getBeginLoc());
            pushTemplateSubstitutionContext(specialization);
            result += replacePlainTemplateSpecializationRecord(primaryDecl);
            popTemplateSubstitutionContext();
        }

        return result;
    }

    bool CPPVisitor::shouldMaterializeTemplateSpecialization(const clang::ClassTemplateSpecializationDecl *specializationDecl) const
    {
        if (specializationDecl == nullptr)
        {
            return false;
        }
        const auto *canonicalSpecialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(specializationDecl->getCanonicalDecl());
        return mMaterializedTemplateSpecializations.contains(canonicalSpecialization);
    }

    bool CPPVisitor::isShaderVariantRootSpecialization(const clang::ClassTemplateSpecializationDecl *specializationDecl) const
    {
        if (specializationDecl == nullptr)
        {
            return false;
        }
        const auto *canonicalSpecialization = llvm::cast<clang::ClassTemplateSpecializationDecl>(specializationDecl->getCanonicalDecl());
        return mShaderVariantRootSpecializations.contains(canonicalSpecialization);
    }

    std::string CPPVisitor::replacePlainTemplateSpecializationRecord(const clang::CXXRecordDecl *primaryDecl)
    {
        if (recordUsesShaderResourceHandles(primaryDecl))
        {
            return replaceShaderResourceBehaviorRecordShell(primaryDecl);
        }

        std::string result;
        const std::string keyword = primaryDecl->isClass() ? "class" : "struct";
        result += mSpaceManager.getSpace() + keyword + " " + getRecordEmissionName(primaryDecl) + generateBaseClassInRecordDefinition(primaryDecl) + NewLine();
        result += enterScope();
        for (auto *innerDecl : primaryDecl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (shouldEmitNestedRecordDefinition(primaryDecl, nestedRecord))
                {
                    result += getLineDirective(nestedRecord->getBeginLoc());
                    result += generateRecordDefinition(nestedRecord);
                }
            }
        }
        result += generateRecordDataMembers(primaryDecl);
        for (auto *method : primaryDecl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += generateFunctionDefinition(method);
            }
        }
        result += endClass();
        return result;
    }

    std::string CPPVisitor::replaceShaderResourceBehaviorRecordShell(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        const std::string keyword = decl->isClass() ? "class" : "struct";
        result += mSpaceManager.getSpace() + keyword + " " + getRecordEmissionName(decl) + generateBaseClassInRecordDefinition(decl) + NewLine();
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

        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += generateShaderResourceBehaviorShellMethod(method);
            }
        }

        result += endClass();
        return result;
    }

    std::string CPPVisitor::generateShaderResourceBehaviorShellMethod(const clang::FunctionDecl *func)
    {
        std::string result;
        result += generateFunctionSignature(func, func->getNameAsString(), "const", &mTypeConvertor);
        result += enterScope();
        if (!func->getReturnType()->isVoidType())
        {
            result += mSpaceManager.getSpace() + "return {}" + EOS();
        }
        result += quitScope();
        return result;
    }

    bool CPPVisitor::directShaderResourceHandleRequiresHostShell(const clang::QualType &type) const
    {
        if (!isShaderResourceHandleType(type))
        {
            return false;
        }

        // UGL::Sampler is a shared handle spelling in the DSL: shader code lowers
        // it to a sampler resource, while generated host code stores it as
        // GVM::RHI::Sampler. Do not shell ordinary host orchestration records just
        // because they cache a sampler created by Device::createSampler().
        return !checkTypeCanonicalName(type, mUGLShaderSamplerName);
    }

    bool CPPVisitor::functionSignatureUsesDirectShaderResourceHandles(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }

        if (directShaderResourceHandleRequiresHostShell(func->getReturnType()))
        {
            return true;
        }

        for (const clang::ParmVarDecl *param : func->parameters())
        {
            if (param != nullptr && directShaderResourceHandleRequiresHostShell(param->getType()))
            {
                return true;
            }
        }

        return false;
    }

    bool CPPVisitor::recordUsesDirectShaderResourceHandles(const clang::CXXRecordDecl *decl) const
    {
        if (decl == nullptr)
        {
            return false;
        }

        const clang::CXXRecordDecl *definition = decl->getDefinition();
        if (definition == nullptr)
        {
            definition = decl;
        }

        for (const clang::FieldDecl *field : definition->fields())
        {
            if (field != nullptr && directShaderResourceHandleRequiresHostShell(field->getType()))
            {
                return true;
            }
        }

        for (const clang::CXXMethodDecl *method : definition->methods())
        {
            if (functionSignatureUsesDirectShaderResourceHandles(method))
            {
                return true;
            }
        }

        return false;
    }

    std::string CPPVisitor::getRecordEmissionName(const clang::CXXRecordDecl *decl)
    {
        if (const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(decl))
        {
            return getRecordVariantLabel(specializationDecl);
        }

        if (const auto *currentSpecialization = getCurrentTemplateSubstitutionContext())
        {
            if (currentSpecialization->getSpecializedTemplate() != nullptr &&
                currentSpecialization->getSpecializedTemplate()->getTemplatedDecl()->getCanonicalDecl() == decl->getCanonicalDecl())
            {
                return getRecordVariantLabel(currentSpecialization);
            }
        }

        return decl == nullptr ? "" : decl->getNameAsString();
    }

    std::string CPPVisitor::getRecordVariantLabel(const clang::CXXRecordDecl *decl)
    {
        if (const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(decl))
        {
            std::string result = specializationDecl->getSpecializedTemplate()->getNameAsString();
            const clang::TemplateArgumentList &templateArgs = specializationDecl->getTemplateArgs();
            for (unsigned i = 0; i < templateArgs.size(); ++i)
            {
                result += "__";
                result += sanitizeVariantIdentifierComponent(translateTemplateArgument(templateArgs.get(i), &mTypeConvertor));
            }
            return result;
        }

        if (const auto *currentSpecialization = getCurrentTemplateSubstitutionContext())
        {
            if (currentSpecialization->getSpecializedTemplate() != nullptr &&
                currentSpecialization->getSpecializedTemplate()->getTemplatedDecl()->getCanonicalDecl() == decl->getCanonicalDecl())
            {
                return getRecordVariantLabel(currentSpecialization);
            }
        }

        return decl == nullptr ? "" : decl->getNameAsString();
    }

    std::string CPPVisitor::inferBindGroupVisibilityExpr(const clang::CXXRecordDecl *bindGroupDecl)
    {
        if (const auto *prepared = mShaderSourcePipelineOptions.preparedShaders)
        {
            const auto typeName = getPreparedRecordSymbolName(*bindGroupDecl);
            if (const auto cached = mBindGroupVisibilityExprCache.find(typeName); cached != mBindGroupVisibilityExprCache.end())
                return cached->second;
            BindGroupStageUsage usage{};
            for (const auto &shader : prepared->shaders)
                for (const auto &group : shader.module.reflection.bindGroups)
                    if (group.typeName == typeName)
                    {
                        usage.usesVertex |= shader.module.reflection.stage == UGLIR::ShaderStage::Vertex;
                        usage.usesFragment |= shader.module.reflection.stage == UGLIR::ShaderStage::Fragment;
                        usage.usesCompute |= shader.module.reflection.stage == UGLIR::ShaderStage::Compute;
                    }
            std::vector<std::string> stages;
            if (usage.usesVertex) stages.emplace_back("GVM::RHI::ShaderStage::Vertex");
            if (usage.usesFragment) stages.emplace_back("GVM::RHI::ShaderStage::Fragment");
            if (usage.usesCompute) stages.emplace_back("GVM::RHI::ShaderStage::Compute");
            const auto visibility = stages.empty()
                ? "GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment | GVM::RHI::ShaderStage::Compute"
                : stringJoin(stages, " | ");
            mBindGroupVisibilityExprCache.emplace(typeName, visibility);
            return visibility;
        }
        const std::string bindGroupCanonicalName = getClassCanonicalName(bindGroupDecl);
        if (mBindGroupVisibilityCacheReady == false)
        {
            auto makeVisibilityExpr = [](const BindGroupStageUsage &usage) {
                std::vector<std::string> stageExprs;
                if (usage.usesVertex)
                {
                    stageExprs.emplace_back("GVM::RHI::ShaderStage::Vertex");
                }
                if (usage.usesFragment)
                {
                    stageExprs.emplace_back("GVM::RHI::ShaderStage::Fragment");
                }
                if (usage.usesCompute)
                {
                    stageExprs.emplace_back("GVM::RHI::ShaderStage::Compute");
                }
                if (usage.usesHull)
                {
                    stageExprs.emplace_back("GVM::RHI::ShaderStage::Hull");
                }
                if (usage.usesDomain)
                {
                    stageExprs.emplace_back("GVM::RHI::ShaderStage::Domain");
                }
                if (stageExprs.empty())
                {
                    return std::string("GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment | GVM::RHI::ShaderStage::Compute");
                }
                return stringJoin(stageExprs, " | ");
            };

            std::unordered_map<std::string, BindGroupStageUsage> bindGroupStageUsageMap;
            std::function<void(const clang::DeclContext *)> visitDeclContext;
            visitDeclContext = [&](const clang::DeclContext *declContext) {
                if (declContext == nullptr)
                {
                    return;
                }

                for (const auto *decl : declContext->decls())
                {
                    if (const auto *namedDecl = llvm::dyn_cast<clang::NamedDecl>(decl))
                    {
                        if (isFromExcludedFile(namedDecl->getLocation()))
                        {
                            continue;
                        }
                    }

                    if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
                    {
                        if (recordDecl->isThisDeclarationADefinition())
                        {
                            const bool isRenderClass = checkDerivedClassByName(recordDecl, mUGLRenderClassBaseName) || checkDerivedClassByName(recordDecl, mUGLPixelLocalRenderClassBaseName);
                            const bool isComputeClass = checkDerivedClassByName(recordDecl, mUGLComputeClassBaseName);
                            if (isRenderClass || isComputeClass)
                            {
                                const BindGroupInfoMap bindGroupInfoMap = createBindGroupInfoMap(recordDecl, 0);
                                for (const auto &[bindingIndex, bindGroupInfo] : bindGroupInfoMap)
                                {
                                    (void)bindingIndex;
                                    if (bindGroupInfo.isRenderSet || bindGroupInfo.typeDecl == nullptr)
                                    {
                                        continue;
                                    }

                                    auto &usage = bindGroupStageUsageMap[getClassCanonicalName(bindGroupInfo.typeDecl)];
                                    if (isComputeClass)
                                    {
                                        usage.usesCompute = true;
                                    }
                                    else
                                    {
                                        const auto *vertexMethod = getMethodFromClass(recordDecl, mUGLVertexShaderFunctionName, makeVertexShaderMethodLookupOptions());
                                        usage.usesVertex = vertexMethod != nullptr || usage.usesVertex;
                                        usage.usesFragment = getMethodFromClass(recordDecl, mUGLFragmentShaderFunctionName, makeFragmentShaderMethodLookupOptions(vertexMethod != nullptr ? std::optional<clang::QualType>(vertexMethod->getReturnType()) : std::nullopt)) != nullptr ||
                                            getMethodFromClass(recordDecl, mUGLPixelShaderFunctionName, makeFragmentShaderMethodLookupOptions(std::nullopt)) != nullptr ||
                                            usage.usesFragment;
                                        usage.usesHull = getMethodFromClass(recordDecl, mUGLHullShaderFunctionName, makeHullShaderMethodLookupOptions()) != nullptr || usage.usesHull;
                                        usage.usesDomain = getMethodFromClass(recordDecl, mUGLDomainShaderFunctionName, makeDomainShaderMethodLookupOptions()) != nullptr || usage.usesDomain;
                                    }
                                }
                            }

                            visitDeclContext(recordDecl);
                        }
                    }
                    else if (const auto *nestedDeclContext = llvm::dyn_cast<clang::DeclContext>(decl))
                    {
                        visitDeclContext(nestedDeclContext);
                    }
                }
            };

            visitDeclContext(Context->getTranslationUnitDecl());

            for (const auto &[name, usage] : bindGroupStageUsageMap)
            {
                mBindGroupVisibilityExprCache.emplace(name, makeVisibilityExpr(usage));
            }
            mBindGroupVisibilityCacheReady = true;
        }

        if (auto it = mBindGroupVisibilityExprCache.find(bindGroupCanonicalName); it != mBindGroupVisibilityExprCache.end())
        {
            return it->second;
        }

        return "GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment | GVM::RHI::ShaderStage::Compute";
    }
    std::string CPPVisitor::generateBindGoupAssign(const BindGroupInfoMap &bmap)
    {
        std::string result;
        for (const auto &[index, b] : bmap)
        {
            if (b.isRenderSet)
            {
                result += mSpaceManager.getSpace() + "this->mRenderSetBindGroupIndex = " + std::to_string(index) + EOS();
            }
            else
            {
                result += mSpaceManager.getSpace() + "this->bindGroups.emplace(" + std::to_string(index) + ", " + b.name + "->mBindGroup)" + EOS();
            }
        }
        return result;
    }
    /** Preserves vector constructor lowering when source code uses a C-style cast. */
    std::string CPPVisitor::translateCStyleCastExpr(const clang::CStyleCastExpr *expr)
    {
        if (const auto *constructor = llvm::dyn_cast<clang::CXXConstructExpr>(expr->getSubExpr()->IgnoreParenCasts());
            constructor != nullptr && Context->hasSameType(constructor->getType(), expr->getType()) &&
            getUGLVectorComponentCount(generateTypeCanonicalName(expr->getType(), &mTypeConvertor)).has_value())
        {
            return translateCXXConstructExprFunction(constructor);
        }
        return BaseASTVisitor::translateCStyleCastExpr(expr);
    }

    /** Preserves vector constructor lowering when source code uses static_cast. */
    std::string CPPVisitor::translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *expr)
    {
        if (const auto *constructor = llvm::dyn_cast<clang::CXXConstructExpr>(expr->getSubExpr()->IgnoreParenCasts());
            constructor != nullptr && Context->hasSameType(constructor->getType(), expr->getType()) &&
            getUGLVectorComponentCount(generateTypeCanonicalName(expr->getType(), &mTypeConvertor)).has_value())
        {
            return translateCXXConstructExprFunction(constructor);
        }
        return BaseASTVisitor::translateCXXStaticCastExpr(expr);
    }

    std::string CPPVisitor::translateCXXConstructExprFunction(const clang::CXXConstructExpr *E)
    {
        // 场景4：处理函数式初始化，例如 `T t(...)`
        // 经过前面几种情况的排除，剩下的必然是需要添加类型名的函数式初始化
        const std::string typeName = generateTypeCanonicalName(E->getType(), &mTypeConvertor);
        std::string result;
        result += typeName;

        result += "(";

        // 先收集所有有效的参数字符串，再用逗号拼接
        std::vector<std::string> argStrs;
        for (unsigned i = 0; i < E->getNumArgs(); ++i)
        {
            std::string argStr = TranslateExpr(E->getArg(i));
            if (!argStr.empty())
            {
                argStrs.push_back(argStr);
            }
        }

        if (argStrs.size() == 1)
        {
            if (const std::optional<int> componentCount = getUGLVectorComponentCount(typeName))
            {
                const clang::Expr *constructorArgument = E->getArg(0)->IgnoreParenImpCasts();
                if (llvm::isa<clang::MemberExpr>(constructorArgument))
                {
                    if (std::optional<std::string> expandedConstructor =
                            materializeSingleSwizzleConstructorArgument(typeName, argStrs.front(), *componentCount))
                    {
                        return *expandedConstructor;
                    }
                }
            }
        }

        for (size_t i = 0; i < argStrs.size(); ++i)
        {
            result += argStrs[i];
            if (i < argStrs.size() - 1)
            {
                result += ", ";
            }
        }

        result += ")";
        return result;
    }

    /**
     * @brief 使用纯字符串操作提取 Slot 数字
     */
    int CPPVisitor::getAttributeNumber(const std::string &input, const std::string &keyword) const
    {


        // 1. 查找关键字位置
        size_t pos = input.find(keyword);
        if (pos == std::string::npos)
        {
            return -1;
        }

        // 2. 计算数字开始的索引
        size_t numStartIdx = pos + keyword.length();

        // 3. 边界检查：
        //    a. 确保 Slot 不是字符串的结尾 (例如 "xx Slot")
        //    b. 确保 Slot 后面紧跟的确实是数字 (防止 "SlotABC")
        if (numStartIdx >= input.length() || !std::isdigit(input[numStartIdx]))
        {
            return -1;
        }

        // 4. 解析数字
        // &input[numStartIdx] 获取从数字开始的 C 风格字符串指针 (const char*)
        // endPtr 用于存储数字解析结束后的位置 (这里我们不需要，传 nullptr 也可以，但在严谨代码中可用于检查)
        char *endPtr = nullptr;

        // 使用 strtol (String to Long)
        // 参数1: 字符串起始地址
        // 参数2: 更新后的结束地址指针 (可选)
        // 参数3: 进制 (10进制)
        long result = std::strtol(&input[numStartIdx], &endPtr, 10);

        // 5. 再次验证 (可选，但在专业代码中推荐)
        // 检查 endPtr 是否确实移动了，且 result 没有因为溢出等原因出错
        // 这里简单处理：只要解析出了数字即可

        return static_cast<int>(result);
    }

    std::string CPPVisitor::translateVarDecl(const clang::VarDecl *VD)
    {
        CPPPixelLocalAnalysis(*this).validateAttachmentValueObjectVarOrThrow(VD);

        std::string staticSpecifier = VD->getStorageClass() == clang::SC_Static ? "static " : "";
        std::string constSpecifier = VD->getType().isConstQualified() ? "const " : "";
        std::string constexprSpecifier = VD->isConstexpr() ? "constexpr" : "";
        std::string result; // = staticSpecifier + constexprSpecifier + constSpecifier + generateTypeCanonicalName(VD->getType(), mDefaultTypeConvertor) + " " + VD->getNameAsString() + generateDeclArraySpecifier(VD->getType());

        setResourceUseInfo(VD->getType());

        result = BaseASTVisitor::translateVarDecl(VD);
        mLastTextureInfo = {};
        mLastBufferInfo = {};
        return result;
    }

    /*  std::string CPPVisitor::translateBinaryOperator(const clang::BinaryOperator *E)
     {
         const auto LHS = E->getLHS();
         if(LHS.)
         std::string L = TranslateExpr(E->getLHS());
         std::string R = TranslateExpr(E->getRHS());
         return L + " " + E->getOpcodeStr().str() + " " + R;
     } */

} // namespace UGLC::CodeGen::CPP
