#pragma once

#include "UGL.h"

using namespace UGL;

/** Holds a matrix and scalar attributes that must retain their values across buffer copies. */
struct MatrixCopyPlacement
{
    float4x4 transform;
    uint marker;
    uint padding0;
    uint padding1;
    uint padding2;
};

/** Exercises nested structs and fixed arrays without prescribing a scene-specific layout. */
struct MatrixCopyRecord
{
    uint4 metadata;
    MatrixCopyPlacement placement;
    MatrixCopyPlacement entries[2];
};

namespace MatrixAggregateCopy
{
    // Four matrix inputs cross five construction/copy paths; each case owns four observations.
    static const uint MatrixKindCount = 4u;
    static const uint CaseCount = 20u;
    static const uint ObservationCount = 4u;

    /** Produces a nonsymmetric matrix, optionally through matrix multiplication. */
    float4x4 makeMatrix(uint matrixKind)
    {
        float4x4 matrix = float4x4(
            float4(1.0f, 2.0f, 3.0f, 4.0f),
            float4(5.0f, 6.0f, 7.0f, 8.0f),
            float4(9.0f, 10.0f, 11.0f, 12.0f),
            float4(13.0f, 14.0f, 15.0f, 16.0f));
        if ((matrixKind & 1u) != 0u)
        {
            matrix = (float4x4)0.0f;
            // A rotated basis with unequal axis scales exposes transposition independently of identity.
            matrix[0][0] = 1.6f;
            matrix[0][2] = 2.4f;
            matrix[1][1] = 3.0f;
            matrix[2][0] = -1.2f;
            matrix[2][2] = 3.2f;
            matrix[3][3] = 1.0f;
        }
        if (matrixKind >= 2u)
        {
            const float4x4 identity = float4x4(
                float4(1.0f, 0.0f, 0.0f, 0.0f),
                float4(0.0f, 1.0f, 0.0f, 0.0f),
                float4(0.0f, 0.0f, 1.0f, 0.0f),
                float4(0.0f, 0.0f, 0.0f, 1.0f));
            matrix = mul(identity, matrix);
        }
        return matrix;
    }

    /** Returns a complete aggregate so optimization must reconstruct a matrix-containing value. */
    MatrixCopyPlacement makePlacement(float4x4 matrix, uint copyPath)
    {
        MatrixCopyPlacement placement;
        placement.transform = matrix;
        placement.marker = copyPath;
        placement.padding0 = 0u;
        placement.padding1 = 0u;
        placement.padding2 = 0u;
        if (copyPath == 1u)
        {
            placement.marker = 17u;
            return placement;
        }
        return placement;
    }

    /** Copies a returned aggregate through a function output parameter. */
    void writePlacement(float4x4 matrix, uint copyPath, OUT MatrixCopyPlacement &placement)
    {
        placement = makePlacement(matrix, copyPath);
    }

    /** Builds the same values through nested assignment, output copyback, and local fixed arrays. */
    MatrixCopyRecord makeRecord(float4x4 matrix, uint copyPath)
    {
        MatrixCopyRecord record;
        record.metadata = uint4(copyPath);
        record.placement = makePlacement(matrix, copyPath);
        if (copyPath == 2u)
        {
            writePlacement(matrix, copyPath, record.placement);
        }
        if (copyPath == 3u)
        {
            MatrixCopyPlacement localEntries[2];
            localEntries[0] = makePlacement(matrix, copyPath);
            localEntries[1] = localEntries[0];
            record.placement = localEntries[1];
        }
        record.entries[0] = record.placement;
        record.entries[1] = record.placement;
        if (copyPath == 4u)
        {
            MatrixCopyRecord copy = record;
            copy.placement.transform = record.entries[1].transform;
            return copy;
        }
        return record;
    }

    /** Binds a physical aggregate cache and independent numerical observations. */
    struct Bindings final : public IBindGroup
    {
        /** Connects the shared records and floating-point result storage. */
        constructor(RWStructuredBuffer<MatrixCopyRecord> records [[Binding0]],
                    RWStructuredBuffer<float4> observations [[Binding1]])
        {
        }
    };

