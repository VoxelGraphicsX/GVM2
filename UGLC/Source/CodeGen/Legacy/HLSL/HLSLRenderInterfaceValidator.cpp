#include "HLSLRenderInterfaceValidator.hpp"

#include <CodeGen/UGLC.Constants.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace UGLC::CodeGen::HLSL
{
    HLSLRenderInterfaceValidator::HLSLRenderInterfaceValidator(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor)
        : mVisitor(visitor), mTypeConvertor(typeConvertor)
    {
    }

    std::string HLSLRenderInterfaceValidator::getFieldSemanticSuffix(const clang::FieldDecl *field, std::optional<int> colorIndexOverride) const
    {
        if (field == nullptr)
        {
            return {};
        }

        if (colorIndexOverride.has_value())
        {
            return " : SV_Target" + std::to_string(*colorIndexOverride);
        }
        if (mVisitor.checkAttibuteByName(field, mUGLAttributePositionName))
        {
            return " : SV_Position";
        }
        if (mVisitor.checkAttibuteByName(field, mUGLAttributePrimitiveIDName))
        {
            return " : SV_PrimitiveID";
        }
        if (mVisitor.checkAttibuteByName(field, mUGLAttributeBarycentricsName))
        {
            return " : SV_Barycentrics";
        }

        for (const auto *attr : mVisitor.getAllAttributes(field))
        {
            const std::string rawAttribute = attr->getAnnotation().str();
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                return makeVulkanLocationSemantic(mVisitor.getIndexedAttributeNumber(rawAttribute, mUGLAttributeAttributeName));
            }
        }

        return {};
    }

    std::string HLSLRenderInterfaceValidator::getParameterAttributeOrSemantic(const clang::ParmVarDecl *param) const
    {
        if (param == nullptr)
        {
            return {};
        }

        if (mVisitor.checkAttibuteByName(param, mUGLAttributeVertexIDName))
        {
            return " : SV_VertexID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeInstanceIDName))
        {
            return " : SV_InstanceID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributePrimitiveIDName))
        {
            return " : SV_PrimitiveID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeBarycentricsName))
        {
            return " : SV_Barycentrics";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributePixelCoordName))
        {
            return " : SV_Position";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeSampleIndexName))
        {
            return " : SV_SampleIndex";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeDispatchThreadIDName))
        {
            return " : SV_DispatchThreadID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeGroupThreadIDName))
        {
            return " : SV_GroupThreadID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeGroupIDName))
        {
            return " : SV_GroupID";
        }
        if (mVisitor.checkAttibuteByName(param, mUGLAttributeGroupIndexName))
        {
            return " : SV_GroupIndex";
        }

        for (const auto *attr : mVisitor.getAllAttributes(param))
        {
            const std::string rawAttribute = attr->getAnnotation().str();
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                return makeVulkanLocationSemantic(mVisitor.getIndexedAttributeNumber(rawAttribute, mUGLAttributeAttributeName));
            }
        }

        return {};
    }

    void HLSLRenderInterfaceValidator::validateEntryRenderInterfaceOrThrow(const clang::CXXRecordDecl *shaderClassDecl, const clang::FunctionDecl *entryFunction) const
    {
        if (shaderClassDecl == nullptr || entryFunction == nullptr || (!mVisitor.checkDerivedClassByName(shaderClassDecl, mUGLRenderClassBaseName) && !mVisitor.checkDerivedClassByName(shaderClassDecl, mUGLPixelLocalRenderClassBaseName)))
        {
            return;
        }

        const std::string renderClassName = shaderClassDecl->getQualifiedNameAsString();
        const std::string entryFunctionName = entryFunction->getNameAsString();
        if (entryFunctionName == mUGLVertexShaderFunctionName)
        {
            if (const auto *vertexInputParam = mVisitor.getParamFromFunctionWithAttribute(entryFunction, mVisitor.getUGLAttributeVertexInputNameByIndex(0)))
            {
                const auto *vertexInputRecord = getSelfOrPointeeCXXRecordDecl(mVisitor.getUnqualifiedType(vertexInputParam->getType()));
                if (vertexInputRecord == nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" uses [[VertexInput0]] parameter \"" + vertexInputParam->getNameAsString() + "\" with non-record type \"" + mVisitor.generateTypeCanonicalName(vertexInputParam->getType(), mTypeConvertor) + "\".");
                }
                validateVertexInputRecordOrThrow(vertexInputRecord, renderClassName);
            }

            const auto *vertexOutputRecord = getSelfOrPointeeCXXRecordDecl(mVisitor.getUnqualifiedType(entryFunction->getReturnType()));
            if (vertexOutputRecord != nullptr && !mVisitor.checkDerivedClassByName(vertexOutputRecord, mUGLFrameBufferBaseName))
            {
                validateRenderVaryingRecordOrThrow(vertexOutputRecord, renderClassName, "vertex output", true);
            }
            return;
        }

        if (entryFunctionName != mUGLFragmentShaderFunctionName)
        {
            return;
        }

        for (unsigned i = 0; i < entryFunction->getNumParams(); ++i)
        {
            const auto *param = entryFunction->getParamDecl(i);
            if (!getParameterAttributeOrSemantic(param).empty())
            {
                continue;
            }

            const auto *fragmentInputRecord = getSelfOrPointeeCXXRecordDecl(mVisitor.getUnqualifiedType(param->getType()));
            if (fragmentInputRecord == nullptr || mVisitor.checkDerivedClassByName(fragmentInputRecord, mUGLFrameBufferBaseName))
            {
                continue;
            }

            validateRenderVaryingRecordOrThrow(fragmentInputRecord, renderClassName, "fragment input");
        }
    }

    void HLSLRenderInterfaceValidator::validateShaderEntryBuiltinParametersOrThrow(const clang::FunctionDecl *shaderFunc) const
    {
        if (shaderFunc == nullptr)
        {
            return;
        }

        const std::string functionName = shaderFunc->getNameAsString();
        const bool isVertex = functionName == mUGLVertexShaderFunctionName;
        const bool isFragment = functionName == mUGLFragmentShaderFunctionName || functionName == mUGLPixelShaderFunctionName;
        const bool isCompute = functionName == mUGLComputeShaderFunctionName;
        const std::string functionLabel = shaderFunc->getQualifiedNameAsString();
        for (unsigned i = 0; i < shaderFunc->getNumParams(); ++i)
        {
            const auto *param = shaderFunc->getParamDecl(i);
            auto requireStage = [&](const std::string &attributeName, bool condition, const std::string &allowedStageDescription) {
                if (mVisitor.checkAttibuteByName(param, attributeName) && !condition)
                {
                    throw std::runtime_error("Shader entry \"" + functionLabel + "\" parameter \"" + param->getNameAsString() + "\" uses [[" + attributeName + "]], which is only valid in " + allowedStageDescription + " shaders.");
                }
            };

            requireStage(mUGLAttributeVertexIDName, isVertex, "vertex");
            requireStage(mUGLAttributeInstanceIDName, isVertex, "vertex");
            requireStage(mUGLAttributeRenderEntityIDName, isVertex, "vertex");
            requireStage(mUGLAttributeRenderEntityInstanceIDName, isVertex, "vertex");
            requireStage(mUGLAttributePrimitiveIDName, isFragment, "fragment");
            requireStage(mUGLAttributeBarycentricsName, isFragment, "fragment");
            requireStage(mUGLAttributePixelCoordName, isFragment, "fragment or pixel-local pixel");
            requireStage(mUGLAttributeSampleIndexName, isFragment, "fragment or pixel-local pixel");
            requireStage(mUGLAttributeDispatchThreadIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupThreadIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupIDName, isCompute, "compute");
            requireStage(mUGLAttributeGroupIndexName, isCompute, "compute");

            const bool hasExplicitShaderParameterDecoration = !getParameterAttributeOrSemantic(param).empty() || mVisitor.checkAttibuteByName(param, mUGLAttributeRenderEntityIDName) || mVisitor.checkAttibuteByName(param, mUGLAttributeRenderEntityInstanceIDName) || mVisitor.checkAttibuteByName(param, mUGLAttributePixelLocalInputName);
            if (isCompute && !hasExplicitShaderParameterDecoration)
            {
                throw std::runtime_error("Compute entry \"" + functionLabel + "\" parameter \"" + param->getNameAsString() + "\" must declare an explicit compute builtin attribute such as [[DispatchThreadID]], [[GroupThreadID]], [[GroupID]], or [[GroupIndex]].");
            }
            if (!isCompute && !hasExplicitShaderParameterDecoration)
            {
                const auto *paramRecordDecl = getSelfOrPointeeCXXRecordDecl(mVisitor.getUnqualifiedType(param->getType()));
                if (paramRecordDecl == nullptr)
                {
                    throw std::runtime_error("Shader entry \"" + functionLabel + "\" parameter \"" + param->getNameAsString() + "\" is unannotated and not a structured stage input type. Add an explicit stage builtin attribute or pass a record type with field semantics.");
                }
            }
        }
    }

    void HLSLRenderInterfaceValidator::validateRenderEntityBuiltinContractOrThrow(const clang::FunctionDecl *shaderFunc, const BindGroupInfoMap &bindGroupInfoMap) const
    {
        if (shaderFunc == nullptr)
        {
            return;
        }

        const auto *renderEntityIDVariable = mVisitor.getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityIDName);
        const auto *renderEntityInstanceIDVariable = mVisitor.getParamFromFunctionWithAttribute(shaderFunc, mUGLAttributeRenderEntityInstanceIDName);
        if (renderEntityIDVariable == nullptr && renderEntityInstanceIDVariable == nullptr)
        {
            return;
        }

        if (shaderFunc->getNameAsString() != mUGLVertexShaderFunctionName)
        {
            throw std::runtime_error("Shader entry \"" + shaderFunc->getQualifiedNameAsString() + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [[" + mUGLAttributeRenderEntityInstanceIDName + "]], but these builtins are currently only supported in vertex shaders.");
        }

        const auto renderSetCount = static_cast<size_t>(std::count_if(bindGroupInfoMap.begin(), bindGroupInfoMap.end(), [](const auto &entry) {
            return entry.second.isRenderSet;
        }));
        if (renderSetCount == 0u)
        {
            throw std::runtime_error("Vertex entry \"" + shaderFunc->getQualifiedNameAsString() + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [[" + mUGLAttributeRenderEntityInstanceIDName + "]], but its owning shader class does not bind any UGL::RenderSet<T>.");
        }
        if (renderSetCount != 1u)
        {
            throw std::runtime_error("Vertex entry \"" + shaderFunc->getQualifiedNameAsString() + "\" uses [[" + mUGLAttributeRenderEntityIDName + "]] / [[" + mUGLAttributeRenderEntityInstanceIDName + "]], but RenderEntity builtins require exactly one UGL::RenderSet<T> binding because InstanceID must be decoded through that RenderSet's RenderEntityCMDParams.");
        }
    }

    int HLSLRenderInterfaceValidator::getValidatedVertexInputAttributeLocation(const clang::FieldDecl *field, const std::string &renderClassName, const std::string &vertexInputTypeName) const
    {
        std::vector<std::string> matchedAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(field))
        {
            const std::string rawAttribute = attr->getAnnotation().str();
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        if (matchedAttributes.empty())
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires every field of vertex input \"" + vertexInputTypeName + "\" to declare an explicit [[AttributeN]]. Field \"" + field->getNameAsString() + "\" is missing one.");
        }

        if (matchedAttributes.size() > 1)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString() + "\" in vertex input \"" + vertexInputTypeName + "\" declares multiple [[AttributeN]] annotations.");
        }

        const int location = mVisitor.getIndexedAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
        if (location < 0)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString() + "\" in vertex input \"" + vertexInputTypeName + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
        }

        return location;
    }

    void HLSLRenderInterfaceValidator::validateVertexInputRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string vertexInputTypeName = recordDecl->getQualifiedNameAsString().empty() ? recordDecl->getNameAsString() : recordDecl->getQualifiedNameAsString();
        std::unordered_map<int, const clang::FieldDecl *> locationToFieldMap;
        for (const auto *field : recordDecl->fields())
        {
            const int location = getValidatedVertexInputAttributeLocation(field, renderClassName, vertexInputTypeName);
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" vertex input \"" + vertexInputTypeName + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \"" + iter->second->getNameAsString() + "\" and field \"" + field->getNameAsString() + "\".");
            }
            locationToFieldMap.emplace(location, field);
        }
    }

    bool HLSLRenderInterfaceValidator::isRenderVaryingSystemSemanticField(const clang::FieldDecl *field) const
    {
        if (field == nullptr)
        {
            return false;
        }

        return mVisitor.checkAttibuteByName(field, mUGLAttributePositionName) || mVisitor.checkAttibuteByName(field, mUGLAttributePrimitiveIDName) || mVisitor.checkAttibuteByName(field, mUGLAttributeBarycentricsName);
    }

    int HLSLRenderInterfaceValidator::getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *field, const std::string &renderClassName, const std::string &recordRole, const std::string &recordTypeName) const
    {
        std::vector<std::string> matchedAttributes;
        for (const auto *attr : mVisitor.getAllAttributes(field))
        {
            const std::string rawAttribute = attr->getAnnotation().str();
            if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeAttributeName))
            {
                matchedAttributes.emplace_back(rawAttribute);
            }
        }

        if (matchedAttributes.empty())
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires every non-system field of " + recordRole + " \"" + recordTypeName + "\" to declare an explicit [[AttributeN]]. Field \"" + field->getNameAsString() + "\" is missing one.");
        }

        if (matchedAttributes.size() > 1)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString() + "\" in " + recordRole + " \"" + recordTypeName + "\" declares multiple [[AttributeN]] annotations.");
        }

        const int location = mVisitor.getIndexedAttributeNumber(matchedAttributes.front(), mUGLAttributeAttributeName);
        if (location < 0)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" field \"" + field->getNameAsString() + "\" in " + recordRole + " \"" + recordTypeName + "\" has an invalid attribute annotation. Expected [[Attribute0]], [[Attribute1]], etc.");
        }

        return location;
    }

    void HLSLRenderInterfaceValidator::validateRenderVaryingRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName, const std::string &recordRole, bool requirePosition) const
    {
        if (recordDecl == nullptr)
        {
            return;
        }

        const std::string recordTypeName = recordDecl->getQualifiedNameAsString().empty() ? recordDecl->getNameAsString() : recordDecl->getQualifiedNameAsString();
        std::unordered_map<int, const clang::FieldDecl *> locationToFieldMap;
        const clang::FieldDecl *positionField = nullptr;
        const clang::FieldDecl *primitiveIDField = nullptr;
        const clang::FieldDecl *barycentricsField = nullptr;
        for (const auto *field : recordDecl->fields())
        {
            if (mVisitor.checkAttibuteByName(field, mUGLAttributePositionName))
            {
                if (positionField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributePositionName + "]] fields \"" + positionField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                positionField = field;
                continue;
            }
            if (mVisitor.checkAttibuteByName(field, mUGLAttributePrimitiveIDName))
            {
                if (primitiveIDField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributePrimitiveIDName + "]] fields \"" + primitiveIDField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                primitiveIDField = field;
                continue;
            }
            if (mVisitor.checkAttibuteByName(field, mUGLAttributeBarycentricsName))
            {
                if (barycentricsField != nullptr)
                {
                    throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" declares duplicate [[" + mUGLAttributeBarycentricsName + "]] fields \"" + barycentricsField->getNameAsString() + "\" and \"" + field->getNameAsString() + "\".");
                }
                barycentricsField = field;
                continue;
            }

            const int location = getValidatedRenderVaryingAttributeLocation(field, renderClassName, recordRole, recordTypeName);
            if (auto iter = locationToFieldMap.find(location); iter != locationToFieldMap.end())
            {
                throw std::runtime_error("RenderClass \"" + renderClassName + "\" " + recordRole + " \"" + recordTypeName + "\" reuses [[Attribute" + std::to_string(location) + "]] on both field \"" + iter->second->getNameAsString() + "\" and field \"" + field->getNameAsString() + "\".");
            }
            locationToFieldMap.emplace(location, field);
        }

        if (requirePosition && positionField == nullptr)
        {
            throw std::runtime_error("RenderClass \"" + renderClassName + "\" requires " + recordRole + " \"" + recordTypeName + "\" to declare exactly one [[" + mUGLAttributePositionName + "]] field.");
        }
    }

    const clang::CXXRecordDecl *HLSLRenderInterfaceValidator::getSelfOrPointeeCXXRecordDecl(clang::QualType type)
    {
        type = type.getCanonicalType().getUnqualifiedType();
        if (const auto *pointerType = type->getAs<clang::PointerType>())
        {
            type = pointerType->getPointeeType().getCanonicalType().getUnqualifiedType();
        }
        if (const auto *referenceType = type->getAs<clang::ReferenceType>())
        {
            type = referenceType->getPointeeType().getCanonicalType().getUnqualifiedType();
        }
        return type->getAsCXXRecordDecl();
    }

    std::string HLSLRenderInterfaceValidator::makeVulkanLocationSemantic(int location)
    {
        return " : TEXCOORD" + std::to_string(location);
    }
} // namespace UGLC::CodeGen::HLSL
