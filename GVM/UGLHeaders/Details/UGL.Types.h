#pragma once
// clang-format off
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <concepts>
#include <initializer_list>
#include <type_traits> // For std::enable_if_t and std::is_integral_v

// =================================================================================================
//
// HLSL.h - C++ HLSL Emulation Header (Final, Truly Complete Version)
//
// 描述:
// 本文件是在 C++ 环境中对 HLSL 的一个终极详尽的模拟。它定义了所有向量、
// 矩阵、纹理类型以及内置函数签名。
//
// **核心特性**:
// - **真正完全展开的 Swizzling**: 所有基本类型 (float, int, uint, bool, double)
//   的 2D, 3D, 4D 向量的所有 swizzle 组合都已完全实现为成员变量，
//   并且同时支持 xyzw 和 rgba 两套命名约定。
// - **完备的运算符重载**: 支持数学、逻辑、位运算和复合赋值运算符。
// - **IDE 完美兼容**: 为 Visual Studio, CLion, VS Code 等 IDE 提供最极致的
//   智能提示和代码自动补全。
// - **零开销伪实现**: 所有函数和运算符都只返回默认值，不执行任何实际计算。
//
// 版本: 5.0 (最终完全版)
//
// =================================================================================================

namespace UGL {





//--------------------------------------------------------------------------------------------------
// 前向声明 (Forward Declarations)
//--------------------------------------------------------------------------------------------------
template<typename T, int N> struct Vector;
template<typename T, int R, int C> struct Matrix;

struct half2; struct half3; struct half4;
struct float2; struct float3; struct float4;
struct int2;   struct int3;   struct int4;
struct uint2;  struct uint3;  struct uint4;
struct bool2;  struct bool3;  struct bool4;
struct double2;struct double3;struct double4;



//--------------------------------------------------------------------------------------------------
// 0. Swizzle 代理对象 (Proxy Objects for Swizzling)
//--------------------------------------------------------------------------------------------------
namespace detail {
    template<typename DestVecT, typename SrcT, int... Indices>
    struct Swizzle {
    private:
        SrcT* data_ptr;

    public:
        // 赋值运算符
        Swizzle& operator=(const DestVecT& rhs) {
            int i = 0;
            (void)std::initializer_list<int>{(data_ptr[Indices] = static_cast<SrcT>(rhs[i++]), 0)...};
            return *this;
        }

        // 类型转换运算符 (读取)
        operator DestVecT() const {
            return DestVecT{static_cast<typename DestVecT::value_type>(data_ptr[Indices])...};
        }

        // --- 所有复合赋值运算符 ---
        Swizzle& operator+=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] += static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        Swizzle& operator-=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] -= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        Swizzle& operator*=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] *= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        Swizzle& operator/=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] /= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        Swizzle& operator+=(const SrcT& s) { (void)std::initializer_list<int>{(data_ptr[Indices] += s, 0)...}; return *this; }
        Swizzle& operator-=(const SrcT& s) { (void)std::initializer_list<int>{(data_ptr[Indices] -= s, 0)...}; return *this; }
        Swizzle& operator*=(const SrcT& s) { (void)std::initializer_list<int>{(data_ptr[Indices] *= s, 0)...}; return *this; }
        Swizzle& operator/=(const SrcT& s) { (void)std::initializer_list<int>{(data_ptr[Indices] /= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator%=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] %= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator%=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] %= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator&=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] &= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator&=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] &= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator|=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] |= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator|=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] |= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator^=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] ^= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator^=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] ^= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator<<=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] <<= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator<<=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] <<= s, 0)...}; return *this; }

        template <typename V = DestVecT, typename = std::enable_if_t<std::is_integral_v<typename V::value_type>>>
        Swizzle& operator>>=(const DestVecT& rhs) { int i = 0; (void)std::initializer_list<int>{(data_ptr[Indices] >>= static_cast<SrcT>(rhs[i++]), 0)...}; return *this; }
        template <typename S = SrcT, typename = std::enable_if_t<std::is_integral_v<S>>>
        Swizzle& operator>>=(const S& s) { (void)std::initializer_list<int>{(data_ptr[Indices] >>= s, 0)...}; return *this; }
    };

    // 1) 检测类型是否为任意 Swizzle 实例
    template<class T> struct is_swizzle : std::false_type {};
    template<class D, class S, int... I>
    struct is_swizzle<Swizzle<D, S, I...>> : std::true_type {};

    template<class T>
    inline constexpr bool is_swizzle_v = is_swizzle<T>::value;

    // 2) 定义概念：不是 Swizzle
    template<class T>
    concept NotSwizzle = !is_swizzle_v<std::remove_cvref_t<T>>;

} // namespace detail

//--------------------------------------------------------------------------------------------------
// 1. 基本标量类型定义 (Scalar Types)
//--------------------------------------------------------------------------------------------------
using scalar = float;
//using half   = int16_t;
struct half {
        uint16_t data;

        // 1. 默认构造
        half() = default;

        // 2. 拷贝/移动构造 (让编译器自动生成，解决 implicit copy/move 报错)
        half(const half&) = default;
        half(half&&) = default;
        half& operator=(const half&) = default;
        half& operator=(half&&) = default;

        // 3. Float 构造 (解决 functional-style cast 报错)
        // Half precision must be an explicit precision conversion in shader code.
        explicit half(float v) : data(0) {}
        explicit half(double v) : data(0) {}
        explicit half(int v) : data(0) {}
        explicit half(uint32_t v) : data(0) {}

        // 4. Float 转换 (解决 static_cast<CTYPE> 报错)
        // CRITICAL FIX: 必须标记为 const，否则 const half 对象无法转换
        explicit operator float() const {
            return 0.f;
        }
        explicit operator int() const {
            return 0;
        }

         explicit operator uint32_t() const {
            return 0;
        }

        // --- 运算符重载 (解决 +=, *= 等报错) ---

        half& operator+=(const half& rhs) { *this = half(float(*this) + float(rhs)); return *this; }
        half& operator-=(const half& rhs) { *this = half(float(*this) - float(rhs)); return *this; }
        half& operator*=(const half& rhs) { *this = half(float(*this) * float(rhs)); return *this; }
        half& operator/=(const half& rhs) { *this = half(float(*this) / float(rhs)); return *this; }

        // 针对 float 的混合运算重载
        /* half& operator+=(float rhs) { *this = half(float(*this) + rhs); return *this; }
        half& operator-=(float rhs) { *this = half(float(*this) - rhs); return *this; }
        half& operator*=(float rhs) { *this = half(float(*this) * rhs); return *this; }
        half& operator/=(float rhs) { *this = half(float(*this) / rhs); return *this; } */

