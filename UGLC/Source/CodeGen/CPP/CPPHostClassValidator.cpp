#include "CPPHostClassValidator.hpp"

#include <CodeGen/IntegerConstantExpressionUtils.hpp>
#include <CodeGen/ShaderBackendRegistry.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <algorithm>
#include <clang/AST/DeclTemplate.h>

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        std::string trimCopy(const std::string &input)
        {
            size_t first = input.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return "";
            }

            size_t last = input.find_last_not_of(" \t\r\n");
            return input.substr(first, last - first + 1);
        }

        bool isFunctionStyleAttributeName(const std::string &rawAttribute, const std::string &attributeName)
        {
            if (rawAttribute == attributeName)
            {
                return true;
            }

            return rawAttribute.size() > attributeName.size() && rawAttribute.starts_with(attributeName) && rawAttribute[attributeName.size()] == '(';
        }

        std::vector<std::string> splitAttributeParameters(const std::string &rawAttribute)
        {
            std::vector<std::string> result;
            size_t leftParen = rawAttribute.find('(');
            size_t rightParen = rawAttribute.rfind(')');
            if (leftParen == std::string::npos || rightParen == std::string::npos || rightParen < leftParen)
            {
                return result;
            }

            std::string params = rawAttribute.substr(leftParen + 1, rightParen - leftParen - 1);
            size_t start = 0;
            while (start <= params.size())
            {
                size_t end = params.find(',', start);
                std::string currentParam = end == std::string::npos ? params.substr(start) : params.substr(start, end - start);
                result.emplace_back(trimCopy(currentParam));
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return result;
        }
    } // namespace

    CPPHostClassValidator::CPPHostClassValidator(CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::array<std::string, 3> CPPHostClassValidator::getValidatedLocalWorkGroupSize(const clang::CXXRecordDecl *decl)
    {
        constexpr const char *LocalWorkGroupSizeAttributeName = "LocalWorkGroupSize";

        std::vector<std::string> matchedAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(decl))
        {
            std::string rawAttribute = mVisitor.generateRawAttribute(attr);
            if (isFunctionStyleAttributeName(rawAttribute, LocalWorkGroupSizeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        std::string computeClassName = decl->getQualifiedNameAsString();
        if (matchedAttributes.empty())
        {
            mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" is missing required attribute [[LocalWorkGroupSize(x, y, z)]].");
        }

        if (matchedAttributes.size() > 1)
        {
            mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" has more than one [[LocalWorkGroupSize(...)]] attribute.");
        }

        std::vector<std::string> params = splitAttributeParameters(matchedAttributes.front());
        if (params.size() != 3)
        {
            mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" requires [[LocalWorkGroupSize(x, y, z)]] with exactly 3 arguments, but got " + std::to_string(params.size()) + ".");
        }

        std::array<std::string, 3> evaluatedParams = {"", "", ""};
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[i].empty())
            {
                mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" has an empty argument in [[LocalWorkGroupSize(x, y, z)]].");
            }

            const auto evaluated = evaluateCompileTimeIntegerExpression(params[i], decl, *mVisitor.Context);
            if (!evaluated.success)
            {
                if (const auto substitutedValue = mVisitor.tryResolveTemplateSubstitutionValueByName(params[i]); substitutedValue.has_value())
                {
                    evaluatedParams[i] = *substitutedValue;
                    continue;
                }
                mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" argument " + std::string(1, static_cast<char>('x' + static_cast<int>(i))) + " in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer, but expression \"" + params[i] + "\" " + evaluated.error + ".");
            }
            if (evaluated.value <= 0)
            {
                mVisitor.throwCodegenError("ComputeClass \"" + computeClassName + "\" argument " + std::string(1, static_cast<char>('x' + static_cast<int>(i))) + " in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer, but expression \"" + params[i] + "\" evaluates to " + std::to_string(evaluated.value) + ".");
            }
            evaluatedParams[i] = std::to_string(evaluated.value);
        }

        return evaluatedParams;
    }

    bool CPPHostClassValidator::isShaderClassBindGroupParamType(const clang::QualType &qt) const
    {
        return mVisitor.checkTypeCanonicalName(qt, mUGLBindGroupName) || mVisitor.checkTypeCanonicalName(qt, mUGLRenderSetName);
    }

    void CPPHostClassValidator::validateShaderClassBindGroupSlots(const clang::CXXRecordDecl *decl, const clang::FunctionDecl *createFunc, const std::string &ownerKind, int bindgroupBufferOffset)
    {
        if (decl == nullptr || createFunc == nullptr)
        {
            return;
        }

        const ShaderBackendCapabilities &capabilities = UGLC::CodeGen::getPrimaryShaderBackendCapabilities();
        std::unordered_map<int, const clang::ParmVarDecl *> usedSlotParams;
        for (unsigned paramIndex = 0; paramIndex < createFunc->getNumParams(); ++paramIndex)
        {
            const clang::ParmVarDecl *param = createFunc->getParamDecl(paramIndex);
            if (!isShaderClassBindGroupParamType(param->getType()))
            {
                continue;
            }

            std::vector<std::string> matchedSlotAttributes;
            for (const auto *attr : mVisitor.getAllAttributes(param))
            {
                const std::string rawAttribute = mVisitor.generateRawAttribute(attr);
                if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeSlotName))
                {
                    matchedSlotAttributes.emplace_back(rawAttribute);
                }
            }

            if (matchedSlotAttributes.size() > 1)
            {
                mVisitor.throwCodegenError(ownerKind + " \"" + decl->getQualifiedNameAsString() + "\" parameter \"" + param->getNameAsString() + "\" declares multiple [[SlotN]] attributes.");
            }

            if (matchedSlotAttributes.empty())
            {
                std::string error = ownerKind + " \"" + decl->getQualifiedNameAsString() + "\" parameter \"" + param->getNameAsString() + "\" of type \"" + mVisitor.generateTypeCanonicalName(param->getType()) + "\" must declare a supported [[SlotN]] attribute. ";
                error += buildBindGroupSlotSupportRangeMessage(capabilities, bindgroupBufferOffset) + " ";
                error += "Add one supported slot annotation or reduce the total number of bind groups.";
                mVisitor.throwCodegenError(error);
            }

            const int bindGroupIndex = mVisitor.getAttributeNumber(matchedSlotAttributes.front(), mUGLAttributeSlotName);
            if (bindGroupIndex < 0)
            {
                mVisitor.throwCodegenError(ownerKind + " \"" + decl->getQualifiedNameAsString() + "\" parameter \"" + param->getNameAsString() + "\" has an invalid slot annotation. Expected [[Slot0]], [[Slot1]], etc.");
            }

            validateBindGroupSlotIndexOrThrow(capabilities, ownerKind, decl->getQualifiedNameAsString(), param->getNameAsString(), bindGroupIndex, bindgroupBufferOffset);

            if (auto iter = usedSlotParams.find(bindGroupIndex); iter != usedSlotParams.end())
            {
                mVisitor.throwCodegenError(ownerKind + " \"" + decl->getQualifiedNameAsString() + "\" reuses [[Slot" + std::to_string(bindGroupIndex) + "]] on both parameter \"" + iter->second->getNameAsString() + "\" and parameter \"" + param->getNameAsString() + "\".");
            }
            usedSlotParams.emplace(bindGroupIndex, param);
        }
    }

    int CPPHostClassValidator::getValidatedVertexAttributeLocation(const clang::FieldDecl *fieldDecl, const std::string &renderClassName, const std::string &vertexInputTypeName)
    {
        std::vector<std::string> matchedAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(fieldDecl))
        {
            std::string rawAttribute = mVisitor.generateRawAttribute(attr);
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        if (matchedAttributes.empty())
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" requires every field of vertex input \"" + vertexInputTypeName + "\" to declare an explicit [[AttributeN]]. Field \"" + fieldDecl->getNameAsString() + "\" is missing one.");
        }

        if (matchedAttributes.size() > 1)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" field \"" + fieldDecl->getNameAsString() + "\" in vertex input \"" + vertexInputTypeName + "\" declares multiple [[AttributeN]] annotations.");
        }

        int location = mVisitor.getAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
        if (location < 0)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" field \"" + fieldDecl->getNameAsString() + "\" in vertex input \"" + vertexInputTypeName + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
        }

        return location;
    }

    std::vector<CPPVisitor::VertexAttributeLayoutInfo> CPPHostClassValidator::getValidatedVertexAttributeLayout(const clang::ParmVarDecl *vertexInputParam, const std::string &renderClassName)
    {
        const auto *vertexInputRecord = mVisitor.getUnqualifiedType(vertexInputParam->getType())->getAsCXXRecordDecl();
        if (vertexInputRecord == nullptr)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" uses [[VertexInput0]] parameter \"" + vertexInputParam->getNameAsString() + "\" with non-record type \"" + mVisitor.generateTypeCanonicalName(vertexInputParam->getType()) + "\".");
        }

        std::string vertexInputTypeName = vertexInputRecord->getQualifiedNameAsString();
        bool pushedTemplateContext = false;
        if (const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(vertexInputRecord);
            specializationDecl != nullptr && specializationDecl->getSpecializedTemplate() != nullptr)
        {
            mVisitor.pushTemplateSubstitutionContext(specializationDecl);
            vertexInputRecord = specializationDecl->getSpecializedTemplate()->getTemplatedDecl();
            vertexInputTypeName = mVisitor.generateTypeCanonicalName(vertexInputParam->getType());
            pushedTemplateContext = true;
        }

        std::vector<CPPVisitor::VertexAttributeLayoutInfo> layoutInfos;
        std::unordered_map<int, const clang::FieldDecl *> locationToFieldMap;
        for (const auto *fieldDecl : mVisitor.getAllFieldFromRecord(vertexInputRecord))
        {
            int location = getValidatedVertexAttributeLocation(fieldDecl, renderClassName, vertexInputTypeName);
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" vertex input \"" + vertexInputTypeName + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \"" + iter->second->getNameAsString() + "\" and field \"" + fieldDecl->getNameAsString() + "\".");
            }

            locationToFieldMap.emplace(location, fieldDecl);
            layoutInfos.push_back({.fieldDecl = fieldDecl, .shaderLocation = location});
        }

        std::sort(layoutInfos.begin(), layoutInfos.end(), [](const CPPVisitor::VertexAttributeLayoutInfo &lhs, const CPPVisitor::VertexAttributeLayoutInfo &rhs) {
            return lhs.shaderLocation < rhs.shaderLocation;
        });

        if (pushedTemplateContext)
        {
            mVisitor.popTemplateSubstitutionContext();
        }
        return layoutInfos;
    }

    bool CPPHostClassValidator::isRenderVaryingSystemSemanticField(const clang::FieldDecl *fieldDecl) const
    {
        if (fieldDecl == nullptr)
        {
            return false;
        }

        return mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributePositionName) || mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributePrimitiveIDName) || mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributeBarycentricsName);
    }

    int CPPHostClassValidator::getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *fieldDecl, const std::string &renderClassName, const std::string &recordRole, const std::string &recordTypeName) const
    {
        std::vector<std::string> matchedAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(fieldDecl))
        {
            const std::string rawAttribute = mVisitor.generateRawAttribute(attr);
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        if (matchedAttributes.empty())
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" requires every non-system field of " + recordRole + " \"" + recordTypeName + "\" to declare an explicit [[AttributeN]]. Field \"" + fieldDecl->getNameAsString() + "\" is missing one.");
        }
        if (matchedAttributes.size() > 1)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" field \"" + fieldDecl->getNameAsString() + "\" in " + recordRole + " \"" + recordTypeName + "\" declares multiple [[AttributeN]] annotations.");
        }

        const int location = mVisitor.getAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
        if (location < 0)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" field \"" + fieldDecl->getNameAsString() + "\" in " + recordRole + " \"" + recordTypeName + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
        }

        return location;
    }

    std::unordered_map<int, CPPVisitor::RenderVaryingLayoutInfo> CPPHostClassValidator::collectValidatedRenderVaryingLayout(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName, const std::string &recordRole, bool requirePosition) const
    {
        std::unordered_map<int, CPPVisitor::RenderVaryingLayoutInfo> locationToFieldMap;
        if (recordDecl == nullptr)
        {
            return locationToFieldMap;
        }

        const std::string recordTypeName = recordDecl->getQualifiedNameAsString().empty() ? recordDecl->getNameAsString() : recordDecl->getQualifiedNameAsString();
        const clang::FieldDecl *positionField = nullptr;
        const clang::FieldDecl *primitiveIDField = nullptr;
        const clang::FieldDecl *barycentricsField = nullptr;
        for (const auto *fieldDecl : recordDecl->fields())
        {
            if (mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributePositionName))
            {
                if (positionField != nullptr)
                {
                    mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributePositionName + "]] fields \"" + positionField->getNameAsString() + "\" and \"" + fieldDecl->getNameAsString() + "\".");
                }
                positionField = fieldDecl;
                continue;
            }
            if (mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributePrimitiveIDName))
            {
                if (primitiveIDField != nullptr)
                {
                    mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributePrimitiveIDName + "]] fields \"" + primitiveIDField->getNameAsString() + "\" and \"" + fieldDecl->getNameAsString() + "\".");
                }
                primitiveIDField = fieldDecl;
                continue;
            }
            if (mVisitor.checkAttibuteByName(fieldDecl, mUGLAttributeBarycentricsName))
            {
                if (barycentricsField != nullptr)
                {
                    mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributeBarycentricsName + "]] fields \"" + barycentricsField->getNameAsString() + "\" and \"" + fieldDecl->getNameAsString() + "\".");
                }
                barycentricsField = fieldDecl;
                continue;
            }

            const int location = getValidatedRenderVaryingAttributeLocation(fieldDecl, renderClassName, recordRole, recordTypeName);
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \"" + iter->second.fieldDecl->getNameAsString() + "\" and field \"" + fieldDecl->getNameAsString() + "\".");
            }

            locationToFieldMap.emplace(location,
                                       CPPVisitor::RenderVaryingLayoutInfo{
                                           .fieldDecl = fieldDecl,
                                           .fieldType = mVisitor.getUnqualifiedType(fieldDecl->getType()),
                                       });
        }

        if (requirePosition && positionField == nullptr)
        {
            mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" requires " + recordRole + " \"" + recordTypeName + "\" to declare exactly one [[" + mUGLAttributePositionName + "]] field.");
        }

        return locationToFieldMap;
    }

    void CPPHostClassValidator::validateVertexFragmentVaryingContract(const clang::CXXRecordDecl *vertexOutputRecord, const clang::CXXRecordDecl *fragmentInputRecord, const std::string &renderClassName) const
    {
        if (vertexOutputRecord == nullptr || fragmentInputRecord == nullptr)
        {
            return;
        }

        const auto vertexLayout = collectValidatedRenderVaryingLayout(vertexOutputRecord, renderClassName, "vertex output", true);
        const auto fragmentLayout = collectValidatedRenderVaryingLayout(fragmentInputRecord, renderClassName, "fragment input");
        const std::string vertexOutputName = vertexOutputRecord->getQualifiedNameAsString().empty() ? vertexOutputRecord->getNameAsString() : vertexOutputRecord->getQualifiedNameAsString();
        const std::string fragmentInputName = fragmentInputRecord->getQualifiedNameAsString().empty() ? fragmentInputRecord->getNameAsString() : fragmentInputRecord->getQualifiedNameAsString();

        for (const auto &[location, fragmentInfo] : fragmentLayout)
        {
            auto vertexIter = vertexLayout.find(location);
            if (vertexIter == vertexLayout.end())
            {
                mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" fragment input \"" + fragmentInputName + "\" field \"" + fragmentInfo.fieldDecl->getNameAsString() + "\" declares [[Attribute" + std::to_string(location) + "]], but vertex output \"" + vertexOutputName + "\" does not provide it.");
            }

            if (!mVisitor.Context->hasSameType(fragmentInfo.fieldType, vertexIter->second.fieldType))
            {
                mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" fragment input \"" + fragmentInputName + "\" field \"" + fragmentInfo.fieldDecl->getNameAsString() + "\" declares [[Attribute" + std::to_string(location) + "]] with type \"" + mVisitor.generateTypeCanonicalName(fragmentInfo.fieldType) + "\", but vertex output \"" + vertexOutputName + "\" field \"" + vertexIter->second.fieldDecl->getNameAsString() + "\" uses type \"" +
                                           mVisitor.generateTypeCanonicalName(vertexIter->second.fieldType) + "\".");
            }
        }
    }

    const clang::FunctionDecl *CPPHostClassValidator::requireCreateMethod(const clang::CXXRecordDecl *decl, const std::string &ownerKind) const
    {
        const auto *createFunc = mVisitor.getMethodFromClass(decl, "create", mVisitor.makeCreateMethodLookupOptions());
        if (createFunc != nullptr)
        {
            return createFunc;
        }

        mVisitor.throwCodegenError(ownerKind + " \"" + decl->getQualifiedNameAsString() + "\" is missing required constructor method \"create(...)\".");
    }

    const clang::FunctionDecl *CPPHostClassValidator::requireRendererRenderMethod(const clang::CXXRecordDecl *decl) const
    {
        MethodLookupOptions options;
        options.requireBody = true;
        const auto *renderFunc = mVisitor.getMethodFromClass(decl, "render", options);
        if (renderFunc != nullptr)
        {
            return renderFunc;
        }

        mVisitor.throwCodegenError("Renderer \"" + decl->getQualifiedNameAsString() + "\" is missing required method \"render(...)\".");
    }

    void CPPHostClassValidator::validateFrameBufferFields(const clang::CXXRecordDecl *decl, const std::string &framebufferName) const
    {
        if (decl == nullptr)
        {
            return;
        }

        bool sawDepthAttachment = false;
        const auto fields = mVisitor.getAllFieldFromRecord(decl);
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto *field = fields[i];
            const bool isColorLikeAttachment = mVisitor.checkTypeCanonicalName(field->getType(), mUGLColorAttachmentName) ||
                                               mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalColorAttachmentName);
            const bool isDepthLikeAttachment = mVisitor.checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName) ||
                                               mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName);
            if (!isColorLikeAttachment && !isDepthLikeAttachment)
            {
                mVisitor.throwCodegenError("FrameBuffer \"" + framebufferName + "\" field \"" + field->getNameAsString() + "\" must be declared as UGL::ColorAttachment<Format>, UGL::DepthStencilAttachment<Format>, UGL::PixelLocalColorAttachment<Format, ...>, or UGL::PixelLocalDepthAttachment<Format, ...>, but found \"" + mVisitor.generateTypeCanonicalName(field->getType()) + "\".");
            }

            (void)requireAttachmentTemplateType(field, "FrameBuffer", framebufferName);
            if (isDepthLikeAttachment)
            {
                if (sawDepthAttachment)
                {
                    mVisitor.throwCodegenError("FrameBuffer \"" + framebufferName + "\" can not contain more than one depth attachment.");
                }
                if (i + 1 != fields.size())
                {
                    mVisitor.throwCodegenError("FrameBuffer \"" + framebufferName + "\" requires depth attachment to be the last field.");
                }
                sawDepthAttachment = true;
            }
        }
    }

    clang::QualType CPPHostClassValidator::requireAttachmentTemplateType(const clang::FieldDecl *fieldDecl, const std::string &ownerKind, const std::string &ownerName) const
    {
        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(fieldDecl->getType());
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
        {
            mVisitor.throwCodegenError(ownerKind + " \"" + ownerName + "\" field \"" + fieldDecl->getNameAsString() + "\" requires exactly one concrete attachment format template argument.");
        }

        return mVisitor.getUnqualifiedType(templateArgs.front().getAsType());
    }

    void CPPHostClassValidator::validateRenderTargetRecord(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string recordName = recordDecl->getQualifiedNameAsString().empty() ? recordDecl->getNameAsString() : recordDecl->getQualifiedNameAsString();
        bool sawDepthAttachment = false;
        const auto fields = mVisitor.getAllFieldFromRecord(recordDecl);
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto *field = fields[i];
            const bool isColorLikeAttachment = mVisitor.checkTypeCanonicalName(field->getType(), mUGLColorAttachmentName) ||
                                               mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalColorAttachmentName);
            const bool isDepthLikeAttachment = mVisitor.checkTypeCanonicalName(field->getType(), mUGLDepthAttachmentName) ||
                                               mVisitor.checkTypeCanonicalName(field->getType(), mUGLPixelLocalDepthAttachmentName);
            if (!isColorLikeAttachment && !isDepthLikeAttachment)
            {
                mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" render target \"" + recordName + "\" field \"" + field->getNameAsString() + "\" must be UGL::ColorAttachment<Format>, UGL::DepthStencilAttachment<Format>, UGL::PixelLocalColorAttachment<Format, ...>, or UGL::PixelLocalDepthAttachment<Format, ...>, but found \"" + mVisitor.generateTypeCanonicalName(field->getType()) + "\".");
            }

            (void)requireAttachmentTemplateType(field, "RenderClass \"" + renderClassName + "\" render target", recordName);
            if (isDepthLikeAttachment)
            {
                if (sawDepthAttachment)
                {
                    mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" has more than one depth attachment in render target \"" + recordName + "\".");
                }
                if (i + 1 != fields.size())
                {
                    mVisitor.throwCodegenError("RenderClass \"" + renderClassName + "\" requires depth attachment to be the last field in render target \"" + recordName + "\".");
                }
                sawDepthAttachment = true;
            }
        }
    }

    void CPPHostClassValidator::validateRenderSetComponentFieldAttributes(const clang::FieldDecl *fieldDecl, const std::string &renderSetName) const
    {
        if (fieldDecl == nullptr)
        {
            return;
        }

        std::vector<std::string> illegalAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(fieldDecl))
        {
            const std::string rawAttribute = mVisitor.generateRawAttribute(attr);
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeBindingName) || mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeSlotName))
            {
                illegalAttributes.emplace_back(rawAttribute);
            }
        }

        if (!illegalAttributes.empty())
        {
            mVisitor.throwCodegenError("RenderSet \"" + renderSetName + "\" field \"" + fieldDecl->getNameAsString() + "\" uses unsupported binding-style annotation(s) " + stringJoin(illegalAttributes, ", ") + ". RenderSet components must use [[RenderSetVertexBuffer]] / [[RenderSetIndexBuffer]] only where applicable.");
        }
    }

    void CPPHostClassValidator::validateRenderSetTextureResourceCount(const clang::FieldDecl *fieldDecl, const std::string &renderSetName, int maxResourceCount) const
    {
        if (maxResourceCount <= 0)
        {
            mVisitor.throwCodegenError("RenderSet \"" + renderSetName + "\" texture component field \"" + fieldDecl->getNameAsString() + "\" requires MaxResourceCount to be greater than 0.");
        }
    }
} // namespace UGLC::CodeGen::CPP
