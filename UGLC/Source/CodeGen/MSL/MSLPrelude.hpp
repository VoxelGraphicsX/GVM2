#pragma once

#include <string>

namespace UGLC::CodeGen::MSL
{
    // Returns the backend-private Metal runtime prelude that is prepended to
    // embedded shader source inside generated host wrappers.
    inline std::string MakeMSLPreludeSource()
{
    static constexpr const char res[] = R"UGL(#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;



// -----------------------------
// Math / utility wrappers
// -----------------------------
template <typename A, typename B>
inline auto mul(A a, B b) -> decltype(a * b)
{
    return a * b;
}

template <typename T>
inline auto frac(T a) -> decltype(fract(a))
{
    return fract(a);
}

template <typename A, typename B, typename C>
inline auto lerp(A a, B b, C c) -> decltype(mix(a, b, c))
{
    return mix(a, b, c);
}

template <typename T>
inline auto ddx(T x) -> decltype(dfdx(x))
{
    return dfdx(x);
}

template <typename T>
inline auto ddy(T x) -> decltype(dfdy(x))
{
    return dfdy(x);
}

inline void UGLC_clip(float x)
{
    if (x < 0.0f)
    {
        discard_fragment();
    }
}

inline void UGLC_clip(half x)
{
    if (x < half(0.0f))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(float2 x)
{
    if (any(x < float2(0.0f)))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(float3 x)
{
    if (any(x < float3(0.0f)))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(float4 x)
{
    if (any(x < float4(0.0f)))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(half2 x)
{
    if (any(x < half2(half(0.0f))))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(half3 x)
{
    if (any(x < half3(half(0.0f))))
    {
        discard_fragment();
    }
}

inline void UGLC_clip(half4 x)
{
    if (any(x < half4(half(0.0f))))
    {
        discard_fragment();
    }
}

// Match the existing macro semantics by bitcasting scalar values to uint or float.
// Vector bitcast overloads can be added later when shader code needs them.
template <typename T>
inline uint asuint(T v)
{
    return as_type<uint>(v);
}

template <typename T>
inline float asfloat(T v)
{
    return as_type<float>(v);
}

// -----------------------------
// Atomic wrappers (device / threadgroup)
// Reference parameters carry explicit address spaces so the overloads stay valid MSL.
// Metal on the tested Apple GPU only accepts relaxed ordering for device and threadgroup atomics. Cross-workgroup
// producer-consumer queues must therefore publish an explicit ready sentinel in the payload itself:
//   payload.ready = 0;
//   payloadBuffer[index] = payload;
//   DeviceMemoryBarrier();
//   payload.ready = READY;
//   payloadBuffer[index] = payload;
//   DeviceMemoryBarrier();
//   atomicAdd(publishedCounter, 1u);
// Consumers must read the counter, fence, wait for payload.ready, fence, and then reread the payload. This mirrors the
// UE RWCoherentByteAddressBuffer + DeviceMemoryBarrier intent without requesting unsupported Metal atomic ordering.
// -----------------------------

template <class T, class U>
inline T atomicOr(device atomic<T>& a, U b)
{
    return atomic_fetch_or_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicOr(threadgroup atomic<T>& a, U b)
{
    return atomic_fetch_or_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicAnd(device atomic<T>& a, U b)
{
    return atomic_fetch_and_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicAnd(threadgroup atomic<T>& a, U b)
{
    return atomic_fetch_and_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
/** Stores and returns the converted value for ordinary assignment expression lowering. */
inline T atomicStore(device atomic<T>& a, U b)
{
    const T value = (T)b;
    atomic_store_explicit(&a, value, memory_order_relaxed);
    return value;
}

template <class T, class U>
/** Stores and returns the converted value for ordinary assignment expression lowering. */
inline T atomicStore(threadgroup atomic<T>& a, U b)
{
    const T value = (T)b;
    atomic_store_explicit(&a, value, memory_order_relaxed);
    return value;
}

template <class T>
inline T atomicLoad(const device atomic<T>& a)
{
    return atomic_load_explicit(&a, memory_order_relaxed);
}

template <class T>
inline T atomicLoad(const threadgroup atomic<T>& a)
{
    return atomic_load_explicit(&a, memory_order_relaxed);
}

template <class T, class U>
inline T atomicMax(device atomic<T>& a, U b)
{
    return atomic_fetch_max_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicMax(threadgroup atomic<T>& a, U b)
{
    return atomic_fetch_max_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicMin(device atomic<T>& a, U b)
{
    return atomic_fetch_min_explicit(&a, (T)b, memory_order_relaxed);
}

template <class T, class U>
inline T atomicMin(threadgroup atomic<T>& a, U b)
{
    return atomic_fetch_min_explicit(&a, (T)b, memory_order_relaxed);
}




template<typename T>
inline T firstbitlow(T x)
{
    return ctz(x);
}
template<typename T>
inline T firstbithigh(T x)
{
    return sizeof(T) * 8-1-clz(x);
}

template<typename T>
inline T WaveReadLaneAt(T x, uint index)
{
    return simd_shuffle(x, ushort(index));
}
template<typename T>
inline T WaveReadLaneFirst(T x)
{
    return simd_broadcast_first(x);
}
// Metal exposes ballot results through simd_vote instead of the HLSL-style
// uint4 mask. Pack the backend-specific vote bits into a deterministic uint4
// so the DSL can keep a backend-neutral wave-mask representation.
inline uint4 UGLC_WaveMaskFromRaw(ulong rawValue, uint laneCount)
{
    if (laneCount == 0u)
    {
        rawValue = 0ul;
    }
    else if (laneCount < 64u)
    {
        rawValue &= ((1ul << laneCount) - 1ul);
    }

    return uint4(uint(rawValue & 0xfffffffful),
                 uint((rawValue >> 32u) & 0xfffffffful),
                 0u,
                 0u);
}
inline uint4 UGLC_WaveActiveBallot(bool predicate, uint laneCount)
{
    return UGLC_WaveMaskFromRaw((ulong)static_cast<simd_vote::vote_t>(simd_ballot(predicate)), laneCount);
}
inline uint WaveActiveCountBits(bool predicate)
{
    return simd_sum(predicate ? 1u : 0u);
}
inline uint WavePrefixCountBits(bool predicate)
{
    return simd_prefix_exclusive_sum(predicate ? 1u : 0u);
}
template<typename T>
inline T WavePrefixSum(T x)
{
    return simd_prefix_exclusive_sum(x);
}
template<typename T>
inline bool UGLC_WaveValuesEqual(T a, T b)
{
    return all(a == b);
}
// Metal does not currently expose a direct simd_match_any-style intrinsic.
// Fall back to a correct broadcast+ballot loop so DSL code can still rely on
// HLSL-style WaveMatch semantics before the future HLSL backend arrives.
template<typename T>
inline uint4 UGLC_WaveMatch(T value, uint laneIndex, uint laneCount)
{
    for (uint sourceLane = 0u; sourceLane < laneCount; ++sourceLane)
    {
        const T sourceValue = simd_broadcast(value, ushort(sourceLane));
        const uint4 candidateMask = UGLC_WaveActiveBallot(UGLC_WaveValuesEqual(value, sourceValue), laneCount);
        if (sourceLane == laneIndex)
        {
            return candidateMask;
        }
    }

    return uint4(0u);
}
template<typename T>
inline T QuadReadLaneAt(T x, uint index)
{
    return quad_shuffle(x, ushort(index));
}
template<typename T>
inline T QuadReadAcrossX(T x)
{
    return quad_shuffle_xor(x,1);
}
template<typename T>
inline T QuadReadAcrossY(T x)
{
    return quad_shuffle_xor(x,2);
}
template<typename T>
inline T QuadReadAcrossDiagonal(T x)
{
    return quad_shuffle_xor(x,3);
}
template<typename T>
inline T WaveReadAcrossX(T x)
{
    return QuadReadAcrossX(x);
}
template<typename T>
inline T WaveReadAcrossY(T x)
{
    return QuadReadAcrossY(x);
}
template<typename T>
inline T WaveReadAcrossDiagonal(T x)
{
    return QuadReadAcrossDiagonal(x);
}
template <class T>
void atomicCompareExchange(threadgroup atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

template <class T>
void atomicCompareExchange(device atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

void DeviceMemoryBarrier()
{
    atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed);
}

void DeviceMemoryBarrierWithGroupSync()
{
    threadgroup_barrier(mem_flags::mem_device);
}

void GroupMemoryBarrier()
{
    atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst);
}

void GroupMemoryBarrierWithGroupSync()
{
    threadgroup_barrier(mem_flags::mem_threadgroup);
}

void AllMemoryBarrierWithGroupSync()
{
    threadgroup_barrier(mem_flags::mem_device | mem_flags::mem_threadgroup);
}
template <class T, class U>
T atomicAdd(device atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}

template <class T, class U>
T atomicAdd(threadgroup atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}

template <class T>
void sincos(thread const T& a,thread T& s,thread T& c)
{
   s = sincos(a,c);
}
struct UGLSampleHalfTextureArrayWraper
{
    texture2d<half,access::sample> texture [[id(0)]];
    half4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    half4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLSampleFloatTextureArrayWraper
{
    texture2d<float,access::sample> texture [[id(0)]];
    float4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    float4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLSampleUIntTextureArrayWraper
{
    texture2d<uint,access::read> texture [[id(0)]];
    uint4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLSampleIntTextureArrayWraper
{
    texture2d<int,access::read> texture [[id(0)]];
    int4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLRWHalfTextureArrayWraper
{
    texture2d<half,access::read_write> texture [[id(0)]];
};
struct UGLRWFloatTextureArrayWraper
{
    texture2d<float,access::read_write> texture [[id(0)]];
};
struct UGLRWUIntTextureArrayWraper
{
    texture2d<uint,access::read_write> texture [[id(0)]];
};
struct UGLRWIntTextureArrayWraper
{
    texture2d<int,access::read_write> texture [[id(0)]];
};

struct UGL_RenderEntityInfo_
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint globalInstanceBase;
    uint vertexCount;
    uint entityVersion;
    uint cmdParamsOffset;
};
)UGL";
    return res;
}
} // namespace UGLC::CodeGen::MSL
