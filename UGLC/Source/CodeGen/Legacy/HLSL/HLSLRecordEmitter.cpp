#include "HLSLRecordEmitter.hpp"

#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>
#include <CodeGen/Legacy/HLSL/HLSLRecordNameUtils.hpp>
#include <CodeGen/Legacy/HLSL/HLSLTypeConvertor.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <llvm/Support/Casting.h>
#include <stdexcept>

namespace UGLC::CodeGen::HLSL
{
    HLSLRecordEmitter::HLSLRecordEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor, HLSLRenderInterfaceValidator &renderInterfaceValidator)
        : mVisitor(visitor)
        , mTypeConvertor(typeConvertor)
        , mRenderInterfaceValidator(renderInterfaceValidator)
    {
    }

    std::string HLSLRecordEmitter::generateRecordFieldDecl(const clang::FieldDecl *decl, AbstractTypeConvertor *typeConvertor)
    {
        std::string result;
        const bool hasExplicitInitializer = decl->hasInClassInitializer() && mVisitor.isImplicitNode(decl->getInClassInitializer()) == false;
        const clang::QualType unqualifiedType = mVisitor.getUnqualifiedType(decl->getType());
        const bool canEmitStaticConstInitializer = hasExplicitInitializer && decl->getType().isConstQualified() && (unqualifiedType->isIntegerType() || unqualifiedType->isEnumeralType() || unqualifiedType->isBooleanType() || unqualifiedType->isRealFloatingType());
        const clang::QualType emittedType = canEmitStaticConstInitializer ? unqualifiedType : decl->getType();
        std::string typeName = mVisitor.generateTypeCanonicalName(emittedType, typeConvertor == nullptr ? mTypeConvertor : typeConvertor);
        std::string semanticSuffix = mRenderInterfaceValidator.getFieldSemanticSuffix(decl);
        result += mVisitor.getLineDirective(decl->getBeginLoc());
        result += mVisitor.mSpaceManager.getSpace();
        if (canEmitStaticConstInitializer)
        {
            result += "static const ";
        }
        result += typeName + " " + sanitizeHLSLIdentifier(decl->getNameAsString()) + mVisitor.generateDeclArraySpecifier(decl->getType()) + semanticSuffix;
        if (canEmitStaticConstInitializer)
        {
            result += " = " + mVisitor.TranslateExpr(decl->getInClassInitializer());
        }
        result += mVisitor.EOS();
        return result;
    }

    std::string HLSLRecordEmitter::generateRecordDefinitionDetailed(const clang::CXXRecordDecl *decl)
    {
        std::string result = "\n";
        const std::string keyword = decl->isClass() ? "class" : "struct";
        std::string recordName = generateGlobalHLSLRecordTypeName(mVisitor, decl, mTypeConvertor);

        result += mVisitor.mSpaceManager.getSpace() + keyword + " " + recordName + mVisitor.NewLine();
        result += mVisitor.enterScope();

        for (auto *innerDecl : decl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (mVisitor.shouldEmitNestedRecordDefinition(decl, nestedRecord))
                {
                    result += mVisitor.getLineDirective(nestedRecord->getBeginLoc());
                    result += mVisitor.generateRecordDefinition(nestedRecord);
                }
            }
        }

        result += mVisitor.generateRecordDataMembers(decl, mTypeConvertor);

        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += mVisitor.generateFunctionDefinition(method);
            }
        }

        result += mVisitor.endClass();
        return result;
    }

    std::string HLSLRecordEmitter::generateFramebufferClass(const clang::CXXRecordDecl *decl, const std::unordered_set<std::string> *includedOutputFields)
    {
        std::string result;
        const std::string keyword = decl->isClass() ? "class" : "struct";
        std::string recordName = generateGlobalHLSLRecordTypeName(mVisitor, decl, mTypeConvertor);

        if (auto *templateDecl = decl->getDescribedClassTemplate();
            templateDecl != nullptr && mVisitor.getCurrentTemplateSubstitutionContext() == nullptr)
        {
            result += mVisitor.getLineDirective(templateDecl->getBeginLoc());
            result += mVisitor.generateTemplateParameters(templateDecl->getTemplateParameters());
        }

        result += mVisitor.mSpaceManager.getSpace() + keyword + " " + recordName + mVisitor.NewLine();
        result += mVisitor.enterScope();

        int colorIndex = 0;
        for (auto *field : decl->fields())
        {
            const auto templateArgs = mVisitor.getTemplateArgumentsFromType(field->getType());
            std::string typeName;
            std::string semanticSuffix;

            if (mVisitor.checkTypeCanonicalName(field->getType(), mUGLColorAttachmentName) ||
                mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalColorAttachmentName))
            {
                const int currentColorIndex = colorIndex++;
                if (includedOutputFields != nullptr && includedOutputFields->find(field->getNameAsString()) == includedOutputFields->end())
                {
                    continue;
                }
                typeName = MakeFormatToVectorTypeForFrameBuffer(mVisitor.translateTemplateArgument(templateArgs.front(), nullptr));
                semanticSuffix = mRenderInterfaceValidator.getFieldSemanticSuffix(field, currentColorIndex);
            }
            else if (mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName))
            {
                if (includedOutputFields != nullptr && includedOutputFields->find(field->getNameAsString()) == includedOutputFields->end())
                {
                    continue;
                }
                const size_t writePatternIndex = 5;
                if (templateArgs.size() <= writePatternIndex)
                {
                    continue;
                }

                const std::string writePattern = mVisitor.translateTemplateArgument(templateArgs.at(writePatternIndex));
                if (writePattern == mUGLDepthStencilAttachmentNoWrite)
                {
                    continue;
                }

                typeName = MakeFormatToVectorTypeForFrameBuffer(mVisitor.translateTemplateArgument(templateArgs.front(), nullptr));
                if (writePattern == mUGLDepthStencilAttachmentWriteLess)
                {
                    semanticSuffix = " : SV_DepthLessEqual";
                }
                else if (writePattern == mUGLDepthStencilAttachmentWriteGreater)
                {
                    semanticSuffix = " : SV_DepthGreaterEqual";
                }
                else
                {
                    throw std::runtime_error("can not found depth attachment write pattern: " + writePattern);
                }
            }
            else if (mVisitor.checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName))
            {
                if (includedOutputFields != nullptr && includedOutputFields->find(field->getNameAsString()) == includedOutputFields->end())
                {
                    continue;
                }
                const size_t writePatternIndex = 1;
                if (templateArgs.size() <= writePatternIndex)
                {
                    continue;
                }

                const std::string writePattern = mVisitor.translateTemplateArgument(templateArgs.at(writePatternIndex));
                if (writePattern == mUGLDepthStencilAttachmentNoWrite)
                {
                    continue;
                }

                typeName = MakeFormatToVectorTypeForFrameBuffer(mVisitor.translateTemplateArgument(templateArgs.front(), nullptr));
                if (writePattern == mUGLDepthStencilAttachmentWriteLess)
                {
                    semanticSuffix = " : SV_DepthLessEqual";
                }
                else if (writePattern == mUGLDepthStencilAttachmentWriteGreater)
                {
                    semanticSuffix = " : SV_DepthGreaterEqual";
                }
                else
                {
                    throw std::runtime_error("can not found depth attachment write pattern: " + writePattern);
                }
            }
            else
            {
                if (includedOutputFields != nullptr && includedOutputFields->find(field->getNameAsString()) == includedOutputFields->end())
                {
                    continue;
                }
                typeName = mVisitor.generateTypeCanonicalName(field->getType(), mTypeConvertor);
                semanticSuffix = mRenderInterfaceValidator.getFieldSemanticSuffix(field);
            }

            result += mVisitor.mSpaceManager.getSpace() + typeName + " " + sanitizeHLSLIdentifier(field->getNameAsString()) + semanticSuffix + mVisitor.EOS();
        }

        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method) && method->hasBody())
            {
                result += mVisitor.generateFunctionDefinition(method);
            }
        }

        result += mVisitor.endClass();
        return result;
    }

    std::string HLSLRecordEmitter::generateShaderClassNestedDeclarations(const clang::CXXRecordDecl *decl)
    {
        if (decl == nullptr)
        {
            return {};
        }

        std::string nestedDeclarations;
        for (auto *innerDecl : decl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (mVisitor.shouldEmitNestedRecordDefinition(decl, nestedRecord))
                {
                    nestedDeclarations += mVisitor.getLineDirective(nestedRecord->getBeginLoc());
                    nestedDeclarations += mVisitor.generateRecordDefinition(nestedRecord);
                }
            }
            else if (const auto *typedefDecl = llvm::dyn_cast<clang::TypedefNameDecl>(innerDecl))
            {
                nestedDeclarations += mVisitor.VisitTypedefNameDecl(typedefDecl);
            }
            else if (const auto *templateClassDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(innerDecl))
            {
                nestedDeclarations += mVisitor.generateTemplateClassDecl(templateClassDecl);
            }
            else if (const auto *functionTemplateDecl = llvm::dyn_cast<clang::FunctionTemplateDecl>(innerDecl))
            {
                nestedDeclarations += mVisitor.generateTemplateFunctionDecl(functionTemplateDecl);
            }
        }

        if (nestedDeclarations.empty())
        {
            return {};
        }

        return nestedDeclarations;
    }
} // namespace UGLC::CodeGen::HLSL
