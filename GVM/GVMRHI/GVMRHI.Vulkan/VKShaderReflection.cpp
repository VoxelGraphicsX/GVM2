#include "VKShaderReflection.hpp"

#include "VKBindGroupLayout.hpp"
#include "VKEnumUtils.hpp"
#include "VKPipelineLayout.hpp"

#define SPV_ENABLE_UTILITY_CODE
#include <spirv_reflect.h>
#undef SPV_ENABLE_UTILITY_CODE

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <stdexcept>
#include <EASTL/string.h>
#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>

namespace GVM::RHI::Vulkan::Detail
{
    namespace
    {
        /** Tracks subgroup instructions reachable through one SPIR-V function's direct calls. */
        struct SubgroupFunctionRequirements
        {
            bool usesSubgroups = false;
            eastl::vector<uint32_t> callees;
        };

        [[noreturn]]
        void throwInvalidArgument(const char *ownerName, const eastl::string &message)
        {
            throw makeInvalidArgument(eastl::string(ownerName) + ": " + message);
        }

        [[noreturn]]
        void throwReflectFailure(const char *operation, SpvReflectResult result)
        {
            throw makeInvalidArgument(eastl::string("SPIR-V reflection failed during ") + operation + " (result=" + eastl::to_string(static_cast<int>(result)) + ").");
        }

        vk::ShaderStageFlagBits translateReflectShaderStage(SpvReflectShaderStageFlagBits stage)
        {
            switch (stage)
            {
            case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: return vk::ShaderStageFlagBits::eVertex;
            case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return vk::ShaderStageFlagBits::eTessellationControl;
            case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return vk::ShaderStageFlagBits::eTessellationEvaluation;
            case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT: return vk::ShaderStageFlagBits::eGeometry;
            case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: return vk::ShaderStageFlagBits::eFragment;
            case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT: return vk::ShaderStageFlagBits::eCompute;
            default: throw makeInvalidArgument("SPIR-V reflection encountered an unsupported Vulkan shader stage.");
            }
        }

        const char *getShaderStageName(vk::ShaderStageFlagBits stage)
        {
            switch (stage)
            {
            case vk::ShaderStageFlagBits::eVertex: return "vertex";
            case vk::ShaderStageFlagBits::eTessellationControl: return "tessellation_control";
            case vk::ShaderStageFlagBits::eTessellationEvaluation: return "tessellation_evaluation";
            case vk::ShaderStageFlagBits::eGeometry: return "geometry";
            case vk::ShaderStageFlagBits::eFragment: return "fragment";
            case vk::ShaderStageFlagBits::eCompute: return "compute";
            default: return "unknown";
            }
        }

        vk::DescriptorType translateReflectDescriptorType(SpvReflectDescriptorType descriptorType)
        {
            switch (descriptorType)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return vk::DescriptorType::eSampler;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return vk::DescriptorType::eCombinedImageSampler;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return vk::DescriptorType::eSampledImage;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return vk::DescriptorType::eStorageImage;
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return vk::DescriptorType::eInputAttachment;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return vk::DescriptorType::eUniformBuffer;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: return vk::DescriptorType::eStorageBuffer;
            default: throw makeInvalidArgument("SPIR-V reflection encountered a descriptor type that is unsupported by the current GVM Vulkan backend.");
            }
        }

        TextureViewDimension translateReflectTextureViewDimension(const SpvReflectImageTraits &imageTraits)
        {
            switch (imageTraits.dim)
            {
            case SpvDim1D: return imageTraits.arrayed != 0u ? TextureViewDimension::Undefined : TextureViewDimension::e1D;
            case SpvDim2D: return imageTraits.arrayed != 0u ? TextureViewDimension::e2DArray : TextureViewDimension::e2D;
            case SpvDim3D: return TextureViewDimension::e3D;
            case SpvDimCube: return imageTraits.arrayed != 0u ? TextureViewDimension::CubeArray : TextureViewDimension::Cube;
            default: return TextureViewDimension::Undefined;
            }
        }

        vk::Format translateReflectImageFormat(SpvImageFormat imageFormat)
        {
            switch (imageFormat)
            {
            case SpvImageFormatUnknown: return vk::Format::eUndefined;
            case SpvImageFormatRgba32f: return vk::Format::eR32G32B32A32Sfloat;
            case SpvImageFormatRgba16f: return vk::Format::eR16G16B16A16Sfloat;
            case SpvImageFormatR32f: return vk::Format::eR32Sfloat;
            case SpvImageFormatRgba8: return vk::Format::eR8G8B8A8Unorm;
            case SpvImageFormatRgba8Snorm: return vk::Format::eR8G8B8A8Snorm;
            case SpvImageFormatRg32f: return vk::Format::eR32G32Sfloat;
            case SpvImageFormatRg16f: return vk::Format::eR16G16Sfloat;
            case SpvImageFormatR11fG11fB10f: return vk::Format::eB10G11R11UfloatPack32;
            case SpvImageFormatR16f: return vk::Format::eR16Sfloat;
            case SpvImageFormatRgba16: return vk::Format::eR16G16B16A16Unorm;
            case SpvImageFormatRgb10A2: return vk::Format::eA2B10G10R10UnormPack32;
            case SpvImageFormatRg16: return vk::Format::eR16G16Unorm;
            case SpvImageFormatRg8: return vk::Format::eR8G8Unorm;
            case SpvImageFormatR16: return vk::Format::eR16Unorm;
            case SpvImageFormatR8: return vk::Format::eR8Unorm;
            case SpvImageFormatRgba16Snorm: return vk::Format::eR16G16B16A16Snorm;
            case SpvImageFormatRg16Snorm: return vk::Format::eR16G16Snorm;
            case SpvImageFormatRg8Snorm: return vk::Format::eR8G8Snorm;
            case SpvImageFormatR16Snorm: return vk::Format::eR16Snorm;
            case SpvImageFormatR8Snorm: return vk::Format::eR8Snorm;
            case SpvImageFormatRgba32i: return vk::Format::eR32G32B32A32Sint;
            case SpvImageFormatRgba16i: return vk::Format::eR16G16B16A16Sint;
            case SpvImageFormatRgba8i: return vk::Format::eR8G8B8A8Sint;
            case SpvImageFormatR32i: return vk::Format::eR32Sint;
            case SpvImageFormatRg32i: return vk::Format::eR32G32Sint;
            case SpvImageFormatRg16i: return vk::Format::eR16G16Sint;
            case SpvImageFormatRg8i: return vk::Format::eR8G8Sint;
            case SpvImageFormatR16i: return vk::Format::eR16Sint;
            case SpvImageFormatR8i: return vk::Format::eR8Sint;
            case SpvImageFormatRgba32ui: return vk::Format::eR32G32B32A32Uint;
            case SpvImageFormatRgba16ui: return vk::Format::eR16G16B16A16Uint;
            case SpvImageFormatRgba8ui: return vk::Format::eR8G8B8A8Uint;
            case SpvImageFormatR32ui: return vk::Format::eR32Uint;
            case SpvImageFormatRgb10a2ui: return vk::Format::eA2B10G10R10UintPack32;
            case SpvImageFormatRg32ui: return vk::Format::eR32G32Uint;
            case SpvImageFormatRg16ui: return vk::Format::eR16G16Uint;
            case SpvImageFormatRg8ui: return vk::Format::eR8G8Uint;
            case SpvImageFormatR16ui: return vk::Format::eR16Uint;
            case SpvImageFormatR8ui: return vk::Format::eR8Uint;
            default:
                throw makeInvalidArgument("SPIR-V reflection encountered a storage image format that is unsupported by the current GVM Vulkan backend.");
            }
        }

