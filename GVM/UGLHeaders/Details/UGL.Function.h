#pragma once
#include "UGL.Types.h"
namespace UGL
{
    /*  template <class T>
     T ddx(T t)
     {
         return t;
     }

     template <class T>
     T ddy(T t)
     {
         return t;
     }

     template <class T>
     T fwidth(T t)
     {
         return t;
     } */

#ifndef __UGLC_HOST_SHADER_ONLY_STUBS_HPP
#define __UGLC_HOST_SHADER_ONLY_STUBS_HPP
#ifdef DISABLE_UGL
    namespace UGLC_HostDetail
    {
        // In generated host-only code we want wave/quad intrinsics to fail at
        // compile time instead of pretending to have a CPU implementation.
        template <class>
        inline constexpr bool AlwaysFalse_v = false;
    } // namespace UGLC_HostDetail

    template <class T>
    inline T QuadReadLaneAt(T value, uint laneIndex)
    {
        (void)value;
        (void)laneIndex;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::QuadReadLaneAt");
        return {};
    }

    template <class T>
    inline T QuadReadAcrossX(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::QuadReadAcrossX");
        return {};
    }

    template <class T>
    inline T QuadReadAcrossY(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::QuadReadAcrossY");
        return {};
    }

    template <class T>
    inline T QuadReadAcrossDiagonal(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::QuadReadAcrossDiagonal");
        return {};
    }

    template <class T>
    inline T WaveReadAcrossX(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveReadAcrossX");
        return {};
    }

    template <class T>
    inline T WaveReadAcrossY(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveReadAcrossY");
        return {};
    }

    template <class T>
    inline T WaveReadAcrossDiagonal(T t)
    {
        (void)t;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveReadAcrossDiagonal");
        return {};
    }

    template <class __UGLC_Dummy = void>
    inline uint WaveGetLaneCount()
    {
        static_assert(UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, "UGLC generated host stub reached for shader-only operation: UGL::WaveGetLaneCount");
        return {};
    }

    template <class __UGLC_Dummy = void>
    inline uint WaveGetLaneIndex()
    {
        static_assert(UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, "UGLC generated host stub reached for shader-only operation: UGL::WaveGetLaneIndex");
        return {};
    }

    template <class T>
    inline T WaveReadLaneAt(T value, uint laneIndex)
    {
        (void)value;
        (void)laneIndex;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveReadLaneAt");
        return {};
    }

    template <class T>
    inline T WaveReadLaneFirst(T value)
    {
        (void)value;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveReadLaneFirst");
        return {};
    }

    template <class T>
    inline T WavePrefixSum(T value)
    {
        (void)value;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WavePrefixSum");
        return {};
    }

    template <class __UGLC_Dummy = void>
    inline uint4 WaveActiveBallot(bool predicate)
    {
        (void)predicate;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, "UGLC generated host stub reached for shader-only operation: UGL::WaveActiveBallot");
        return {};
    }

    template <class __UGLC_Dummy = void>
    inline uint WaveActiveCountBits(bool predicate)
    {
        (void)predicate;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, "UGLC generated host stub reached for shader-only operation: UGL::WaveActiveCountBits");
        return {};
    }

    template <class __UGLC_Dummy = void>
    inline uint WavePrefixCountBits(bool predicate)
    {
        (void)predicate;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, "UGLC generated host stub reached for shader-only operation: UGL::WavePrefixCountBits");
        return {};
    }

    template <class T>
    inline uint4 WaveMatch(T value)
    {
        (void)value;
        static_assert(UGLC_HostDetail::AlwaysFalse_v<T>, "UGLC generated host stub reached for shader-only operation: UGL::WaveMatch");
        return {};
    }
#else
    template <class T>
    inline T QuadReadLaneAt(T value, uint laneIndex)
    {
        (void)laneIndex;
        return value;
    }

    template <class T>
    inline T QuadReadAcrossX(T t)
    {
        return t;
    }

    template <class T>
    inline T QuadReadAcrossY(T t)
    {
        return t;
    }

    template <class T>
    inline T QuadReadAcrossDiagonal(T t)
    {
        return t;
    }

    template <class T>
    inline T WaveReadAcrossX(T t)
    {
        return QuadReadAcrossX(t);
    }

    template <class T>
    inline T WaveReadAcrossY(T t)
    {
        return QuadReadAcrossY(t);
    }

    template <class T>
    inline T WaveReadAcrossDiagonal(T t)
    {
        return QuadReadAcrossDiagonal(t);
    }

    inline uint WaveGetLaneCount()
    {
        return 0;
    }

    inline uint WaveGetLaneIndex()
    {
        return 0;
    }

    template <class T>
    inline T WaveReadLaneAt(T value, uint laneIndex)
    {
        (void)laneIndex;
        return value;
    }

    template <class T>
    inline T WaveReadLaneFirst(T value)
    {
        return value;
    }

    template <class T>
    inline T WavePrefixSum(T value)
    {
        return value;
    }

    inline uint4 WaveActiveBallot(bool predicate)
    {
        return predicate ? uint4(1u, 0u, 0u, 0u) : uint4(0u, 0u, 0u, 0u);
    }

    inline uint WaveActiveCountBits(bool predicate)
    {
        return predicate ? 1u : 0u;
    }

    inline uint WavePrefixCountBits(bool predicate)
    {
        (void)predicate;
        return 0u;
    }

    template <class T>
    inline uint4 WaveMatch(T value)
    {
        (void)value;
        return uint4(1u, 0u, 0u, 0u);
    }
#endif
#endif

    /* template <class T>
    uint packUnorm2x16(T t)
    {
        return 0;
    }

    template <class T>
    uint packSnorm2x16(T t)
    {
        return 0;
    }


    float2 unpackUnorm2x16(uint)
    {
        return {};
    }


    float2 unpackSnorm2x16(uint)
    {
        return {};
    } */
    inline void discard_fragment()
    {
    }

    inline void GroupMemoryBarrier()
    {
    }
    inline void GroupMemoryBarrierWithGroupSync()
    {
    }
    inline void DeviceMemoryBarrier()
    {
    }
    inline void DeviceMemoryBarrierWithGroupSync()
    {
    }

    inline void AllMemoryBarrierWithGroupSync()
    {
    }


    template <class T>
    T firstbitlow(T t)
    {
        return t;
    }

    template <class T>
    T firstbithigh(T t)
    {
        return t;
    }
} // namespace UGL