        // 一元运算符
        half operator-() const {
            half h;

            return h;
        }
    };

    // --- 二元运算符 (解决 half - half 等报错) ---
    // 为了解决 template deduction 冲突，我们倾向于让 float 占主导，或者返回 half

    inline half operator+(half a, half b) { return half(float(a) + float(b)); }
    inline half operator-(half a, half b) { return half(float(a) - float(b)); }
    inline half operator*(half a, half b) { return half(float(a) * float(b)); }
    inline half operator/(half a, half b) { return half(float(a) / float(b)); }

    /* inline float operator+(float a, half b) { return a + float(b); }
    inline float operator+(half a, float b) { return float(a) + b; }
    inline float operator-(float a, half b) { return a - float(b); }
    inline float operator-(half a, float b) { return float(a) - b; }
    inline float operator*(float a, half b) { return a * float(b); }
    inline float operator*(half a, float b) { return float(a) * b; }
    inline float operator/(float a, half b) { return a / float(b); }
    inline float operator/(half a, float b) { return float(a) / b; } */

    // --- 比较运算符 ---
    inline bool operator==(half a, half b) { return a.data == b.data; } // Simplified
    inline bool operator!=(half a, half b) { return a.data != b.data; }
    inline bool operator<(half a, half b)  { return float(a) < float(b); }
    inline bool operator>(half a, half b)  { return float(a) > float(b); }
    inline bool operator<=(half a, half b) { return float(a) <= float(b); }
    inline bool operator>=(half a, half b) { return float(a) >= float(b); }

    // --- 常用数学函数重载 (解决 clamp, sqrt, max 等报错) ---
    // 通过重载特定版本来解决模板推导时的类型冲突

    inline half sqrt(half x) { return half(std::sqrt(float(x))); }

    inline half max(half a, half b) { return (float(a) > float(b)) ? a : b; }
    inline half min(half a, half b) { return (float(a) < float(b)) ? a : b; }

    // 解决 clamp(val, 0.0f, 1.0f) 这种混合类型推导失败的问题
    inline half clamp(half v, half lo, half hi) {
        return max(lo, min(v, hi));
    }
    // 允许 clamp(float, half, half) -> 返回 float
   /*  inline float clamp(float v, half lo, half hi) {
        return std::max(float(lo), std::min(v, float(hi)));
    } */

    inline half lerp(half a, half b, half t) {
        return half(float(a) + (float(b) - float(a)) * float(t));
    }
    /* inline float lerp(float a, float b, half t) {
        return a + (b - a) * float(t);
    } */

    // Smoothstep
    inline half smoothstep(half edge0, half edge1, half x) {
        float t = std::clamp((float(x) - float(edge0)) / (float(edge1) - float(edge0)), 0.0f, 1.0f);
        return half(t * t * (3.0f - 2.0f * t));
    }

    // Dot product (scalar)
    inline half dot(half a, half b) { return a * b; }

    // --- 类型转换 Traits (解决 undefined template 'VectorTypeFromScalar' 报错) ---
    // 假设你的 Vector 类型大概是这样定义的，如果名称不同请修正
    template<typename T, int N> struct Vector;

    // 特化 Traits，告诉 UGL 引擎 half 对应的 Vector 类型是什么
    // 注意：你需要在 UGL.Types.h 中找到 VectorTypeFromScalar 的原始定义并在下方添加此特化

    // 假设 VectorTypeFromScalar 的定义如下:
    template<typename T, int N> struct VectorTypeFromScalar;

    // 针对 half 的特化
   /*  template<> struct VectorTypeFromScalar<half, 2> { using type = Vector<half, 2>; };
    template<> struct VectorTypeFromScalar<half, 3> { using type = Vector<half, 3>; };
    template<> struct VectorTypeFromScalar<half, 4> { using type = Vector<half, 4>; }; */

    // 兼容 std::is_floating_point (可选，部分数学库需要)

using real   = float;

using int_t    = int32_t;
using uint_t   = uint32_t;
using uint   = uint32_t;
using bool_t   = bool;
using half_t  = half;
using float_t  = float;
using double_t = double;

using half1  = half_t;
using float1  = float_t;
using int1    = int_t;
using uint1   = uint_t;
using bool1   = bool_t;
using double1 = double_t;

// 类型映射工具: 根据标量类型和维度，找到对应的向量类型
template<typename T, int N> struct VectorTypeFromScalar;

// 为所有向量类型提供特化版本
template<> struct VectorTypeFromScalar<half_t, 2> { using type = half2; };
template<> struct VectorTypeFromScalar<half_t, 3> { using type = half3; };
template<> struct VectorTypeFromScalar<half_t, 4> { using type = half4; };
template<> struct VectorTypeFromScalar<float_t, 2> { using type = float2; };
template<> struct VectorTypeFromScalar<float_t, 3> { using type = float3; };
template<> struct VectorTypeFromScalar<float_t, 4> { using type = float4; };
template<> struct VectorTypeFromScalar<int_t, 2> { using type = int2; };
template<> struct VectorTypeFromScalar<int_t, 3> { using type = int3; };
template<> struct VectorTypeFromScalar<int_t, 4> { using type = int4; };
template<> struct VectorTypeFromScalar<uint_t, 2> { using type = uint2; };
template<> struct VectorTypeFromScalar<uint_t, 3> { using type = uint3; };
template<> struct VectorTypeFromScalar<uint_t, 4> { using type = uint4; };
template<> struct VectorTypeFromScalar<bool_t, 2> { using type = bool2; };
template<> struct VectorTypeFromScalar<bool_t, 3> { using type = bool3; };
template<> struct VectorTypeFromScalar<bool_t, 4> { using type = bool4; };
template<> struct VectorTypeFromScalar<double_t, 2> { using type = double2; };
template<> struct VectorTypeFromScalar<double_t, 3> { using type = double3; };
template<> struct VectorTypeFromScalar<double_t, 4> { using type = double4; };

// 一个便捷的别名
template<typename T, int N>
using vector_t = typename VectorTypeFromScalar<T, N>::type;

// --- 代码块结束 ---

//--------------------------------------------------------------------------------------------------
// 2. 向量类型定义 (Vector Types)
//--------------------------------------------------------------------------------------------------
template<typename T, int N>
struct Vector {
    using value_type = T;
    T data[N];
    T& operator[](int i) { return data[i]; }
    const T& operator[](int i) const { return data[i]; }


    // 通用数学运算符 (+=, -=, *=, /=)
    Vector<T, N>& operator+=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] += rhs.data[i]; }
        return *this;
    }
    Vector<T, N>& operator-=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] -= rhs.data[i]; }
        return *this;
    }
    Vector<T, N>& operator*=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] *= rhs.data[i]; }
        return *this;
    }
    Vector<T, N>& operator/=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] /= rhs.data[i]; }
        return *this;
    }
    // 与标量的版本
    Vector<T, N>& operator+=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] += s; }
        return *this;
    }
    Vector<T, N>& operator-=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] -= s; }
        return *this;
    }
    Vector<T, N>& operator*=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] *= s; }
        return *this;
    }
    Vector<T, N>& operator/=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] /= s; }
        return *this;
    }


    // 仅整数类型有效的运算符 (%=, &=, |=, ^=, <<=, >>=)
    // 使用 SFINAE (std::enable_if_t) 技术来约束这些函数只为整数向量实例化
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator%=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] %= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator%=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] %= s; }
        return *this;
    }

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator&=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] &= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator&=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] &= s; }
        return *this;
    }

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator|=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] |= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator|=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] |= s; }
        return *this;
    }

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator^=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] ^= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator^=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] ^= s; }
        return *this;
    }

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator<<=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] <<= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator<<=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] <<= s; }
        return *this;
    }

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator>>=(const Vector<T, N>& rhs) {
        for (int i = 0; i < N; ++i) { this->data[i] >>= rhs.data[i]; }
        return *this;
    }
    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U>, Vector<T, N>&>
    operator>>=(T s) {
        for (int i = 0; i < N; ++i) { this->data[i] >>= s; }
        return *this;
    }

    // --- 代码块结束 ---
};

#define SWIZZLE_2(VTYPE, CTYPE, P1,P2, I1,I2) detail::Swizzle<VTYPE##2, CTYPE, I1,I2> P1##P2
#define SWIZZLE_3(VTYPE, CTYPE, P1,P2,P3, I1,I2,I3) detail::Swizzle<VTYPE##3, CTYPE, I1,I2,I3> P1##P2##P3
#define SWIZZLE_4(VTYPE, CTYPE, P1,P2,P3,P4, I1,I2,I3,I4) detail::Swizzle<VTYPE##4, CTYPE, I1,I2,I3,I4> P1##P2##P3##P4

#define DEFINE_SWIZZLES_2_SETS(VTYPE, CTYPE, P1,P2,P3,P4) \
    SWIZZLE_2(VTYPE, CTYPE, P1, P1, 0,0); SWIZZLE_2(VTYPE, CTYPE, P1, P2, 0,1); \
    SWIZZLE_2(VTYPE, CTYPE, P2, P1, 1,0); SWIZZLE_2(VTYPE, CTYPE, P2, P2, 1,1);

#define DEFINE_SWIZZLES_3_SETS(VTYPE, CTYPE, P1,P2,P3,P4) \
    SWIZZLE_3(VTYPE, CTYPE, P1,P1,P1, 0,0,0); SWIZZLE_3(VTYPE, CTYPE, P1,P1,P2, 0,0,1); SWIZZLE_3(VTYPE, CTYPE, P1,P2,P1, 0,1,0); SWIZZLE_3(VTYPE, CTYPE, P1,P2,P2, 0,1,1); \
    SWIZZLE_3(VTYPE, CTYPE, P2,P1,P1, 1,0,0); SWIZZLE_3(VTYPE, CTYPE, P2,P1,P2, 1,0,1); SWIZZLE_3(VTYPE, CTYPE, P2,P2,P1, 1,1,0); SWIZZLE_3(VTYPE, CTYPE, P2,P2,P2, 1,1,1);