        StorageBufferAccess translateReflectStorageBufferAccess(SpvReflectDecorationFlags decorationFlags)
        {
            const bool nonWritable = (decorationFlags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0u;
            const bool nonReadable = (decorationFlags & SPV_REFLECT_DECORATION_NON_READABLE) != 0u;
            if (nonWritable && nonReadable)
            {
                return StorageBufferAccess::Undefined;
            }
            if (nonWritable)
            {
                return StorageBufferAccess::ReadOnly;
            }
            if (nonReadable)
            {
                return StorageBufferAccess::WriteOnly;
            }
            return StorageBufferAccess::ReadWrite;
        }

        StorageTextureAccess translateReflectStorageTextureAccess(SpvReflectDecorationFlags decorationFlags)
        {
            const bool nonWritable = (decorationFlags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0u;
            const bool nonReadable = (decorationFlags & SPV_REFLECT_DECORATION_NON_READABLE) != 0u;
            if (nonWritable && nonReadable)
            {
                return StorageTextureAccess::Undefined;
            }
            if (nonWritable)
            {
                return StorageTextureAccess::ReadOnly;
            }
            if (nonReadable)
            {
                return StorageTextureAccess::WriteOnly;
            }
            return StorageTextureAccess::ReadWrite;
        }

        bool isRuntimeDescriptorArray(const SpvReflectDescriptorBinding &binding)
        {
            for (uint32_t index = 0; index < binding.array.dims_count; ++index)
            {
                if (binding.array.dims[index] == SPV_REFLECT_ARRAY_DIM_RUNTIME)
                {
                    return true;
                }
            }
            return false;
        }

        bool storageBufferAccessSatisfies(StorageBufferAccess layoutAccess, StorageBufferAccess shaderAccess)
        {
            switch (shaderAccess)
            {
            case StorageBufferAccess::ReadOnly:
                return layoutAccess == StorageBufferAccess::ReadOnly || layoutAccess == StorageBufferAccess::ReadWrite;
            case StorageBufferAccess::WriteOnly:
                return layoutAccess == StorageBufferAccess::WriteOnly || layoutAccess == StorageBufferAccess::ReadWrite;
            case StorageBufferAccess::ReadWrite:
                return layoutAccess == StorageBufferAccess::ReadWrite;
            default:
                return false;
            }
        }

        bool storageTextureAccessSatisfies(StorageTextureAccess layoutAccess, StorageTextureAccess shaderAccess)
        {
            switch (shaderAccess)
            {
            case StorageTextureAccess::ReadOnly:
                return layoutAccess == StorageTextureAccess::ReadOnly || layoutAccess == StorageTextureAccess::ReadWrite;
            case StorageTextureAccess::WriteOnly:
                return layoutAccess == StorageTextureAccess::WriteOnly || layoutAccess == StorageTextureAccess::ReadWrite;
            case StorageTextureAccess::ReadWrite:
                return layoutAccess == StorageTextureAccess::ReadWrite;
            default:
                return false;
            }
        }

        const vk::DescriptorSetLayoutBinding *findNativeLayoutBinding(const VKBindGroupLayout &layout, uint32_t binding)
        {
            for (const vk::DescriptorSetLayoutBinding &nativeBinding : layout.getNativeBindings())
            {
                if (nativeBinding.binding == binding)
                {
                    return &nativeBinding;
                }
            }
            return nullptr;
        }

        const BindGroupLayoutEntry *findLayoutEntry(const VKBindGroupLayout &layout, uint32_t binding)
        {
            for (const BindGroupLayoutEntry &entry : layout.getDescriptor().entries)
            {
                if (entry.binding == binding)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        eastl::string decodeSpirvStringLiteral(const uint32_t *words, uint32_t wordCount)
        {
            eastl::string result;
            result.reserve(wordCount * sizeof(uint32_t));
            for (uint32_t wordIndex = 0; wordIndex < wordCount; ++wordIndex)
            {
                const uint32_t word = words[wordIndex];
                for (uint32_t byteIndex = 0; byteIndex < sizeof(uint32_t); ++byteIndex)
                {
                    const char ch = static_cast<char>((word >> (byteIndex * 8u)) & 0xffu);
                    if (ch == '\0')
                    {
                        return result;
                    }
                    result.push_back(ch);
                }
            }
            return result;
        }

        void reflectModuleRequirements(const eastl::vector<uint32_t> &spirv, ReflectedShaderModule &reflection)
        {
            if (spirv.size() < 5u)
            {
                throw makeInvalidArgument("SPIR-V reflection requires a valid SPIR-V header.");
            }

            eastl::hash_map<uint32_t, SubgroupFunctionRequirements> subgroupFunctions;
            eastl::hash_set<uint32_t> subgroupBuiltins;
            eastl::hash_set<uint32_t> extendedTypes;
            eastl::hash_map<uint32_t, uint32_t> valueTypes;
            uint32_t currentFunction = 0;
            uint32_t wordIndex = 5u;
            while (wordIndex < spirv.size())
            {
                const uint32_t instruction = spirv[wordIndex];
                const uint32_t wordCount = instruction >> 16u;
                const uint32_t opcode = instruction & 0xffffu;
                if (wordCount == 0u || (wordIndex + wordCount) > spirv.size())
                {
                    throw makeInvalidArgument("SPIR-V reflection encountered a malformed instruction while scanning module requirements.");
                }
                if (((opcode == SpvOpTypeFloat || opcode == SpvOpTypeInt) && wordCount >= 3u && spirv[wordIndex + 2u] != 32u) ||
                    (opcode == SpvOpTypeVector && wordCount >= 4u && extendedTypes.count(spirv[wordIndex + 2u]) != 0))
                {
                    extendedTypes.insert(spirv[wordIndex + 1u]);
                }
                bool hasResult = false;
                bool hasResultType = false;
                SpvHasResultAndType(static_cast<SpvOp>(opcode), &hasResult, &hasResultType);
                if (hasResult && hasResultType && wordCount >= 3u) { valueTypes[spirv[wordIndex + 2u]] = spirv[wordIndex + 1u]; }
                if (opcode >= SpvOpGroupNonUniformElect && opcode <= SpvOpGroupNonUniformQuadSwap && wordCount >= 3u)
                {
                    if (extendedTypes.count(spirv[wordIndex + 1u]) != 0) { reflection.usesSubgroupExtendedTypes = true; }
                    if (opcode == SpvOpGroupNonUniformAllEqual && wordCount >= 5u)
                    {
                        const auto type = valueTypes.find(spirv[wordIndex + 4u]);
                        if (type != valueTypes.end() && extendedTypes.count(type->second) != 0) { reflection.usesSubgroupExtendedTypes = true; }
                    }
                }

                if (opcode == SpvOpCapability && wordCount >= 2u)
                {
                    const SpvCapability capability = static_cast<SpvCapability>(spirv[wordIndex + 1u]);
                    switch (capability)
                    {
                    case SpvCapabilityGroupNonUniform: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eBasic; break;
                    case SpvCapabilityGroupNonUniformVote: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eVote; break;
                    case SpvCapabilityGroupNonUniformArithmetic: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eArithmetic; break;
                    case SpvCapabilityGroupNonUniformBallot: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eBallot; break;
                    case SpvCapabilityGroupNonUniformShuffle: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eShuffle; break;
                    case SpvCapabilityGroupNonUniformShuffleRelative: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eShuffleRelative; break;
                    case SpvCapabilityGroupNonUniformClustered: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eClustered; break;
                    case SpvCapabilityGroupNonUniformQuad: reflection.requiredSubgroupOperations |= vk::SubgroupFeatureFlagBits::eQuad; break;
                    case SpvCapabilityStorageImageExtendedFormats: reflection.usesStorageImageExtendedFormats = true; break;
                    case SpvCapabilityImageGatherExtended: reflection.usesImageGatherExtended = true; break;
                    default: break;
                    }
                    if (capability == SpvCapabilityGeometry)
                    {
                        reflection.usesGeometryCapability = true;
                    }
                    if (capability == SpvCapabilityTessellation)
                    {
                        reflection.usesTessellationCapability = true;
                    }
                    if (capability == SpvCapabilityFloat16)
                    {
                        reflection.usesFloat16Capability = true;
                    }
                    if (capability == SpvCapabilityStorageBuffer16BitAccess)
                    {
                        reflection.usesStorageBuffer16BitAccess = true;
                    }
                    if (capability == SpvCapabilityUniformAndStorageBuffer16BitAccess)
                    {
                        reflection.usesUniformAndStorageBuffer16BitAccess = true;
                    }
                    if (capability == SpvCapabilityStoragePushConstant16)
                    {
                        reflection.usesStoragePushConstant16 = true;
                    }
                    if (capability == SpvCapabilityStorageInputOutput16)
                    {
                        reflection.usesStorageInputOutput16 = true;
                    }
                    if (capability == SpvCapabilityFragmentBarycentricKHR || capability == SpvCapabilityFragmentBarycentricNV)
                    {
                        reflection.usesFragmentShaderBarycentricCapability = true;
                    }
                }
                else if (opcode == SpvOpExtension && wordCount >= 2u)
                {
                    const eastl::string extensionName = decodeSpirvStringLiteral(spirv.data() + wordIndex + 1u, wordCount - 1u);
                    if (extensionName == "SPV_KHR_fragment_shader_barycentric" || extensionName == "SPV_NV_fragment_shader_barycentric")
                    {
                        reflection.usesFragmentShaderBarycentricExtension = true;
                    }
                }

                if (opcode == SpvOpDecorate && wordCount >= 4u && spirv[wordIndex + 2u] == SpvDecorationBuiltIn)
                {
                    const uint32_t builtin = spirv[wordIndex + 3u];
                    if (builtin == SpvBuiltInSubgroupSize || builtin == SpvBuiltInSubgroupLocalInvocationId ||
                        builtin == SpvBuiltInNumSubgroups || builtin == SpvBuiltInSubgroupId)
                    {
                        subgroupBuiltins.insert(spirv[wordIndex + 1u]);
                    }
                }
                if (opcode == SpvOpFunction && wordCount >= 5u)
                {
                    currentFunction = spirv[wordIndex + 2u];
                    subgroupFunctions[currentFunction];
                }
                else if (opcode == SpvOpFunctionEnd) { currentFunction = 0; }
                else if (currentFunction != 0)
                {
                    SubgroupFunctionRequirements &requirements = subgroupFunctions[currentFunction];
                    if ((opcode >= SpvOpGroupNonUniformElect && opcode <= SpvOpGroupNonUniformQuadSwap) ||
                        (opcode == SpvOpLoad && wordCount >= 4u && subgroupBuiltins.count(spirv[wordIndex + 3u]) != 0))
                    {
                        requirements.usesSubgroups = true;
                    }
                    if (opcode == SpvOpFunctionCall && wordCount >= 4u) { requirements.callees.push_back(spirv[wordIndex + 3u]); }
                }
                wordIndex += wordCount;
            }
            bool changed = false;
            do
            {
                changed = false;
                for (auto &entry : subgroupFunctions)
                {
                    if (entry.second.usesSubgroups) { continue; }
                    for (uint32_t callee : entry.second.callees)
                    {
                        const auto found = subgroupFunctions.find(callee);
                        if (found != subgroupFunctions.end() && found->second.usesSubgroups)
                        {
                            entry.second.usesSubgroups = true;
                            changed = true;
                            break;
                        }
                    }
                }
            } while (changed);
            for (ReflectedEntryPoint &entry : reflection.entryPoints)
            {
                const auto found = subgroupFunctions.find(entry.functionId);
                entry.usesSubgroupOperations = found != subgroupFunctions.end() && found->second.usesSubgroups;
            }
        }

        ReflectedDescriptorBinding reflectDescriptorBinding(const SpvReflectDescriptorBinding &binding)
        {
            ReflectedDescriptorBinding reflected = {};
            reflected.name = binding.name == nullptr ? "" : binding.name;
            reflected.set = binding.set;
            reflected.binding = binding.binding;
            reflected.descriptorType = translateReflectDescriptorType(binding.descriptor_type);
            reflected.isRuntimeDescriptorArray = isRuntimeDescriptorArray(binding);
            reflected.inputAttachmentIndex = binding.input_attachment_index;

            if (reflected.descriptorType == vk::DescriptorType::eCombinedImageSampler)
            {
                throw makeInvalidArgument("SPIR-V reflection encountered a combined image sampler, which is unsupported by the current GVM Vulkan binding model.");
            }
            if (!reflected.isRuntimeDescriptorArray && binding.count == 0u)
            {
                throw makeInvalidArgument("SPIR-V reflection encountered a descriptor binding with count 0.");
            }

            reflected.descriptorCount = reflected.isRuntimeDescriptorArray ? 0u : binding.count;

            if (reflected.descriptorType == vk::DescriptorType::eSampledImage ||
                reflected.descriptorType == vk::DescriptorType::eStorageImage)
            {
                reflected.textureViewDimension = translateReflectTextureViewDimension(binding.image);
                if (reflected.textureViewDimension == TextureViewDimension::Undefined)
                {
                    throw makeInvalidArgument("SPIR-V reflection encountered an image descriptor with an unsupported view dimension.");
                }
            }

            if (reflected.descriptorType == vk::DescriptorType::eStorageImage)
            {
                reflected.imageFormat = translateReflectImageFormat(binding.image.image_format);
                reflected.storageTextureAccess = translateReflectStorageTextureAccess(binding.decoration_flags);
            }
            else if (reflected.descriptorType == vk::DescriptorType::eStorageBuffer)
            {
                reflected.storageBufferAccess = translateReflectStorageBufferAccess(binding.decoration_flags);
            }

            return reflected;
        }

        bool isUserInterfaceVariable(const SpvReflectInterfaceVariable &variable)
        {
            return variable.built_in < 0 && variable.location != eastl::numeric_limits<uint32_t>::max();
        }

        ReflectedInterfaceVariable reflectInterfaceVariable(const SpvReflectInterfaceVariable &variable)
        {
            if (!isUserInterfaceVariable(variable))
            {
                throw makeInvalidArgument("SPIR-V reflection attempted to reflect a built-in interface variable as a user-defined variable.");
            }
            if (variable.format == SPV_REFLECT_FORMAT_UNDEFINED)
            {
                throw makeInvalidArgument("SPIR-V reflection encountered an interface variable with an undefined format.");
            }

            ReflectedInterfaceVariable reflected = {};
            reflected.name = variable.name == nullptr ? "" : variable.name;
            reflected.location = variable.location;
            reflected.format = static_cast<vk::Format>(variable.format);
            return reflected;
        }

        template <typename T>
        void sortAndValidateUniqueLocations(eastl::vector<T> &variables, const char *kind)
        {
            eastl::sort(variables.begin(), variables.end(), [](const T &lhs, const T &rhs)
            {
                return lhs.location < rhs.location;
            });

            for (size_t index = 1; index < variables.size(); ++index)
            {
                if (variables[index - 1].location == variables[index].location)
                {
                    throw makeInvalidArgument(eastl::string("SPIR-V reflection encountered duplicate ") + kind + " interface locations.");
                }
            }
        }

        enum class NumericClass
        {
            Undefined,
            Float,
            Sint,
            Uint,
        };

        NumericClass classifyInterfaceFormat(vk::Format format)
        {
            switch (format)
            {
            case vk::Format::eR32Sfloat:
            case vk::Format::eR32G32Sfloat:
            case vk::Format::eR32G32B32Sfloat:
            case vk::Format::eR32G32B32A32Sfloat:
            case vk::Format::eR16Sfloat:
            case vk::Format::eR16G16Sfloat:
            case vk::Format::eR16G16B16A16Sfloat:
                return NumericClass::Float;
            case vk::Format::eR32Sint:
            case vk::Format::eR32G32Sint:
            case vk::Format::eR32G32B32Sint:
            case vk::Format::eR32G32B32A32Sint:
            case vk::Format::eR16Sint:
            case vk::Format::eR16G16Sint:
            case vk::Format::eR16G16B16A16Sint:
            case vk::Format::eR8Sint:
            case vk::Format::eR8G8Sint:
            case vk::Format::eR8G8B8A8Sint:
                return NumericClass::Sint;
            case vk::Format::eR32Uint:
            case vk::Format::eR32G32Uint:
            case vk::Format::eR32G32B32Uint:
            case vk::Format::eR32G32B32A32Uint:
            case vk::Format::eR16Uint:
            case vk::Format::eR16G16Uint:
            case vk::Format::eR16G16B16A16Uint:
            case vk::Format::eR8Uint:
            case vk::Format::eR8G8Uint:
            case vk::Format::eR8G8B8A8Uint:
                return NumericClass::Uint;
            default:
                return NumericClass::Undefined;
            }
        }

        NumericClass classifyVertexAttributeFormat(vk::Format format)
        {
            switch (format)
            {
            case vk::Format::eR32Sfloat:
            case vk::Format::eR32G32Sfloat:
            case vk::Format::eR32G32B32Sfloat:
            case vk::Format::eR32G32B32A32Sfloat:
            case vk::Format::eR16Sfloat:
            case vk::Format::eR16G16Sfloat:
            case vk::Format::eR16G16B16A16Sfloat:
            case vk::Format::eR8Unorm:
            case vk::Format::eR8G8Unorm:
            case vk::Format::eR8G8B8A8Unorm:
            case vk::Format::eR8Snorm:
            case vk::Format::eR8G8Snorm:
            case vk::Format::eR8G8B8A8Snorm:
            case vk::Format::eR16Unorm:
            case vk::Format::eR16G16Unorm:
            case vk::Format::eR16G16B16A16Unorm:
            case vk::Format::eR16Snorm:
            case vk::Format::eR16G16Snorm:
            case vk::Format::eR16G16B16A16Snorm:
                return NumericClass::Float;
            case vk::Format::eR32Sint:
            case vk::Format::eR32G32Sint:
            case vk::Format::eR32G32B32Sint:
            case vk::Format::eR32G32B32A32Sint:
            case vk::Format::eR16Sint:
            case vk::Format::eR16G16Sint:
            case vk::Format::eR16G16B16A16Sint:
            case vk::Format::eR8Sint:
            case vk::Format::eR8G8Sint:
            case vk::Format::eR8G8B8A8Sint:
                return NumericClass::Sint;
            case vk::Format::eR32Uint:
            case vk::Format::eR32G32Uint:
            case vk::Format::eR32G32B32Uint:
            case vk::Format::eR32G32B32A32Uint:
            case vk::Format::eR16Uint:
            case vk::Format::eR16G16Uint:
            case vk::Format::eR16G16B16A16Uint:
            case vk::Format::eR8Uint:
            case vk::Format::eR8G8Uint:
            case vk::Format::eR8G8B8A8Uint:
                return NumericClass::Uint;
            default:
                return NumericClass::Undefined;
            }
        }

        uint32_t getFormatComponentCount(vk::Format format)
        {
            switch (format)
            {
            case vk::Format::eR8Unorm:
            case vk::Format::eR8Snorm:
            case vk::Format::eR8Uint:
            case vk::Format::eR8Sint:
            case vk::Format::eR16Unorm:
            case vk::Format::eR16Snorm:
            case vk::Format::eR16Uint:
            case vk::Format::eR16Sint:
            case vk::Format::eR16Sfloat:
            case vk::Format::eR32Sfloat:
            case vk::Format::eR32Uint:
            case vk::Format::eR32Sint:
                return 1u;
            case vk::Format::eR8G8Unorm:
            case vk::Format::eR8G8Snorm:
            case vk::Format::eR8G8Uint:
            case vk::Format::eR8G8Sint:
            case vk::Format::eR16G16Unorm:
            case vk::Format::eR16G16Snorm:
            case vk::Format::eR16G16Uint:
            case vk::Format::eR16G16Sint:
            case vk::Format::eR16G16Sfloat:
            case vk::Format::eR32G32Sfloat:
            case vk::Format::eR32G32Uint:
            case vk::Format::eR32G32Sint:
                return 2u;
            case vk::Format::eR32G32B32Sfloat:
            case vk::Format::eR32G32B32Uint:
            case vk::Format::eR32G32B32Sint:
                return 3u;
            case vk::Format::eR8G8B8A8Unorm:
            case vk::Format::eR8G8B8A8Snorm:
            case vk::Format::eR8G8B8A8Uint:
            case vk::Format::eR8G8B8A8Sint:
            case vk::Format::eR16G16B16A16Unorm:
            case vk::Format::eR16G16B16A16Snorm:
            case vk::Format::eR16G16B16A16Uint:
            case vk::Format::eR16G16B16A16Sint:
            case vk::Format::eR16G16B16A16Sfloat:
            case vk::Format::eR32G32B32A32Sfloat:
            case vk::Format::eR32G32B32A32Uint:
            case vk::Format::eR32G32B32A32Sint:
                return 4u;
            default:
                return 0u;
            }
        }

        bool isVertexAttributeFormatCompatible(vk::Format attributeFormat, vk::Format reflectedFormat)
        {
            const NumericClass attributeClass = classifyVertexAttributeFormat(attributeFormat);
            const NumericClass reflectedClass = classifyInterfaceFormat(reflectedFormat);
            if (attributeClass == NumericClass::Undefined || reflectedClass == NumericClass::Undefined)
            {
                return false;
            }
            if (attributeClass != reflectedClass)
            {
                return false;
            }
            return getFormatComponentCount(attributeFormat) == getFormatComponentCount(reflectedFormat);
        }

        NumericClass classifyColorTargetFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::R8Unorm:
            case TextureFormat::R8Snorm:
            case TextureFormat::R16Float:
            case TextureFormat::RG8Unorm:
            case TextureFormat::RG8Snorm:
            case TextureFormat::R32Float:
            case TextureFormat::RG16Float:
            case TextureFormat::RGBA8Unorm:
            case TextureFormat::RGBA8UnormSrgb:
            case TextureFormat::RGBA8Snorm:
            case TextureFormat::BGRA8Unorm:
            case TextureFormat::BGRA8UnormSrgb:
            case TextureFormat::RGB10A2Unorm:
            case TextureFormat::RG11B10Ufloat:
            case TextureFormat::RGB9E5Ufloat:
            case TextureFormat::RG32Float:
            case TextureFormat::RGBA16Float:
            case TextureFormat::RGBA32Float:
                return NumericClass::Float;
            case TextureFormat::R8Sint:
            case TextureFormat::RG8Sint:
            case TextureFormat::R16Sint:
            case TextureFormat::RG16Sint:
            case TextureFormat::RGBA8Sint:
            case TextureFormat::R32Sint:
            case TextureFormat::RG32Sint:
            case TextureFormat::RGBA16Sint:
            case TextureFormat::RGBA32Sint:
                return NumericClass::Sint;
            case TextureFormat::R8Uint:
            case TextureFormat::RG8Uint:
            case TextureFormat::R16Uint:
            case TextureFormat::RG16Uint:
            case TextureFormat::RGBA8Uint:
            case TextureFormat::R32Uint:
            case TextureFormat::RG32Uint:
            case TextureFormat::RGBA16Uint:
            case TextureFormat::RGBA32Uint:
            case TextureFormat::RGB10A2Uint:
                return NumericClass::Uint;
            default:
                return NumericClass::Undefined;
            }
        }

        ReflectedEntryPoint reflectEntryPoint(const SpvReflectEntryPoint &entryPoint)
        {
            if (entryPoint.name == nullptr || entryPoint.name[0] == '\0')
            {
                throw makeInvalidArgument("SPIR-V reflection encountered an entry point without a valid name.");
            }

            ReflectedEntryPoint reflected = {};
            reflected.name = entryPoint.name;
            reflected.functionId = entryPoint.id;
            reflected.stage = translateReflectShaderStage(entryPoint.shader_stage);
            reflected.localSizeX = entryPoint.local_size.x;
            reflected.localSizeY = entryPoint.local_size.y;
            reflected.localSizeZ = entryPoint.local_size.z;
            reflected.usesPushConstants = entryPoint.used_push_constant_count != 0u;

            for (uint32_t setIndex = 0; setIndex < entryPoint.descriptor_set_count; ++setIndex)
            {
                const SpvReflectDescriptorSet &descriptorSet = entryPoint.descriptor_sets[setIndex];
                for (uint32_t bindingIndex = 0; bindingIndex < descriptorSet.binding_count; ++bindingIndex)
                {
                    const SpvReflectDescriptorBinding *binding = descriptorSet.bindings[bindingIndex];
                    if (binding == nullptr)
                    {
                        throw makeInvalidArgument("SPIR-V reflection returned a null descriptor binding pointer.");
                    }
                    reflected.descriptorBindings.push_back(reflectDescriptorBinding(*binding));
                }
            }

            eastl::sort(reflected.descriptorBindings.begin(), reflected.descriptorBindings.end(), [](const ReflectedDescriptorBinding &lhs, const ReflectedDescriptorBinding &rhs)
            {
                if (lhs.set != rhs.set)
                {
                    return lhs.set < rhs.set;
                }
                return lhs.binding < rhs.binding;
            });

            for (size_t index = 1; index < reflected.descriptorBindings.size(); ++index)
            {
                const ReflectedDescriptorBinding &previous = reflected.descriptorBindings[index - 1];
                const ReflectedDescriptorBinding &current = reflected.descriptorBindings[index];
                if (previous.set == current.set && previous.binding == current.binding)
                {
                    throw makeInvalidArgument("SPIR-V reflection encountered duplicate descriptor binding coordinates within one entry point.");
                }
            }

            reflected.inputVariables.reserve(entryPoint.input_variable_count);
            for (uint32_t inputIndex = 0; inputIndex < entryPoint.input_variable_count; ++inputIndex)
            {
                const SpvReflectInterfaceVariable *inputVariable = entryPoint.input_variables[inputIndex];
                if (inputVariable == nullptr || !isUserInterfaceVariable(*inputVariable))
                {
                    continue;
                }
                reflected.inputVariables.push_back(reflectInterfaceVariable(*inputVariable));
            }
            sortAndValidateUniqueLocations(reflected.inputVariables, "input");

            reflected.outputVariables.reserve(entryPoint.output_variable_count);
            for (uint32_t outputIndex = 0; outputIndex < entryPoint.output_variable_count; ++outputIndex)
            {
                const SpvReflectInterfaceVariable *outputVariable = entryPoint.output_variables[outputIndex];
                if (outputVariable == nullptr || !isUserInterfaceVariable(*outputVariable))
                {
                    continue;
                }
                reflected.outputVariables.push_back(reflectInterfaceVariable(*outputVariable));
            }
            sortAndValidateUniqueLocations(reflected.outputVariables, "output");

            return reflected;
        }
    } // namespace

    void validate16BitStorageRequirements(const ReflectedShaderModule &reflection,
                                          const vk::PhysicalDevice16BitStorageFeatures &enabledFeatures,
                                          const eastl::string &shaderLabel)
    {
        const char *missingFeature = nullptr;
        if (reflection.usesStorageBuffer16BitAccess && !enabledFeatures.storageBuffer16BitAccess)
        {
            missingFeature = "storageBuffer16BitAccess";
        }
        else if (reflection.usesUniformAndStorageBuffer16BitAccess && !enabledFeatures.uniformAndStorageBuffer16BitAccess)
        {
            missingFeature = "uniformAndStorageBuffer16BitAccess";
        }
        else if (reflection.usesStoragePushConstant16 && !enabledFeatures.storagePushConstant16)
        {
            missingFeature = "storagePushConstant16";
        }
        else if (reflection.usesStorageInputOutput16 && !enabledFeatures.storageInputOutput16)
        {
            missingFeature = "storageInputOutput16";
        }
        if (missingFeature != nullptr)
        {
            throw makeInvalidArgument("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature " + missingFeature + ".");
        }
    }

    void validateImageAndSubgroupRequirements(const ReflectedShaderModule &reflection,
                                             const vk::PhysicalDeviceFeatures &enabledFeatures,
                                             const vk::PhysicalDeviceSubgroupProperties &subgroupProperties,
                                             bool shaderSubgroupExtendedTypes,
                                             const eastl::string &shaderLabel)
    {
        if (reflection.usesSubgroupExtendedTypes && !shaderSubgroupExtendedTypes)
        {
            throw makeInvalidArgument("SPIR-V module '" + shaderLabel + "' requires disabled Vulkan feature shaderSubgroupExtendedTypes.");
        }
        if (reflection.usesStorageImageExtendedFormats && !enabledFeatures.shaderStorageImageExtendedFormats)
        {
            throw makeInvalidArgument("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature shaderStorageImageExtendedFormats.");
        }
        if (reflection.usesImageGatherExtended && !enabledFeatures.shaderImageGatherExtended)
        {
            throw makeInvalidArgument("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature shaderImageGatherExtended.");
        }
        if ((subgroupProperties.supportedOperations & reflection.requiredSubgroupOperations) != reflection.requiredSubgroupOperations)
        {
            throw makeInvalidArgument("SPIR-V module '" + shaderLabel + "' requires unsupported Vulkan subgroup operations.");
        }
        for (const ReflectedEntryPoint &entry : reflection.entryPoints)
        {
            if (entry.usesSubgroupOperations && !(subgroupProperties.supportedStages & entry.stage))
            {
                throw makeInvalidArgument("SPIR-V entry '" + entry.name + "' in module '" + shaderLabel + "' uses subgroup operations in an unsupported Vulkan stage.");
            }
        }
    }

    ReflectedShaderModule reflectShaderModule(const eastl::vector<uint32_t> &spirv)
    {
        if (spirv.empty())
        {
            throw makeInvalidArgument("SPIR-V reflection requires non-empty SPIR-V bytecode.");
        }

        struct ShaderModuleGuard
        {
            SpvReflectShaderModule module = {};
            bool valid = false;

            ~ShaderModuleGuard()
            {
                if (valid)
                {
                    spvReflectDestroyShaderModule(&module);
                }
            }
        } shaderModuleGuard;

        const SpvReflectResult createResult = spvReflectCreateShaderModule(
            spirv.size() * sizeof(uint32_t),
            spirv.data(),
            &shaderModuleGuard.module);
        if (createResult != SPV_REFLECT_RESULT_SUCCESS)
        {
            throwReflectFailure("spvReflectCreateShaderModule", createResult);
        }
        shaderModuleGuard.valid = true;

        ReflectedShaderModule reflection = {};
        reflection.entryPoints.reserve(shaderModuleGuard.module.entry_point_count);
        for (uint32_t entryPointIndex = 0; entryPointIndex < shaderModuleGuard.module.entry_point_count; ++entryPointIndex)
        {
            reflection.entryPoints.push_back(reflectEntryPoint(shaderModuleGuard.module.entry_points[entryPointIndex]));
        }

        if (reflection.entryPoints.empty())
        {
            throw makeInvalidArgument("SPIR-V reflection found no entry points in the shader module.");
        }

        reflectModuleRequirements(spirv, reflection);

        return reflection;
    }

    const ReflectedEntryPoint &requireShaderEntryPoint(
        const ReflectedShaderModule &reflection,
        const eastl::string &entryPoint,
        vk::ShaderStageFlagBits expectedStage,
        const char *ownerName)
    {
        if (entryPoint.empty())
        {
            throwInvalidArgument(ownerName, "received an empty shader entry point.");
        }

        for (const ReflectedEntryPoint &candidate : reflection.entryPoints)
        {
            if (candidate.name != entryPoint)
            {
                continue;
            }
            if (candidate.stage != expectedStage)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.c_str() + "' is a " + getShaderStageName(candidate.stage) +
                        " shader, not a " + getShaderStageName(expectedStage) + " shader.");
            }
            return candidate;
        }

