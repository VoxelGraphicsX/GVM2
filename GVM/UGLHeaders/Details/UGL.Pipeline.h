#pragma once

namespace UGL
{
    enum class BlendFactor
    {
        Zero = 0x00000000,
        One = 0x00000001,
        Src = 0x00000002,
        OneMinusSrc = 0x00000003,
        SrcAlpha = 0x00000004,
        OneMinusSrcAlpha = 0x00000005,
        Dst = 0x00000006,
        OneMinusDst = 0x00000007,
        DstAlpha = 0x00000008,
        OneMinusDstAlpha = 0x00000009,
        SrcAlphaSaturated = 0x0000000A,
        Constant = 0x0000000B,
        OneMinusConstant = 0x0000000C,
        Force32 = 0x7FFFFFFF
    };

    enum class BlendOperation
    {
        Add = 0x00000000,
        Subtract = 0x00000001,
        ReverseSubtract = 0x00000002,
        Min = 0x00000003,
        Max = 0x00000004,
        Force32 = 0x7FFFFFFF
    };

    struct BlendComponent
    {
        BlendOperation operation = BlendOperation::Add;
        BlendFactor srcFactor = BlendFactor::One;
        BlendFactor dstFactor = BlendFactor::One;
    };

    struct BlendState
    {
        BlendComponent color;
        BlendComponent alpha;
    };

    enum class CullMode
    {
        None = 0x00000000,
        Front = 0x00000001,
        Back = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class PrimitiveTopology
    {
        PointList = 0x00000000,
        LineList = 0x00000001,
        LineStrip = 0x00000002,
        TriangleList = 0x00000003,
        TriangleStrip = 0x00000004,
        PatchList = 0x00000005,
        Force32 = 0x7FFFFFFF
    };

    class RenderPipelineDescriptor final
    {
    };
} // namespace UGL
