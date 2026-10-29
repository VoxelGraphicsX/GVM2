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
#include <EASTL/string_view.h>
#include <cstring>

namespace GVM::RHI::Vulkan::Detail
{
    namespace
    {
        /** Tracks subgroup instructions reachable through one SPIR-V function's direct calls. */
        struct SubgroupFunctionRequirements
        {
            uint32_t functionId = 0u;
            bool usesSubgroups = false;
            eastl::vector<uint32_t> callees;
        };

        [[noreturn]]
        void throwInvalidArgument(const char *ownerName, const eastl::string &message)
        {
            throw std::invalid_argument((eastl::string(ownerName) + ": " + message).c_str());
        }

        [[noreturn]]
        void throwReflectFailure(const char *operation, SpvReflectResult result)
        {
            throw std::invalid_argument((eastl::string("SPIR-V reflection failed during ") + operation + " (result=" + eastl::to_string(static_cast<int>(result)) + ").").c_str());
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
            default: throw std::invalid_argument("SPIR-V reflection encountered an unsupported Vulkan shader stage.");
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
            default: throw std::invalid_argument("SPIR-V reflection encountered a descriptor type that is unsupported by the current GVM Vulkan backend.");
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




        /** Borrows a SPIR-V literal from an instruction whose word range has already been checked. */
        eastl::string_view decodeSpirvStringLiteral(const uint32_t *words, uint32_t wordCount)
        {
            const auto *text = reinterpret_cast<const char *>(words);
            const size_t byteCount = static_cast<size_t>(wordCount) * sizeof(uint32_t);
            const auto *end = static_cast<const char *>(std::memchr(text, 0, byteCount));
            return {text, end != nullptr ? static_cast<size_t>(end - text) : byteCount};
        }

        /** Reuses reflected capabilities and analyzes subgroup reachability only for modules that require it. */
        void reflectModuleRequirements(const eastl::vector<uint32_t> &spirv,
            const SpvReflectShaderModule &module, ReflectedShaderModule &reflection)
        {
            bool subgroupDispatch = false;
            for (uint32_t index = 0u; index < module.capability_count; ++index)
            {
                const SpvCapability capability = module.capabilities[index].value;
                subgroupDispatch |= capability == SpvCapabilitySubgroupDispatch;
            switch (capability)
            {
            case SpvCapabilityStorageImageArrayDynamicIndexing: reflection.usesStorageImageArrayDynamicIndexing = true; break;
            case SpvCapabilityStorageImageArrayNonUniformIndexing: reflection.usesStorageImageArrayNonUniformIndexing = true; break;
            case SpvCapabilityPhysicalStorageBufferAddresses: reflection.usesPhysicalStorageBuffers = true; break;
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
            case SpvCapabilityClipDistance:
                reflection.usesClipDistance = true;
                break;
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

            uint32_t wordIndex = 5u;
            while (wordIndex < spirv.size())
            {
                const uint32_t wordCount = spirv[wordIndex] >> 16u;
                const uint32_t opcode = spirv[wordIndex] & 0xffffu;
                if (wordCount == 0u || wordCount > spirv.size() - wordIndex)
                    throw std::invalid_argument("SPIR-V reflection encountered a malformed module instruction.");
                if (opcode != SpvOpCapability && opcode != SpvOpExtension && opcode != SpvOpNop)
                    break;
                if (opcode == SpvOpExtension && wordCount >= 2u)
                {
                    const auto extension = decodeSpirvStringLiteral(spirv.data() + wordIndex + 1u, wordCount - 1u);
                    reflection.usesFragmentShaderBarycentricExtension |=
                        extension == "SPV_KHR_fragment_shader_barycentric" || extension == "SPV_NV_fragment_shader_barycentric";
                }
                wordIndex += wordCount;
            }
            if (!reflection.requiredSubgroupOperations && !subgroupDispatch)
                return;

            eastl::vector<SubgroupFunctionRequirements> functions;
            eastl::vector<uint32_t> subgroupBuiltins;
            eastl::vector<uint32_t> extendedTypes;
            eastl::vector<eastl::pair<uint32_t, uint32_t>> vectorTypes;
            eastl::vector<eastl::pair<uint32_t, uint32_t>> valueTypes;
            eastl::vector<uint32_t> subgroupResultTypes;
            eastl::vector<uint32_t> allEqualOperands;
            const bool needsOperandTypes = bool(reflection.requiredSubgroupOperations & vk::SubgroupFeatureFlagBits::eVote);
            uint32_t currentFunction = 0u;
            while (wordIndex < spirv.size())
            {
                const uint32_t wordCount = spirv[wordIndex] >> 16u;
                const uint32_t opcode = spirv[wordIndex] & 0xffffu;
                if (wordCount == 0u || wordCount > spirv.size() - wordIndex)
                    throw std::invalid_argument("SPIR-V reflection encountered a malformed subgroup instruction.");
                if ((opcode == SpvOpTypeFloat || opcode == SpvOpTypeInt) && wordCount >= 3u && spirv[wordIndex + 2u] != 32u)
                    extendedTypes.push_back(spirv[wordIndex + 1u]);
                else if (opcode == SpvOpTypeVector && wordCount >= 4u)
                    vectorTypes.emplace_back(spirv[wordIndex + 1u], spirv[wordIndex + 2u]);
                if (needsOperandTypes)
                {
                    bool hasResult = false, hasResultType = false;
                    SpvHasResultAndType(static_cast<SpvOp>(opcode), &hasResult, &hasResultType);
                    if (hasResult && hasResultType && wordCount >= 3u)
                        valueTypes.emplace_back(spirv[wordIndex + 2u], spirv[wordIndex + 1u]);
                }
                const bool subgroupOperation = opcode >= SpvOpGroupNonUniformElect && opcode <= SpvOpGroupNonUniformQuadSwap;
                if (subgroupOperation && wordCount >= 3u)
                {
                    subgroupResultTypes.push_back(spirv[wordIndex + 1u]);
                    if (opcode == SpvOpGroupNonUniformAllEqual && wordCount >= 5u)
                        allEqualOperands.push_back(spirv[wordIndex + 4u]);
                }
                if (opcode == SpvOpDecorate && wordCount >= 4u && spirv[wordIndex + 2u] == SpvDecorationBuiltIn)
                {
                    const uint32_t builtin = spirv[wordIndex + 3u];
                    if (builtin == SpvBuiltInSubgroupSize || builtin == SpvBuiltInSubgroupLocalInvocationId ||
                        builtin == SpvBuiltInNumSubgroups || builtin == SpvBuiltInSubgroupId)
                        subgroupBuiltins.push_back(spirv[wordIndex + 1u]);
                }
                if (opcode == SpvOpFunction && wordCount >= 5u)
                {
                    if (functions.empty())
                        eastl::sort(subgroupBuiltins.begin(), subgroupBuiltins.end());
                    functions.push_back({.functionId = spirv[wordIndex + 2u]});
                    currentFunction = static_cast<uint32_t>(functions.size());
                }
                else if (opcode == SpvOpFunctionEnd)
                    currentFunction = 0u;
                else if (currentFunction != 0u)
                {
                    auto &function = functions[currentFunction - 1u];
                    if (subgroupOperation || (opcode == SpvOpLoad && wordCount >= 4u &&
                        eastl::binary_search(subgroupBuiltins.begin(), subgroupBuiltins.end(), spirv[wordIndex + 3u])))
                        function.usesSubgroups = true;
                    if (opcode == SpvOpFunctionCall && wordCount >= 4u)
                        function.callees.push_back(spirv[wordIndex + 3u]);
                }
                wordIndex += wordCount;
            }

            eastl::sort(extendedTypes.begin(), extendedTypes.end());
            const size_t scalarTypeCount = extendedTypes.size();
            for (const auto &type : vectorTypes)
                if (eastl::binary_search(extendedTypes.begin(), extendedTypes.begin() + scalarTypeCount, type.second))
                    extendedTypes.push_back(type.first);
            eastl::sort(extendedTypes.begin(), extendedTypes.end());
            for (uint32_t type : subgroupResultTypes)
                if (eastl::binary_search(extendedTypes.begin(), extendedTypes.end(), type))
                    reflection.usesSubgroupExtendedTypes = true;
            if (!allEqualOperands.empty() && !reflection.usesSubgroupExtendedTypes)
            {
                eastl::sort(valueTypes.begin(), valueTypes.end());
                for (uint32_t operand : allEqualOperands)
                {
                    const auto found = eastl::lower_bound(valueTypes.begin(), valueTypes.end(), eastl::pair<uint32_t, uint32_t>{operand, 0u});
                    if (found != valueTypes.end() && found->first == operand &&
                        eastl::binary_search(extendedTypes.begin(), extendedTypes.end(), found->second))
                        reflection.usesSubgroupExtendedTypes = true;
                }
            }
            eastl::sort(functions.begin(), functions.end(), [](const auto &left, const auto &right) { return left.functionId < right.functionId; });
            for (auto &function : functions)
                for (uint32_t &callee : function.callees)
                {
                    const auto found = eastl::lower_bound(functions.begin(), functions.end(), callee,
                        [](const SubgroupFunctionRequirements &candidate, uint32_t id) { return candidate.functionId < id; });
                    callee = found != functions.end() && found->functionId == callee
                        ? static_cast<uint32_t>(found - functions.begin()) : ~uint32_t(0);
                }
            bool changed;
            do
            {
                changed = false;
                for (auto &function : functions)
                {
                    if (function.usesSubgroups)
                        continue;
                    for (uint32_t callee : function.callees)
                        if (callee < functions.size() && functions[callee].usesSubgroups)
                        {
                            function.usesSubgroups = true;
                            changed = true;
                            break;
                        }
                }
            } while (changed);
            for (auto &entry : reflection.entryPoints)
            {
                const auto found = eastl::lower_bound(functions.begin(), functions.end(), entry.functionId,
                    [](const SubgroupFunctionRequirements &candidate, uint32_t id) { return candidate.functionId < id; });
                entry.usesSubgroupOperations = found != functions.end() && found->functionId == entry.functionId && found->usesSubgroups;
            }
        }

        ReflectedDescriptorBinding reflectDescriptorBinding(const SpvReflectDescriptorBinding &binding)
        {
            ReflectedDescriptorBinding reflected = {};
            reflected.set = binding.set;
            reflected.binding = binding.binding;
            reflected.descriptorType = translateReflectDescriptorType(binding.descriptor_type);
            reflected.inputAttachmentIndex = binding.input_attachment_index;

            if (reflected.descriptorType == vk::DescriptorType::eCombinedImageSampler)
            {
                throw std::invalid_argument("SPIR-V reflection encountered a combined image sampler, which is unsupported by the current GVM Vulkan binding model.");
            }

            if (reflected.descriptorType == vk::DescriptorType::eStorageImage)
            {
                reflected.storageTextureAccess = translateReflectStorageTextureAccess(binding.decoration_flags);
            }
            else if (reflected.descriptorType == vk::DescriptorType::eStorageBuffer)
            {
                reflected.storageBufferAccess = translateReflectStorageBufferAccess(binding.decoration_flags);
            }

            return reflected;
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



        ReflectedEntryPoint reflectEntryPoint(const SpvReflectShaderModule &module, const SpvReflectEntryPoint &entryPoint)
        {
            if (entryPoint.name == nullptr || entryPoint.name[0] == '\0')
            {
                throw std::invalid_argument("SPIR-V reflection encountered an entry point without a valid name.");
            }

            ReflectedEntryPoint reflected = {};
            reflected.name = entryPoint.name;
            reflected.functionId = entryPoint.id;
            reflected.stage = translateReflectShaderStage(entryPoint.shader_stage);
            reflected.localSizeX = entryPoint.local_size.x;
            reflected.localSizeY = entryPoint.local_size.y;
            reflected.localSizeZ = entryPoint.local_size.z;
            reflected.usesPushConstants = entryPoint.used_push_constant_count != 0u;
            if (entryPoint.used_push_constant_count == 1)
            {
                for (uint32_t index = 0; index < module.push_constant_block_count; ++index)
                {
                    const auto &block = module.push_constant_blocks[index];
                    if (block.spirv_id != entryPoint.used_push_constants[0] || block.member_count != 1 || block.offset != 0 || block.size != 8)
                    {
                        continue;
                    }
                    const auto &member = block.members[0];
                    reflected.hasValidArgumentRoot = member.offset == 0 && member.numeric.scalar.width == 32 &&
                        member.numeric.scalar.signedness == 0 && member.numeric.vector.component_count == 2 &&
                        member.type_description != nullptr && (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0;
                }
            }

            for (uint32_t setIndex = 0; setIndex < entryPoint.descriptor_set_count; ++setIndex)
            {
                const SpvReflectDescriptorSet &descriptorSet = entryPoint.descriptor_sets[setIndex];
                for (uint32_t bindingIndex = 0; bindingIndex < descriptorSet.binding_count; ++bindingIndex)
                {
                    const SpvReflectDescriptorBinding *binding = descriptorSet.bindings[bindingIndex];
                    if (binding == nullptr)
                    {
                        throw std::invalid_argument("SPIR-V reflection returned a null descriptor binding pointer.");
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
                if (previous.set == current.set && previous.binding == current.binding &&
                    !(reflected.hasValidArgumentRoot && current.set == 0))
                {
                    throw std::invalid_argument("SPIR-V reflection encountered duplicate descriptor binding coordinates within one entry point.");
                }
            }

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
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature " + missingFeature + ".").c_str());
        }
    }

    void validateImageAndSubgroupRequirements(const ReflectedShaderModule &reflection,
                                             const vk::PhysicalDeviceFeatures &enabledFeatures,
                                             const vk::PhysicalDeviceSubgroupProperties &subgroupProperties,
                                             bool shaderSubgroupExtendedTypes,
                                             const eastl::string &shaderLabel)
    {
        if (reflection.usesClipDistance && !enabledFeatures.shaderClipDistance)
        {
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature shaderClipDistance.").c_str());
        }
        if (reflection.usesSubgroupExtendedTypes && !shaderSubgroupExtendedTypes)
        {
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires disabled Vulkan feature shaderSubgroupExtendedTypes.").c_str());
        }
        if (reflection.usesStorageImageExtendedFormats && !enabledFeatures.shaderStorageImageExtendedFormats)
        {
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature shaderStorageImageExtendedFormats.").c_str());
        }
        if (reflection.usesImageGatherExtended && !enabledFeatures.shaderImageGatherExtended)
        {
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires the disabled Vulkan feature shaderImageGatherExtended.").c_str());
        }
        if ((subgroupProperties.supportedOperations & reflection.requiredSubgroupOperations) != reflection.requiredSubgroupOperations)
        {
            throw std::invalid_argument(("SPIR-V module '" + shaderLabel + "' requires unsupported Vulkan subgroup operations.").c_str());
        }
        for (const ReflectedEntryPoint &entry : reflection.entryPoints)
        {
            const bool preRasterStage = entry.stage == vk::ShaderStageFlagBits::eVertex ||
                entry.stage == vk::ShaderStageFlagBits::eTessellationControl || entry.stage == vk::ShaderStageFlagBits::eTessellationEvaluation ||
                entry.stage == vk::ShaderStageFlagBits::eGeometry;
            const char *disabledStorageFeature = preRasterStage && !enabledFeatures.vertexPipelineStoresAndAtomics ? "vertexPipelineStoresAndAtomics" :
                entry.stage == vk::ShaderStageFlagBits::eFragment && !enabledFeatures.fragmentStoresAndAtomics ? "fragmentStoresAndAtomics" : nullptr;
            if (disabledStorageFeature != nullptr)
            {
                for (const auto &binding : entry.descriptorBindings)
                {
                    if ((binding.descriptorType == vk::DescriptorType::eStorageBuffer && binding.storageBufferAccess != StorageBufferAccess::ReadOnly) ||
                        (binding.descriptorType == vk::DescriptorType::eStorageImage && binding.storageTextureAccess != StorageTextureAccess::ReadOnly))
                    {
                        throw std::invalid_argument(("SPIR-V entry '" + entry.name + "' in module '" + shaderLabel + "' requires the disabled Vulkan feature " + disabledStorageFeature + ".").c_str());
                    }
                }
            }
            if (entry.usesSubgroupOperations && !(subgroupProperties.supportedStages & entry.stage))
            {
                throw std::invalid_argument(("SPIR-V entry '" + entry.name + "' in module '" + shaderLabel + "' uses subgroup operations in an unsupported Vulkan stage.").c_str());
            }
        }
    }

    ReflectedShaderModule reflectShaderModule(const eastl::vector<uint32_t> &spirv)
    {
        if (spirv.empty())
        {
            throw std::invalid_argument("SPIR-V reflection requires non-empty SPIR-V bytecode.");
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
            reflection.entryPoints.push_back(reflectEntryPoint(shaderModuleGuard.module, shaderModuleGuard.module.entry_points[entryPointIndex]));
        }

        if (reflection.entryPoints.empty())
        {
            throw std::invalid_argument("SPIR-V reflection found no entry points in the shader module.");
        }

        reflectModuleRequirements(spirv, shaderModuleGuard.module, reflection);
        for (auto &entry : reflection.entryPoints)
        {
            entry.usesArgumentAddressing = reflection.usesPhysicalStorageBuffers;
        }

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

    void validateArgumentAbiAgainstEntryPoint(
        const VKPipelineLayout &pipelineLayout,
        const ReflectedEntryPoint &entryPoint,
        const char *ownerName)
    {
        const bool argumentLayout = pipelineLayout.getDescriptor().arguments.byteSize != 0;
        if (argumentLayout)
        {
            if (!entryPoint.usesArgumentAddressing || (entryPoint.usesPushConstants && !entryPoint.hasValidArgumentRoot))
            {
                throwInvalidArgument(ownerName, "Args pipelines require the internal eight-byte BDA root ABI.");
            }
        }
        else if (entryPoint.usesPushConstants || entryPoint.usesArgumentAddressing)
        {
            throwInvalidArgument(ownerName, eastl::string("entry point '") + entryPoint.name.c_str() + "' uses push constants, which are not exposed by the current GVM RHI contract.");
        }

        if (argumentLayout)
        {
            for (const ReflectedDescriptorBinding &reflectedBinding : entryPoint.descriptorBindings)
            {
                if (reflectedBinding.descriptorType == vk::DescriptorType::eInputAttachment)
                {
                    continue;
                }
                if (reflectedBinding.set != 0 || pipelineLayout.getArgumentTable() == nullptr)
                {
                    throwInvalidArgument(ownerName, "Args image and sampler descriptors require the internal table at set zero.");
                }
            }
        }
    }



} // namespace GVM::RHI::Vulkan::Detail
