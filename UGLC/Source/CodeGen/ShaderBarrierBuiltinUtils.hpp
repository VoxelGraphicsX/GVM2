#pragma once

#include <CodeGen/UGLC.Constants.hpp>

#include <string>

namespace UGLC::CodeGen
{
    enum class ShaderBarrierBuiltinKind
    {
        None,
        GroupMemoryBarrier,
        GroupMemoryBarrierWithGroupSync,
        DeviceMemoryBarrier,
        DeviceMemoryBarrierWithGroupSync,
        AllMemoryBarrierWithGroupSync,
    };

    inline ShaderBarrierBuiltinKind classifyShaderBarrierBuiltin(const std::string &qualifiedName)
    {
        if (qualifiedName == mUGLFunctionGroupMemoryBarrierName)
        {
            return ShaderBarrierBuiltinKind::GroupMemoryBarrier;
        }
        if (qualifiedName == mUGLFunctionGroupMemoryBarrierWithGroupSyncName)
        {
            return ShaderBarrierBuiltinKind::GroupMemoryBarrierWithGroupSync;
        }
        if (qualifiedName == mUGLFunctionDeviceMemoryBarrierName)
        {
            return ShaderBarrierBuiltinKind::DeviceMemoryBarrier;
        }
        if (qualifiedName == mUGLFunctionDeviceMemoryBarrierWithGroupSyncName)
        {
            return ShaderBarrierBuiltinKind::DeviceMemoryBarrierWithGroupSync;
        }
        if (qualifiedName == mUGLFunctionAllMemoryBarrierWithGroupSyncName)
        {
            return ShaderBarrierBuiltinKind::AllMemoryBarrierWithGroupSync;
        }
        return ShaderBarrierBuiltinKind::None;
    }

    inline const char *getShaderBarrierBuiltinDisplayName(const ShaderBarrierBuiltinKind kind)
    {
        switch (kind)
        {
        case ShaderBarrierBuiltinKind::GroupMemoryBarrier:
            return "GroupMemoryBarrier";
        case ShaderBarrierBuiltinKind::GroupMemoryBarrierWithGroupSync:
            return "GroupMemoryBarrierWithGroupSync";
        case ShaderBarrierBuiltinKind::DeviceMemoryBarrier:
            return "DeviceMemoryBarrier";
        case ShaderBarrierBuiltinKind::DeviceMemoryBarrierWithGroupSync:
            return "DeviceMemoryBarrierWithGroupSync";
        case ShaderBarrierBuiltinKind::AllMemoryBarrierWithGroupSync:
            return "AllMemoryBarrierWithGroupSync";
        case ShaderBarrierBuiltinKind::None:
            break;
        }
        return "";
    }

    inline std::string makeHLSLShaderBarrierBuiltinCall(const ShaderBarrierBuiltinKind kind)
    {
        return std::string(getShaderBarrierBuiltinDisplayName(kind)) + "()";
    }

    inline std::string makeMSLShaderBarrierBuiltinCall(const ShaderBarrierBuiltinKind kind)
    {
        switch (kind)
        {
        case ShaderBarrierBuiltinKind::GroupMemoryBarrier:
            return "atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst)";
        case ShaderBarrierBuiltinKind::GroupMemoryBarrierWithGroupSync:
            return "threadgroup_barrier(mem_flags::mem_threadgroup)";
        case ShaderBarrierBuiltinKind::DeviceMemoryBarrier:
            return "atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed)";
        case ShaderBarrierBuiltinKind::DeviceMemoryBarrierWithGroupSync:
            return "threadgroup_barrier(mem_flags::mem_device)";
        case ShaderBarrierBuiltinKind::AllMemoryBarrierWithGroupSync:
            return "threadgroup_barrier(mem_flags::mem_device | mem_flags::mem_threadgroup)";
        case ShaderBarrierBuiltinKind::None:
            break;
        }
        return {};
    }

    inline bool isShaderBarrierBuiltin(const ShaderBarrierBuiltinKind kind)
    {
        return kind != ShaderBarrierBuiltinKind::None;
    }
} // namespace UGLC::CodeGen
