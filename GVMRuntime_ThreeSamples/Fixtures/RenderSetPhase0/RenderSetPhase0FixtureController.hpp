#pragma once

#include <GVMCore/GVMCore.Public.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Tracks the deterministic entity lifecycle exercised by the Phase 0 RenderSet fixture. */
    struct RenderSetPhase0FixtureState
    {
        GVM::Core::RenderEntityIndex removedSingleEntity = UINT32_MAX;
        GVM::Core::RenderEntityIndex instancedEntity = UINT32_MAX;
        GVM::Core::RenderEntityIndex replacementEntity = UINT32_MAX;
        bool initialized = false;
        bool mutationApplied = false;
    };

    /** Populates and mutates the generated Phase 0 RenderSet through its exported command ABI. */
    class RenderSetPhase0FixtureController final
    {
    public:
        /** Allocates one ordinary entity and one three-instance entity before the first frame. */
        void initialize(GVM::Core::AbstractRendererImpl &renderer);

        /** Removes and reallocates the ordinary entity immediately before deterministic frame one. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, uint32_t frameIndex);

        /** Writes the RenderSet structure and entity lifecycle expected by the visual fixture. */
        void writeSnapshot(const std::filesystem::path &outputPath) const;

        /** Returns the entity lifecycle state recorded by the fixture controller. */
        [[nodiscard]] const RenderSetPhase0FixtureState &getState() const;

    private:
        /** Allocates the single-instance triangle used before or after the lifecycle mutation. */
        GVM::Core::RenderEntityIndex allocateSingleEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            bool replacement) const;

        /** Allocates the three-instance quad entity used by both deterministic fixture frames. */
        GVM::Core::RenderEntityIndex allocateInstancedEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const;

        RenderSetPhase0FixtureState state;
    };
} // namespace GVM::ThreeSamples