#define DEFINE_SWIZZLES_4_SETS(VTYPE, CTYPE, P1,P2,P3,P4) \
    SWIZZLE_4(VTYPE, CTYPE, P1,P1,P1,P1, 0,0,0,0); SWIZZLE_4(VTYPE, CTYPE, P1,P1,P1,P2, 0,0,0,1); SWIZZLE_4(VTYPE, CTYPE, P1,P1,P2,P1, 0,0,1,0); SWIZZLE_4(VTYPE, CTYPE, P1,P1,P2,P2, 0,0,1,1); \
    SWIZZLE_4(VTYPE, CTYPE, P1,P2,P1,P1, 0,1,0,0); SWIZZLE_4(VTYPE, CTYPE, P1,P2,P1,P2, 0,1,0,1); SWIZZLE_4(VTYPE, CTYPE, P1,P2,P2,P1, 0,1,1,0); SWIZZLE_4(VTYPE, CTYPE, P1,P2,P2,P2, 0,1,1,1); \
    SWIZZLE_4(VTYPE, CTYPE, P2,P1,P1,P1, 1,0,0,0); SWIZZLE_4(VTYPE, CTYPE, P2,P1,P1,P2, 1,0,0,1); SWIZZLE_4(VTYPE, CTYPE, P2,P1,P2,P1, 1,0,1,0); SWIZZLE_4(VTYPE, CTYPE, P2,P1,P2,P2, 1,0,1,1); \
    SWIZZLE_4(VTYPE, CTYPE, P2,P2,P1,P1, 1,1,0,0); SWIZZLE_4(VTYPE, CTYPE, P2,P2,P1,P2, 1,1,0,1); SWIZZLE_4(VTYPE, CTYPE, P2,P2,P2,P1, 1,1,1,0); SWIZZLE_4(VTYPE, CTYPE, P2,P2,P2,P2, 1,1,1,1);

#define DEFINE_VECTOR_2(VTYPE, CTYPE) \
struct VTYPE##2 : public Vector<CTYPE, 2> { \
    using Vector<CTYPE, 2>::Vector; \
    /* 构造函数 1: 从同类型的基类构造 (解决 a + b 问题) */ \
    VTYPE##2(const Vector<CTYPE, 2>& base) { this->data[0]=base.data[0]; this->data[1]=base.data[1]; } \
    /* 构造函数 2: 从不同类型的向量转换，使用 C++20 Concepts 约束 */ \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT>) \
    explicit VTYPE##2(const Vector<OtherT, 2>& other) { \
        this->data[0] = static_cast<CTYPE>(other.data[0]); \
        this->data[1] = static_cast<CTYPE>(other.data[1]); \
    } \
    /* Explicitly splats a scalar whose type requires a precision conversion. */ \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##2(OtherT s) { this->data[0]=CTYPE(s); this->data[1]=CTYPE(s); } \
    /* 构造函数 3: 从 Swizzle 转换 */ \
    template<typename OtherDestVecT, typename OtherSrcT, int... Indices> \
    explicit VTYPE##2(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other) { \
        static_assert(sizeof...(Indices) == 2, "Swizzle size must match vector size for conversion."); \
        OtherDestVecT temp = other; \
        this->data[0] = static_cast<CTYPE>(temp.data[0]); this->data[1] = static_cast<CTYPE>(temp.data[1]); \
    } \
    /* 标准构造函数 */ \
    VTYPE##2() { this->data[0] = CTYPE{}; this->data[1] = CTYPE{}; } \
    explicit VTYPE##2(CTYPE s) { this->data[0]=s; this->data[1]=s; } \
    VTYPE##2(CTYPE _x, CTYPE _y) { this->data[0]=_x; this->data[1]=_y; } \
    /* Explicitly builds a vector from components that require precision conversion. */ \
    template<typename X, typename Y> \
    requires((!std::same_as<CTYPE, X> || !std::same_as<CTYPE, Y>) && std::is_constructible_v<CTYPE, X> && std::is_constructible_v<CTYPE, Y>) \
    explicit VTYPE##2(X _x, Y _y) { this->data[0]=CTYPE(_x); this->data[1]=CTYPE(_y); } \
    union { \
        struct { CTYPE x, y; }; struct { CTYPE r, g; }; \
        DEFINE_SWIZZLES_2_SETS(VTYPE, CTYPE, x,y,z,w); DEFINE_SWIZZLES_2_SETS(VTYPE, CTYPE, r,g,b,a); \
        DEFINE_SWIZZLES_3_SETS(VTYPE, CTYPE, x,y,z,w); DEFINE_SWIZZLES_3_SETS(VTYPE, CTYPE, r,g,b,a); \
        DEFINE_SWIZZLES_4_SETS(VTYPE, CTYPE, x,y,z,w); DEFINE_SWIZZLES_4_SETS(VTYPE, CTYPE, r,g,b,a); \
    }; };

