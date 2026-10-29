#include "ComputePassValidator.hpp"

#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <GVMRHI/Private/TextureViewResolution.hpp>

#include <stdexcept>

namespace GVM::RHI::Private
{
    void ComputePassValidator::init(const eastl::string &passLabel, const ComputePassLimits &limits)
    {
        mPassLabel = passLabel;
        mLimits = limits;
        mBoundGroups.assign(limits.maxBindGroups, BoundGroup{});
    }

    void ComputePassValidator::setPipeline(const ComputePipeline &pipeline, const PipelineLayout &layout, const PipelineLayoutDescriptor *layoutDescriptor)
    {
        if (pipeline == nullptr)
        {
            throw std::invalid_argument("ComputePassEncoder::setPipeline requires a valid pipeline.");
        }
        if (pipeline.get() != mPipeline.get())
        {
            mHasArguments = false;
            mArgumentTextureUses.clear();
        }
        mPipeline = pipeline;
        mLayout = layout;
        mLayoutDescriptor = layoutDescriptor;
        mBindGroupCheckDirty = true;
        mTextureRoleCheckDirty = true;
    }

    void ComputePassValidator::checkArguments(const PipelineLayout &snapshotLayout) const
    {
        if (mPipeline == nullptr || snapshotLayout != mLayout)
        {
            throw std::invalid_argument("Compute Args snapshot does not belong to the selected pipeline layout.");
        }
    }

    void ComputePassValidator::setArguments(eastl::span<const PassTextureUse> textureUses)
    {
        mArgumentTextureUses.assign(textureUses.begin(), textureUses.end());
        mHasArguments = true;
        mTextureRoleCheckDirty = true;
    }

    void ComputePassValidator::checkBindGroup(const BindGroup &group, uint32_t groupIndex) const
    {
        if (group == nullptr)
        {
            throw std::invalid_argument("ComputePassEncoder::setBindGroup requires a valid bind group.");
        }
        if (groupIndex >= mLimits.maxBindGroups)
        {
            throw std::invalid_argument((eastl::string("ComputePassEncoder::setBindGroup supports at most ") +
                eastl::to_string(mLimits.maxBindGroups) + " bind groups.").c_str());
        }
    }

    void ComputePassValidator::setBindGroup(const BindGroup &group, uint32_t groupIndex, const BindGroupLayout &layout, eastl::span<const PassTextureUse> textureUses)
    {
        checkBindGroup(group, groupIndex);
        mBoundGroups[groupIndex] = BoundGroup{group, layout, textureUses};
        mBindGroupCheckDirty = true;
        mTextureRoleCheckDirty = true;
    }

