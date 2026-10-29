#include "SPIRVCapabilityTracker.hpp"

namespace UGLC::CodeGen::SPIRVEmitter
{
    void SPIRVCapabilityTracker::declareCapability(spv::Capability capability)
    {
        if (mDeclaredCapabilities.insert(static_cast<uint32_t>(capability)).second)
        {
            mCapabilities.appendInstruction(spv::OpCapability, {static_cast<uint32_t>(capability)});
        }
    }

    void SPIRVCapabilityTracker::declareExtension(const char *name)
    {
        mExtensions.appendInstructionWithString(spv::OpExtension, {}, name);
    }

    void SPIRVCapabilityTracker::ensureImageGatherExtendedCapability()
    {
        declareCapability(spv::CapabilityImageGatherExtended);
    }

    void SPIRVCapabilityTracker::ensureClipDistanceCapability()
    {
        declareCapability(spv::CapabilityClipDistance);
    }

    void SPIRVCapabilityTracker::ensureFloat16Capability()
    {
        declareCapability(spv::CapabilityFloat16);
    }

    void SPIRVCapabilityTracker::ensureStorageBuffer16BitAccessCapability()
    {
        declareCapability(spv::CapabilityStorageBuffer16BitAccess);
    }

    void SPIRVCapabilityTracker::ensureUniformAndStorageBuffer16BitAccessCapability()
    {
        declareCapability(spv::CapabilityUniformAndStorageBuffer16BitAccess);
    }

    void SPIRVCapabilityTracker::ensureStorageInputOutput16Capability()
    {
        declareCapability(spv::CapabilityStorageInputOutput16);
    }

    void SPIRVCapabilityTracker::ensureImageQueryCapability()
    {
        declareCapability(spv::CapabilityImageQuery);
    }

    void SPIRVCapabilityTracker::ensureInputAttachmentCapability()
    {
        declareCapability(spv::CapabilityInputAttachment);
    }

    void SPIRVCapabilityTracker::ensureStorageImageExtendedFormatsCapability()
    {
        declareCapability(spv::CapabilityStorageImageExtendedFormats);
    }

    void SPIRVCapabilityTracker::ensureGroupNonUniformCapability()
    {
        declareCapability(spv::CapabilityGroupNonUniform);
    }

    void SPIRVCapabilityTracker::ensureGroupNonUniformBallotCapability()
    {
        ensureGroupNonUniformCapability();
        declareCapability(spv::CapabilityGroupNonUniformBallot);
    }

    void SPIRVCapabilityTracker::ensureGroupNonUniformShuffleCapability()
    {
        ensureGroupNonUniformCapability();
        declareCapability(spv::CapabilityGroupNonUniformShuffle);
    }

    void SPIRVCapabilityTracker::ensureShaderNonUniformCapability()
    {
        declareCapability(spv::CapabilityShaderNonUniform);
    }

    void SPIRVCapabilityTracker::ensureSampledImageArrayNonUniformIndexingCapability()
    {
        if (!isDeclared(spv::CapabilitySampledImageArrayNonUniformIndexing))
        {
            ensureShaderNonUniformCapability();
            declareCapability(spv::CapabilitySampledImageArrayNonUniformIndexing);
            declareExtension("SPV_EXT_descriptor_indexing");
        }
    }

    void SPIRVCapabilityTracker::ensureStorageImageArrayNonUniformIndexingCapability()
    {
        if (!isDeclared(spv::CapabilityStorageImageArrayNonUniformIndexing))
        {
            ensureShaderNonUniformCapability();
            declareCapability(spv::CapabilityStorageImageArrayNonUniformIndexing);
        }
    }

    void SPIRVCapabilityTracker::ensureImageArrayDynamicIndexingCapability(bool storage)
    {
        declareCapability(storage ? spv::CapabilityStorageImageArrayDynamicIndexing : spv::CapabilitySampledImageArrayDynamicIndexing);
    }

    void SPIRVCapabilityTracker::ensureRuntimeDescriptorArrayCapability()
    {
        if (!isDeclared(spv::CapabilityRuntimeDescriptorArray))
        {
            declareCapability(spv::CapabilityRuntimeDescriptorArray);
            declareExtension("SPV_EXT_descriptor_indexing");
        }
    }

    void SPIRVCapabilityTracker::ensureFragmentBarycentricCapability()
    {
        if (!isDeclared(spv::CapabilityFragmentBarycentricKHR))
        {
            declareCapability(spv::CapabilityFragmentBarycentricKHR);
            declareExtension("SPV_KHR_fragment_shader_barycentric");
        }
    }

    void SPIRVCapabilityTracker::ensureFragmentPrimitiveIdCapability()
    {
        declareCapability(spv::CapabilityTessellation);
    }

    const SPIRVInstructionBuilder &SPIRVCapabilityTracker::getCapabilities() const
    {
        return mCapabilities;
    }

    const SPIRVInstructionBuilder &SPIRVCapabilityTracker::getExtensions() const
    {
        return mExtensions;
    }

    bool SPIRVCapabilityTracker::isDeclared(spv::Capability capability) const
    {
        return mDeclaredCapabilities.find(static_cast<uint32_t>(capability)) != mDeclaredCapabilities.end();
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