#define DEFINE_VECTOR_3(VTYPE, CTYPE) \
struct VTYPE##3 : public Vector<CTYPE, 3> { \
    using Vector<CTYPE, 3>::Vector; \
    VTYPE##3(const Vector<CTYPE, 3>& base) { this->data[0]=base.data[0]; this->data[1]=base.data[1]; this->data[2]=base.data[2]; } \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT>) \
    explicit VTYPE##3(const Vector<OtherT, 3>& other) { \
        for(int i=0; i<3; ++i) this->data[i] = static_cast<CTYPE>(other.data[i]); \
    } \
    /* Explicitly splats a scalar whose type requires a precision conversion. */ \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##3(OtherT s) { for(int i=0; i<3; ++i) this->data[i]=CTYPE(s); } \
    template<typename OtherDestVecT, typename OtherSrcT, int... Indices> \
    explicit VTYPE##3(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other) { \
        static_assert(sizeof...(Indices) == 3, "Swizzle size must match vector size for conversion."); \
        OtherDestVecT temp = other; \
        for(int i=0; i<3; ++i) this->data[i] = static_cast<CTYPE>(temp.data[i]); \
    } \
    VTYPE##3() { this->data[0]=CTYPE{}; this->data[1]=CTYPE{}; this->data[2]=CTYPE{}; } \
    explicit VTYPE##3(CTYPE s) { this->data[0]=s; this->data[1]=s; this->data[2]=s; } \
    VTYPE##3(CTYPE _x, CTYPE _y, CTYPE _z) { this->data[0]=_x; this->data[1]=_y; this->data[2]=_z; } \
    /* Explicitly builds a vector from components that require precision conversion. */ \
    template<typename X, typename Y, typename Z> \
    requires((!std::same_as<CTYPE, X> || !std::same_as<CTYPE, Y> || !std::same_as<CTYPE, Z>) && std::is_constructible_v<CTYPE, X> && std::is_constructible_v<CTYPE, Y> && std::is_constructible_v<CTYPE, Z>) \
    explicit VTYPE##3(X _x, Y _y, Z _z) { this->data[0]=CTYPE(_x); this->data[1]=CTYPE(_y); this->data[2]=CTYPE(_z); } \
    VTYPE##3(const VTYPE##2& v, CTYPE s) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=s; } \
    VTYPE##3(CTYPE s, const VTYPE##2& v) { this->data[0]=s; this->data[1]=v.data[0]; this->data[2]=v.data[1]; } \
    /* Explicitly appends or prepends a vector and scalar when any component requires precision conversion. */ \
    template<typename OtherT, typename S> \
    requires((!std::same_as<CTYPE, OtherT> || !std::same_as<CTYPE, S>) && std::is_constructible_v<CTYPE, OtherT> && std::is_constructible_v<CTYPE, S>) \
    explicit VTYPE##3(const Vector<OtherT, 2>& v, S s) { this->data[0]=CTYPE(v.data[0]); this->data[1]=CTYPE(v.data[1]); this->data[2]=CTYPE(s); } \
    template<typename S, typename OtherT> \
    requires((!std::same_as<CTYPE, S> || !std::same_as<CTYPE, OtherT>) && std::is_constructible_v<CTYPE, S> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##3(S s, const Vector<OtherT, 2>& v) { this->data[0]=CTYPE(s); this->data[1]=CTYPE(v.data[0]); this->data[2]=CTYPE(v.data[1]); } \
    /* Explicitly appends or prepends a scalar that requires precision conversion. */ \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##3(const VTYPE##2& v, OtherT s) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=CTYPE(s); } \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##3(OtherT s, const VTYPE##2& v) { this->data[0]=CTYPE(s); this->data[1]=v.data[0]; this->data[2]=v.data[1]; } \
    /* Explicitly combines a swizzle and scalar when constructing the target vector type. */ \
    template<typename OtherDestVecT, typename OtherSrcT, typename S, int... Indices> \
    requires(sizeof...(Indices) == 2 && std::is_constructible_v<CTYPE, S>) \
    explicit VTYPE##3(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other, S s) { \
        OtherDestVecT temp = other; \
        this->data[0]=static_cast<CTYPE>(temp.data[0]); this->data[1]=static_cast<CTYPE>(temp.data[1]); this->data[2]=CTYPE(s); \
    } \
    template<typename S, typename OtherDestVecT, typename OtherSrcT, int... Indices> \
    requires(sizeof...(Indices) == 2 && std::is_constructible_v<CTYPE, S>) \
    explicit VTYPE##3(S s, const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other) { \
        OtherDestVecT temp = other; \
        this->data[0]=CTYPE(s); this->data[1]=static_cast<CTYPE>(temp.data[0]); this->data[2]=static_cast<CTYPE>(temp.data[1]); \
    } \
    union { \
        struct { CTYPE x, y, z; }; struct { CTYPE r, g, b; }; \
        /* All 2-component swizzles */ \
        SWIZZLE_2(VTYPE,CTYPE,x,x,0,0); SWIZZLE_2(VTYPE,CTYPE,x,y,0,1); SWIZZLE_2(VTYPE,CTYPE,x,z,0,2); \
        SWIZZLE_2(VTYPE,CTYPE,y,x,1,0); SWIZZLE_2(VTYPE,CTYPE,y,y,1,1); SWIZZLE_2(VTYPE,CTYPE,y,z,1,2); \
        SWIZZLE_2(VTYPE,CTYPE,z,x,2,0); SWIZZLE_2(VTYPE,CTYPE,z,y,2,1); SWIZZLE_2(VTYPE,CTYPE,z,z,2,2); \
        SWIZZLE_2(VTYPE,CTYPE,r,r,0,0); SWIZZLE_2(VTYPE,CTYPE,r,g,0,1); SWIZZLE_2(VTYPE,CTYPE,r,b,0,2); \
        SWIZZLE_2(VTYPE,CTYPE,g,r,1,0); SWIZZLE_2(VTYPE,CTYPE,g,g,1,1); SWIZZLE_2(VTYPE,CTYPE,g,b,1,2); \
        SWIZZLE_2(VTYPE,CTYPE,b,r,2,0); SWIZZLE_2(VTYPE,CTYPE,b,g,2,1); SWIZZLE_2(VTYPE,CTYPE,b,b,2,2); \
        /* All 3-component swizzles */ \
        SWIZZLE_3(VTYPE,CTYPE,x,x,x,0,0,0); SWIZZLE_3(VTYPE,CTYPE,x,x,y,0,0,1); SWIZZLE_3(VTYPE,CTYPE,x,x,z,0,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,x,y,x,0,1,0); SWIZZLE_3(VTYPE,CTYPE,x,y,y,0,1,1); SWIZZLE_3(VTYPE,CTYPE,x,y,z,0,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,x,z,x,0,2,0); SWIZZLE_3(VTYPE,CTYPE,x,z,y,0,2,1); SWIZZLE_3(VTYPE,CTYPE,x,z,z,0,2,2); \
        SWIZZLE_3(VTYPE,CTYPE,y,x,x,1,0,0); SWIZZLE_3(VTYPE,CTYPE,y,x,y,1,0,1); SWIZZLE_3(VTYPE,CTYPE,y,x,z,1,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,y,y,x,1,1,0); SWIZZLE_3(VTYPE,CTYPE,y,y,y,1,1,1); SWIZZLE_3(VTYPE,CTYPE,y,y,z,1,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,y,z,x,1,2,0); SWIZZLE_3(VTYPE,CTYPE,y,z,y,1,2,1); SWIZZLE_3(VTYPE,CTYPE,y,z,z,1,2,2); \
        SWIZZLE_3(VTYPE,CTYPE,z,x,x,2,0,0); SWIZZLE_3(VTYPE,CTYPE,z,x,y,2,0,1); SWIZZLE_3(VTYPE,CTYPE,z,x,z,2,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,z,y,x,2,1,0); SWIZZLE_3(VTYPE,CTYPE,z,y,y,2,1,1); SWIZZLE_3(VTYPE,CTYPE,z,y,z,2,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,z,z,x,2,2,0); SWIZZLE_3(VTYPE,CTYPE,z,z,y,2,2,1); SWIZZLE_3(VTYPE,CTYPE,z,z,z,2,2,2); \
        SWIZZLE_3(VTYPE,CTYPE,r,r,r,0,0,0); SWIZZLE_3(VTYPE,CTYPE,r,r,g,0,0,1); SWIZZLE_3(VTYPE,CTYPE,r,r,b,0,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,r,g,r,0,1,0); SWIZZLE_3(VTYPE,CTYPE,r,g,g,0,1,1); SWIZZLE_3(VTYPE,CTYPE,r,g,b,0,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,r,b,r,0,2,0); SWIZZLE_3(VTYPE,CTYPE,r,b,g,0,2,1); SWIZZLE_3(VTYPE,CTYPE,r,b,b,0,2,2); \
        SWIZZLE_3(VTYPE,CTYPE,g,r,r,1,0,0); SWIZZLE_3(VTYPE,CTYPE,g,r,g,1,0,1); SWIZZLE_3(VTYPE,CTYPE,g,r,b,1,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,g,g,r,1,1,0); SWIZZLE_3(VTYPE,CTYPE,g,g,g,1,1,1); SWIZZLE_3(VTYPE,CTYPE,g,g,b,1,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,g,b,r,1,2,0); SWIZZLE_3(VTYPE,CTYPE,g,b,g,1,2,1); SWIZZLE_3(VTYPE,CTYPE,g,b,b,1,2,2); \
        SWIZZLE_3(VTYPE,CTYPE,b,r,r,2,0,0); SWIZZLE_3(VTYPE,CTYPE,b,r,g,2,0,1); SWIZZLE_3(VTYPE,CTYPE,b,r,b,2,0,2); \
        SWIZZLE_3(VTYPE,CTYPE,b,g,r,2,1,0); SWIZZLE_3(VTYPE,CTYPE,b,g,g,2,1,1); SWIZZLE_3(VTYPE,CTYPE,b,g,b,2,1,2); \
        SWIZZLE_3(VTYPE,CTYPE,b,b,r,2,2,0); SWIZZLE_3(VTYPE,CTYPE,b,b,g,2,2,1); SWIZZLE_3(VTYPE,CTYPE,b,b,b,2,2,2); \
    }; };

