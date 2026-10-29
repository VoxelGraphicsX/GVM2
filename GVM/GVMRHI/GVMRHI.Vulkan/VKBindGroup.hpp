#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"
#include <GVMRHI/Private/RenderPassRecorder.hpp>

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKBindGroup final : public BindGroupImpl
    {
    public:
        struct ResolvedBinding
        {
            BindGroupLayoutEntry layoutEntry = {};
            BindGroupEntry descriptorEntry = {};
        };

        VKBindGroup() = default;
        ~VKBindGroup() override;

        void init(VKDevice *device, const BindGroupDescriptor &descriptor);
        /** Returns the owning device when capturing compatibility Args. */
        VKDevice &getDevice() const { return *mDevice; }

        [[nodiscard]] vk::DescriptorSet getNativeDescriptorSet() const;
        [[nodiscard]] const BindGroupLayout &getLayoutHandle() const;
        [[nodiscard]] const eastl::vector<ResolvedBinding> &getResolvedBindings() const;
        /** Returns the render-stage texture uses of this group in backend-neutral form for render-pass validation. */
        [[nodiscard]] eastl::span<const Private::PassTextureUse> getRenderTextureUses() const { return mRenderTextureUses; }
        /** Returns the texture subresource uses this group exposes to compute shaders. */
        [[nodiscard]] eastl::span<const Private::PassTextureUse> getComputeTextureUses() const { return mComputeTextureUses; }

        Logger getLogger() const;
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

    private:
        VKDevice *mDevice = nullptr;
        Logger mLogger;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        BindGroupDescriptor mDescriptor = {};
        BindGroupLayout mLayout = nullptr;
        eastl::vector<ResolvedBinding> mResolvedBindings;
        eastl::vector<Private::PassTextureUse> mRenderTextureUses;
        eastl::vector<Private::PassTextureUse> mComputeTextureUses;
        bool mResourceReferencesRegistered = false;
        vk::DescriptorPool mDescriptorPool = nullptr;
        vk::DescriptorSet mDescriptorSet;
    };
} // namespace GVM::RHI::Vulkan