    void ComputePassValidator::checkDispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        requirePipelineBound("dispatchWorkgroups");
        const eastl::array<uint32_t, 3> &limits = mLimits.maxWorkgroupCount;
        if (x > limits[0] || y > limits[1] || z > limits[2])
        {
            throw std::out_of_range((eastl::string("ComputePassEncoder::dispatchWorkgroups requested ") +
                eastl::to_string(x) + "x" + eastl::to_string(y) + "x" + eastl::to_string(z) +
                " workgroups, exceeding the device limit of " +
                eastl::to_string(limits[0]) + "x" + eastl::to_string(limits[1]) + "x" + eastl::to_string(limits[2]) + ".").c_str());
        }
        requireDispatchBindings("dispatchWorkgroups");
        requireCompatibleTextureRoles();
    }

    void ComputePassValidator::checkDispatchIndirect(const BufferRange &indirectBuffer)
    {
        requirePipelineBound("dispatchWorkgroupsIndirect");
        if (indirectBuffer.size < sizeof(uint32_t) * 3u)
        {
            throw std::out_of_range("Indirect dispatch requires three native workgroup counts.");
        }
        requireDispatchBindings("dispatchWorkgroupsIndirect");
        requireCompatibleTextureRoles();
    }

    void ComputePassValidator::requirePipelineBound(const char *apiName) const
    {
        if (mPipeline == nullptr)
        {
            throw std::logic_error((eastl::string("ComputePassEncoder::") + apiName +
                " requires a pipeline to be bound first.").c_str());
        }
    }

    bool ComputePassValidator::pipelineDeclaresArguments() const
    {
        return mLayoutDescriptor != nullptr && mLayoutDescriptor->arguments.byteSize != 0u;
    }

    void ComputePassValidator::requireDispatchBindings(const char *apiName)
    {
        if (mLayoutDescriptor == nullptr)
        {
            return;
        }
        if (pipelineDeclaresArguments())
        {
            requireArgumentSnapshot(true, mHasArguments);
            return;
        }
        if (mBindGroupCheckDirty)
        {
            mBindGroupsSatisfied = true;
            const eastl::vector<BindGroupLayout> &required = mLayoutDescriptor->bindGroupLayouts;
            for (uint32_t groupIndex = 0; groupIndex < required.size(); ++groupIndex)
            {
                if (required[groupIndex] == nullptr)
                {
                    continue;
                }
                if (groupIndex >= mBoundGroups.size() || mBoundGroups[groupIndex].layout.get() != required[groupIndex].get())
                {
                    mBindGroupsSatisfied = false;
                    break;
                }
            }
            mBindGroupCheckDirty = false;
        }
        if (!mBindGroupsSatisfied)
        {
            throw std::logic_error(buildMissingBindGroupMessage(apiName).c_str());
        }
    }

    void ComputePassValidator::requireCompatibleTextureRoles()
    {
        if (!mTextureRoleCheckDirty || mLayoutDescriptor == nullptr)
        {
            return;
        }
        const bool arguments = pipelineDeclaresArguments();
        eastl::unordered_map<const TextureImpl *, bool> storageRoles;
        if (arguments)
        {
            checkTextureRoleUses(mArgumentTextureUses, true, storageRoles);
        }
        else
        {
            const eastl::vector<BindGroupLayout> &required = mLayoutDescriptor->bindGroupLayouts;
            for (uint32_t groupIndex = 0; groupIndex < required.size() && groupIndex < mBoundGroups.size(); ++groupIndex)
            {
                const BoundGroup &bound = mBoundGroups[groupIndex];
                if (bound.group == nullptr || required[groupIndex] == nullptr || bound.layout.get() != required[groupIndex].get())
                {
                    continue;
                }
                checkTextureRoleUses(bound.textureUses, false, storageRoles);
            }
        }
        mTextureRoleCheckDirty = false;
    }

    void ComputePassValidator::checkTextureRoleUses(eastl::span<const PassTextureUse> uses, bool arguments,
        eastl::unordered_map<const TextureImpl *, bool> &storageRoles) const
    {
        for (const PassTextureUse &use : uses)
        {
            if (use.texture == nullptr)
            {
                continue;
            }
            const auto [role, inserted] = storageRoles.emplace(use.texture, use.storage);
            if (!inserted && role->second != use.storage)
            {
                throw std::logic_error((eastl::string("Compute pass '") + mPassLabel + "': " +
                    (arguments ? "Args texture '" : "texture '") + describeTextureLabel(use.texture) +
                    "' is used both as a sampled texture and as a storage texture in one dispatch; split these accesses into ordered compute dispatches.").c_str());
            }
        }
    }

    eastl::string ComputePassValidator::buildMissingBindGroupMessage(const char *apiName) const
    {
        eastl::string message = eastl::string("ComputePassEncoder::") + apiName + " cannot execute because pipeline '";
        message += (mPipeline != nullptr && !mPipeline->getLabelName().empty()) ? mPipeline->getLabelName().c_str() : "<unnamed>";
        message += "' has unresolved bind groups: ";
        bool hasIssue = false;
        const eastl::vector<BindGroupLayout> &required = mLayoutDescriptor->bindGroupLayouts;
        for (uint32_t groupIndex = 0; groupIndex < required.size(); ++groupIndex)
        {
            if (required[groupIndex] == nullptr)
            {
                continue;
            }
            const bool bound = groupIndex < mBoundGroups.size() && mBoundGroups[groupIndex].group != nullptr;
            const bool compatible = bound && mBoundGroups[groupIndex].layout.get() == required[groupIndex].get();
            if (compatible)
            {
                continue;
            }
            if (hasIssue)
            {
                message += "; ";
            }
            message += "set ";
            message += eastl::to_string(groupIndex);
            if (!bound)
            {
                message += " not bound";
            }
            else
            {
                const eastl::string &groupLabel = mBoundGroups[groupIndex].group->getLabelName();
                message += " bound to incompatible bind group '";
                message += groupLabel.empty() ? "<unnamed>" : groupLabel.c_str();
                message += "'";
            }
            hasIssue = true;
        }
        message += ".";
        return message;
    }
} // namespace GVM::RHI::Private
