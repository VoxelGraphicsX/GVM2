#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/unordered_map.h>

namespace GVM::RHI::Vulkan
{
    class VKPresentManager final
    {
    public:
        struct DirectPresentSubmitSync
        {
            const VKTexture *texture = nullptr;
            vk::Semaphore acquireSemaphore = nullptr;
            vk::PipelineStageFlags2 acquireStageMask = {};
            vk::Semaphore renderCompleteSemaphore = nullptr;
        };

        void reset();

        void registerDirectPresentTexture(const VKTexture *texture);
        void unregisterDirectPresentTexture(const VKTexture *texture);
        void resetDirectPresentSyncState(const VKTexture *texture);

        void setDirectPresentAcquireState(
            const VKTexture *texture,
            vk::Semaphore acquireSemaphore,
            vk::PipelineStageFlags2 waitStageMask,
            vk::Semaphore renderCompleteSemaphore);
        /** Sets the first-use wait stage; returns true only when chaining the external acquire. */
        [[nodiscard]] bool setDirectPresentFirstUse(const VKTexture &texture, vk::PipelineStageFlags2 stageMask);
        void setDirectPresentSubmissionArmed(const VKTexture *texture);
        void setDirectPresentPresented(const VKTexture *texture);

        [[nodiscard]] bool isDirectPresentTexture(const VKTexture *texture) const;
        [[nodiscard]] bool isDirectPresentSubmissionArmed(const VKTexture *texture) const;
        [[nodiscard]] bool tryGetDirectPresentSubmitSync(const VKTexture *texture, DirectPresentSubmitSync &outSync) const;
        [[nodiscard]] bool tryFindPendingDirectPresentSubmit(const eastl::vector<Texture> &retainedTextures, DirectPresentSubmitSync &outSync) const;
        [[nodiscard]] vk::Semaphore getDirectPresentRenderCompleteSemaphore(const VKTexture *texture) const;

    private:
        struct DirectPresentTextureState
        {
            vk::Semaphore acquireSemaphore = nullptr;
            vk::PipelineStageFlags2 acquireStageMask = {};
            vk::Semaphore renderCompleteSemaphore = nullptr;
            bool acquirePending = false;
            bool submissionArmed = false;
        };

        [[nodiscard]]
        DirectPresentTextureState *findDirectPresentStateLocked(const VKTexture *texture);

        [[nodiscard]]
        const DirectPresentTextureState *findDirectPresentStateLocked(const VKTexture *texture) const;

        [[nodiscard]]
        DirectPresentTextureState &requireDirectPresentStateLocked(const VKTexture *texture, const char *apiName);

        mutable Mutex mMutex;
        eastl::unordered_map<const VKTexture *, DirectPresentTextureState> mDirectPresentTextures;
    };
} // namespace GVM::RHI::Vulkan