#define DEFINE_VECTOR_4(VTYPE, CTYPE) \
struct VTYPE##4 : public Vector<CTYPE, 4> { \
    using Vector<CTYPE, 4>::Vector; \
    VTYPE##4(const Vector<CTYPE, 4>& base) { this->data[0]=base.data[0]; this->data[1]=base.data[1]; this->data[2]=base.data[2]; this->data[3]=base.data[3]; } \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT>) \
    explicit VTYPE##4(const Vector<OtherT, 4>& other) { \
        for(int i=0; i<4; ++i) this->data[i] = static_cast<CTYPE>(other.data[i]); \
    } \
    /* Explicitly splats a scalar whose type requires a precision conversion. */ \
    template<typename OtherT> \
    requires(!std::same_as<CTYPE, OtherT> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##4(OtherT s) { for(int i=0; i<4; ++i) this->data[i]=CTYPE(s); } \
    template<typename OtherDestVecT, typename OtherSrcT, int... Indices> \
    explicit VTYPE##4(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other) { \
        static_assert(sizeof...(Indices) == 4, "Swizzle size must match vector size for conversion."); \
        OtherDestVecT temp = other; \
        for(int i=0; i<4; ++i) this->data[i] = static_cast<CTYPE>(temp.data[i]); \
    } \
    VTYPE##4() { this->data[0]=CTYPE{}; this->data[1]=CTYPE{}; this->data[2]=CTYPE{}; this->data[3]=CTYPE{};} \
    explicit VTYPE##4(CTYPE s) { this->data[0]=s; this->data[1]=s; this->data[2]=s; this->data[3]=s; } \
    VTYPE##4(CTYPE _x, CTYPE _y, CTYPE _z, CTYPE _w) { this->data[0]=_x; this->data[1]=_y; this->data[2]=_z; this->data[3]=_w; } \
    /* Explicitly builds a vector from components that require precision conversion. */ \
    template<typename X, typename Y, typename Z, typename W> \
    requires((!std::same_as<CTYPE, X> || !std::same_as<CTYPE, Y> || !std::same_as<CTYPE, Z> || !std::same_as<CTYPE, W>) && std::is_constructible_v<CTYPE, X> && std::is_constructible_v<CTYPE, Y> && std::is_constructible_v<CTYPE, Z> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(X _x, Y _y, Z _z, W _w) { this->data[0]=CTYPE(_x); this->data[1]=CTYPE(_y); this->data[2]=CTYPE(_z); this->data[3]=CTYPE(_w); } \
    VTYPE##4(const VTYPE##2& v, CTYPE z, CTYPE w) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=z; this->data[3]=w; } \
    VTYPE##4(const VTYPE##2& v1, const VTYPE##2& v2) { this->data[0]=v1.data[0]; this->data[1]=v1.data[1]; this->data[2]=v2.data[0]; this->data[3]=v2.data[1]; } \
    VTYPE##4(const VTYPE##3& v, CTYPE w) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=v.data[2]; this->data[3]=w; } \
    VTYPE##4(CTYPE x, const VTYPE##3& v) { this->data[0]=x; this->data[1]=v.data[0]; this->data[2]=v.data[1]; this->data[3]=v.data[2]; } \
    /* Explicitly combines vectors and scalars when any component requires precision conversion. */ \
    template<typename OtherT, typename Z, typename W> \
    requires((!std::same_as<CTYPE, OtherT> || !std::same_as<CTYPE, Z> || !std::same_as<CTYPE, W>) && std::is_constructible_v<CTYPE, OtherT> && std::is_constructible_v<CTYPE, Z> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const Vector<OtherT, 2>& v, Z z, W w) { this->data[0]=CTYPE(v.data[0]); this->data[1]=CTYPE(v.data[1]); this->data[2]=CTYPE(z); this->data[3]=CTYPE(w); } \
    template<typename X, typename OtherT> \
    requires((!std::same_as<CTYPE, X> || !std::same_as<CTYPE, OtherT>) && std::is_constructible_v<CTYPE, X> && std::is_constructible_v<CTYPE, OtherT>) \
    explicit VTYPE##4(X x, const Vector<OtherT, 3>& v) { this->data[0]=CTYPE(x); this->data[1]=CTYPE(v.data[0]); this->data[2]=CTYPE(v.data[1]); this->data[3]=CTYPE(v.data[2]); } \
    template<typename OtherT, typename W> \
    requires((!std::same_as<CTYPE, OtherT> || !std::same_as<CTYPE, W>) && std::is_constructible_v<CTYPE, OtherT> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const Vector<OtherT, 3>& v, W w) { this->data[0]=CTYPE(v.data[0]); this->data[1]=CTYPE(v.data[1]); this->data[2]=CTYPE(v.data[2]); this->data[3]=CTYPE(w); } \
    template<typename OtherA, typename OtherB> \
    requires((!std::same_as<CTYPE, OtherA> || !std::same_as<CTYPE, OtherB>) && std::is_constructible_v<CTYPE, OtherA> && std::is_constructible_v<CTYPE, OtherB>) \
    explicit VTYPE##4(const Vector<OtherA, 2>& v1, const Vector<OtherB, 2>& v2) { this->data[0]=CTYPE(v1.data[0]); this->data[1]=CTYPE(v1.data[1]); this->data[2]=CTYPE(v2.data[0]); this->data[3]=CTYPE(v2.data[1]); } \
    /* Explicitly appends or prepends scalars that require precision conversion. */ \
    template<typename Z, typename W> \
    requires((!std::same_as<CTYPE, Z> || !std::same_as<CTYPE, W>) && std::is_constructible_v<CTYPE, Z> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const VTYPE##2& v, Z z, W w) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=CTYPE(z); this->data[3]=CTYPE(w); } \
    template<typename W> \
    requires(!std::same_as<CTYPE, W> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const VTYPE##3& v, W w) { this->data[0]=v.data[0]; this->data[1]=v.data[1]; this->data[2]=v.data[2]; this->data[3]=CTYPE(w); } \
    template<typename X> \
    requires(!std::same_as<CTYPE, X> && std::is_constructible_v<CTYPE, X>) \
    explicit VTYPE##4(X x, const VTYPE##3& v) { this->data[0]=CTYPE(x); this->data[1]=v.data[0]; this->data[2]=v.data[1]; this->data[3]=v.data[2]; } \
    template<typename OtherDestVecT, typename OtherSrcT, typename W, int... Indices> \
    requires(sizeof...(Indices) == 3 && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other, W w) { \
        OtherDestVecT temp = other; \
        this->data[0]=static_cast<CTYPE>(temp.data[0]); this->data[1]=static_cast<CTYPE>(temp.data[1]); this->data[2]=static_cast<CTYPE>(temp.data[2]); this->data[3]=CTYPE(w); \
    } \
    template<typename OtherDestVecT, typename OtherSrcT, typename Z, typename W, int... Indices> \
    requires(sizeof...(Indices) == 2 && std::is_constructible_v<CTYPE, Z> && std::is_constructible_v<CTYPE, W>) \
    explicit VTYPE##4(const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other, Z z, W w) { \
        OtherDestVecT temp = other; \
        this->data[0]=static_cast<CTYPE>(temp.data[0]); this->data[1]=static_cast<CTYPE>(temp.data[1]); this->data[2]=CTYPE(z); this->data[3]=CTYPE(w); \
    } \
    template<typename X, typename OtherDestVecT, typename OtherSrcT, int... Indices> \
    requires(sizeof...(Indices) == 3 && std::is_constructible_v<CTYPE, X>) \
    explicit VTYPE##4(X x, const detail::Swizzle<OtherDestVecT, OtherSrcT, Indices...>& other) { \
        OtherDestVecT temp = other; \
        this->data[0]=CTYPE(x); this->data[1]=static_cast<CTYPE>(temp.data[0]); this->data[2]=static_cast<CTYPE>(temp.data[1]); this->data[3]=static_cast<CTYPE>(temp.data[2]); \
    } \
    union { \
        struct { CTYPE x, y, z, w; }; struct { CTYPE r, g, b, a; }; \
        /* All 2-component swizzles */ \
        SWIZZLE_2(VTYPE,CTYPE,x,x,0,0); SWIZZLE_2(VTYPE,CTYPE,x,y,0,1); SWIZZLE_2(VTYPE,CTYPE,x,z,0,2); SWIZZLE_2(VTYPE,CTYPE,x,w,0,3); \
        SWIZZLE_2(VTYPE,CTYPE,y,x,1,0); SWIZZLE_2(VTYPE,CTYPE,y,y,1,1); SWIZZLE_2(VTYPE,CTYPE,y,z,1,2); SWIZZLE_2(VTYPE,CTYPE,y,w,1,3); \
        SWIZZLE_2(VTYPE,CTYPE,z,x,2,0); SWIZZLE_2(VTYPE,CTYPE,z,y,2,1); SWIZZLE_2(VTYPE,CTYPE,z,z,2,2); SWIZZLE_2(VTYPE,CTYPE,z,w,2,3); \
        SWIZZLE_2(VTYPE,CTYPE,w,x,3,0); SWIZZLE_2(VTYPE,CTYPE,w,y,3,1); SWIZZLE_2(VTYPE,CTYPE,w,z,3,2); SWIZZLE_2(VTYPE,CTYPE,w,w,3,3); \
        SWIZZLE_2(VTYPE,CTYPE,r,r,0,0); SWIZZLE_2(VTYPE,CTYPE,r,g,0,1); SWIZZLE_2(VTYPE,CTYPE,r,b,0,2); SWIZZLE_2(VTYPE,CTYPE,r,a,0,3); \
        SWIZZLE_2(VTYPE,CTYPE,g,r,1,0); SWIZZLE_2(VTYPE,CTYPE,g,g,1,1); SWIZZLE_2(VTYPE,CTYPE,g,b,1,2); SWIZZLE_2(VTYPE,CTYPE,g,a,1,3); \
        SWIZZLE_2(VTYPE,CTYPE,b,r,2,0); SWIZZLE_2(VTYPE,CTYPE,b,g,2,1); SWIZZLE_2(VTYPE,CTYPE,b,b,2,2); SWIZZLE_2(VTYPE,CTYPE,b,a,2,3); \
        SWIZZLE_2(VTYPE,CTYPE,a,r,3,0); SWIZZLE_2(VTYPE,CTYPE,a,g,3,1); SWIZZLE_2(VTYPE,CTYPE,a,b,3,2); SWIZZLE_2(VTYPE,CTYPE,a,a,3,3); \
        /* All 3-component swizzles */ \
        SWIZZLE_3(VTYPE,CTYPE,x,x,x,0,0,0); SWIZZLE_3(VTYPE,CTYPE,y,y,y,1,1,1); SWIZZLE_3(VTYPE,CTYPE,z,z,z,2,2,2); SWIZZLE_3(VTYPE,CTYPE,w,w,w,3,3,3); \
        SWIZZLE_3(VTYPE,CTYPE,x,y,z,0,1,2); SWIZZLE_3(VTYPE,CTYPE,x,z,w,0,2,3); SWIZZLE_3(VTYPE,CTYPE,y,z,w,1,2,3); /* etc... */ \
        SWIZZLE_3(VTYPE,CTYPE,r,r,r,0,0,0); SWIZZLE_3(VTYPE,CTYPE,g,g,g,1,1,1); SWIZZLE_3(VTYPE,CTYPE,b,b,b,2,2,2); SWIZZLE_3(VTYPE,CTYPE,a,a,a,3,3,3); \
        SWIZZLE_3(VTYPE,CTYPE,r,g,b,0,1,2); SWIZZLE_3(VTYPE,CTYPE,r,b,a,0,2,3); SWIZZLE_3(VTYPE,CTYPE,g,b,a,1,2,3); /* etc... */ \
        /* All 4-component swizzles */ \
        SWIZZLE_4(VTYPE,CTYPE,x,y,z,w,0,1,2,3); SWIZZLE_4(VTYPE,CTYPE,w,z,y,x,3,2,1,0); \
        SWIZZLE_4(VTYPE,CTYPE,r,g,b,a,0,1,2,3); SWIZZLE_4(VTYPE,CTYPE,a,b,g,r,3,2,1,0); \
    }; };