    /** Writes one aggregate per case and observes the logical value before serialization. */
    class [[LocalWorkGroupSize(1, 1, 1)]] Producer final : public IComputeClass
    {
    public:
        /** Creates a producer using the shared cache bindings. */
        constructor(BindGroup<Bindings> bindings [[Slot0]])
        {
        }

    private:
        /** Computes and stores a record without forwarding storage loads from another dispatch. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const float4x4 matrix = makeMatrix(threadID.x % MatrixKindCount);
            const MatrixCopyRecord record = makeRecord(matrix, threadID.x / MatrixKindCount);
            bindings->observations[threadID.x * ObservationCount] =
                mul(record.placement.transform, float4(1.0f, 2.0f, 3.0f, 1.0f));
            bindings->records[threadID.x] = record;
        }
    };

    /** Loads whole records, nested fixed arrays, and individual storage matrix members. */
    class [[LocalWorkGroupSize(1, 1, 1)]] Consumer final : public IComputeClass
    {
    public:
        /** Creates the independent consumer dispatch. */
        constructor(BindGroup<Bindings> bindings [[Slot0]])
        {
        }

    private:
        /** Exercises both physical-to-logical copies and direct member access chains. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const MatrixCopyRecord record = bindings->records[threadID.x];
            const float4 point = float4(1.0f, 2.0f, 3.0f, 1.0f);
            const uint base = threadID.x * ObservationCount;
            bindings->observations[base + 1u] = mul(record.placement.transform, point);
            bindings->observations[base + 2u] =
                mul(bindings->records[threadID.x].entries[1].transform, point);
            MatrixCopyPlacement entries[2] = {record.entries[0], record.entries[1]};
            bindings->records[threadID.x].entries[1] = entries[1];
            bindings->observations[base + 3u] = mul(entries[0].transform, point);
        }
    };

    /** Exposes the cache read-only to both raster stages. */
    struct RasterBindings final : public IBindGroup
    {
        /** Connects records produced by the earlier compute submission. */
        constructor(StructuredBuffer<MatrixCopyRecord> records [[Binding0]])
        {
        }
    };

    /** Keeps one numerical transform per case constant across the rasterized quad. */
    struct RasterOutput
    {
        float4 position [[Position]];
        float4 transformed [[Attribute0]];
        uint caseIndex [[Attribute1]];
    };

    /** Retains exact floating-point results from the vertex and fragment stages. */
    struct RasterFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA32Float> vertexResult;
        ColorAttachment<TextureFormat::RGBA32Float> fragmentResult;
    };

    /** Draws one pixel-wide quad per case into two headless floating-point attachments. */
    class Raster final : public IRenderClass
    {
    public:
        /** Creates raster verification using the existing aggregate cache. */
        constructor(BindGroup<RasterBindings> bindings [[Slot0]])
        {
        }

    private:
        /** Builds two triangles per case and calculates a vertex-stage matrix result. */
        RasterOutput vertex(uint vertexID [[VertexID]])
        {
            RasterOutput output;
            // A quad consists of six vertices; each case occupies one attachment column.
            const uint corner = vertexID % 6u;
            const uint caseIndex = vertexID / 6u;
            const float right = (corner == 1u || corner == 4u || corner == 5u) ? 1.0f : 0.0f;
            const float top = (corner == 2u || corner == 3u || corner == 5u) ? 1.0f : 0.0f;
            output.position = float4(
                (float(caseIndex) + right) / float(CaseCount) * 2.0f - 1.0f,
                top * 2.0f - 1.0f, 0.0f, 1.0f);
            const MatrixCopyRecord record = bindings->records[caseIndex];
            output.transformed = mul(record.placement.transform, float4(1.0f, 2.0f, 3.0f, 1.0f));
            output.caseIndex = caseIndex;
            return output;
        }

        /** Repeats the storage read in the fragment stage, independently of the vertex result. */
        RasterFrameBuffer fragment(RasterOutput input)
        {
            RasterFrameBuffer output;
            output.vertexResult = input.transformed;
            const MatrixCopyRecord record = bindings->records[input.caseIndex];
            output.fragmentResult = mul(record.entries[1].transform, float4(1.0f, 2.0f, 3.0f, 1.0f));
            return output;
        }
    };
}
