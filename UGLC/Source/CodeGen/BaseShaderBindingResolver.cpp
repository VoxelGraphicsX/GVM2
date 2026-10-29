#include "BaseShaderBindingResolver.hpp"

#include "BaseASTVisitor.hpp"
#include "HLSL/HLSLTextureTypes.hpp"
#include "MSL/MSLTextureTypes.hpp"
#include "UGLC.Constants.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace UGLC::CodeGen
{
    namespace
    {
        std::string normalizeUGLScalarVectorCanonicalName(const std::string &canonicalName)
        {
            if (canonicalName == "float" || canonicalName == "float1" || canonicalName == "UGL::float1")
            {
                return "UGL::float";
            }
            if (canonicalName == "float2" || canonicalName == "float3" || canonicalName == "float4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "int" || canonicalName == "int1" || canonicalName == "UGL::int1")
            {
                return "UGL::int";
            }
            if (canonicalName == "int2" || canonicalName == "int3" || canonicalName == "int4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "uint" || canonicalName == "uint1" || canonicalName == "unsigned int" || canonicalName == "UGL::uint1")
            {
                return "UGL::uint";
            }
            if (canonicalName == "uint2" || canonicalName == "uint3" || canonicalName == "uint4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "half" || canonicalName == "half1" || canonicalName == "UGL::half1")
            {
                return "UGL::half";
            }
            if (canonicalName == "half2" || canonicalName == "half3" || canonicalName == "half4")
            {
                return "UGL::" + canonicalName;
            }
            if (canonicalName == "bool" || canonicalName == "bool1" || canonicalName == "UGL::bool1")
            {
                return "UGL::bool";
            }
            if (canonicalName == "bool2" || canonicalName == "bool3" || canonicalName == "bool4")
            {
                return "UGL::" + canonicalName;
            }

            return canonicalName;
        }

        std::string buildShaderTypePath(const std::vector<std::string> &segments)
        {
            if (segments.empty())
            {
                return "<element>";
            }

            std::string result;
            for (const auto &segment : segments)
            {
                if (segment == "[]")
                {
                    result += "[]";
                    continue;
                }
                if (!result.empty())
                {
                    result += ".";
                }
                result += segment;
            }
            return result;
        }

        bool isKnownLeafShaderValueType(const std::string &canonicalName)
        {
            if (canonicalName.empty())
            {
                return false;
            }
            if (canonicalName == "_Bool" || canonicalName == "bool" || canonicalName == "std::nullptr_t")
            {
                return true;
            }
            if (canonicalName.starts_with("UGL::"))
            {
                return true;
            }
            if (canonicalName == "float" || canonicalName == "double" || canonicalName == "half")
            {
                return true;
            }
            if (canonicalName == "int" || canonicalName == "unsigned int" || canonicalName == "short" || canonicalName == "unsigned short" ||
                canonicalName == "char" || canonicalName == "unsigned char" || canonicalName == "long" || canonicalName == "unsigned long" ||
                canonicalName == "long long" || canonicalName == "unsigned long long")
            {
                return true;
            }
            return false;
        }
    } // namespace

    BaseShaderBindingResolver::BaseShaderBindingResolver(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    BaseShaderTextureSampleType BaseShaderBindingResolver::resolveTextureSampleType(const clang::QualType &sampleType,
                                                                                       const clang::FieldDecl *fieldDecl,
                                                                                       const clang::CXXRecordDecl *bindGroupDecl,
                                                                                       int unwrapDepth) const
    {
        if (unwrapDepth > 8)
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" exceeded sampled texture type resolution depth while inspecting \""
                                     + mVisitor.generateTypeCanonicalName(sampleType) + "\".");
        }

        const clang::QualType resolvedType = mVisitor.getUnqualifiedType(sampleType);
        const std::string canonicalName = normalizeUGLScalarVectorCanonicalName(mVisitor.generateTypeCanonicalName(resolvedType));
        std::string diagnosticTypeName = mVisitor.generateTypeCanonicalName(resolvedType);
        if (diagnosticTypeName == "_Bool")
        {
            diagnosticTypeName = "bool";
        }

        if (resolvedType->isBooleanType() ||
            canonicalName == "UGL::bool" ||
            canonicalName == "UGL::bool2" ||
            canonicalName == "UGL::bool3" ||
            canonicalName == "UGL::bool4")
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" uses unsupported sampled texture type \"" + diagnosticTypeName
                                     + "\". Supported sampled texture element types must resolve to float/half, signed integer, unsigned integer, or UGL::TextureFormat::Depth*.");
        }

        if (canonicalName.starts_with("UGL::TextureFormat::Depth"))
        {
            return BaseShaderTextureSampleType::Depth;
        }

        if (resolvedType->isRealFloatingType() ||
            canonicalName == "UGL::half" ||
            canonicalName == "UGL::half2" ||
            canonicalName == "UGL::half3" ||
            canonicalName == "UGL::half4" ||
            canonicalName == "UGL::float" ||
            canonicalName == "UGL::float2" ||
            canonicalName == "UGL::float3" ||
            canonicalName == "UGL::float4")
        {
            return BaseShaderTextureSampleType::Float;
        }

        if (resolvedType->isUnsignedIntegerType() ||
            canonicalName == "UGL::uint" ||
            canonicalName == "UGL::uint2" ||
            canonicalName == "UGL::uint3" ||
            canonicalName == "UGL::uint4")
        {
            return BaseShaderTextureSampleType::Uint;
        }

        if (resolvedType->isSignedIntegerType() ||
            canonicalName == "UGL::int" ||
            canonicalName == "UGL::int2" ||
            canonicalName == "UGL::int3" ||
            canonicalName == "UGL::int4")
        {
            return BaseShaderTextureSampleType::Sint;
        }

        if (auto trueType = mVisitor.resolveRecordNestedTrueType(resolvedType))
        {
            return resolveTextureSampleType(*trueType, fieldDecl, bindGroupDecl, unwrapDepth + 1);
        }

        throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                 + "\" field \"" + fieldDecl->getNameAsString()
                                 + "\" uses unsupported sampled texture type \"" + diagnosticTypeName
                                 + "\". Supported sampled texture element types must resolve to float/half, signed integer, unsigned integer, or UGL::TextureFormat::Depth*.");
    }

    std::vector<clang::TemplateArgument> BaseShaderBindingResolver::requireTemplateArguments(const clang::QualType &resourceType,
                                                                                             size_t expectedCount,
                                                                                             const clang::FieldDecl *fieldDecl,
                                                                                             const clang::CXXRecordDecl *bindGroupDecl) const
    {
        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(resourceType);
        if (templateArgs.size() != expectedCount)
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" is declared as \""
                                     + mVisitor.generateTypeCanonicalName(resourceType)
                                     + "\", but it requires exactly " + std::to_string(expectedCount)
                                     + " template argument" + (expectedCount == 1 ? "" : "s") + ".");
        }
        return templateArgs;
    }

    clang::QualType BaseShaderBindingResolver::requireConcreteTypeTemplateArgument(const clang::QualType &resourceType,
                                                                                   size_t index,
                                                                                   const clang::FieldDecl *fieldDecl,
                                                                                   const clang::CXXRecordDecl *bindGroupDecl) const
    {
        const auto templateArgs = requireTemplateArguments(resourceType, index + 1, fieldDecl, bindGroupDecl);
        if (templateArgs[index].getKind() != clang::TemplateArgument::Type || templateArgs[index].getAsType().isNull())
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" is declared as \""
                                     + mVisitor.generateTypeCanonicalName(resourceType)
                                     + "\", but template argument " + std::to_string(index)
                                     + " must be a concrete type.");
        }
        return mVisitor.getUnqualifiedType(templateArgs[index].getAsType());
    }

    void BaseShaderBindingResolver::validateShaderBufferElementType(const clang::QualType &elementType,
                                                                    const clang::FieldDecl *fieldDecl,
                                                                    const clang::CXXRecordDecl *bindGroupDecl,
                                                                    const std::string &wrapperTypeName) const
    {
        std::unordered_set<const clang::CXXRecordDecl *> visitedRecords;
        std::function<void(const clang::QualType &, std::vector<std::string> &)> validateType = [&](const clang::QualType &qt, std::vector<std::string> &path) {
            const clang::QualType resolvedType = mVisitor.getUnqualifiedType(qt);
            const clang::QualType semanticType = mVisitor.Context == nullptr ? resolvedType : mVisitor.Context->getCanonicalType(resolvedType);
            const std::string typeName = mVisitor.generateTypeCanonicalName(resolvedType);

            auto throwIllegalMember = [&](const std::string &reason) {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" uses " + wrapperTypeName + " element type \""
                                         + mVisitor.generateTypeCanonicalName(elementType)
                                         + "\", but member path \"" + buildShaderTypePath(path)
                                         + "\" resolves to illegal shader data type \"" + typeName + "\". " + reason);
            };

            if (semanticType->isReferenceType())
            {
                throwIllegalMember(wrapperTypeName + " element types cannot contain references.");
            }
            if (semanticType->isPointerType() || semanticType->isMemberPointerType() || semanticType->isFunctionPointerType())
            {
                throwIllegalMember(wrapperTypeName + " element types cannot contain pointers.");
            }
            if (semanticType->isFunctionType())
            {
                throwIllegalMember(wrapperTypeName + " element types cannot contain functions.");
            }

            if (mVisitor.isShaderResourceOrBindingType(resolvedType) || mVisitor.isShaderResourceOrBindingType(semanticType))
            {
                throwIllegalMember(wrapperTypeName + " element types may only contain plain shader data. Nested textures, samplers, bind groups, render sets, and host resource handles are not allowed.");
            }

            if (const auto *arrayType = llvm::dyn_cast<clang::ArrayType>(semanticType.getTypePtrOrNull()))
            {
                path.emplace_back("[]");
                validateType(arrayType->getElementType(), path);
                path.pop_back();
                return;
            }

            if (semanticType->isBuiltinType() || semanticType->isEnumeralType() || isKnownLeafShaderValueType(typeName))
            {
                return;
            }

            const auto *recordDecl = semanticType->getAsCXXRecordDecl();
            if (recordDecl == nullptr)
            {
                return;
            }

            recordDecl = recordDecl->getCanonicalDecl();
            if (!visitedRecords.emplace(recordDecl).second)
            {
                return;
            }

            for (const auto *nestedField : recordDecl->fields())
            {
                path.emplace_back(nestedField->getNameAsString());
                validateType(nestedField->getType(), path);
                path.pop_back();
            }
        };

        std::vector<std::string> path;
        validateType(elementType, path);
    }

    void BaseShaderBindingResolver::validateStorageTextureElementType(const clang::QualType &elementType,
                                                                      const clang::FieldDecl *fieldDecl,
                                                                      const clang::CXXRecordDecl *bindGroupDecl,
                                                                      const std::string &resourceTypeName) const
    {
        const clang::QualType resolvedType = mVisitor.getUnqualifiedType(elementType);
        const std::string canonicalName = mVisitor.generateTypeCanonicalName(resolvedType);
        if (!canonicalName.starts_with("UGL::TextureFormat::"))
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" is declared as \"" + resourceTypeName
                                     + "\", but read-write storage textures require a UGL::TextureFormat::* template argument. Found \""
                                     + canonicalName + "\".");
        }

        if (canonicalName.starts_with("UGL::TextureFormat::Depth"))
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" uses storage texture format \"" + canonicalName
                                     + "\", but depth formats are read-only sampled textures and cannot be used with "
                                     + resourceTypeName + ".");
        }

        try
        {
            UGLC::CodeGen::HLSL::MakeFormatToVectorTypeForStorageTexture(canonicalName);
            UGLC::CodeGen::HLSL::MakeVulkanImageFormatForStorageTexture(canonicalName);
            UGLC::CodeGen::MSL::MakeFormatToVectorTypeForStorageTexture(canonicalName);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                     + "\" field \"" + fieldDecl->getNameAsString()
                                     + "\" uses unsupported storage texture format \"" + canonicalName
                                     + "\". UGLC currently only accepts storage texture formats with a valid MSL lowering and an explicit Vulkan/HLSL image_format lowering.");
        }
    }

    std::vector<BindGroupFieldBindingInfo> BaseShaderBindingResolver::resolveFieldBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc) const
    {
        std::vector<BindGroupFieldBindingInfo> result;
        if (bindGroupDecl == nullptr)
        {
            return result;
        }

        if (createFunc == nullptr)
        {
            createFunc = mVisitor.getMethodFromClass(bindGroupDecl, mUGLCTORFunctionName, mVisitor.makeCreateMethodLookupOptions());
        }

        const auto fields = mVisitor.getAllFieldFromRecord(bindGroupDecl);
        result.reserve(fields.size());

        std::vector<std::pair<int, const clang::FieldDecl *>> usedBindings;
        for (const auto *fieldDecl : fields)
        {
            const clang::Decl *bindingSourceDecl = fieldDecl;
            if (const auto *paramDecl = mVisitor.getParamFromFunctionByName(createFunc, fieldDecl->getNameAsString()))
            {
                // Bind-group resource annotations belong to the user-authored
                // create(...) parameter surface. Prefer that source so the DSL
                // contract stays explicit even if Clang also synthesizes field
                // declarations behind the scenes.
                bindingSourceDecl = paramDecl;
            }

            if (fieldDecl != nullptr && fieldDecl->isInvalidDecl())
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" has an invalid declared type. Fix the underlying C++ type error before running UGLC code generation.");
            }
            if (bindingSourceDecl != nullptr && bindingSourceDecl->isInvalidDecl())
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" maps to an invalid create(...) parameter declaration. Fix the underlying C++ type error before running UGLC code generation.");
            }

            std::vector<std::string> bindingAttributes;
            std::vector<std::string> legacySlotAttributes;
            for (const auto *attr : mVisitor.getAllAttributes(bindingSourceDecl))
            {
                const std::string rawAttribute = mVisitor.generateRawAttribute(attr);
                if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeBindingName))
                {
                    bindingAttributes.emplace_back(rawAttribute);
                }
                else if (mVisitor.isExactIndexedAttribute(rawAttribute, mUGLAttributeSlotName))
                {
                    legacySlotAttributes.emplace_back(rawAttribute);
                }
            }

            if (!legacySlotAttributes.empty())
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" uses legacy [[SlotN]] syntax. Bind-group resources must declare explicit [[BindingN]] attributes, while [[SlotN]] is reserved for shader-class BindGroup<...>/RenderSet<...> parameters.");
            }

            if (bindingAttributes.empty())
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" requires an explicit [[BindingN]] attribute. Bind-group resources no longer default to declaration order.");
            }

            if (bindingAttributes.size() > 1)
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" declares multiple [[BindingN]] attributes.");
            }

            const int bindingIndex = mVisitor.getIndexedAttributeNumber(bindingAttributes.front(), mUGLAttributeBindingName);
            if (bindingIndex < 0)
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" has an invalid binding annotation. Expected [[Binding0]], [[Binding1]], etc.");
            }
            if (bindingIndex >= MaxBindGroupResourceBindingCount)
            {
                // Bind-group resource bindings are part of the explicit shader ABI.
                // Reject out-of-range indices early so later backends never inherit
                // a partially generated descriptor layout with unsupported holes.
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" field \"" + fieldDecl->getNameAsString()
                                         + "\" uses [[Binding" + std::to_string(bindingIndex)
                                         + "]], but UGLC currently only supports [[Binding0]] through [[Binding"
                                         + std::to_string(MaxBindGroupResourceBindingCount - 1)
                                         + "]] for bind-group resources.");
            }

            const auto duplicateBindingIter = std::find_if(usedBindings.begin(), usedBindings.end(),
                                                           [bindingIndex](const auto &entry)
                                                           {
                                                               return entry.first == bindingIndex;
                                                           });
            if (duplicateBindingIter != usedBindings.end())
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" reuses [[Binding" + std::to_string(bindingIndex)
                                         + "]] on both field \"" + duplicateBindingIter->second->getNameAsString()
                                         + "\" and field \"" + fieldDecl->getNameAsString() + "\".");
            }

            usedBindings.emplace_back(bindingIndex, fieldDecl);
            result.push_back({fieldDecl, bindingIndex});
        }

        return result;
    }

    std::vector<BaseShaderResourceBinding> BaseShaderBindingResolver::resolveResourceBindings(const clang::CXXRecordDecl *bindGroupDecl, const clang::FunctionDecl *createFunc) const
    {
        std::vector<BaseShaderResourceBinding> result;
        if (bindGroupDecl == nullptr)
        {
            return result;
        }

        const auto resolvedFieldBindings = resolveFieldBindings(bindGroupDecl, createFunc);
        result.reserve(resolvedFieldBindings.size());

        for (const auto &fieldBinding : resolvedFieldBindings)
        {
            BaseShaderResourceBinding baseBinding{};
            baseBinding.fieldDecl = fieldBinding.fieldDecl;
            baseBinding.bindingIndex = fieldBinding.bindingIndex;
            baseBinding.resourceType = mVisitor.getUnqualifiedType(fieldBinding.fieldDecl->getType());
            baseBinding.resourceTypeName = mVisitor.generateTypeCanonicalName(baseBinding.resourceType);

            if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderUniformBufferName))
            {
                baseBinding.kind = BaseShaderResourceKind::UniformBuffer;
                baseBinding.access = BaseShaderResourceAccess::ReadOnly;
                baseBinding.elementType = requireConcreteTypeTemplateArgument(baseBinding.resourceType, 0, fieldBinding.fieldDecl, bindGroupDecl);
                baseBinding.elementTypeName = mVisitor.generateTypeCanonicalName(baseBinding.elementType);
                validateShaderBufferElementType(baseBinding.elementType, fieldBinding.fieldDecl, bindGroupDecl, "UniformBuffer");
            }
            else if (mVisitor.isLegacyStorageBufferType(baseBinding.resourceType))
            {
                mVisitor.throwLegacyStorageBufferMigrationError("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                                                + "\" field \"" + fieldBinding.fieldDecl->getNameAsString() + "\"");
            }
            else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderStructuredBufferName))
            {
                baseBinding.kind = BaseShaderResourceKind::StorageBuffer;
                baseBinding.access = BaseShaderResourceAccess::ReadOnly;
                baseBinding.elementType = requireConcreteTypeTemplateArgument(baseBinding.resourceType, 0, fieldBinding.fieldDecl, bindGroupDecl);
                baseBinding.elementTypeName = mVisitor.generateTypeCanonicalName(baseBinding.elementType);
                validateShaderBufferElementType(baseBinding.elementType, fieldBinding.fieldDecl, bindGroupDecl, "StructuredBuffer");
            }
            else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWStructuredBufferName))
            {
                baseBinding.kind = BaseShaderResourceKind::StorageBuffer;
                baseBinding.access = BaseShaderResourceAccess::ReadWrite;
                baseBinding.elementType = requireConcreteTypeTemplateArgument(baseBinding.resourceType, 0, fieldBinding.fieldDecl, bindGroupDecl);
                baseBinding.elementTypeName = mVisitor.generateTypeCanonicalName(baseBinding.elementType);
                validateShaderBufferElementType(baseBinding.elementType, fieldBinding.fieldDecl, bindGroupDecl, "RWStructuredBuffer");
            }
            else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderTexture2DName) ||
                     mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderTexture2DArrayName) ||
                     mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderTexture3DName))
            {
                baseBinding.kind = BaseShaderResourceKind::SampledTexture;
                if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderTexture2DArrayName))
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture2DArray;
                }
                else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderTexture3DName))
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture3D;
                }
                else
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture2D;
                }
                baseBinding.access = BaseShaderResourceAccess::ReadOnly;
                baseBinding.elementType = requireConcreteTypeTemplateArgument(baseBinding.resourceType, 0, fieldBinding.fieldDecl, bindGroupDecl);
                baseBinding.elementTypeName = mVisitor.generateTypeCanonicalName(baseBinding.elementType);
                baseBinding.sampleType = resolveTextureSampleType(baseBinding.elementType, fieldBinding.fieldDecl, bindGroupDecl);
            }
            else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWTexture2DName) ||
                     mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWTexture2DArrayName) ||
                     mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWTexture3DName))
            {
                baseBinding.kind = BaseShaderResourceKind::StorageTexture;
                if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWTexture2DArrayName))
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture2DArray;
                }
                else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderRWTexture3DName))
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture3D;
                }
                else
                {
                    baseBinding.dimension = BaseShaderTextureDimension::Texture2D;
                }
                baseBinding.access = BaseShaderResourceAccess::ReadWrite;
                baseBinding.elementType = requireConcreteTypeTemplateArgument(baseBinding.resourceType, 0, fieldBinding.fieldDecl, bindGroupDecl);
                baseBinding.elementTypeName = mVisitor.generateTypeCanonicalName(baseBinding.elementType);
                validateStorageTextureElementType(baseBinding.elementType, fieldBinding.fieldDecl, bindGroupDecl, baseBinding.resourceTypeName);
            }
            else if (mVisitor.checkTypeCanonicalName(baseBinding.resourceType, mUGLShaderSamplerName))
            {
                baseBinding.kind = BaseShaderResourceKind::Sampler;
            }
            else
            {
                throw std::runtime_error("BindGroup \"" + bindGroupDecl->getQualifiedNameAsString()
                                         + "\" contains unsupported field type \"" + baseBinding.resourceTypeName
                                         + "\" on field \"" + fieldBinding.fieldDecl->getNameAsString() + "\".");
            }

            result.push_back(baseBinding);
        }

        return result;
    }
} // namespace UGLC::CodeGen