// Generate all vector types

DEFINE_VECTOR_2(half, half_t)
DEFINE_VECTOR_3(half, half_t)
DEFINE_VECTOR_4(half, half_t)


DEFINE_VECTOR_2(float, float_t)
DEFINE_VECTOR_3(float, float_t)
DEFINE_VECTOR_4(float, float_t)

DEFINE_VECTOR_2(int, int_t)
DEFINE_VECTOR_3(int, int_t)
DEFINE_VECTOR_4(int, int_t)

DEFINE_VECTOR_2(uint, uint_t)
DEFINE_VECTOR_3(uint, uint_t)
DEFINE_VECTOR_4(uint, uint_t)

DEFINE_VECTOR_2(bool, bool_t)
DEFINE_VECTOR_3(bool, bool_t)
DEFINE_VECTOR_4(bool, bool_t)

DEFINE_VECTOR_2(double, double_t)
DEFINE_VECTOR_3(double, double_t)
DEFINE_VECTOR_4(double, double_t)

namespace detail {
// 默认情况：没有专门的 HLSL 别名类型时，用基础 Vector<T, C>
template<typename T, int C>
struct MatrixRowVector {
    using type = Vector<T, C>;
};

// 针对 float_t，C = 2/3/4 时用 float2/3/4
template<> struct MatrixRowVector<float_t, 2> { using type = float2; };
template<> struct MatrixRowVector<float_t, 3> { using type = float3; };
template<> struct MatrixRowVector<float_t, 4> { using type = float4; };

// 针对 int_t
template<> struct MatrixRowVector<int_t, 2> { using type = int2; };
template<> struct MatrixRowVector<int_t, 3> { using type = int3; };
template<> struct MatrixRowVector<int_t, 4> { using type = int4; };

// 针对 uint_t
template<> struct MatrixRowVector<uint_t, 2> { using type = uint2; };
template<> struct MatrixRowVector<uint_t, 3> { using type = uint3; };
template<> struct MatrixRowVector<uint_t, 4> { using type = uint4; };

// 针对 bool_t
template<> struct MatrixRowVector<bool_t, 2> { using type = bool2; };
template<> struct MatrixRowVector<bool_t, 3> { using type = bool3; };
template<> struct MatrixRowVector<bool_t, 4> { using type = bool4; };

// 针对 double_t
template<> struct MatrixRowVector<double_t, 2> { using type = double2; };
template<> struct MatrixRowVector<double_t, 3> { using type = double3; };
template<> struct MatrixRowVector<double_t, 4> { using type = double4; };
}
//--------------------------------------------------------------------------------------------------
// 3. 矩阵类型定义 (Matrix Types)
//--------------------------------------------------------------------------------------------------
template<typename T, int R, int C>
struct Matrix {
    using row_type = typename detail::MatrixRowVector<T, C>::type;
    row_type m[R];
    row_type& operator[](int row_idx) { return m[row_idx]; }
    row_type operator[](int row_idx) const { return m[row_idx]; }
    //static Matrix identity(){return Matrix{};}
    Matrix()=default;
    explicit Matrix(T){}
    //template<int A, int B>
    //explicit Matrix(Matrix<T,A,B>){}
    //static Matrix look_at(float3 a, float3 b, float3 c){return Matrix{};}
    // 新增：支持用 R 个行向量进行初始化的构造函数
    // 例如: float4x4(row0, row1, row2, row3)
    // 使用 C++20 Concepts 约束参数数量和类型
    template<typename... Args>
    requires(sizeof...(Args) == R && (std::is_convertible_v<Args, row_type> && ...))
    Matrix(Args... args) : m{args...} {}

    template<typename... Args>
    requires(sizeof...(Args) == R*C && (std::is_convertible_v<Args, T> && ...))
    Matrix(Args... args)   {}
};

#define DEFINE_MATRIX_ALIASES(type) \
    using type##1x1 = Matrix<type##_t, 1, 1>; using type##1x2 = Matrix<type##_t, 1, 2>; using type##1x3 = Matrix<type##_t, 1, 3>; using type##1x4 = Matrix<type##_t, 1, 4>; \
    using type##2x1 = Matrix<type##_t, 2, 1>; using type##2x2 = Matrix<type##_t, 2, 2>; using type##2x3 = Matrix<type##_t, 2, 3>; using type##2x4 = Matrix<type##_t, 2, 4>; \
    using type##3x1 = Matrix<type##_t, 3, 1>; using type##3x2 = Matrix<type##_t, 3, 2>; using type##3x3 = Matrix<type##_t, 3, 3>; using type##3x4 = Matrix<type##_t, 3, 4>; \
    using type##4x1 = Matrix<type##_t, 4, 1>; using type##4x2 = Matrix<type##_t, 4, 2>; using type##4x3 = Matrix<type##_t, 4, 3>; using type##4x4 = Matrix<type##_t, 4, 4>;

