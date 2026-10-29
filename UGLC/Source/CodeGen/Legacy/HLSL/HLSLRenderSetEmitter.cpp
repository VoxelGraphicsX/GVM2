#include "HLSLRenderSetEmitter.hpp"

#include <CodeGen/Legacy/HLSL/HLSLTypeConvertor.hpp>
#include <CodeGen/RenderSetMemberCallUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <stdexcept>

namespace UGLC::CodeGen::HLSL
{
    HLSLRenderSetEmitter::HLSLRenderSetEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor)
        : mVisitor(visitor), mTypeConvertor(typeConvertor)
    {
    }

    std::string HLSLRenderSetEmitter::getResourceGlobalName(const std::string &renderSetName, const std::string &resourceName) const
    {
        return renderSetName + "_" + resourceName;
    }

    std::string HLSLRenderSetEmitter::generateResourceDeclarations(const ShaderBindGroupInfo &bindGroupInfo)
    {
        if (bindGroupInfo.typeDecl == nullptr)
        {
            return {};
        }

        std::string result;
        const RenderSetLayoutInfo layoutInfo = buildRenderSetLayoutInfo(bindGroupInfo.typeDecl, mVisitor);
        int baseBindingIndex = 0;
        int registerIndex = 0;
        std::string renderSetHelperDefinitions;
        const std::string accessBoundDataGlobalName = getAccessBoundDataGlobalName(bindGroupInfo.name);
        const std::string entityInfoGlobalName = getEntityInfoGlobalName(bindGroupInfo.name);
        const std::string cmdParamsGlobalName = getCMDParamsGlobalName(bindGroupInfo.name);
        const std::string loadEntityInfoHelperName = getLoadEntityInfoHelperName(bindGroupInfo.name);
        const std::string loadCMDParamsHelperName = getLoadCMDParamsHelperName(bindGroupInfo.name);
        const std::string checkRenderEntityValidHelperName = getCheckValidHelperName(bindGroupInfo.name);

        result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "StructuredBuffer<uint2> " + accessBoundDataGlobalName + " : register(t" + std::to_string(registerIndex++) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();

        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "UGL_RenderEntityInfo_ " + loadEntityInfoHelperName + "(uint entity)" + mVisitor.NewLine();
        renderSetHelperDefinitions += mVisitor.enterScope();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_header = " + accessBoundDataGlobalName + "[0u]" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_entity_count_nz = max(__uglc_header.x, 1u)" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(entity, __uglc_entity_count_nz - 1u)" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "return " + entityInfoGlobalName + "[__uglc_safe_entity]" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.quitScope();

        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "uint2 " + loadCMDParamsHelperName + "(uint cmdIndex)" + mVisitor.NewLine();
        renderSetHelperDefinitions += mVisitor.enterScope();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_header = " + accessBoundDataGlobalName + "[0u]" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_cmd_count_nz = max(__uglc_header.y, 1u)" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_cmd_index = min(cmdIndex, __uglc_cmd_count_nz - 1u)" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "return " + cmdParamsGlobalName + "[__uglc_safe_cmd_index]" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.quitScope();

        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "bool " + checkRenderEntityValidHelperName + "(uint entity)" + mVisitor.NewLine();
        renderSetHelperDefinitions += mVisitor.enterScope();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_header = " + accessBoundDataGlobalName + "[0u]" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const bool __uglc_entity_in_range = entity < __uglc_header.x" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "const UGL_RenderEntityInfo_ __uglc_info = " + loadEntityInfoHelperName + "(entity)" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.mSpaceManager.getSpace() + "return __uglc_entity_in_range && __uglc_info.instanceCount > 0u && __uglc_info.indexCount > 0u && __uglc_info.instanceCount != 0xffffffffu && __uglc_info.indexCount != 0xffffffffu" + mVisitor.EOS();
        renderSetHelperDefinitions += mVisitor.quitScope();

        for (size_t fieldIndex = 0; fieldIndex < layoutInfo.fields.size(); ++fieldIndex)
        {
            const auto &fieldInfo = layoutInfo.fields[fieldIndex];
            const auto templateArgs = mVisitor.getTemplateArgumentsFromType(fieldInfo.componentType);
            const std::string fieldTypeName = mVisitor.getClassCanonicalName(mVisitor.getUnqualifiedType(fieldInfo.componentType)->getAsCXXRecordDecl(), nullptr);
            const std::string resourceGlobalName = getResourceGlobalName(bindGroupInfo.name, fieldInfo.fieldName);
            const std::string componentListGlobalName = getComponentListGlobalName(bindGroupInfo.name, fieldInfo.fieldName);
            const std::string accessBoundEntryIndex = std::to_string(fieldIndex + 1u) + "u";
            result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "StructuredBuffer<uint> " + componentListGlobalName + " : register(t" + std::to_string(registerIndex++) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();

            if (fieldTypeName == mUGLRenderSetBufferComponentClassName)
            {
                if (templateArgs.empty() || templateArgs.front().getAsType().isNull())
                {
                    throw std::runtime_error("RenderSet \"" + bindGroupInfo.name + "\" field \"" + fieldInfo.fieldName + "\" requires BufferComponent<ElementType> for HLSL code generation.");
                }

                const std::string elementTypeName = generateTypeName(templateArgs.front().getAsType());
                result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "StructuredBuffer<" + elementTypeName + "> " + resourceGlobalName + " : register(t" + std::to_string(registerIndex++) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
                appendBufferHelperDefinitions(renderSetHelperDefinitions,
                                              accessBoundDataGlobalName,
                                              accessBoundEntryIndex,
                                              componentListGlobalName,
                                              resourceGlobalName,
                                              elementTypeName);
            }
            else if (fieldTypeName == mUGLRenderSetTextureComponentClassName)
            {
                if (templateArgs.size() < 2 || templateArgs.front().getAsType().isNull())
                {
                    throw std::runtime_error("RenderSet \"" + bindGroupInfo.name + "\" field \"" + fieldInfo.fieldName + "\" requires TextureComponent<ElementType, MaxResourceCount> for HLSL code generation.");
                }

                const int maxResourceCount = static_cast<int>(mVisitor.getIntValueFromTemplateArgument(templateArgs.at(1)));
                if (maxResourceCount <= 0)
                {
                    throw std::runtime_error("RenderSet \"" + bindGroupInfo.name + "\" field \"" + fieldInfo.fieldName + "\" resolves an invalid MaxResourceCount = " + std::to_string(maxResourceCount) + " for HLSL code generation. MaxResourceCount must be greater than 0.");
                }
                const std::string textureSampleTypeName = MakeSampledTextureType(generateTypeName(templateArgs.front().getAsType()));
                result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "Texture2D<" + textureSampleTypeName + "> " + resourceGlobalName + "[]" + " : register(t" + std::to_string(registerIndex) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
                appendTextureHelperDefinitions(renderSetHelperDefinitions,
                                               accessBoundDataGlobalName,
                                               accessBoundEntryIndex,
                                               componentListGlobalName,
                                               resourceGlobalName,
                                               textureSampleTypeName);
                ++registerIndex;
            }
            else
            {
                throw std::runtime_error("RenderSet \"" + bindGroupInfo.name + "\" field \"" + fieldInfo.fieldName + "\" uses unsupported component type \"" + generateTypeName(fieldInfo.componentType) + "\" for HLSL code generation.");
            }
        }

        result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "StructuredBuffer<UGL_RenderEntityInfo_> " + entityInfoGlobalName + " : register(t" + std::to_string(registerIndex++) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
        result += mVisitor.mSpaceManager.getSpace() + makeVulkanBindingAttribute(baseBindingIndex++, bindGroupInfo.bindingIndex) + "StructuredBuffer<uint2> " + cmdParamsGlobalName + " : register(t" + std::to_string(registerIndex++) + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ")" + mVisitor.EOS();
        result += renderSetHelperDefinitions;
        return result;
    }

    std::optional<std::string> HLSLRenderSetEmitter::tryTranslateBufferComponentCall(const clang::CXXMemberCallExpr *expr, const std::string &componentExpr, const std::string &methodName)
    {
        const std::string arg0Expr = expr->getNumArgs() > 0 ? mVisitor.TranslateExpr(expr->getArg(0)) : std::string();
        const std::string arg1Expr = expr->getNumArgs() > 1 ? mVisitor.TranslateExpr(expr->getArg(1)) : std::string();

        switch (classifyRenderSetBufferComponentCall(methodName))
        {
        case RenderSetBufferComponentCallKind::GetRaw:
            return getBufferGetRawHelperName(componentExpr) + "(" + arg0Expr + ")";
        case RenderSetBufferComponentCallKind::CheckValid:
            return getBufferCheckValidHelperName(componentExpr) + "(" + arg0Expr + ")";
        case RenderSetBufferComponentCallKind::Get:
            return getBufferGetHelperName(componentExpr) + "(" + arg0Expr + ", " + arg1Expr + ")";
        case RenderSetBufferComponentCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLRenderSetEmitter::tryTranslateTextureComponentCall(const clang::CXXMemberCallExpr *expr, const std::string &componentExpr, const std::string &methodName)
    {
        switch (classifyRenderSetTextureComponentCall(methodName))
        {
        case RenderSetTextureComponentCallKind::Get:
            return getTextureGetHelperName(componentExpr) + "(" + translateArg(expr, 0) + ", " + translateArg(expr, 1) + ")";
        case RenderSetTextureComponentCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLRenderSetEmitter::tryTranslateDataPackCall(const clang::CXXMemberCallExpr *expr, const std::string &renderSetExpr, const std::string &methodName)
    {
        const std::string loadEntityInfoHelperName = getLoadEntityInfoHelperName(renderSetExpr);
        const std::string loadCMDParamsHelperName = getLoadCMDParamsHelperName(renderSetExpr);
        const std::string checkValidHelperName = getCheckValidHelperName(renderSetExpr);
        const auto entityFieldAccess = [&](std::string_view fieldName) {
            return loadEntityInfoHelperName + "(" + translateArg(expr, 0) + ")." + std::string(fieldName);
        };

        switch (classifyRenderSetDataPackCall(methodName))
        {
        case RenderSetDataPackCallKind::GetRenderEntityInfo: {
            return "{ const UGL_RenderEntityInfo_ __uglc_render_entity_info = " + loadEntityInfoHelperName + "(" + translateArg(expr, 0) + "); " + translateArg(expr, 1) + " = __uglc_render_entity_info.indexCount; " + translateArg(expr, 2) + " = __uglc_render_entity_info.instanceCount; " + translateArg(expr, 3) + " = __uglc_render_entity_info.firstIndex; " + translateArg(expr, 4) + " = __uglc_render_entity_info.vertexOffset; " + translateArg(expr, 5) + " = __uglc_render_entity_info.globalInstanceBase; }";
        }
        case RenderSetDataPackCallKind::GetRenderEntityCMDParams: {
            return "{ const uint2 __uglc_render_entity_cmd = " + loadCMDParamsHelperName + "(" + translateArg(expr, 0) + "); " + translateArg(expr, 1) + " = __uglc_render_entity_cmd.x; " + translateArg(expr, 2) + " = __uglc_render_entity_cmd.y; }";
        }
        case RenderSetDataPackCallKind::GetRenderEntityIndexCount:
            return entityFieldAccess("indexCount");
        case RenderSetDataPackCallKind::GetRenderEntityInstanceCount:
            return entityFieldAccess("instanceCount");
        case RenderSetDataPackCallKind::GetRenderEntityFirstIndex:
            return entityFieldAccess("firstIndex");
        case RenderSetDataPackCallKind::GetRenderEntityVertexOffset:
            return entityFieldAccess("vertexOffset");
        case RenderSetDataPackCallKind::GetRenderEntityGlobalInstanceBase:
            return entityFieldAccess("globalInstanceBase");
        case RenderSetDataPackCallKind::GetRenderEntityVersion:
            return entityFieldAccess("entityVersion");
        case RenderSetDataPackCallKind::CheckValid: {
            return checkValidHelperName + "(" + translateArg(expr, 0) + ")";
        }
        case RenderSetDataPackCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::string HLSLRenderSetEmitter::getAccessBoundDataGlobalName(const std::string &renderSetName) const
    {
        return renderSetName + "_RenderSetAccessBoundData";
    }

    std::string HLSLRenderSetEmitter::getComponentListGlobalName(const std::string &renderSetName, const std::string &resourceName) const
    {
        return getResourceGlobalName(renderSetName, resourceName) + "ComponentList";
    }

    std::string HLSLRenderSetEmitter::getEntityInfoGlobalName(const std::string &renderSetName) const
    {
        return renderSetName + "_RenderEntityInfo";
    }

    std::string HLSLRenderSetEmitter::getCMDParamsGlobalName(const std::string &renderSetName) const
    {
        return renderSetName + "_RenderEntityCMDParams";
    }

    std::string HLSLRenderSetEmitter::getLoadEntityInfoHelperName(const std::string &renderSetName) const
    {
        return renderSetName + "_UGLLoadRenderEntityInfoSafe";
    }

    std::string HLSLRenderSetEmitter::getLoadCMDParamsHelperName(const std::string &renderSetName) const
    {
        return renderSetName + "_UGLLoadRenderEntityCMDParamsSafe";
    }

    std::string HLSLRenderSetEmitter::getCheckValidHelperName(const std::string &renderSetName) const
    {
        return renderSetName + "_UGLCheckRenderEntityValidSafe";
    }

    std::string HLSLRenderSetEmitter::getBufferGetRawHelperName(const std::string &resourceGlobalName) const
    {
        return resourceGlobalName + "_UGLGetRawSafe";
    }

    std::string HLSLRenderSetEmitter::getBufferCheckValidHelperName(const std::string &resourceGlobalName) const
    {
        return resourceGlobalName + "_UGLCheckValid";
    }

    std::string HLSLRenderSetEmitter::getBufferGetHelperName(const std::string &resourceGlobalName) const
    {
        return resourceGlobalName + "_UGLGetSafe";
    }

    std::string HLSLRenderSetEmitter::getTextureResolveIndexHelperName(const std::string &resourceGlobalName) const
    {
        return resourceGlobalName + "_UGLResolveTextureIndexSafe";
    }

    std::string HLSLRenderSetEmitter::getTextureGetHelperName(const std::string &resourceGlobalName) const
    {
        return resourceGlobalName + "_UGLGetSafe";
    }

    void HLSLRenderSetEmitter::appendBufferHelperDefinitions(std::string &helperDefinitions,
                                                             const std::string &accessBoundDataGlobalName,
                                                             const std::string &accessBoundEntryIndex,
                                                             const std::string &componentListGlobalName,
                                                             const std::string &resourceGlobalName,
                                                             const std::string &elementTypeName)
    {
        const std::string getRawHelperName = getBufferGetRawHelperName(resourceGlobalName);
        const std::string checkValidHelperName = getBufferCheckValidHelperName(resourceGlobalName);
        const std::string getHelperName = getBufferGetHelperName(resourceGlobalName);

        helperDefinitions += mVisitor.mSpaceManager.getSpace() + elementTypeName + " " + getRawHelperName + "(uint index)" + mVisitor.NewLine();
        helperDefinitions += mVisitor.enterScope();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundDataGlobalName + "[" + accessBoundEntryIndex + "]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_element_count_nz = max(__uglc_bounds.y, 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_index = min(index, __uglc_element_count_nz - 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "return " + resourceGlobalName + "[__uglc_safe_index]" + mVisitor.EOS();
        helperDefinitions += mVisitor.quitScope();

        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "bool " + checkValidHelperName + "(uint entity)" + mVisitor.NewLine();
        helperDefinitions += mVisitor.enterScope();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundDataGlobalName + "[" + accessBoundEntryIndex + "]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_component_count_nz = max(__uglc_bounds.x, 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(entity, __uglc_component_count_nz - 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_raw_base = " + componentListGlobalName + "[__uglc_safe_entity]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const bool __uglc_entity_in_range = entity < __uglc_bounds.x" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const bool __uglc_base_valid = __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "return __uglc_entity_in_range && __uglc_base_valid" + mVisitor.EOS();
        helperDefinitions += mVisitor.quitScope();

        helperDefinitions += mVisitor.mSpaceManager.getSpace() + elementTypeName + " " + getHelperName + "(uint entity, uint subIndex)" + mVisitor.NewLine();
        helperDefinitions += mVisitor.enterScope();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundDataGlobalName + "[" + accessBoundEntryIndex + "]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_component_count_nz = max(__uglc_bounds.x, 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_element_count_nz = max(__uglc_bounds.y, 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_entity = min(entity, __uglc_component_count_nz - 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_raw_base = " + componentListGlobalName + "[__uglc_safe_entity]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const bool __uglc_base_valid = entity < __uglc_bounds.x && __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_base = __uglc_base_valid ? __uglc_raw_base : 0u" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_base_clamped = min(__uglc_safe_base, __uglc_element_count_nz - 1u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_physical_remaining = (__uglc_element_count_nz - 1u) - __uglc_safe_base_clamped" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_offset = min(subIndex, __uglc_physical_remaining)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "return " + resourceGlobalName + "[__uglc_safe_base_clamped + __uglc_safe_offset]" + mVisitor.EOS();
        helperDefinitions += mVisitor.quitScope();
    }

    void HLSLRenderSetEmitter::appendTextureHelperDefinitions(std::string &helperDefinitions,
                                                              const std::string &accessBoundDataGlobalName,
                                                              const std::string &accessBoundEntryIndex,
                                                              const std::string &componentListGlobalName,
                                                              const std::string &resourceGlobalName,
                                                              const std::string &textureSampleTypeName)
    {
        const std::string resolveIndexHelperName = getTextureResolveIndexHelperName(resourceGlobalName);
        const std::string getTextureHelperName = getTextureGetHelperName(resourceGlobalName);

        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "uint " + resolveIndexHelperName + "(uint entity, uint slot)" + mVisitor.NewLine();
        helperDefinitions += mVisitor.enterScope();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint2 __uglc_bounds = " + accessBoundDataGlobalName + "[" + accessBoundEntryIndex + "]" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_safe_slot = min(slot, " + std::to_string(RenderTextureMaxTextureCountPerEntity - 1u) + "u)" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_raw_component_index = (entity * " + std::to_string(RenderTextureMaxTextureCountPerEntity) + "u) + __uglc_safe_slot" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "const uint __uglc_raw_descriptor = (__uglc_raw_component_index < __uglc_bounds.x) ? " + componentListGlobalName + "[__uglc_raw_component_index] : 0u" + mVisitor.EOS();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "return (__uglc_raw_descriptor != 4294967295u && __uglc_raw_descriptor < __uglc_bounds.y) ? __uglc_raw_descriptor : 0u" + mVisitor.EOS();
        helperDefinitions += mVisitor.quitScope();

        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "Texture2D<" + textureSampleTypeName + "> " + getTextureHelperName + "(uint entity, uint slot)" + mVisitor.NewLine();
        helperDefinitions += mVisitor.enterScope();
        helperDefinitions += mVisitor.mSpaceManager.getSpace() + "return " + resourceGlobalName + "[NonUniformResourceIndex(" + resolveIndexHelperName + "(entity, slot))]" + mVisitor.EOS();
        helperDefinitions += mVisitor.quitScope();
    }

    std::string HLSLRenderSetEmitter::generateTypeName(clang::QualType type)
    {
        return mVisitor.generateTypeCanonicalName(type, mTypeConvertor);
    }

    std::string HLSLRenderSetEmitter::translateArg(const clang::CXXMemberCallExpr *expr, unsigned argIndex)
    {
        return mVisitor.TranslateExpr(expr->getArg(argIndex));
    }

    std::string HLSLRenderSetEmitter::makeVulkanBindingAttribute(int bindingIndex, int bindGroupIndex)
    {
        return "[[vk::binding(" + std::to_string(bindingIndex) + ", " + std::to_string(bindGroupIndex) + ")]] ";
    }
} // namespace UGLC::CodeGen::HLSL
