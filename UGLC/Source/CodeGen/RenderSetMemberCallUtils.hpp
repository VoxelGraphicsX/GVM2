#pragma once

#include <string_view>

namespace UGLC::CodeGen
{
    enum class RenderSetBufferComponentCallKind
    {
        Unknown,
        GetRaw,
        CheckValid,
        Get,
    };

    enum class RenderSetTextureComponentCallKind
    {
        Unknown,
        Get,
    };

    enum class RenderSetDataPackCallKind
    {
        Unknown,
        GetRenderEntityInfo,
        GetRenderEntityCMDParams,
        GetRenderEntityIndexCount,
        GetRenderEntityInstanceCount,
        GetRenderEntityFirstIndex,
        GetRenderEntityVertexOffset,
        GetRenderEntityGlobalInstanceBase,
        GetRenderEntityVersion,
        CheckValid,
    };

    inline RenderSetBufferComponentCallKind classifyRenderSetBufferComponentCall(std::string_view methodName)
    {
        if (methodName == "getRaw")
        {
            return RenderSetBufferComponentCallKind::GetRaw;
        }
        if (methodName == "checkValid")
        {
            return RenderSetBufferComponentCallKind::CheckValid;
        }
        if (methodName == "get")
        {
            return RenderSetBufferComponentCallKind::Get;
        }
        return RenderSetBufferComponentCallKind::Unknown;
    }

    inline RenderSetTextureComponentCallKind classifyRenderSetTextureComponentCall(std::string_view methodName)
    {
        if (methodName == "get")
        {
            return RenderSetTextureComponentCallKind::Get;
        }
        return RenderSetTextureComponentCallKind::Unknown;
    }

    inline RenderSetDataPackCallKind classifyRenderSetDataPackCall(std::string_view methodName)
    {
        if (methodName == "getRenderEntityInfo")
        {
            return RenderSetDataPackCallKind::GetRenderEntityInfo;
        }
        if (methodName == "getRenderEntityCMDParams")
        {
            return RenderSetDataPackCallKind::GetRenderEntityCMDParams;
        }
        if (methodName == "getRenderEntityIndexCount")
        {
            return RenderSetDataPackCallKind::GetRenderEntityIndexCount;
        }
        if (methodName == "getRenderEntityInstanceCount")
        {
            return RenderSetDataPackCallKind::GetRenderEntityInstanceCount;
        }
        if (methodName == "getRenderEntityFirstIndex")
        {
            return RenderSetDataPackCallKind::GetRenderEntityFirstIndex;
        }
        if (methodName == "getRenderEntityVertexOffset")
        {
            return RenderSetDataPackCallKind::GetRenderEntityVertexOffset;
        }
        if (methodName == "getRenderEntityGlobalInstanceBase")
        {
            return RenderSetDataPackCallKind::GetRenderEntityGlobalInstanceBase;
        }
        if (methodName == "getRenderEntityVersion")
        {
            return RenderSetDataPackCallKind::GetRenderEntityVersion;
        }
        if (methodName == "checkValid")
        {
            return RenderSetDataPackCallKind::CheckValid;
        }
        return RenderSetDataPackCallKind::Unknown;
    }
} // namespace UGLC::CodeGen