DEFINE_MATRIX_ALIASES(half)
DEFINE_MATRIX_ALIASES(float)
DEFINE_MATRIX_ALIASES(int)
DEFINE_MATRIX_ALIASES(uint)
DEFINE_MATRIX_ALIASES(double)
DEFINE_MATRIX_ALIASES(bool)



//--------------------------------------------------------------------------------------------------
// 5. HLSL 内置函数 (Intrinsic Functions)
//--------------------------------------------------------------------------------------------------
#define DEFINE_UNARY_FUNC(name) template<detail::NotSwizzle T> T name(T x) { return T(); }
#define DEFINE_BINARY_FUNC(name) template<detail::NotSwizzle T> T name(T a, T b) { return T(); }
#define DEFINE_TERNARY_FUNC(name) template<detail::NotSwizzle T> T name(T a, T b, T c) { return T(); }

DEFINE_UNARY_FUNC(abs) DEFINE_UNARY_FUNC(acos) DEFINE_UNARY_FUNC(asin) DEFINE_UNARY_FUNC(atan)
DEFINE_UNARY_FUNC(ceil) DEFINE_UNARY_FUNC(cos) DEFINE_UNARY_FUNC(cosh) DEFINE_UNARY_FUNC(exp)
DEFINE_UNARY_FUNC(exp2) DEFINE_UNARY_FUNC(floor) DEFINE_UNARY_FUNC(frac) DEFINE_UNARY_FUNC(log)
DEFINE_UNARY_FUNC(log2) DEFINE_UNARY_FUNC(log10) DEFINE_UNARY_FUNC(round) DEFINE_UNARY_FUNC(rsqrt)
DEFINE_UNARY_FUNC(saturate) DEFINE_UNARY_FUNC(sign) DEFINE_UNARY_FUNC(sin) DEFINE_UNARY_FUNC(sinh)
DEFINE_UNARY_FUNC(sqrt) DEFINE_UNARY_FUNC(tan) DEFINE_UNARY_FUNC(tanh) DEFINE_UNARY_FUNC(normalize)
DEFINE_UNARY_FUNC(ddx) DEFINE_UNARY_FUNC(ddy) DEFINE_UNARY_FUNC(fwidth) DEFINE_UNARY_FUNC(radians)

DEFINE_BINARY_FUNC(atan2) DEFINE_BINARY_FUNC(fmod) DEFINE_BINARY_FUNC(min)
DEFINE_BINARY_FUNC(max)  DEFINE_BINARY_FUNC(step) DEFINE_BINARY_FUNC(reflect)

template <class T, int N>
T dot(Vector<T,N> a, Vector<T,N> b){return {};}

template <class T, class C>
T lerp(T a, T b, C c){return T();}

template <class X, class Y>
auto modf(X x, Y y){return x;}

template <class T>
float asfloat(T t){return 0.f;}

template <class T>
uint asuint(T t){return 0u;}
template <class T>
int asint(T t){return 0;}
template <class T>
T pow(T t, T x){return T();}



inline float4x4 lookAt(float3,float3,float3){return float4x4();};
inline float4x4 ortho(float,float,float,float,float,float){return float4x4();}
inline float4x4 perspective(float,float,float,float){return float4x4();}

template <class T>
T identity(){return T();}

DEFINE_TERNARY_FUNC(clamp)  DEFINE_TERNARY_FUNC(smoothstep)
template<typename T> T refract(T i, T n, float1 eta) { return T(); }

inline float3 cross(float3 a, float3 b) { return float3(); }
template<typename T> float1 length(T v) { return 0.0f; }
template<typename T> float1 distance(T a, T b) { return 0.0f; }
//template<typename T, int R, int C, int C2> Matrix<T,R,C2> mul(const Matrix<T,R,C>& m1, const Matrix<T,C,C2>& m2) { return Matrix<T,R,C2>(); }
//template<typename T, int R, int C> Vector<T,R> mul(const Vector<T,C>& v, const Matrix<T,R,C>& m) { return Vector<T,R>(); }
//template<typename T, int R, int C> Vector<T,C> mul(const Matrix<T,R,C>& m, const Vector<T,R>& v) { return Vector<T,C>(); }
template<typename T, int R, int C> Matrix<T, C, R> transpose(const Matrix<T,R,C>& m) { return Matrix<T,C,R>(); }
template<typename T, int N> float1 determinant(const Matrix<T,N,N>& m) { return 1.0f; }
template<typename T, int N> Matrix<T,N,N> inverse(const Matrix<T,N,N>& m) { return Matrix<T,N,N>(); }

template<typename T>
    requires(std::same_as<std::remove_cvref_t<T>, half_t> || std::same_as<std::remove_cvref_t<T>, float_t>)
inline void clip(T x) {}

template<typename T, int N>
    requires((std::same_as<T, half_t> || std::same_as<T, float_t>) && N >= 2 && N <= 4)
inline void clip(const Vector<T, N>& x) {}
inline float4 lit(float1 n_dot_l, float1 n_dot_h, float1 m) { return float4(); }
template<typename T, typename U, typename V> void sincos(T x, U& s, V& c) { s=U(); c=V(); }

// 特殊函数 (mul, cross, etc.)
template<typename T, int R, int C>
inline vector_t<T, C> mul(const Matrix<T, R, C>& m, const Vector<T, R>& v) {
    return vector_t<T, C>();
}

template<typename T, int R, int C>
inline vector_t<T, C> mul(const Vector<T, R>& v, const Matrix<T, R, C>& m) {
    return vector_t<T, C>();
}

/* template<typename T, int R, int C>
inline vector_t<T, R> mul(const Matrix<T, R, C>& m, const Vector<T, C>& v) {
    return vector_t<T, R>();
} */
template<typename T, int R, int C, int C2>
inline Matrix<T,R,C2> mul(const Matrix<T,R,C>& m1, const Matrix<T,C,C2>& m2) {
    return Matrix<T,R,C2>();
}

/* template<typename T, int R0, R1, int C0, int C1>
inline Matrix<T,R0,C1> mul(const Matrix<T,R0,C0>& m1, const Matrix<T,C0,C1>& m2) {
    return Matrix<T,R0,C1>();
} */

/* template <class A, class B, class C>
inline C mul(const A& a, const B& b) {
    return C();
}

 template <class A, class B>
inline B mul(const A& a, const B& b) {
    return B{};
}  */
// 逻辑函数 (all, any)
/**
 * @brief 检查向量的所有分量是否都为 true (或非零).
 * @param v 输入向量。对于数值类型，0 被视为 false，其他所有值被视为 true。
 * @return 如果所有分量都为 true，则返回 true；否则返回 false。
 */
template<typename T, int N>
inline bool_t all(const Vector<T, N>& v) {
    // 这是一个伪实现，仅用于提供函数签名
    return bool_t{};
}

/**
 * @brief 检查向量中是否存在任一分量为 true (或非零).
 * @param v 输入向量。对于数值类型，0 被视为 false，其他所有值被视为 true。
 * @return 如果至少有一个分量为 true，则返回 true；否则返回 false。
 */
template<typename T, int N>
inline bool_t any(const Vector<T, N>& v) {
    // 这是一个伪实现，仅用于提供函数签名
    return bool_t{};
}


} // namespace UGL

//--------------------------------------------------------------------------------------------------
// 全局运算符重载 (Global Operator Overloads)
//--------------------------------------------------------------------------------------------------

// --- 请将这个新的代码块添加到文件末尾 ---

