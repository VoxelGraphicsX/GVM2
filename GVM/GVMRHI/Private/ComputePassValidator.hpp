#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/RenderPassRecorder.hpp>

#include <EASTL/array.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::RHI::Private
{
    /** Device limits a compute pass validator checks against. */
    struct ComputePassLimits
    {
        uint32_t maxBindGroups = 0;
        eastl::array<uint32_t, 3> maxWorkgroupCount = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    };

    /** Backend-neutral validation and binding-state tracking for one compute pass.
        Both backends call it before recording or encoding each command, so user errors are reported with the
        same rules and messages on Vulkan and Metal, and a rejected call leaves the validator state unchanged. */
    class ComputePassValidator
    {
    public:
        /** Starts validation for a pass with the given label and device limits. */
        void init(const eastl::string &passLabel, const ComputePassLimits &limits);

        /** Selects pipeline; layout and layoutDescriptor describe its pipeline layout and must stay valid while the pipeline is alive. Selecting a different pipeline drops the current Args snapshot. */
        void setPipeline(const ComputePipeline &pipeline, const PipelineLayout &layout, const PipelineLayoutDescriptor *layoutDescriptor);

        /** Rejects an Args snapshot whose layout is not the selected pipeline's layout, or any snapshot before a pipeline is selected. */
        void checkArguments(const PipelineLayout &snapshotLayout) const;

        /** Records the compute-visible texture uses of the Args snapshot accepted by checkArguments. */
        void setArguments(eastl::span<const PassTextureUse> textureUses);

        /** Rejects a null bind group or an index at or above the device's bind group limit. */
        void checkBindGroup(const BindGroup &group, uint32_t groupIndex) const;

        /** Records group at groupIndex with its layout and compute-visible texture uses; textureUses must stay valid while the group is alive. */
        void setBindGroup(const BindGroup &group, uint32_t groupIndex, const BindGroupLayout &layout, eastl::span<const PassTextureUse> textureUses);

        /** Validates a direct dispatch: pipeline selected, workgroup counts within device limits, required Args or bind groups present, and no texture used as both sampled and storage. */
        void checkDispatch(uint32_t x, uint32_t y, uint32_t z);

        /** Validates an indirect dispatch: pipeline selected, a range holding three workgroup counts, required Args or bind groups present, and no texture used as both sampled and storage. */
        void checkDispatchIndirect(const BufferRange &indirectBuffer);

    private:
        /** One bind group recorded at a pipeline-layout index. */
        struct BoundGroup
        {
            BindGroup group = nullptr;
            BindGroupLayout layout = nullptr;
            eastl::span<const PassTextureUse> textureUses;
        };

        /** Throws when no pipeline has been selected. */
        void requirePipelineBound(const char *apiName) const;

        /** Requires the Args snapshot when the pipeline layout declares Args, otherwise every bind group the layout requires. */
        void requireDispatchBindings(const char *apiName);

        /** Throws when one texture is bound as both a sampled texture and a storage texture for the next dispatch. */
        void requireCompatibleTextureRoles();

        /** Adds uses to storageRoles (texture -> bound as storage) and throws when a texture already recorded with the other role appears; arguments selects the Args wording of the message. */
        void checkTextureRoleUses(eastl::span<const PassTextureUse> uses, bool arguments,
            eastl::unordered_map<const TextureImpl *, bool> &storageRoles) const;

        /** Reports whether the selected pipeline layout declares an Args block. */
        bool pipelineDeclaresArguments() const;

        /** Builds the message listing the bind groups the selected pipeline still needs. */
        eastl::string buildMissingBindGroupMessage(const char *apiName) const;

        eastl::string mPassLabel;
        ComputePassLimits mLimits;
        ComputePipeline mPipeline = nullptr;
        PipelineLayout mLayout = nullptr;
        const PipelineLayoutDescriptor *mLayoutDescriptor = nullptr;
        eastl::vector<BoundGroup> mBoundGroups;
        eastl::vector<PassTextureUse> mArgumentTextureUses;
        bool mHasArguments = false;
        bool mBindGroupCheckDirty = true;
        bool mBindGroupsSatisfied = false;
        bool mTextureRoleCheckDirty = true;
    };
} // namespace GVM::RHI::Private
