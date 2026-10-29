#pragma once

#include "SPIRVInstructionBuilder.hpp"

#include <cstdint>
#include <unordered_set>

#include <spirv/unified1/spirv.hpp>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Owns the OpCapability and OpExtension sections of one SPIR-V module.
        Each capability is emitted once, in the order it is first required; the ensure functions encode
        capabilities that imply others or need an extension. Use one tracker per module being built. */
    class SPIRVCapabilityTracker
    {
    public:
        /** Emits OpCapability for capability unless an earlier call already declared it. */
        void declareCapability(spv::Capability capability);

        /** Emits OpExtension for name; callers request each extension once when its capability is first declared. */
        void declareExtension(const char *name);

        /** Declares ImageGatherExtended, required when an image gather uses offset operands. */
        void ensureImageGatherExtendedCapability();

        /** Declares ClipDistance, required when a stage exports or imports clip distances. */
        void ensureClipDistanceCapability();

        /** Declares Float16, required when native half scalar or vector types are emitted. */
        void ensureFloat16Capability();

        /** Declares StorageBuffer16BitAccess, required when storage-buffer elements contain half values. */
        void ensureStorageBuffer16BitAccessCapability();

        /** Declares UniformAndStorageBuffer16BitAccess, required when block resources contain half values. */
        void ensureUniformAndStorageBuffer16BitAccessCapability();

        /** Declares StorageInputOutput16, required when shader stage IO uses half values. */
        void ensureStorageInputOutput16Capability();

        /** Declares ImageQuery, required when the module queries texture dimensions. */
        void ensureImageQueryCapability();

        /** Declares InputAttachment, required when the module uses Vulkan subpass input attachments. */
        void ensureInputAttachmentCapability();

        /** Declares StorageImageExtendedFormats, required by storage image formats outside the core set. */
        void ensureStorageImageExtendedFormatsCapability();

        /** Declares GroupNonUniform, required by every wave/subgroup intrinsic. */
        void ensureGroupNonUniformCapability();

        /** Declares GroupNonUniformBallot (and GroupNonUniform first) for wave ballot and count intrinsics. */
        void ensureGroupNonUniformBallotCapability();

        /** Declares GroupNonUniformShuffle (and GroupNonUniform first) for wave lane-read intrinsics. */
        void ensureGroupNonUniformShuffleCapability();

        /** Declares ShaderNonUniform, the capability shared by non-uniform image and sampler table indexing. */
        void ensureShaderNonUniformCapability();

        /** Declares SampledImageArrayNonUniformIndexing after ShaderNonUniform, plus SPV_EXT_descriptor_indexing. */
        void ensureSampledImageArrayNonUniformIndexingCapability();

        /** Declares StorageImageArrayNonUniformIndexing after ShaderNonUniform for Args selections that vary per invocation. */
        void ensureStorageImageArrayNonUniformIndexingCapability();

        /** Declares the core dynamic-indexing capability for a uniform storage (storage = true) or sampled image table access. */
        void ensureImageArrayDynamicIndexingCapability(bool storage);

        /** Declares RuntimeDescriptorArray, plus SPV_EXT_descriptor_indexing, for runtime-sized descriptor arrays. */
        void ensureRuntimeDescriptorArrayCapability();

        /** Declares FragmentBarycentricKHR, plus SPV_KHR_fragment_shader_barycentric, for barycentric fragment inputs. */
        void ensureFragmentBarycentricCapability();

        /** Declares Tessellation, the Vulkan capability gate required by standalone fragment PrimitiveId inputs. */
        void ensureFragmentPrimitiveIdCapability();

        /** Returns the encoded OpCapability section. */
        const SPIRVInstructionBuilder &getCapabilities() const;

        /** Returns the encoded OpExtension section. */
        const SPIRVInstructionBuilder &getExtensions() const;

    private:
        /** Reports whether capability has already been declared. */
        bool isDeclared(spv::Capability capability) const;

        SPIRVInstructionBuilder mCapabilities;
        SPIRVInstructionBuilder mExtensions;
        std::unordered_set<uint32_t> mDeclaredCapabilities;
    };
} // namespace UGLC::CodeGen::SPIRVEmitter