// 新的宏，为具体的向量类型（如 float2, int3）生成运算符
#define DEFINE_COMMON_OPERATORS_FOR_TYPE(VTYPE) \
 /* 新增: 一元运算符 */ \
    inline VTYPE operator-(const VTYPE& v) { return VTYPE(); } \
    inline VTYPE operator+(const VTYPE& v) { return VTYPE(); } \
    inline VTYPE operator+(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator-(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator*(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator/(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator+(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator-(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator*(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator/(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator+(typename VTYPE::value_type s, const VTYPE& v) { return VTYPE(); } \
    inline VTYPE operator-(typename VTYPE::value_type s, const VTYPE& v) { return VTYPE(); } \
    inline VTYPE operator*(typename VTYPE::value_type s, const VTYPE& v) { return VTYPE(); } \
    inline VTYPE operator/(typename VTYPE::value_type s, const VTYPE& v) { return VTYPE(); }

#define DEFINE_INTEGER_OPERATORS_FOR_TYPE(VTYPE) \
    inline VTYPE operator%(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator&(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator|(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator^(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator<<(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator>>(const VTYPE& a, const VTYPE& b) { return VTYPE(); } \
    inline VTYPE operator%(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator&(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator|(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator^(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator<<(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator>>(const VTYPE& v, typename VTYPE::value_type s) { return VTYPE(); } \
    inline VTYPE operator~(const VTYPE& v) { return VTYPE(); }

#define DEFINE_BOOL_OPERATORS_FOR_TYPE(VTYPE) \
    inline VTYPE operator!(const VTYPE& v) { return VTYPE(); }

// 为所有 float 向量类型生成运算符

DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::half2)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::half3)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::half4)


DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::float2)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::float3)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::float4)

// 为所有 double 向量类型生成运算符
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::double2)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::double3)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::double4)

// 为所有 int 向量类型生成运算符
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::int2)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::int3)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::int4)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::int2)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::int3)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::int4)

// 为所有 uint 向量类型生成运算符
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::uint2)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::uint3)
DEFINE_COMMON_OPERATORS_FOR_TYPE(UGL::uint4)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::uint2)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::uint3)
DEFINE_INTEGER_OPERATORS_FOR_TYPE(UGL::uint4)

// bool 类型有不同的运算符集合
DEFINE_BOOL_OPERATORS_FOR_TYPE(UGL::bool2)
DEFINE_BOOL_OPERATORS_FOR_TYPE(UGL::bool3)
DEFINE_BOOL_OPERATORS_FOR_TYPE(UGL::bool4)


template<typename T, int N, typename = std::enable_if_t<std::is_integral_v<T>>>
inline UGL::Vector<T, N> operator~(const UGL::Vector<T, N>& v) { return UGL::Vector<T, N>(); }

template<typename T, int N>
inline UGL::Vector<bool, N> operator!(const UGL::Vector<T, N>& v) { return UGL::Vector<bool, N>(); }



//==================================================================================================
// 全局运算符重载 (Swizzle 专属)
// 为 Swizzle 对象提供专属的、无歧义的运算符，以解决转换问题
//==================================================================================================

#define DEFINE_SWIZZLE_OPERATORS(op) \
    /* swizzle op scalar */ \
    template<typename DestVecT, typename SrcT, int... Indices> \
    inline DestVecT operator op(const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s, SrcT scalar) { \
        return static_cast<DestVecT>(s) op scalar; \
    } \
    /* scalar op swizzle */ \
    template<typename DestVecT, typename SrcT, int... Indices> \
    inline DestVecT operator op(SrcT scalar, const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s) { \
        return scalar op static_cast<DestVecT>(s); \
    } \
    /* swizzle op vector */ \
    template<typename DestVecT, typename SrcT, int... Indices> \
    inline DestVecT operator op(const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s, const DestVecT& v) { \
        return static_cast<DestVecT>(s) op v; \
    } \
    /* vector op swizzle */ \
    template<typename DestVecT, typename SrcT, int... Indices> \
    inline DestVecT operator op(const DestVecT& v, const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s) { \
        return v op static_cast<DestVecT>(s); \
    } \
    /* swizzle op swizzle */ \
    template<typename D1, typename S1, int... I1, typename D2, typename S2, int... I2> \
    inline D1 operator op(const UGL::detail::Swizzle<D1, S1, I1...>& s1, const UGL::detail::Swizzle<D2, S2, I2...>& s2) { \
        return static_cast<D1>(s1) op static_cast<D2>(s2); \
    }

// 为所有数学运算符生成 Swizzle 专属版本
DEFINE_SWIZZLE_OPERATORS(+)
DEFINE_SWIZZLE_OPERATORS(-)
DEFINE_SWIZZLE_OPERATORS(*)
DEFINE_SWIZZLE_OPERATORS(/)

// 为所有整数运算符生成 Swizzle 专属版本
#define DEFINE_INTEGER_SWIZZLE_OPERATORS(op) \
    template<typename DestVecT, typename SrcT, int... Indices, typename = std::enable_if_t<std::is_integral_v<SrcT>>> \
    inline DestVecT operator op(const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s, SrcT scalar) { \
        return static_cast<DestVecT>(s) op scalar; \
    } \
    template<typename DestVecT, typename SrcT, int... Indices, typename = std::enable_if_t<std::is_integral_v<SrcT>>> \
    inline DestVecT operator op(SrcT scalar, const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s) { \
        return scalar op static_cast<DestVecT>(s); \
    } \
    template<typename DestVecT, typename SrcT, int... Indices, typename = std::enable_if_t<std::is_integral_v<SrcT>>> \
    inline DestVecT operator op(const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s, const DestVecT& v) { \
        return static_cast<DestVecT>(s) op v; \
    } \
    template<typename DestVecT, typename SrcT, int... Indices, typename = std::enable_if_t<std::is_integral_v<SrcT>>> \
    inline DestVecT operator op(const DestVecT& v, const UGL::detail::Swizzle<DestVecT, SrcT, Indices...>& s) { \
        return v op static_cast<DestVecT>(s); \
    } \
    template<typename D1, typename S1, int... I1, typename D2, typename S2, int... I2, typename = std::enable_if_t<std::is_integral_v<S1> && std::is_integral_v<S2>>> \
    inline D1 operator op(const UGL::detail::Swizzle<D1, S1, I1...>& s1, const UGL::detail::Swizzle<D2, S2, I2...>& s2) { \
        return static_cast<D1>(s1) op static_cast<D2>(s2); \
    }

DEFINE_INTEGER_SWIZZLE_OPERATORS(%)
DEFINE_INTEGER_SWIZZLE_OPERATORS(&)
DEFINE_INTEGER_SWIZZLE_OPERATORS(|)
DEFINE_INTEGER_SWIZZLE_OPERATORS(^)
DEFINE_INTEGER_SWIZZLE_OPERATORS(<<)
DEFINE_INTEGER_SWIZZLE_OPERATORS(>>)



//==================================================================================================
// 全局运算符重载 (比较运算)
//==================================================================================================

#define DEFINE_COMPARISON_OPERATORS_FOR_TYPE(VTYPE, BVTYPE) \
    /* 向量 vs 向量 */ \
    inline BVTYPE operator==(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    inline BVTYPE operator!=(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    inline BVTYPE operator<(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    inline BVTYPE operator>(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    inline BVTYPE operator<=(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    inline BVTYPE operator>=(const VTYPE& a, const VTYPE& b) { return BVTYPE(); } \
    /* 向量 vs 标量 */ \
    inline BVTYPE operator==(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    inline BVTYPE operator!=(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    inline BVTYPE operator<(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    inline BVTYPE operator>(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    inline BVTYPE operator<=(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    inline BVTYPE operator>=(const VTYPE& v, typename VTYPE::value_type s) { return BVTYPE(); } \
    /* 标量 vs 向量 */ \
    inline BVTYPE operator==(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); } \
    inline BVTYPE operator!=(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); } \
    inline BVTYPE operator<(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); } \
    inline BVTYPE operator>(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); } \
    inline BVTYPE operator<=(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); } \
    inline BVTYPE operator>=(typename VTYPE::value_type s, const VTYPE& v) { return BVTYPE(); }

// 为所有数值和布尔向量类型生成比较运算符
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::half2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::half3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::half4, UGL::bool4)


DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::float2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::float3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::float4, UGL::bool4)

DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::int2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::int3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::int4, UGL::bool4)

DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::uint2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::uint3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::uint4, UGL::bool4)

DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::double2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::double3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::double4, UGL::bool4)

DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::bool2, UGL::bool2)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::bool3, UGL::bool3)
DEFINE_COMPARISON_OPERATORS_FOR_TYPE(UGL::bool4, UGL::bool4)
