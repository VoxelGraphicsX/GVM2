#pragma once

/**
 * Converts the compact visible-entry count into an indirect dispatch for duplicate-counter projection passes.
 *
 * Include this pass only in samples where ProjectGaussians consumes the entries counted in FrameStateBindGroup's
 * duplicateCounter and where DuplicateWorkGroupSize matches the projection compute pass local size.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] BuildProjectDispatchPass final : public IComputeClass
{
public:
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]],
                BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
    )
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint workgroupCount = max((activeCount + DuplicateWorkGroupSize - 1u) / DuplicateWorkGroupSize, 1u);
        entryDispatchBindGroup->entryDispatch[0].x = workgroupCount;
        entryDispatchBindGroup->entryDispatch[0].y = 1u;
        entryDispatchBindGroup->entryDispatch[0].z = 1u;
    }
};