        throwInvalidArgument(ownerName, eastl::string("entry point '") + entryPoint.c_str() + "' was not found in the SPIR-V shader module.");
    }

    void validatePipelineLayoutAgainstEntryPoint(
        const VKPipelineLayout &pipelineLayout,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName)
    {
        if (entryPoint.usesPushConstants)
        {
            throwInvalidArgument(ownerName, eastl::string("entry point '") + entryPoint.name.c_str() + "' uses push constants, which are not exposed by the current GVM RHI contract.");
        }

        const PipelineLayoutDescriptor &pipelineLayoutDescriptor = pipelineLayout.getDescriptor();
        for (const ReflectedDescriptorBinding &reflectedBinding : entryPoint.descriptorBindings)
        {
            if (reflectedBinding.descriptorType == vk::DescriptorType::eInputAttachment)
            {
                continue;
            }

            if (reflectedBinding.set >= pipelineLayoutDescriptor.bindGroupLayouts.size())
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.name.c_str() + "' requires descriptor set " +
                        eastl::to_string(reflectedBinding.set) + ", but the pipeline layout only exposes " +
                        eastl::to_string(pipelineLayoutDescriptor.bindGroupLayouts.size()) + " bind group layout(s).");
            }

            const BindGroupLayout &bindGroupLayoutHandle = pipelineLayoutDescriptor.bindGroupLayouts[reflectedBinding.set];
            if (bindGroupLayoutHandle == nullptr)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("pipeline layout set ") + eastl::to_string(reflectedBinding.set) + " is null while validating SPIR-V reflection.");
            }

            const auto *bindGroupLayout = static_cast<const VKBindGroupLayout *>(bindGroupLayoutHandle.get());
            const vk::DescriptorSetLayoutBinding *nativeLayoutBinding = findNativeLayoutBinding(*bindGroupLayout, reflectedBinding.binding);
            const BindGroupLayoutEntry *layoutEntry = findLayoutEntry(*bindGroupLayout, reflectedBinding.binding);
            if (nativeLayoutBinding == nullptr || layoutEntry == nullptr)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.name.c_str() + "' requires binding " +
                        eastl::to_string(reflectedBinding.binding) + " in set " + eastl::to_string(reflectedBinding.set) +
                        ", but the pipeline layout does not declare it.");
            }

            if (nativeLayoutBinding->descriptorType != reflectedBinding.descriptorType)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.name.c_str() + "' requires a different descriptor type for set " +
                        eastl::to_string(reflectedBinding.set) + " binding " + eastl::to_string(reflectedBinding.binding) + ".");
            }
            if (!reflectedBinding.isRuntimeDescriptorArray &&
                nativeLayoutBinding->descriptorCount < reflectedBinding.descriptorCount)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.name.c_str() + "' requires " +
                        eastl::to_string(reflectedBinding.descriptorCount) + " descriptor(s) at set " +
                        eastl::to_string(reflectedBinding.set) + " binding " + eastl::to_string(reflectedBinding.binding) +
                        ", but the pipeline layout only provides " + eastl::to_string(nativeLayoutBinding->descriptorCount) + ".");
            }
            if ((nativeLayoutBinding->stageFlags & entryPoint.stage) != entryPoint.stage)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("entry point '") + entryPoint.name.c_str() + "' is not included in the visibility mask for set " +
                        eastl::to_string(reflectedBinding.set) + " binding " + eastl::to_string(reflectedBinding.binding) + ".");
            }

            switch (reflectedBinding.descriptorType)
            {
            case vk::DescriptorType::eUniformBuffer:
                if (layoutEntry->buffer.type != BufferBindingType::Uniform)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " must be declared as a uniform buffer.");
                }
                break;
            case vk::DescriptorType::eStorageBuffer:
                if (layoutEntry->buffer.type != BufferBindingType::Storage &&
                    layoutEntry->buffer.type != BufferBindingType::ReadOnlyStorage)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " must be declared as a storage buffer.");
                }
                if (layoutEntry->buffer.type == BufferBindingType::ReadOnlyStorage &&
                    reflectedBinding.storageBufferAccess != StorageBufferAccess::ReadOnly)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " is declared as read-only storage, but the shader writes to it.");
                }
                if (!storageBufferAccessSatisfies(layoutEntry->buffer.access, reflectedBinding.storageBufferAccess))
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " does not expose sufficient storage buffer access for the shader.");
                }
                break;
            case vk::DescriptorType::eSampler:
                if (layoutEntry->sampler.type == SamplerBindingType::Undefined)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " must be declared as a sampler.");
                }
                break;
            case vk::DescriptorType::eSampledImage:
                if (layoutEntry->texture.sampleType == TextureSampleType::Undefined)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " must be declared as a sampled texture.");
                }
                if (layoutEntry->texture.viewDimension != reflectedBinding.textureViewDimension)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " uses a different texture view dimension than the shader expects.");
                }
                break;
            case vk::DescriptorType::eStorageImage:
                if (layoutEntry->storageTexture.access == StorageTextureAccess::Undefined)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " must be declared as a storage texture.");
                }
                if (layoutEntry->storageTexture.viewDimension != reflectedBinding.textureViewDimension)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " uses a different storage texture view dimension than the shader expects.");
                }
                if (reflectedBinding.imageFormat != vk::Format::eUndefined &&
                    translateTextureFormat(layoutEntry->storageTexture.format) != reflectedBinding.imageFormat)
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " uses a different storage texture format than the shader expects.");
                }
                if (!storageTextureAccessSatisfies(layoutEntry->storageTexture.access, reflectedBinding.storageTextureAccess))
                {
                    throwInvalidArgument(
                        ownerName,
                        eastl::string("set ") + eastl::to_string(reflectedBinding.set) + " binding " +
                            eastl::to_string(reflectedBinding.binding) + " does not expose sufficient storage texture access for the shader.");
                }
                break;
            default:
                throwInvalidArgument(ownerName, "encountered an unsupported descriptor type during pipeline layout validation.");
            }
        }
    }

    void validateVertexInputsAgainstEntryPoint(
        const VertexState &vertexState,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName)
    {
        eastl::vector<vk::VertexInputAttributeDescription> attributes;
        for (uint32_t bindingIndex = 0; bindingIndex < vertexState.buffers.size(); ++bindingIndex)
        {
            const VertexBufferLayout &bufferLayout = vertexState.buffers[bindingIndex];
            if (bufferLayout.stepMode == VertexStepMode::VertexBufferNotUsed)
            {
                continue;
            }
            for (const VertexAttribute &attribute : bufferLayout.attributes)
            {
                attributes.push_back(translateVertexAttribute(attribute, bindingIndex));
            }
        }

        eastl::sort(attributes.begin(), attributes.end(), [](const vk::VertexInputAttributeDescription &lhs, const vk::VertexInputAttributeDescription &rhs)
        {
            return lhs.location < rhs.location;
        });

        for (size_t index = 1; index < attributes.size(); ++index)
        {
            if (attributes[index - 1].location == attributes[index].location)
            {
                throwInvalidArgument(ownerName, "vertex input descriptor contains duplicate shader locations.");
            }
        }

        for (const ReflectedInterfaceVariable &inputVariable : entryPoint.inputVariables)
        {
            auto attributeIt = eastl::find_if(attributes.begin(), attributes.end(), [&](const vk::VertexInputAttributeDescription &attribute)
            {
                return attribute.location == inputVariable.location;
            });
            if (attributeIt == attributes.end())
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("vertex entry point '") + entryPoint.name.c_str() + "' requires vertex attribute location " +
                        eastl::to_string(inputVariable.location) + ", but the pipeline descriptor does not provide it.");
            }
            if (!isVertexAttributeFormatCompatible(attributeIt->format, inputVariable.format))
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("vertex attribute location ") + eastl::to_string(inputVariable.location) +
                        " uses a format that does not match the reflected vertex shader input.");
            }
        }
    }

    void validateStageInterface(
        const ReflectedEntryPoint &producerEntryPoint,
        const ReflectedEntryPoint &consumerEntryPoint,
        const char *ownerName)
    {
        for (const ReflectedInterfaceVariable &consumerInput : consumerEntryPoint.inputVariables)
        {
            auto producerOutputIt = eastl::find_if(
                producerEntryPoint.outputVariables.begin(),
                producerEntryPoint.outputVariables.end(),
                [&](const ReflectedInterfaceVariable &producerOutput)
                {
                    return producerOutput.location == consumerInput.location;
                });

            if (producerOutputIt == producerEntryPoint.outputVariables.end())
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("shader stage interface mismatch: producer '") + producerEntryPoint.name.c_str() +
                        "' does not provide location " + eastl::to_string(consumerInput.location) +
                        " required by consumer '" + consumerEntryPoint.name.c_str() + "'.");
            }
            if (producerOutputIt->format != consumerInput.format)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("shader stage interface mismatch at location ") + eastl::to_string(consumerInput.location) +
                        ": producer and consumer use different formats.");
            }
        }
    }

    void validateFragmentTargetsAgainstEntryPoint(
        const FragmentState &fragmentState,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName)
    {
        for (const ReflectedInterfaceVariable &outputVariable : entryPoint.outputVariables)
        {
            if (outputVariable.location >= fragmentState.targets.size())
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("fragment entry point '") + entryPoint.name.c_str() + "' writes to color location " +
                        eastl::to_string(outputVariable.location) + ", but the pipeline descriptor only defines " +
                        eastl::to_string(fragmentState.targets.size()) + " color target(s).");
            }

            const ColorTargetState &target = fragmentState.targets[outputVariable.location];
            const NumericClass outputClass = classifyInterfaceFormat(outputVariable.format);
            const NumericClass targetClass = classifyColorTargetFormat(target.format);
            if (outputClass == NumericClass::Undefined)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("fragment entry point '") + entryPoint.name.c_str() + "' uses an unsupported output format at location " +
                        eastl::to_string(outputVariable.location) + ".");
            }
            if (targetClass == NumericClass::Undefined)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("fragment target ") + eastl::to_string(outputVariable.location) +
                        " uses a format that is not a valid color attachment for the reflected fragment shader output.");
            }
            if (outputClass != targetClass)
            {
                throwInvalidArgument(
                    ownerName,
                    eastl::string("fragment output location ") + eastl::to_string(outputVariable.location) +
                        " uses a numeric type that is incompatible with the pipeline color target format.");
            }
        }
    }
} // namespace GVM::RHI::Vulkan::Detail
