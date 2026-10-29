#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/vector.h>
#include <cstdint>

namespace GVM::RHI::Private
{
    /// Returns the bit used to represent a color attachment in a pixel-local access mask.
    uint64_t makePixelLocalColorAttachmentBit(uint32_t colorAttachmentIndex);

    /// Returns a color attachment mask with the first colorAttachmentCount bits set.
    uint64_t makeAllPixelLocalColorAttachmentMask(uint32_t colorAttachmentCount);

    /// Reports whether the requested color attachment is read by a pixel-local pass.
    bool isPixelLocalColorAttachmentRead(const PixelLocalPassAttachmentAccess &access, uint32_t colorAttachmentIndex);

    /// Reports whether the requested color attachment is written by a pixel-local pass.
    bool isPixelLocalColorAttachmentWritten(const PixelLocalPassAttachmentAccess &access, uint32_t colorAttachmentIndex);

    /// Reports whether source-level pixel-local attachment access metadata was provided.
    bool hasExplicitPixelLocalAttachmentAccess(const PixelLocalPassAttachmentAccess &access);

    /// Reports whether a render pipeline descriptor participates in the pixel-local path.
    bool isPixelLocalRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor);

    /// Derives ordinary raster pass writes from pipeline color/depth write state.
    PixelLocalPassAttachmentAccess derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(const RenderPipelineDescriptor &descriptor);

    /// Merges source pixel-local attachment access into destination without clearing existing bits.
    void mergePixelLocalPassAttachmentAccess(PixelLocalPassAttachmentAccess &destination, const PixelLocalPassAttachmentAccess &source);

    /// Builds per-pass pixel-local attachment access from ordered draw tasks and explicit next-pass boundaries.
    class PixelLocalPassAccessBuilder final
    {
    public:
        /// Adds one draw task's attachment access to the current pixel-local pass.
        void appendTask(const PixelLocalPassAttachmentAccess &access);

        /// Opens the next pixel-local pass, preserving empty state-only phases.
        void nextPass();

        /// Returns one access entry per encoded pixel-local pass, including empty state-only passes.
        eastl::vector<PixelLocalPassAttachmentAccess> finish();

    private:
        /// Ensures that the builder has a current pass before a task is appended.
        void ensurePass();

        eastl::vector<PixelLocalPassAttachmentAccess> mPasses;
    };

    /// Normalizes pass access to the render pass count.
    /// Pixel-local render passes must provide exact metadata; non-pixel-local render passes use
    /// compatibility access that keeps their color/depth attachments live.
    eastl::vector<PixelLocalPassAttachmentAccess> normalizePixelLocalPassAttachmentAccesses(
        uint32_t colorAttachmentCount,
        bool hasDepthAttachment,
        bool pixelLocalEnabled,
        uint32_t passCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses);

    /// Checks whether a color attachment's current contents are needed before the next write.
    bool colorAttachmentWillBeReadBeforeNextWrite(
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses,
        uint32_t startPassIndex,
        uint32_t colorAttachmentIndex);

} // namespace GVM::RHI::Private
