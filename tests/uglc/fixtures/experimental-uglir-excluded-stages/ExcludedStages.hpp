#pragma once
#include "UGL.h"

using namespace UGL;

/** Provides a valid vertex output so the test reaches the excluded stage checks. */
struct ExcludedStageOutput
{
    float4 position [[Position]];
};

/** Declares stages excluded from the user's requested experimental support scope. */
class ExcludedStagesPass final : public IRenderClass
{
public:
    /** Creates the resource-free stage diagnostic fixture. */
    constructor() {}

private:
    /** Supplies the required vertex entry without resource or layout dependencies. */
    ExcludedStageOutput vertex(uint vertexID [[VertexID]])
    {
        ExcludedStageOutput output;
        output.position = float4(float(vertexID), 0.0f, 0.0f, 1.0f);
        return output;
    }

    /** Exercises explicit rejection of the excluded hull stage. */
    void hull() {}

    /** Exercises explicit rejection of the excluded domain stage. */
    void domain() {}
};
