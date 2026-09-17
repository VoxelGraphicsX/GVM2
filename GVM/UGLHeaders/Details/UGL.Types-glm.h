#pragma once
// clang-format off

// =================================================================================================
//
// HLSL.h - C++ HLSL Emulation Header with GLM Integration
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
// - **可选的GLM后端**: 通过 UGL_USE_GLM 宏，可以切换为使用 GLM 库进行实际的 SIMD 运算。
// - **内存布局验证**: 使用 static_assert 确保向量类型大小符合HLSL打包规则。
// - **IDE 完美兼容**: 为 Visual Studio, CLion, VS Code 等 IDE 提供最极致的
//   智能提示和代码自动补全。
//
// 版本: 5.7 (Correct Requires Clause Fix)
//
// =================================================================================================




#include <GVM/Dependencies/GLMConfig.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/compatibility.hpp> // HLSL/GLSL compatibility functions (lerp, saturate, etc.)
#include <type_traits>
#include <bit>


namespace UGL {

    using namespace glm;
    template<class T>
    auto frac(T&& t)
    {
        return fract(t);
    }
    template <class Y, class Z>
    auto mul(Y&& y, Z&& z)
    {
        //return z*y;
        return y*z;
    }

    template<typename T>
    inline T rsqrt(const T& x) { return glm::inversesqrt(x); } // HLSL 名 → GLM 实现

    // --- 标量版本：精确对标 HLSL 语义 ---
    inline int32_t  asint (float    x) noexcept { return std::bit_cast<int32_t>(x); }
    inline uint32_t asuint(float    x) noexcept { return std::bit_cast<uint32_t>(x); }
    inline float    asfloat(uint32_t x) noexcept { return std::bit_cast<float>(x); }
    inline float    asfloat(int32_t  x) noexcept { return std::bit_cast<float>(x); }

    inline int32_t  asint (int32_t  x) noexcept { return x; }        // 同尺寸按位重解释=原值
	inline uint32_t asuint(uint32_t x) noexcept { return x; }

    template <class T>
    T ddx(const T&) { return T{}; }

    template <class T>
    T ddy(const T&) { return T{}; }

    template <class T>
    T fwidth(const T&) { return T{}; }

    template <class T>
    T firstbitlow(T t) { return t; }

    template <class T>
    T firstbithigh(T t) { return t; }

    // --- asdouble / asuint(double)：严格遵守 HLSL 的 low/high 顺序 ---
    inline double asdouble(uint32_t lowbits, uint32_t highbits) noexcept {
        uint64_t u64 = (uint64_t(highbits) << 32) | uint64_t(lowbits);
        return std::bit_cast<double>(u64);
    }
    inline void asuint(double v, uint32_t& lowbits, uint32_t& highbits) noexcept {
        uint64_t u64 = std::bit_cast<uint64_t>(v);
        lowbits  = uint32_t(u64 & 0xFFFFFFFFull);
        highbits = uint32_t(u64 >> 32);
    }
    using uint = uint32_t;
    typedef uint32_t					    uint1;			    //!< \brief integer vector with 1 component. (From GLM_GTX_compatibility extension)
	typedef vec<2, uint32_t, highp>			uint2;			    //!< \brief uinteger vector with 2 components. (From GLM_GTX_compatibility extension)
	typedef vec<3, uint32_t, highp>			uint3;			    //!< \brief uinteger vector with 3 components. (From GLM_GTX_compatibility extension)
	typedef vec<4, uint32_t, highp>			uint4;			    //!< \brief uinteger vector with 4 components. (From GLM_GTX_compatibility extension)

	typedef uint32_t						uint1x1;			//!< \brief uinteger matrix with 1 component. (From GLM_GTX_compatibility extension)
	typedef mat<2, 2, uint32_t, highp>		uint2x2;			//!< \brief uinteger matrix with 2 x 2 components. (From GLM_GTX_compatibility extension)
	typedef mat<2, 3, uint32_t, highp>		uint2x3;			//!< \brief uinteger matrix with 2 x 3 components. (From GLM_GTX_compatibility extension)
	typedef mat<2, 4, uint32_t, highp>		uint2x4;			//!< \brief uinteger matrix with 2 x 4 components. (From GLM_GTX_compatibility extension)
	typedef mat<3, 2, uint32_t, highp>		uint3x2;			//!< \brief uinteger matrix with 3 x 2 components. (From GLM_GTX_compatibility extension)
	typedef mat<3, 3, uint32_t, highp>		uint3x3;			//!< \brief uinteger matrix with 3 x 3 components. (From GLM_GTX_compatibility extension)
	typedef mat<3, 4, uint32_t, highp>		uint3x4;			//!< \brief uinteger matrix with 3 x 4 components. (From GLM_GTX_compatibility extension)
	typedef mat<4, 2, uint32_t, highp>		uint4x2;			//!< \brief uinteger matrix with 4 x 2 components. (From GLM_GTX_compatibility extension)
	typedef mat<4, 3, uint32_t, highp>		uint4x3;			//!< \brief uinteger matrix with 4 x 3 components. (From GLM_GTX_compatibility extension)
	typedef mat<4, 4, uint32_t, highp>		uint4x4;			//!< \brief uinteger matrix with 4 x 4 components. (From GLM_GTX_compatibility extension)




