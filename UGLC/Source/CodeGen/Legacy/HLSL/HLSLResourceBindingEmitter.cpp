#include "HLSLResourceBindingEmitter.hpp"

#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>
#include <CodeGen/Legacy/HLSL/HLSLTypeConvertor.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <llvm/Support/Casting.h>

#include <stdexcept>
#include <unordered_set>

namespace UGLC::CodeGen::HLSL
{
    HLSLResourceBindingEmitter::HLSLResourceBindingEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor)
        : mVisitor(visitor)
        , mTypeConvertor(typeConvertor)
    {
    }

    std::string HLSLResourceBindingEmitter::getResourceGlobalName(const std::string &bindGroupName, const std::string &resourceName) const
    {
        return bindGroupName + "_" + resourceName;
    }

    std::string HLSLResourceBindingEmitter::getBindGroupHandleTypeName(const clang::CXXRecordDecl *bindGroupDecl) const
    {
        const std::string bindGroupTypeName = bindGroupDecl == nullptr ? std::string("UGLC_BindGroup") : flattenQualifiedHLSLIdentifierForHelperName(bindGroupDecl->getQualifiedNameAsString());
        return bindGroupTypeName + "_UGLBindGroupHandle";
    }

    std::string HLSLResourceBindingEmitter::getBindGroupHandleTypeNameFromType(const clang::QualType &type) const
    {
        return getBindGroupHandleTypeName(tryGetBindGroupTypeDeclFromType(type));
    }

    const clang::CXXRecordDecl *HLSLResourceBindingEmitter::tryGetBindGroupTypeDeclFromType(const clang::QualType &type) const
    {
        const clang::QualType resolvedType = mVisitor.getUnqualifiedType(type);
        if (!mVisitor.checkTypeCanonicalName(resolvedType, mUGLBindGroupName))
        {
            return nullptr;
        }

        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(resolvedType);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
        {
            throw std::runtime_error("HLSL helper parameter of type \"" + mVisitor.generateTypeCanonicalName(type, mTypeConvertor) + "\" requires a concrete UGL::BindGroup<T> record type argument.");
        }

        const clang::QualType bindGroupType = mVisitor.getUnqualifiedType(templateArgs.front().getAsType());
        const auto *bindGroupDecl = bindGroupType->getAsCXXRecordDecl();
        if (bindGroupDecl == nullptr)
        {
            throw std::runtime_error("HLSL helper parameter of type \"" + mVisitor.generateTypeCanonicalName(type, mTypeConvertor) + "\" resolves non-record bind-group type \"" + mVisitor.generateTypeCanonicalName(bindGroupType, mTypeConvertor) + "\".");
        }

        return bindGroupDecl->getCanonicalDecl();
    }

    bool HLSLResourceBindingEmitter::isBindGroupParameterType(const clang::QualType &type) const
    {
        return tryGetBindGroupTypeDeclFromType(type) != nullptr;
    }

    std::vector<BaseShaderResourceBinding> HLSLResourceBindingEmitter::resolveParameterBindings(const clang::QualType &type)
    {
        const auto *bindGroupDecl = tryGetBindGroupTypeDeclFromType(type);
        if (bindGroupDecl == nullptr)
        {
            return {};
        }
        return mVisitor.resolveBaseShaderResourceBindings(bindGroupDecl);
    }

    std::string HLSLResourceBindingEmitter::generateHandleParameter(const clang::ParmVarDecl *param)
    {
        if (param == nullptr)
        {
            return {};
        }
        return getBindGroupHandleTypeNameFromType(param->getType()) + " " + sanitizeHLSLIdentifier(param->getNameAsString());
    }

    std::string HLSLResourceBindingEmitter::generateHandleFieldDecl(const clang::FieldDecl *fieldDecl)
    {
        if (fieldDecl == nullptr)
        {
            return {};
        }
        return mVisitor.getLineDirective(fieldDecl->getBeginLoc())
               + mVisitor.mSpaceManager.getSpace()
               + getBindGroupHandleTypeNameFromType(fieldDecl->getType()) + " "
               + sanitizeHLSLIdentifier(fieldDecl->getNameAsString())
               + mVisitor.generateDeclArraySpecifier(fieldDecl->getType())
               + mVisitor.EOS();
    }

    std::string HLSLResourceBindingEmitter::generateHandleStructDefinitions(const BindGroupInfoMap &bindGroupInfoMap,
                                                                            const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls)
    {
        std::string result;
        for (const BindGroupRecordBindings &recordBindings : collectUniqueBindGroupRecordBindings(bindGroupInfoMap, extraBindGroupDecls))
        {
            result += generateHandleStructDefinition(recordBindings.bindGroupDecl, recordBindings.resourceBindings);
        }

        if (!result.empty())
        {
            result += mVisitor.NewLine();
        }
        return result;
    }

    std::string HLSLResourceBindingEmitter::generateHandleMaterialization(const ShaderBindGroupInfo &bindGroupInfo)
    {
        if (bindGroupInfo.isRenderSet || bindGroupInfo.typeDecl == nullptr)
        {
            return {};
        }

        const std::string handleName = sanitizeHLSLIdentifier(bindGroupInfo.name);
        std::string result;
        result += mVisitor.mSpaceManager.getSpace() + getBindGroupHandleTypeName(bindGroupInfo.typeDecl) + " " + handleName + mVisitor.EOS();
        for (const auto &resourceBinding : bindGroupInfo.resourceBindings)
        {
            if (resourceBinding.fieldDecl == nullptr)
            {
                throw std::runtime_error("HLSL bind-group handle materialization encountered a resource binding without a field declaration.");
            }
            const std::string fieldName = sanitizeHLSLIdentifier(resourceBinding.fieldDecl->getNameAsString());
            const std::string resourceGlobalName = getResourceGlobalName(bindGroupInfo.name, resourceBinding.fieldDecl->getNameAsString());
            result += mVisitor.mSpaceManager.getSpace() + handleName + "." + fieldName + " = " + resourceGlobalName + mVisitor.EOS();
        }
        return result;
    }

    std::string HLSLResourceBindingEmitter::generateHandleStructDefinition(const clang::CXXRecordDecl *bindGroupDecl, const std::vector<BaseShaderResourceBinding> &resourceBindings)
    {
        std::string result;
        result += mVisitor.mSpaceManager.getSpace() + "struct " + getBindGroupHandleTypeName(bindGroupDecl) + mVisitor.NewLine();
        result += mVisitor.enterScope();
        for (const auto &resourceBinding : resourceBindings)
        {
            result += generateHandleResourceFieldDecl(bindGroupDecl, resourceBinding);
        }
        result += mVisitor.endClass();
        result += mVisitor.NewLine();
        return result;
    }

    std::string HLSLResourceBindingEmitter::generateHandleResourceFieldDecl(const clang::CXXRecordDecl *bindGroupDecl, const BaseShaderResourceBinding &resourceBinding)
    {
        if (resourceBinding.fieldDecl == nullptr)
        {
            throw std::runtime_error("HLSL bind-group handle struct generation encountered a resource binding without a field declaration.");
        }

        std::string fieldTypeName;
        if (resourceBinding.kind == BaseShaderResourceKind::UniformBuffer)
        {
            fieldTypeName = "ConstantBuffer<" + getUniformWrapperTypeName(bindGroupDecl, resourceBinding.fieldDecl) + ">";
        }
        else
        {
            fieldTypeName = mVisitor.generateTypeCanonicalName(resourceBinding.fieldDecl->getType(), mTypeConvertor);
        }

        return mVisitor.mSpaceManager.getSpace()
               + fieldTypeName + " "
               + sanitizeHLSLIdentifier(resourceBinding.fieldDecl->getNameAsString())
               + mVisitor.EOS();
    }

    std::string HLSLResourceBindingEmitter::generateUniformWrapperDefinitions(const BindGroupInfoMap &bindGroupInfoMap)
    {
        return generateUniformWrapperDefinitions(bindGroupInfoMap, {});
    }

    std::string HLSLResourceBindingEmitter::generateUniformWrapperDefinitions(const BindGroupInfoMap &bindGroupInfoMap,
                                                                              const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls)
    {
        std::string result;
        std::unordered_set<std::string> emittedWrapperTypes;

        for (const BindGroupRecordBindings &recordBindings : collectUniqueBindGroupRecordBindings(bindGroupInfoMap, extraBindGroupDecls))
        {
            for (const auto &resourceBinding : recordBindings.resourceBindings)
            {
                if (resourceBinding.kind != BaseShaderResourceKind::UniformBuffer)
                {
                    continue;
                }

                const std::string wrapperTypeName = getUniformWrapperTypeName(recordBindings.bindGroupDecl, resourceBinding.fieldDecl);
                if (!emittedWrapperTypes.insert(wrapperTypeName).second)
                {
                    continue;
                }

                result += generateUniformWrapperTypeDefinition(recordBindings.bindGroupDecl, resourceBinding);
            }
        }

        if (!result.empty())
        {
            result += mVisitor.NewLine();
        }
        return result;
    }

    std::vector<HLSLResourceBindingEmitter::BindGroupRecordBindings> HLSLResourceBindingEmitter::collectUniqueBindGroupRecordBindings(const BindGroupInfoMap &bindGroupInfoMap,
                                                                                                                                      const std::vector<const clang::CXXRecordDecl *> &extraBindGroupDecls)
    {
        std::vector<BindGroupRecordBindings> result;
        std::unordered_set<std::string> emittedHandleTypes;

        for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
        {
            (void)slotIndex;
            if (bindGroupInfo.isRenderSet || bindGroupInfo.typeDecl == nullptr)
            {
                continue;
            }
            const std::string handleTypeName = getBindGroupHandleTypeName(bindGroupInfo.typeDecl);
            if (emittedHandleTypes.insert(handleTypeName).second)
            {
                result.push_back({bindGroupInfo.typeDecl, bindGroupInfo.resourceBindings});
            }
        }

        for (const clang::CXXRecordDecl *bindGroupDecl : extraBindGroupDecls)
        {
            if (bindGroupDecl == nullptr)
            {
                continue;
            }
            const std::string handleTypeName = getBindGroupHandleTypeName(bindGroupDecl);
            if (emittedHandleTypes.insert(handleTypeName).second)
            {
                result.push_back({bindGroupDecl, mVisitor.resolveBaseShaderResourceBindings(bindGroupDecl)});
            }
        }

        return result;
    }

    std::string HLSLResourceBindingEmitter::generateResourceDeclarations(const ShaderBindGroupInfo &bindGroupInfo)
    {
        std::string result;
        if (bindGroupInfo.isRenderSet)
        {
            return result;
        }

        for (const auto &resourceBinding : bindGroupInfo.resourceBindings)
        {
            const std::string typeName = mVisitor.generateTypeCanonicalName(resourceBinding.fieldDecl->getType(), mTypeConvertor);
            const std::string resourceGlobalName = getResourceGlobalName(bindGroupInfo.name, resourceBinding.fieldDecl->getNameAsString());
            std::string registerClass;
            switch (resourceBinding.kind)
            {
            case BaseShaderResourceKind::UniformBuffer:
                registerClass = "b";
                break;
            case BaseShaderResourceKind::SampledTexture:
                registerClass = "t";
                break;
            case BaseShaderResourceKind::Sampler:
                registerClass = "s";
                break;
            case BaseShaderResourceKind::StorageBuffer:
                registerClass = resourceBinding.access == BaseShaderResourceAccess::ReadOnly ? "t" : "u";
                break;
            case BaseShaderResourceKind::StorageTexture:
                registerClass = "u";
                break;
            }

            if (resourceBinding.kind == BaseShaderResourceKind::UniformBuffer)
            {
                const std::string wrapperTypeName = getUniformWrapperTypeName(bindGroupInfo.typeDecl, resourceBinding.fieldDecl);
                result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(resourceBinding.bindingIndex, bindGroupInfo.bindingIndex) + "ConstantBuffer<" + wrapperTypeName + "> " + resourceGlobalName + " : register(" + registerClass + std::to_string(resourceBinding.bindingIndex) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
                continue;
            }

            std::string resourceAttributePrefix = makeVulkanBindingAttribute(resourceBinding.bindingIndex, bindGroupInfo.bindingIndex);
            if (resourceBinding.kind == BaseShaderResourceKind::StorageTexture)
            {
                const std::string formatName = resourceBinding.elementTypeName.empty() ? mVisitor.generateTypeCanonicalName(resourceBinding.elementType) : resourceBinding.elementTypeName;
                resourceAttributePrefix += "[[vk::image_format(\"" + MakeVulkanImageFormatForStorageTexture(formatName) + "\")]] ";
            }

            result += mVisitor.mSpaceManager.getSpace() + resourceAttributePrefix + typeName + " " + resourceGlobalName + " : register(" + registerClass + std::to_string(resourceBinding.bindingIndex) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
        }
        return result;
    }

    std::string HLSLResourceBindingEmitter::getUniformWrapperTypeName(const clang::CXXRecordDecl *bindGroupDecl, const clang::FieldDecl *fieldDecl) const
    {
        const std::string bindGroupTypeName = bindGroupDecl == nullptr ? std::string("UGLC_BindGroup") : flattenQualifiedHLSLIdentifierForHelperName(bindGroupDecl->getQualifiedNameAsString());
        const std::string fieldName = fieldDecl == nullptr ? std::string("resource") : sanitizeHLSLIdentifier(fieldDecl->getNameAsString());
        return bindGroupTypeName + "_" + fieldName + "_UniformValue";
    }

    std::string HLSLResourceBindingEmitter::generateUniformWrapperTypeDefinition(const clang::CXXRecordDecl *bindGroupDecl, const BaseShaderResourceBinding &resourceBinding)
    {
        const std::string wrapperTypeName = getUniformWrapperTypeName(bindGroupDecl, resourceBinding.fieldDecl);
        const std::string elementTypeName = resourceBinding.elementType.isNull() ? mVisitor.generateTypeCanonicalName(resourceBinding.fieldDecl->getType(), mTypeConvertor) : mVisitor.generateTypeCanonicalName(resourceBinding.elementType, mTypeConvertor);

        std::string result;
        result += mVisitor.mSpaceManager.getSpace() + "struct " + wrapperTypeName + mVisitor.NewLine();
        result += mVisitor.mSpaceManager.getSpace() + "{" + mVisitor.NewLine();
        mVisitor.mSpaceManager.enter();
        result += mVisitor.mSpaceManager.getSpace() + elementTypeName + " value" + mVisitor.EOS();
        mVisitor.mSpaceManager.quit();
        result += mVisitor.mSpaceManager.getSpace() + "};" + mVisitor.NewLine();
        return result;
    }

    std::string HLSLResourceBindingEmitter::makeVulkanBindingAttribute(const int bindingIndex, const int bindGroupIndex)
    {
        return "[[vk::binding(" + std::to_string(bindingIndex) + ", " + std::to_string(bindGroupIndex) + ")]] ";
    }
} // namespace UGLC::CodeGen::HLSL
