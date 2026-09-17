#pragma once

/**
 * Draws a small debug overlay when projection or scatter overflow counters are non-zero.
 *
 * Include this header inside namespace GsViewer after DebugCounterBindGroup and
 * OutputBindGroup have been declared.
 */
class [[LocalWorkGroupSize(8, 8, 1)]] DebugOverlayPass final : public IComputeClass
{
public:
    /**
     * Binds overflow counters and the output texture used for the debug overlay.
     */
    constructor(BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot0]], BindGroup<OutputBindGroup> outputBindGroup [[Slot1]])
    {
    }

private:
    /**
     * Writes a 12-by-12 overlay marker when projection or scatter overflow has occurred.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x >= 12u || threadID.y >= 12u)
        {
            return;
        }

        float3 debugColor = float3(0.0f);
        if (atomicLoad(debugCounterBindGroup->duplicateOverflowCounter[0]) > 0u)
        {
            debugColor += float3(1.0f, 0.0f, 1.0f);
        }
        if (atomicLoad(debugCounterBindGroup->scatterOverflowCounter[0]) > 0u)
        {
            debugColor += float3(0.0f, 1.0f, 1.0f);
        }
        if (debugColor.x <= 0.0f && debugColor.y <= 0.0f && debugColor.z <= 0.0f)
        {
            return;
        }

        outputBindGroup->outputTexture->write(threadID.xy, half4(saturate(debugColor), 1.0f));
    }
};

/**
 * Clears the half-float output texture to the current viewer background color.
 *
 * Include this pass in samples that render into an RGBA16Float storage texture
 * before optional swapchain presentation or diagnostic readback.
 */
class [[LocalWorkGroupSize(8, 8, 1)]] ClearOutputPass final : public IComputeClass
{
public:
    /**
     * Binds the frame background state and output texture to clear.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<OutputBindGroup> outputBindGroup [[Slot1]])
    {
    }

private:
    /**
     * Clears one output pixel to the current background color.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        if (threadID.x >= globals.imageInfo.x || threadID.y >= globals.imageInfo.y)
        {
            return;
        }

        outputBindGroup->outputTexture->write(threadID.xy, half4(backgroundColor(globals), 1.0f));
    }
};
