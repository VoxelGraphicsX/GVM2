#pragma once

#include <string_view>

namespace UGLC::CodeGen
{
    enum class TextureMemberCallKind
    {
        Unknown,
        GetDimensions,
        Write,
        Read,
        SampleGrad,
        SampleLevel,
        Sample,
        GatherCmp,
        Gather,
        GatherRed,
        GatherGreen,
        GatherBlue,
        GatherAlpha,
    };

    inline TextureMemberCallKind classifyTextureMemberCall(std::string_view methodName)
    {
        if (methodName == "getDimensions")
        {
            return TextureMemberCallKind::GetDimensions;
        }
        if (methodName == "write")
        {
            return TextureMemberCallKind::Write;
        }
        if (methodName == "read")
        {
            return TextureMemberCallKind::Read;
        }
        if (methodName == "sampleGrad")
        {
            return TextureMemberCallKind::SampleGrad;
        }
        if (methodName == "sampleLevel")
        {
            return TextureMemberCallKind::SampleLevel;
        }
        if (methodName == "sample")
        {
            return TextureMemberCallKind::Sample;
        }
        if (methodName == "gatherCmp")
        {
            return TextureMemberCallKind::GatherCmp;
        }
        if (methodName == "gather")
        {
            return TextureMemberCallKind::Gather;
        }
        if (methodName == "gatherRed")
        {
            return TextureMemberCallKind::GatherRed;
        }
        if (methodName == "gatherGreen")
        {
            return TextureMemberCallKind::GatherGreen;
        }
        if (methodName == "gatherBlue")
        {
            return TextureMemberCallKind::GatherBlue;
        }
        if (methodName == "gatherAlpha")
        {
            return TextureMemberCallKind::GatherAlpha;
        }
        return TextureMemberCallKind::Unknown;
    }

    inline std::string_view getHLSLGatherIntrinsic(TextureMemberCallKind kind)
    {
        switch (kind)
        {
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
            return "GatherRed";
        case TextureMemberCallKind::GatherGreen:
            return "GatherGreen";
        case TextureMemberCallKind::GatherBlue:
            return "GatherBlue";
        case TextureMemberCallKind::GatherAlpha:
            return "GatherAlpha";
        default:
            return {};
        }
    }

    inline std::string_view getMSLGatherComponent(TextureMemberCallKind kind)
    {
        switch (kind)
        {
        case TextureMemberCallKind::GatherGreen:
            return "component::y";
        case TextureMemberCallKind::GatherBlue:
            return "component::z";
        case TextureMemberCallKind::GatherAlpha:
            return "component::w";
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        default:
            return "component::x";
        }
    }
} // namespace UGLC::CodeGen