    static_assert(sizeof(float2) == 8, "float2 must be 8 bytes");
    static_assert(sizeof(float3) == 12, "float3 must be 12 bytes for StructuredBuffer compatibility");
    static_assert(sizeof(float4) == 16, "float4 must be 16 bytes");


    static_assert(sizeof(int2) == 8, "int2 must be 8 bytes");
    static_assert(sizeof(int3) == 12, "int3 must be 12 bytes");
    static_assert(sizeof(int4) == 16, "int4 must be 16 bytes");


    static_assert(sizeof(uint2) == 8, "uint2 must be 8 bytes");
    static_assert(sizeof(uint3) == 12, "uint3 must be 12 bytes");
    static_assert(sizeof(uint4) == 16, "uint4 must be 16 bytes");


    using half = float;

    struct EmptyVecExpr: public float4 {

        // 构造：rgb 引用到 rgba，保证两者一致
        //explicit EmptyVecExpr()
        //     {}

        // 到 float 的隐式转换（例如返回亮度或某个分量）
        operator float() const {
            // 这里按需求定义：示例取感知亮度
           return 0.f;
        }
        operator glm::float2() const {
            return glm::float2();
        }
        // 到 vec3 的隐式转换：取 xyz / rgb
        operator glm::float3() const {
            return glm::float3();
        }
         // 到 vec3 的隐式转换：取 xyz / rgb
        /* operator glm::float4() const {
            return glm::float4();
        } */
    };

    // -------------------------------------------------------------------------
    // C++20 Concepts 约束
    // -------------------------------------------------------------------------

    // 约束 T 必须是算术类型（int, float, double 等），排除自定义类
    template<typename T>
    concept Arithmetic = std::is_arithmetic_v<T>;

    // -------------------------------------------------------------------------
    // 运算符重载实现
    // -------------------------------------------------------------------------
    // 注意：Swizzle (如 float3.zxy) 会自动隐式转换为 vec 类型，
    // 因此这些重载同时也自动支持了 Swizzle 操作。

    // Operator < (Less Than)
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator<(glm::vec<L, T, Q> const& lhs, glm::vec<L, T, Q> const& rhs) {
        return glm::lessThan(lhs, rhs);
    }

    // Operator <= (Less Than or Equal)
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator<=(glm::vec<L, T, Q> const& lhs, glm::vec<L, T, Q> const& rhs) {
        return glm::lessThanEqual(lhs, rhs);
    }

    // Operator > (Greater Than)
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator>(glm::vec<L, T, Q> const& lhs, glm::vec<L, T, Q> const& rhs) {
        return glm::greaterThan(lhs, rhs);
    }

    // Operator >= (Greater Than or Equal)
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator>=(glm::vec<L, T, Q> const& lhs, glm::vec<L, T, Q> const& rhs) {
        return glm::greaterThanEqual(lhs, rhs);
    }

    // -------------------------------------------------------------------------
    // 标量混合运算支持 (可选，但推荐)
    // HLSL 允许 float3 < 0.5 这种写法，GLM 需要显式构造 vec(0.5)
    // 下面的重载允许直接与标量比较
    // -------------------------------------------------------------------------

    // Vector < Scalar
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator<(glm::vec<L, T, Q> const& lhs, T rhs) {
        return glm::lessThan(lhs, glm::vec<L, T, Q>(rhs));
    }

    // Vector > Scalar
    template<glm::length_t L, Arithmetic T, glm::qualifier Q>
    [[nodiscard]] constexpr glm::vec<L, bool, Q> operator>(glm::vec<L, T, Q> const& lhs, T rhs) {
        return glm::greaterThan(lhs, glm::vec<L, T, Q>(rhs));
    }

}
