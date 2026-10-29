#include "BaseDeclQuery.hpp"

#include "BaseASTVisitor.hpp"
#include "UGLC.Constants.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>

#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen
{
    BaseDeclQuery::BaseDeclQuery(const BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    bool BaseDeclQuery::isTopLevel(const clang::Decl *decl) const
    {
        const clang::DeclContext *context = decl->getLexicalDeclContext();
        return context->isTranslationUnit() || context->isNamespace();
    }

    bool BaseDeclQuery::checkDerivedClassByName(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        if (decl == nullptr)
        {
            return false;
        }
        const clang::CXXRecordDecl *definition = decl->getDefinition();
        if (definition == nullptr)
        {
            return false;
        }
        decl = definition;

        for (auto baseClass : decl->bases())
        {
            const auto *baseRecordDecl = getUnqualifiedType(baseClass.getType())->getAsCXXRecordDecl();
            if (baseRecordDecl == nullptr)
            {
                continue;
            }

            if (mVisitor.getClassCanonicalName(baseRecordDecl) == name)
            {
                return true;
            }
        }
        return false;
    }

    bool BaseDeclQuery::checkTypeCanonicalName(const clang::QualType &type, const std::string &name) const
    {
        const clang::QualType unqualifiedType = getUnqualifiedType(type);
        if (const auto *templateSpecType = unqualifiedType->getAs<clang::TemplateSpecializationType>())
        {
            if (const auto *templateDecl = templateSpecType->getTemplateName().getAsTemplateDecl())
            {
                if (const auto *recordTemplateDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(templateDecl))
                {
                    return mVisitor.getClassCanonicalName(recordTemplateDecl->getTemplatedDecl()) == name;
                }
            }
        }

        const auto *recordDecl = unqualifiedType->getAsCXXRecordDecl();
        if (recordDecl == nullptr)
        {
            return false;
        }

        return mVisitor.getClassCanonicalName(recordDecl) == name;
    }

    bool BaseDeclQuery::isLegacyStorageBufferType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLShaderLegacyStorageBufferName);
    }

    [[noreturn]] void BaseDeclQuery::throwLegacyStorageBufferMigrationError(const std::string &usageContext) const
    {
        throw std::runtime_error(usageContext
                                 + " still uses deprecated UGL::StorageBuffer<T>. "
                                   "Replace it with UGL::StructuredBuffer<T> for read-only access "
                                   "or UGL::RWStructuredBuffer<T> for read-write access.");
    }

    bool BaseDeclQuery::isBindGroupHandleType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLBindGroupName);
    }

    bool BaseDeclQuery::isRenderSetHandleType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLRenderSetName);
    }

    bool BaseDeclQuery::isShaderResourceHandleType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLShaderUniformBufferName) ||
               checkTypeCanonicalName(type, mUGLShaderStructuredBufferName) ||
               checkTypeCanonicalName(type, mUGLShaderRWStructuredBufferName) ||
               checkTypeCanonicalName(type, mUGLShaderLegacyStorageBufferName) ||
               checkTypeCanonicalName(type, mUGLShaderTexture2DName) ||
               checkTypeCanonicalName(type, mUGLShaderRWTexture2DName) ||
               checkTypeCanonicalName(type, mUGLShaderTexture2DArrayName) ||
               checkTypeCanonicalName(type, mUGLShaderRWTexture2DArrayName) ||
               checkTypeCanonicalName(type, mUGLShaderTexture3DName) ||
               checkTypeCanonicalName(type, mUGLShaderRWTexture3DName) ||
               checkTypeCanonicalName(type, mUGLShaderSamplerName);
    }

    bool BaseDeclQuery::isSampledTextureOrSamplerHandleType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLShaderTexture2DName) ||
               checkTypeCanonicalName(type, mUGLShaderTexture2DArrayName) ||
               checkTypeCanonicalName(type, mUGLShaderTexture3DName) ||
               checkTypeCanonicalName(type, mUGLShaderSamplerName);
    }

    bool BaseDeclQuery::isInputOnlyShaderHandleParameterType(const clang::QualType &type) const
    {
        return isBindGroupHandleType(type) || isRenderSetHandleType(type) || isShaderResourceHandleType(type);
    }

    bool BaseDeclQuery::isAnyShaderResourceHandleType(const clang::QualType &type) const
    {
        return isBindGroupHandleType(type) || isRenderSetHandleType(type) || isShaderResourceHandleType(type);
    }

    bool BaseDeclQuery::isHostResourceHandleType(const clang::QualType &type) const
    {
        return checkTypeCanonicalName(type, mUGLHostBufferName) ||
               checkTypeCanonicalName(type, mUGLHostBufferRangeName) ||
               checkTypeCanonicalName(type, mUGLHostTextureName) ||
               checkTypeCanonicalName(type, mUGLHostTextureViewName) ||
               checkTypeCanonicalName(type, mUGLHostDeviceName) ||
               checkTypeCanonicalName(type, mUGLHostQueueName);
    }

    bool BaseDeclQuery::templateArgumentContainsHostResourceHandle(const clang::TemplateArgument &arg, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const
    {
        if (arg.getKind() == clang::TemplateArgument::Type)
        {
            return typeContainsHostResourceHandle(arg.getAsType(), visitedRecords);
        }
        if (arg.getKind() == clang::TemplateArgument::Pack)
        {
            for (const clang::TemplateArgument &packedArg : arg.pack_elements())
            {
                if (templateArgumentContainsHostResourceHandle(packedArg, visitedRecords))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool BaseDeclQuery::typeContainsHostResourceHandle(const clang::QualType &type, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const
    {
        const clang::QualType unqualifiedType = getUnqualifiedType(type);
        if (isHostResourceHandleType(unqualifiedType))
        {
            return true;
        }

        if (const auto *templateType = unqualifiedType->getAs<clang::TemplateSpecializationType>())
        {
            for (const clang::TemplateArgument &arg : templateType->template_arguments())
            {
                if (templateArgumentContainsHostResourceHandle(arg, visitedRecords))
                {
                    return true;
                }
            }
        }

        const clang::CXXRecordDecl *recordDecl = unqualifiedType->getAsCXXRecordDecl();
        if (recordDecl == nullptr)
        {
            return false;
        }
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordDecl))
        {
            for (const clang::TemplateArgument &arg : specializationDecl->getTemplateArgs().asArray())
            {
                if (templateArgumentContainsHostResourceHandle(arg, visitedRecords))
                {
                    return true;
                }
            }
        }

        return recordContainsHostResourceHandles(recordDecl, visitedRecords);
    }

    bool BaseDeclQuery::typeContainsHostResourceHandle(const clang::QualType &type) const
    {
        std::unordered_set<const clang::CXXRecordDecl *> visitedRecords;
        return typeContainsHostResourceHandle(type, visitedRecords);
    }

    bool BaseDeclQuery::functionSignatureUsesShaderResourceHandles(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }

        if (isAnyShaderResourceHandleType(func->getReturnType()))
        {
            return true;
        }

        for (const auto *param : func->parameters())
        {
            if (param != nullptr && isAnyShaderResourceHandleType(param->getType()))
            {
                return true;
            }
        }

        return false;
    }

    bool BaseDeclQuery::recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl, std::unordered_set<const clang::CXXRecordDecl *> &visitedRecords) const
    {
        if (decl == nullptr)
        {
            return false;
        }

        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            for (const clang::TemplateArgument &arg : specializationDecl->getTemplateArgs().asArray())
            {
                if (templateArgumentContainsHostResourceHandle(arg, visitedRecords))
                {
                    return true;
                }
            }
            decl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
        }

        const clang::CXXRecordDecl *definition = decl->getDefinition();
        if (definition == nullptr)
        {
            definition = decl;
        }

        definition = definition->getCanonicalDecl();
        if (!visitedRecords.emplace(definition).second)
        {
            return false;
        }

        for (const clang::FieldDecl *field : definition->fields())
        {
            if (field != nullptr && typeContainsHostResourceHandle(field->getType(), visitedRecords))
            {
                return true;
            }
        }

        return false;
    }

    bool BaseDeclQuery::recordContainsHostResourceHandles(const clang::CXXRecordDecl *decl) const
    {
        std::unordered_set<const clang::CXXRecordDecl *> visitedRecords;
        return recordContainsHostResourceHandles(decl, visitedRecords);
    }

    bool BaseDeclQuery::isShaderResourceOrBindingType(const clang::QualType &type) const
    {
        return isShaderResourceHandleType(type) ||
               isBindGroupHandleType(type) ||
               isRenderSetHandleType(type) ||
               isHostResourceHandleType(type);
    }

    bool BaseDeclQuery::recordUsesShaderResourceHandles(const clang::CXXRecordDecl *decl) const
    {
        if (decl == nullptr)
        {
            return false;
        }

        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            decl = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
        }

        const clang::CXXRecordDecl *definition = decl->getDefinition();
        if (definition == nullptr)
        {
            definition = decl;
        }

        for (const auto *field : definition->fields())
        {
            if (field != nullptr && isAnyShaderResourceHandleType(field->getType()))
            {
                return true;
            }
        }

        for (const auto *method : definition->methods())
        {
            if (functionSignatureUsesShaderResourceHandles(method))
            {
                return true;
            }
        }

        return false;
    }

    bool BaseDeclQuery::recordContainsShaderResourceOrBindingFields(const clang::CXXRecordDecl *decl) const
    {
        if (decl == nullptr)
        {
            return false;
        }

        std::unordered_set<const clang::CXXRecordDecl *> visitedRecords;
        std::function<bool(const clang::CXXRecordDecl *)> visitRecord = [&](const clang::CXXRecordDecl *recordDecl) -> bool
        {
            if (recordDecl == nullptr)
            {
                return false;
            }

            recordDecl = recordDecl->getCanonicalDecl();
            if (!visitedRecords.emplace(recordDecl).second)
            {
                return false;
            }

            for (const auto *field : recordDecl->fields())
            {
                const clang::QualType fieldType = getUnqualifiedType(field->getType());
                if (isShaderResourceOrBindingType(fieldType))
                {
                    return true;
                }

                if (const auto *arrayType = llvm::dyn_cast<clang::ArrayType>(fieldType.getTypePtrOrNull()))
                {
                    const clang::QualType elementType = getUnqualifiedType(arrayType->getElementType());
                    if (isShaderResourceOrBindingType(elementType))
                    {
                        return true;
                    }
                    if (const auto *nestedArrayRecord = elementType->getAsCXXRecordDecl(); nestedArrayRecord != nullptr)
                    {
                        if (visitRecord(nestedArrayRecord))
                        {
                            return true;
                        }
                    }
                }

                if (const auto *nestedRecord = fieldType->getAsCXXRecordDecl(); nestedRecord != nullptr)
                {
                    if (visitRecord(nestedRecord))
                    {
                        return true;
                    }
                }
            }

            return false;
        };

        return visitRecord(decl);
    }

    bool BaseDeclQuery::checkFunctionName(const clang::FunctionDecl *decl, const std::string &name) const
    {
        return decl->getNameAsString() == name;
    }

    bool BaseDeclQuery::shouldEmitNestedRecordDefinition(const clang::CXXRecordDecl *parentDecl, const clang::CXXRecordDecl *nestedDecl) const
    {
        if (parentDecl == nullptr || nestedDecl == nullptr)
        {
            return false;
        }
        if (!nestedDecl->isThisDeclarationADefinition())
        {
            return false;
        }
        if (nestedDecl->isImplicit())
        {
            return false;
        }
        return nestedDecl->getCanonicalDecl() != parentDecl->getCanonicalDecl();
    }

    std::vector<clang::FieldDecl *> BaseDeclQuery::getAllFieldsFromRecord(const clang::CXXRecordDecl *decl) const
    {
        std::vector<clang::FieldDecl *> fields;
        if (decl == nullptr)
        {
            return fields;
        }

        for (auto *field : decl->fields())
        {
            fields.emplace_back(field);
        }
        return fields;
    }

    clang::FieldDecl *BaseDeclQuery::getFieldFromClassWithAttribute(const clang::CXXRecordDecl *decl, const std::string &name) const
    {
        auto allFields = getAllFieldsFromRecord(decl);
        for (auto *field : allFields)
        {
            if (mVisitor.checkAttibuteByName(field, name))
            {
                return field;
            }
        }
        return nullptr;
    }

    const clang::ParmVarDecl *BaseDeclQuery::getParamFromFunctionWithAttribute(const clang::FunctionDecl *decl, const std::string &name) const
    {
        if (decl == nullptr)
        {
            return nullptr;
        }

        for (unsigned i = 0; i < decl->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = decl->getParamDecl(i);
            if (mVisitor.checkAttibuteByName(param, name))
            {
                return param;
            }
        }
        return nullptr;
    }

    const clang::ParmVarDecl *BaseDeclQuery::getParamFromFunctionByName(const clang::FunctionDecl *decl, const std::string &name) const
    {
        if (decl == nullptr)
        {
            return nullptr;
        }

        for (unsigned i = 0; i < decl->getNumParams(); ++i)
        {
            const clang::ParmVarDecl *param = decl->getParamDecl(i);
            if (param != nullptr && param->getNameAsString() == name)
            {
                return param;
            }
        }
        return nullptr;
    }

    std::optional<clang::QualType> BaseDeclQuery::resolveRecordNestedTrueType(const clang::QualType &type) const
    {
        const auto *recordDecl = getUnqualifiedType(type)->getAsCXXRecordDecl();
        if (recordDecl == nullptr)
        {
            return std::nullopt;
        }

        for (const auto *innerDecl : recordDecl->decls())
        {
            if (const auto *typedefDecl = llvm::dyn_cast<clang::TypedefNameDecl>(innerDecl))
            {
                if (typedefDecl->getNameAsString() == "TrueType")
                {
                    return getUnqualifiedType(typedefDecl->getUnderlyingType());
                }
            }
        }

        return std::nullopt;
    }

    clang::QualType BaseDeclQuery::getUnqualifiedType(const clang::QualType &type) const
    {
        clang::QualType realType = type;
        if (type->isPointerType())
        {
            realType = type->getPointeeType();
        }

        if (type->isReferenceType())
        {
            realType = type.getNonReferenceType();
        }

        while (realType->isArrayType())
        {
            const clang::ArrayType *arrayType = mVisitor.Context->getAsArrayType(realType);
            realType = arrayType->getElementType();
        }

        realType = realType.getUnqualifiedType();
        if (const auto resolvedType = mVisitor.tryResolveTemplateSubstitutionType(realType); resolvedType.has_value())
        {
            return getUnqualifiedType(*resolvedType);
        }

        return realType;
    }

    std::string BaseDeclQuery::getFullNamespace(const clang::Decl *decl) const
    {
        if (decl == nullptr)
        {
            return "";
        }

        std::vector<std::string> namespaceParts;
        const clang::DeclContext *declContext = decl->getDeclContext();

        while (declContext != nullptr && !clang::isa<clang::TranslationUnitDecl>(declContext))
        {
            if (const auto *namespaceDecl = clang::dyn_cast<clang::NamespaceDecl>(declContext))
            {
                if (!namespaceDecl->isAnonymousNamespace())
                {
                    namespaceParts.push_back(namespaceDecl->getNameAsString());
                }
            }

            declContext = declContext->getParent();
        }

        std::reverse(namespaceParts.begin(), namespaceParts.end());

        std::string result;
        for (size_t index = 0; index < namespaceParts.size(); ++index)
        {
            result += namespaceParts[index];
            if (index < namespaceParts.size() - 1)
            {
                result += "::";
            }
        }
        return result;
    }
} // namespace UGLC::CodeGen
