#include "VKPresentManager.hpp"

#include "VKTexture.hpp"

namespace GVM::RHI::Vulkan
{
    void VKPresentManager::reset()
    {
        ScopedLock lock(mMutex);
        mDirectPresentTextures.clear();
    }

    void VKPresentManager::registerDirectPresentTexture(const VKTexture *texture)
    {
        if (texture == nullptr)
        {
            throw makeInvalidArgument("VKPresentManager::registerDirectPresentTexture requires a valid texture.");
        }

        ScopedLock lock(mMutex);
        mDirectPresentTextures[texture] = {};
    }

    void VKPresentManager::unregisterDirectPresentTexture(const VKTexture *texture)
    {
        if (texture == nullptr)
        {
            return;
        }

        ScopedLock lock(mMutex);
        mDirectPresentTextures.erase(texture);
    }

    void VKPresentManager::resetDirectPresentSyncState(const VKTexture *texture)
    {
        ScopedLock lock(mMutex);
        DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
        if (state == nullptr)
        {
            return;
        }

        *state = {};
    }

    void VKPresentManager::setDirectPresentAcquireState(
        const VKTexture *texture,
        vk::Semaphore acquireSemaphore,
        vk::PipelineStageFlags waitStageMask,
        vk::Semaphore renderCompleteSemaphore)
    {
        ScopedLock lock(mMutex);
        DirectPresentTextureState &state = requireDirectPresentStateLocked(
            texture,
            "VKPresentManager::setDirectPresentAcquireState");
        state.acquireSemaphore = acquireSemaphore;
        state.acquireStageMask = waitStageMask;
        state.renderCompleteSemaphore = renderCompleteSemaphore;
        state.acquirePending = true;
        state.submissionArmed = false;
    }

    void VKPresentManager::setDirectPresentSubmissionArmed(const VKTexture *texture)
    {
        ScopedLock lock(mMutex);
        DirectPresentTextureState &state = requireDirectPresentStateLocked(
            texture,
            "VKPresentManager::setDirectPresentSubmissionArmed");
        state.acquireSemaphore = nullptr;
        state.acquireStageMask = {};
        state.acquirePending = false;
        state.submissionArmed = true;
    }

    void VKPresentManager::setDirectPresentPresented(const VKTexture *texture)
    {
        ScopedLock lock(mMutex);
        DirectPresentTextureState &state = requireDirectPresentStateLocked(
            texture,
            "VKPresentManager::setDirectPresentPresented");
        state.acquireSemaphore = nullptr;
        state.acquireStageMask = {};
        state.renderCompleteSemaphore = nullptr;
        state.acquirePending = false;
        state.submissionArmed = false;
    }

    bool VKPresentManager::isDirectPresentTexture(const VKTexture *texture) const
    {
        ScopedLock lock(mMutex);
        return findDirectPresentStateLocked(texture) != nullptr;
    }

    bool VKPresentManager::isDirectPresentSubmissionArmed(const VKTexture *texture) const
    {
        ScopedLock lock(mMutex);
        const DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
        return state != nullptr && state->submissionArmed;
    }

    bool VKPresentManager::tryGetDirectPresentSubmitSync(const VKTexture *texture, DirectPresentSubmitSync &outSync) const
    {
        ScopedLock lock(mMutex);
        const DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
        if (state == nullptr ||
            !state->acquirePending ||
            !static_cast<bool>(state->acquireSemaphore) ||
            !static_cast<bool>(state->renderCompleteSemaphore))
        {
            outSync = {};
            return false;
        }

        outSync = {
            .texture = texture,
            .acquireSemaphore = state->acquireSemaphore,
            .acquireStageMask = state->acquireStageMask,
            .renderCompleteSemaphore = state->renderCompleteSemaphore,
        };
        return true;
    }

    bool VKPresentManager::tryFindPendingDirectPresentSubmit(const eastl::vector<Texture> &retainedTextures, DirectPresentSubmitSync &outSync) const
    {
        ScopedLock lock(mMutex);
        outSync = {};
        for (const Texture &retainedTextureHandle : retainedTextures)
        {
            auto *texture = static_cast<const VKTexture *>(retainedTextureHandle.get());
            const DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
            if (state == nullptr ||
                !state->acquirePending ||
                !static_cast<bool>(state->acquireSemaphore) ||
                !static_cast<bool>(state->renderCompleteSemaphore))
            {
                continue;
            }

            if (outSync.texture != nullptr && outSync.texture != texture)
            {
                throw makeLogicError("VKPresentManager::tryFindPendingDirectPresentSubmit found multiple pending direct-present swapchain textures in a single submission.");
            }

            outSync = {
                .texture = texture,
                .acquireSemaphore = state->acquireSemaphore,
                .acquireStageMask = state->acquireStageMask,
                .renderCompleteSemaphore = state->renderCompleteSemaphore,
            };
        }

        return outSync.texture != nullptr;
    }

    vk::Semaphore VKPresentManager::getDirectPresentRenderCompleteSemaphore(const VKTexture *texture) const
    {
        ScopedLock lock(mMutex);
        const DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
        return state != nullptr ? state->renderCompleteSemaphore : vk::Semaphore{};
    }

    VKPresentManager::DirectPresentTextureState *VKPresentManager::findDirectPresentStateLocked(const VKTexture *texture)
    {
        if (texture == nullptr)
        {
            return nullptr;
        }

        const auto existing = mDirectPresentTextures.find(texture);
        return existing != mDirectPresentTextures.end() ? &existing->second : nullptr;
    }

    const VKPresentManager::DirectPresentTextureState *VKPresentManager::findDirectPresentStateLocked(const VKTexture *texture) const
    {
        if (texture == nullptr)
        {
            return nullptr;
        }

        const auto existing = mDirectPresentTextures.find(texture);
        return existing != mDirectPresentTextures.end() ? &existing->second : nullptr;
    }

    VKPresentManager::DirectPresentTextureState &VKPresentManager::requireDirectPresentStateLocked(const VKTexture *texture, const char *apiName)
    {
        DirectPresentTextureState *state = findDirectPresentStateLocked(texture);
        if (state == nullptr)
        {
            throw makeLogicError(eastl::string(apiName) + " requires a registered direct-present texture.");
        }
        return *state;
    }
} // namespace GVM::RHI::Vulkan
