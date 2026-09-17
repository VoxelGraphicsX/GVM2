#include "ShaderBufferLayoutValidator.hpp"

#include "BaseASTVisitor.hpp"
#include "clang/AST/RecordLayout.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

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
                return "<whole element>";
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

        std::vector<std::string> pathForRecordSizeMismatch(const clang::CXXRecordDecl *recordDecl, const std::vector<std::string> &path)
        {
            std::vector<std::string> result = path;
            if (recordDecl == nullptr)
            {
                return result;
            }

            const clang::FieldDecl *lastField = nullptr;
            for (const auto *field : recordDecl->fields())
            {
                if (field != nullptr)
                {
                    lastField = field;
                }
            }

            if (lastField != nullptr)
            {
                result.emplace_back(lastField->getNameAsString() + " (implicit tail padding after this member)");
                return result;
            }

            const std::string recordName = recordDecl->getQualifiedNameAsString();
            if (!recordName.empty())
            {
                result.emplace_back(recordName);
            }
            return result;
        }

        enum class BufferLayoutPolicy
        {
            PackedRegister16,
            NaturalStruct,
        };

        struct ScalarVectorLayoutInfo
        {
            std::string scalarName;
            uint64_t componentSize = 4;
            uint64_t componentCount = 1;
        };

        struct ShaderBackendTypeLayout
        {
            uint64_t size = 0;
            uint64_t align = 1;
            std::optional<uint64_t> arrayStride;
        };

        struct ShaderBackendFieldLayout
        {
            const clang::FieldDecl *field = nullptr;
            uint64_t offset = 0;
            ShaderBackendTypeLayout typeLayout;
        };

        uint64_t alignUpTo(uint64_t value, uint64_t alignment)
        {
            if (alignment <= 1)
            {
                return value;
            }
            return ((value + alignment - 1) / alignment) * alignment;
        }

        std::string stripTypeNamePrefix(std::string name)
        {
            while (name.starts_with("const "))
            {
                name.erase(0, 6);
            }
            while (name.starts_with("volatile "))
            {
                name.erase(0, 9);
            }
            while (name.starts_with("struct "))
            {
                name.erase(0, 7);
            }
            while (name.starts_with("class "))
            {
                name.erase(0, 6);
            }
            while (name.starts_with("::"))
            {
                name.erase(0, 2);
            }
            if (const size_t separator = name.rfind("::"); separator != std::string::npos)
            {
                name = name.substr(separator + 2);
            }
            return name;
        }

        std::optional<ScalarVectorLayoutInfo> parseScalarVectorTypeName(std::string name)
        {
            name = stripTypeNamePrefix(std::move(name));
            if (name == "float" || name == "float1")
            {
                return ScalarVectorLayoutInfo{.scalarName = "float", .componentSize = 4, .componentCount = 1};
            }
            if (name == "double" || name == "double1")
            {
                return ScalarVectorLayoutInfo{.scalarName = "double", .componentSize = 4, .componentCount = 1};
            }
            if (name == "half" || name == "half1")
            {
                return ScalarVectorLayoutInfo{.scalarName = "half", .componentSize = 2, .componentCount = 1};
            }
            if (name == "int" || name == "int1" || name == "int32_t")
            {
                return ScalarVectorLayoutInfo{.scalarName = "int", .componentSize = 4, .componentCount = 1};
            }
            if (name == "uint" || name == "uint1" || name == "uint32_t" || name == "unsigned int")
            {
                return ScalarVectorLayoutInfo{.scalarName = "uint", .componentSize = 4, .componentCount = 1};
            }
            if (name == "bool" || name == "bool1" || name == "_Bool")
            {
                return ScalarVectorLayoutInfo{.scalarName = "bool", .componentSize = 4, .componentCount = 1};
            }

            auto parseVectorSuffix = [&](std::string_view prefix, std::string scalarName, uint64_t componentSize) -> std::optional<ScalarVectorLayoutInfo>
            {
                if (!std::string_view(name).starts_with(prefix) || name.size() != prefix.size() + 1)
                {
                    return std::nullopt;
                }
                const char width = name.back();
                if (width < '2' || width > '4')
                {
                    return std::nullopt;
                }
                return ScalarVectorLayoutInfo{.scalarName = std::move(scalarName), .componentSize = componentSize, .componentCount = static_cast<uint64_t>(width - '0')};
            };

            if (auto info = parseVectorSuffix("float", "float", 4))
            {
                return info;
            }
            if (auto info = parseVectorSuffix("double", "double", 4))
            {
                return info;
            }
            if (auto info = parseVectorSuffix("half", "half", 2))
            {
                return info;
            }
            if (auto info = parseVectorSuffix("uint", "uint", 4))
            {
                return info;
            }
            if (auto info = parseVectorSuffix("int", "int", 4))
            {
                return info;
            }
            if (auto info = parseVectorSuffix("bool", "bool", 4))
            {
                return info;
            }

            return std::nullopt;
        }

        class ShaderBufferLayoutValidator
        {
        public:
            ShaderBufferLayoutValidator(const BaseASTVisitor &visitor, BufferLayoutPolicy policy, std::string_view backendName, std::string_view layoutName)
                : mVisitor(visitor)
                , mPolicy(policy)
                , mBackendName(backendName)
                , mLayoutName(layoutName)
            {
            }

            void validateBindGroupInfoMap(const BindGroupInfoMap &bindGroupInfoMap) const
            {
                for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
                {
                    (void)slotIndex;
                    if (bindGroupInfo.isRenderSet)
                    {
                        continue;
                    }

                    for (const auto &resourceBinding : bindGroupInfo.resourceBindings)
                    {
                        if (!shouldValidateResourceBinding(resourceBinding))
                        {
                            continue;
                        }

                        std::vector<std::string> path;
                        validateType(resourceBinding.elementType, resourceBinding, bindGroupInfo.typeDecl, path);
                    }
                }
            }

        private:
            const BaseASTVisitor &mVisitor;
            BufferLayoutPolicy mPolicy;
            std::string mBackendName;
            std::string mLayoutName;

            bool shouldValidateResourceBinding(const BaseShaderResourceBinding &resourceBinding) const
            {
                if (resourceBinding.kind == BaseShaderResourceKind::UniformBuffer)
                {
                    return true;
                }
                if (mPolicy == BufferLayoutPolicy::NaturalStruct && resourceBinding.kind == BaseShaderResourceKind::StorageBuffer)
                {
                    return true;
                }
                return false;
            }

            clang::ASTContext &context() const
            {
                return *mVisitor.Context;
            }

            std::string typeName(const clang::QualType &type) const
            {
                if (type.isNull())
                {
                    return "<null>";
                }
                return const_cast<BaseASTVisitor &>(mVisitor).generateTypeCanonicalName(type);
            }

            clang::QualType stripPointerReferenceAndQualifiers(clang::QualType type) const
            {
                if (type.isNull())
                {
                    return type;
                }
                if (type->isReferenceType())
                {
                    type = type.getNonReferenceType();
                }
                if (!type.isNull() && type->isPointerType())
                {
                    type = type->getPointeeType();
                }
                return type.getUnqualifiedType();
            }

            std::optional<ScalarVectorLayoutInfo> classifyScalarVectorType(const clang::QualType &type) const
            {
                const clang::QualType resolvedType = stripPointerReferenceAndQualifiers(type);
                if (resolvedType.isNull())
                {
                    return std::nullopt;
                }

                const std::string generatedName = normalizeUGLScalarVectorCanonicalName(typeName(resolvedType));
                if (auto info = parseScalarVectorTypeName(generatedName))
                {
                    return info;
                }
                if (auto info = parseScalarVectorTypeName(resolvedType.getAsString()))
                {
                    return info;
                }

                const auto *recordType = resolvedType->getAs<clang::RecordType>();
                if (recordType == nullptr)
                {
                    return std::nullopt;
                }

                const auto *specializationDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordType->getDecl());
                if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
                {
                    return std::nullopt;
                }

                const std::string templateName = specializationDecl->getSpecializedTemplate()->getQualifiedNameAsString();
                if (templateName != "glm::vec" && !templateName.ends_with("::vec"))
                {
                    return std::nullopt;
                }

                const clang::TemplateArgumentList &templateArgs = specializationDecl->getTemplateArgs();
                if (templateArgs.size() < 2 || templateArgs.get(0).getKind() != clang::TemplateArgument::Integral || templateArgs.get(1).getKind() != clang::TemplateArgument::Type)
                {
                    return std::nullopt;
                }

                auto scalarInfo = parseScalarVectorTypeName(templateArgs.get(1).getAsType().getAsString());
                if (!scalarInfo)
                {
                    scalarInfo = parseScalarVectorTypeName(typeName(templateArgs.get(1).getAsType()));
                }
                if (!scalarInfo)
                {
                    return std::nullopt;
                }

                const uint64_t componentCount = static_cast<uint64_t>(templateArgs.get(0).getAsIntegral().getZExtValue());
                if (componentCount < 1 || componentCount > 4)
                {
                    return std::nullopt;
                }

                scalarInfo->componentCount = componentCount;
                return scalarInfo;
            }

            bool isBackendOpaqueRecord(const clang::CXXRecordDecl *recordDecl) const
            {
                if (recordDecl == nullptr)
                {
                    return false;
                }
                const std::string qualifiedName = recordDecl->getQualifiedNameAsString();
                return qualifiedName.starts_with("glm::") ||
                       qualifiedName.starts_with("std::") ||
                       qualifiedName.starts_with("UGL::");
            }

            /** Computes UGL matrix storage from its vector dimensions; buffer policies must not reuse the Host size. */
            std::optional<ShaderBackendTypeLayout> computeMatrixLayout(const clang::QualType &type, bool useDslLayout) const
            {
                const auto *recordDecl = stripPointerReferenceAndQualifiers(type)->getAsCXXRecordDecl();
                const auto *specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
                if (specialization == nullptr || specialization->getSpecializedTemplate() == nullptr ||
                    specialization->getSpecializedTemplate()->getQualifiedNameAsString() != "UGL::Matrix")
                {
                    return std::nullopt;
                }
                const clang::TemplateArgumentList &arguments = specialization->getTemplateArgs();
                if (arguments.size() != 3 || arguments[0].getKind() != clang::TemplateArgument::Type ||
                    arguments[1].getKind() != clang::TemplateArgument::Integral || arguments[2].getKind() != clang::TemplateArgument::Integral)
                {
                    return std::nullopt;
                }
                auto scalar = classifyScalarVectorType(arguments[0].getAsType());
                if (!scalar) { return std::nullopt; }
                const uint64_t vectorCount = arguments[1].getAsIntegral().getZExtValue();
                scalar->componentCount = arguments[2].getAsIntegral().getZExtValue();
                uint64_t stride = scalar->componentSize * scalar->componentCount;
                uint64_t alignment = scalar->componentSize;
                if (!useDslLayout)
                {
                    if (mPolicy == BufferLayoutPolicy::PackedRegister16)
                    {
                        stride = alignUpTo(stride, 16);
                        alignment = 16;
                    }
                    else
                    {
                        const ShaderBackendTypeLayout vectorLayout = makeScalarVectorLayout(*scalar);
                        stride = vectorLayout.size;
                        alignment = vectorLayout.align;
                    }
                }
                return ShaderBackendTypeLayout{.size = vectorCount * stride, .align = alignment, .arrayStride = std::nullopt};
            }

            const clang::CXXRecordDecl *getUserRecordDecl(const clang::QualType &type) const
            {
                const clang::QualType resolvedType = stripPointerReferenceAndQualifiers(type);
                if (resolvedType.isNull() || classifyScalarVectorType(resolvedType).has_value())
                {
                    return nullptr;
                }

                const auto *recordDecl = resolvedType->getAsCXXRecordDecl();
                if (recordDecl == nullptr)
                {
                    return nullptr;
                }
                if (const auto *definition = recordDecl->getDefinition())
                {
                    recordDecl = definition;
                }
                recordDecl = recordDecl->getCanonicalDecl();
                if (isBackendOpaqueRecord(recordDecl) || !recordDecl->isCompleteDefinition())
                {
                    return nullptr;
                }
                return recordDecl;
            }

            ShaderBackendTypeLayout makeScalarVectorLayout(const ScalarVectorLayoutInfo &info) const
            {
                if (mPolicy == BufferLayoutPolicy::PackedRegister16)
                {
                    return ShaderBackendTypeLayout{
                        .size = info.componentSize * info.componentCount,
                        .align = info.componentSize,
                        .arrayStride = std::nullopt,
                    };
                }

                uint64_t size = info.componentSize * info.componentCount;
                uint64_t align = info.componentSize;
                if (info.componentCount == 2)
                {
                    align = info.componentSize * 2;
                }
                else if (info.componentCount >= 3)
                {
                    align = info.componentSize * 4;
                    size = info.componentSize * 4;
                }

                return ShaderBackendTypeLayout{
                    .size = size,
                    .align = std::max<uint64_t>(align, 1),
                    .arrayStride = std::nullopt,
                };
            }

            ShaderBackendTypeLayout makeHostEquivalentLayout(const clang::QualType &type) const
            {
                return ShaderBackendTypeLayout{
                    .size = static_cast<uint64_t>(context().getTypeSizeInChars(type).getQuantity()),
                    .align = std::max<uint64_t>(static_cast<uint64_t>(context().getTypeAlignInChars(type).getQuantity()), 1),
                    .arrayStride = std::nullopt,
                };
            }

            bool isArrayType(const clang::QualType &type) const
            {
                if (type.isNull())
                {
                    return false;
                }
                return context().getAsArrayType(stripPointerReferenceAndQualifiers(type)) != nullptr;
            }

            ShaderBackendTypeLayout computeLayout(const clang::QualType &type) const
            {
                const clang::QualType resolvedType = stripPointerReferenceAndQualifiers(type);
                if (resolvedType.isNull())
                {
                    return {};
                }

                if (const auto *arrayType = context().getAsConstantArrayType(resolvedType))
                {
                    const ShaderBackendTypeLayout elementLayout = computeLayout(arrayType->getElementType());
                    const uint64_t elementCount = static_cast<uint64_t>(arrayType->getSize().getZExtValue());
                    const uint64_t stride = mPolicy == BufferLayoutPolicy::PackedRegister16
                                                ? alignUpTo(std::max<uint64_t>(elementLayout.size, 1), 16)
                                                : alignUpTo(elementLayout.size, std::max<uint64_t>(elementLayout.align, 1));
                    return ShaderBackendTypeLayout{
                        .size = stride * elementCount,
                        .align = mPolicy == BufferLayoutPolicy::PackedRegister16 ? 16 : std::max<uint64_t>(elementLayout.align, 1),
                        .arrayStride = stride,
                    };
                }

                if (context().getAsArrayType(resolvedType) != nullptr)
                {
                    return makeHostEquivalentLayout(resolvedType);
                }

                if (auto scalarVectorInfo = classifyScalarVectorType(resolvedType))
                {
                    return makeScalarVectorLayout(*scalarVectorInfo);
                }

                if (auto matrixLayout = computeMatrixLayout(resolvedType, false)) { return *matrixLayout; }

                if (const auto *recordDecl = getUserRecordDecl(resolvedType))
                {
                    return computeRecordLayout(recordDecl).first;
                }

                return makeHostEquivalentLayout(resolvedType);
            }

            ShaderBackendTypeLayout computeDslLayout(const clang::QualType &type) const
            {
                const clang::QualType resolvedType = stripPointerReferenceAndQualifiers(type);
                if (resolvedType.isNull())
                {
                    return {};
                }

                if (const auto *arrayType = context().getAsConstantArrayType(resolvedType))
                {
                    const ShaderBackendTypeLayout elementLayout = computeDslLayout(arrayType->getElementType());
                    const uint64_t elementCount = static_cast<uint64_t>(arrayType->getSize().getZExtValue());
                    const uint64_t stride = std::max<uint64_t>(elementLayout.size, 1);
                    return ShaderBackendTypeLayout{
                        .size = stride * elementCount,
                        .align = std::max<uint64_t>(elementLayout.align, 1),
                        .arrayStride = stride,
                    };
                }

                if (context().getAsArrayType(resolvedType) != nullptr)
                {
                    return makeHostEquivalentLayout(resolvedType);
                }

                if (auto scalarVectorInfo = classifyScalarVectorType(resolvedType))
                {
                    return ShaderBackendTypeLayout{
                        .size = scalarVectorInfo->componentSize * scalarVectorInfo->componentCount,
                        .align = scalarVectorInfo->componentSize,
                        .arrayStride = std::nullopt,
                    };
                }

                if (const auto *recordDecl = getUserRecordDecl(resolvedType))
                {
                    return computeDslRecordLayout(recordDecl).first;
                }

                if (auto matrixLayout = computeMatrixLayout(resolvedType, true)) { return *matrixLayout; }

                return makeHostEquivalentLayout(resolvedType);
            }

            std::pair<ShaderBackendTypeLayout, std::vector<ShaderBackendFieldLayout>> computeRecordLayout(const clang::CXXRecordDecl *recordDecl) const
            {
                uint64_t offset = 0;
                uint64_t maxAlign = 1;
                std::vector<ShaderBackendFieldLayout> fields;

                for (const auto *field : recordDecl->fields())
                {
                    if (field == nullptr)
                    {
                        continue;
                    }

                    const ShaderBackendTypeLayout fieldLayout = computeLayout(field->getType());
                    if (mPolicy == BufferLayoutPolicy::PackedRegister16)
                    {
                        const bool forceRegisterBoundary = isArrayType(field->getType()) || getUserRecordDecl(field->getType()) != nullptr;
                        if (forceRegisterBoundary)
                        {
                            offset = alignUpTo(offset, 16);
                        }
                        else
                        {
                            if ((offset % 16) + fieldLayout.size > 16)
                            {
                                offset = alignUpTo(offset, 16);
                            }
                            offset = alignUpTo(offset, std::max<uint64_t>(fieldLayout.align, 1));
                        }
                        maxAlign = 16;
                    }
                    else
                    {
                        offset = alignUpTo(offset, std::max<uint64_t>(fieldLayout.align, 1));
                        maxAlign = std::max(maxAlign, std::max<uint64_t>(fieldLayout.align, 1));
                    }

                    fields.push_back(ShaderBackendFieldLayout{
                        .field = field,
                        .offset = offset,
                        .typeLayout = fieldLayout,
                    });
                    offset += fieldLayout.size;
                }

                const ShaderBackendTypeLayout recordLayout{
                    .size = alignUpTo(offset, maxAlign),
                    .align = maxAlign,
                    .arrayStride = std::nullopt,
                };
                return {recordLayout, fields};
            }

            std::pair<ShaderBackendTypeLayout, std::vector<ShaderBackendFieldLayout>> computeDslRecordLayout(const clang::CXXRecordDecl *recordDecl) const
            {
                uint64_t offset = 0;
                uint64_t maxAlign = 1;
                std::vector<ShaderBackendFieldLayout> fields;

                for (const auto *field : recordDecl->fields())
                {
                    if (field == nullptr)
                    {
                        continue;
                    }

                    const ShaderBackendTypeLayout fieldLayout = computeDslLayout(field->getType());
                    offset = alignUpTo(offset, std::max<uint64_t>(fieldLayout.align, 1));
                    maxAlign = std::max(maxAlign, std::max<uint64_t>(fieldLayout.align, 1));
                    fields.push_back(ShaderBackendFieldLayout{
                        .field = field,
                        .offset = offset,
                        .typeLayout = fieldLayout,
                    });
                    offset += fieldLayout.size;
                }

                const ShaderBackendTypeLayout recordLayout{
                    .size = alignUpTo(offset, maxAlign),
                    .align = maxAlign,
                    .arrayStride = std::nullopt,
                };
                return {recordLayout, fields};
            }

            std::string wrapperTypeName(const BaseShaderResourceBinding &resourceBinding) const
            {
                if (resourceBinding.kind == BaseShaderResourceKind::UniformBuffer)
                {
                    return "UniformBuffer";
                }
                if (resourceBinding.access == BaseShaderResourceAccess::ReadWrite)
                {
                    return "RWStructuredBuffer";
                }
                return "StructuredBuffer";
            }

            std::string suggestionForPolicy() const
            {
                if (mPolicy == BufferLayoutPolicy::PackedRegister16)
                {
                    return "HLSL constant-buffer layout is register packed: array elements consume a 16-byte register stride, while scalars/vectors share a register only when they do not cross a 16-byte boundary. Replace scalar padding arrays such as float pad[3] with three explicit scalar members, or use float4/explicit backend-neutral padding so the C++ and shader offsets are identical.";
                }
                return "Metal buffer structs use natural Metal alignment: float3/int3/uint3 occupy a four-component slot in arrays and structs. Use float4, split the value into scalar fields, or add explicit backend-neutral padding so the C++ and shader offsets are identical.";
            }

            [[noreturn]] void throwMismatch(const BaseShaderResourceBinding &resourceBinding,
                                            const clang::CXXRecordDecl *bindGroupDecl,
                                            const std::vector<std::string> &path,
                                            const std::string &what,
                                            uint64_t hostValue,
                                            uint64_t backendValue) const
            {
                const std::string bindGroupName = bindGroupDecl == nullptr ? "<unknown>" : bindGroupDecl->getQualifiedNameAsString();
                const std::string fieldName = resourceBinding.fieldDecl == nullptr ? "<unknown>" : resourceBinding.fieldDecl->getNameAsString();

                std::ostringstream message;
                message << mBackendName << " " << wrapperTypeName(resourceBinding)
                        << " buffer layout mismatch for BindGroup \"" << bindGroupName
                        << "\" field \"" << fieldName
                        << "\" element type \"" << (resourceBinding.elementTypeName.empty() ? typeName(resourceBinding.elementType) : resourceBinding.elementTypeName)
                        << "\" at member path \"" << buildShaderTypePath(path)
                        << "\": DSL/C++ " << what << " = " << hostValue
                        << " bytes, but " << mLayoutName << " " << what << " = " << backendValue
                        << " bytes. " << suggestionForPolicy();
                throw std::runtime_error(message.str());
            }

            void validateType(const clang::QualType &type,
                              const BaseShaderResourceBinding &resourceBinding,
                              const clang::CXXRecordDecl *bindGroupDecl,
                              std::vector<std::string> &path) const
            {
                const clang::QualType resolvedType = stripPointerReferenceAndQualifiers(type);
                if (resolvedType.isNull())
                {
                    return;
                }

                const ShaderBackendTypeLayout dslLayout = computeDslLayout(resolvedType);
                const ShaderBackendTypeLayout backendLayout = computeLayout(resolvedType);

                if (const auto *arrayType = context().getAsConstantArrayType(resolvedType))
                {
                    if (!backendLayout.arrayStride || !dslLayout.arrayStride)
                    {
                        return;
                    }
                    if (*dslLayout.arrayStride != *backendLayout.arrayStride)
                    {
                        throwMismatch(resourceBinding, bindGroupDecl, path, "array stride", *dslLayout.arrayStride, *backendLayout.arrayStride);
                    }

                    path.emplace_back("[]");
                    validateType(arrayType->getElementType(), resourceBinding, bindGroupDecl, path);
                    path.pop_back();

                    if (dslLayout.size != backendLayout.size)
                    {
                        throwMismatch(resourceBinding, bindGroupDecl, path, "size", dslLayout.size, backendLayout.size);
                    }
                    return;
                }

                const auto *recordDecl = getUserRecordDecl(resolvedType);
                if (recordDecl != nullptr)
                {
                    const auto [dslRecordLayout, dslFields] = computeDslRecordLayout(recordDecl);
                    const auto [recordLayout, backendFields] = computeRecordLayout(recordDecl);

                    for (const auto *field : recordDecl->fields())
                    {
                        if (field == nullptr)
                        {
                            continue;
                        }
                        const auto dslFieldLayoutIter = std::find_if(dslFields.begin(), dslFields.end(), [field](const ShaderBackendFieldLayout &layout)
                                                                     { return layout.field == field; });
                        const auto fieldLayoutIter = std::find_if(backendFields.begin(), backendFields.end(), [field](const ShaderBackendFieldLayout &layout)
                                                                  { return layout.field == field; });
                        if (dslFieldLayoutIter == dslFields.end() || fieldLayoutIter == backendFields.end())
                        {
                            continue;
                        }

                        path.emplace_back(field->getNameAsString());
                        if (dslFieldLayoutIter->offset != fieldLayoutIter->offset)
                        {
                            throwMismatch(resourceBinding, bindGroupDecl, path, "offset", dslFieldLayoutIter->offset, fieldLayoutIter->offset);
                        }
                        validateType(field->getType(), resourceBinding, bindGroupDecl, path);
                        path.pop_back();
                    }

                    if (dslRecordLayout.size != recordLayout.size)
                    {
                        const std::vector<std::string> sizeMismatchPath = pathForRecordSizeMismatch(recordDecl, path);
                        throwMismatch(resourceBinding, bindGroupDecl, sizeMismatchPath, "size", dslRecordLayout.size, recordLayout.size);
                    }
                    return;
                }

                if (dslLayout.size != backendLayout.size)
                {
                    throwMismatch(resourceBinding, bindGroupDecl, path, "size", dslLayout.size, backendLayout.size);
                }
            }
        };
    } // namespace

    void validateHLSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap)
    {
        if (visitor.Context == nullptr)
        {
            return;
        }
        ShaderBufferLayoutValidator validator(visitor,
                                              BufferLayoutPolicy::PackedRegister16,
                                              "HLSL/Vulkan",
                                              "HLSL constant-buffer");
        validator.validateBindGroupInfoMap(bindGroupInfoMap);
    }

    void validateMSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap)
    {
        if (visitor.Context == nullptr)
        {
            return;
        }
        ShaderBufferLayoutValidator validator(visitor,
                                              BufferLayoutPolicy::NaturalStruct,
                                              "MSL/Metal",
                                              "Metal buffer-struct");
        validator.validateBindGroupInfoMap(bindGroupInfoMap);
    }
} // namespace UGLC::CodeGen
