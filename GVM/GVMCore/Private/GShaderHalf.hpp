#pragma once

#include <cstdint>
#include <bit>
#include <limits>

namespace GVM::Core::Math
{
    /** Encodes IEEE binary32 as binary16 with round-to-nearest, ties-to-even, without configuring a math library. */
    constexpr uint16_t encodeShaderHalf(float value)
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
        const uint32_t exponent = (bits >> 23u) & 0xffu;
        const uint32_t fraction = bits & 0x7fffffu;
        if (exponent == 0xffu)
        {
            const uint16_t payload = static_cast<uint16_t>(fraction >> 13u);
            return static_cast<uint16_t>(sign | 0x7c00u | (fraction == 0 ? 0u : (payload == 0 ? 1u : payload)));
        }
        int32_t halfExponent = static_cast<int32_t>(exponent) - 112;
        if (halfExponent >= 31) { return static_cast<uint16_t>(sign | 0x7c00u); }
        if (halfExponent <= 0)
        {
            if (halfExponent < -10) { return sign; }
            const uint32_t significand = fraction | 0x800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - halfExponent);
            uint32_t rounded = significand >> shift;
            const uint32_t remainder = significand & ((1u << shift) - 1u);
            const uint32_t halfway = 1u << (shift - 1u);
            if (remainder > halfway || (remainder == halfway && (rounded & 1u))) { ++rounded; }
            return static_cast<uint16_t>(sign | rounded);
        }
        uint32_t rounded = fraction >> 13u;
        const uint32_t remainder = fraction & 0x1fffu;
        if (remainder > 0x1000u || (remainder == 0x1000u && (rounded & 1u))) { ++rounded; }
        if (rounded == 0x400u) { rounded = 0; ++halfExponent; }
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExponent) << 10u) | rounded);
    }

    /** Decodes every binary16 representation exactly into IEEE binary32, preserving signed zero and NaN payload bits. */
    constexpr float decodeShaderHalf(uint16_t value)
    {
        const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16u;
        uint32_t exponent = (value >> 10u) & 0x1fu;
        uint32_t fraction = value & 0x3ffu;
        if (exponent == 0)
        {
            if (fraction == 0) { return std::bit_cast<float>(sign); }
            exponent = 113u;
            while ((fraction & 0x400u) == 0) { fraction <<= 1u; --exponent; }
            fraction &= 0x3ffu;
        }
        else { exponent = exponent == 31u ? 255u : exponent + 112u; }
        return std::bit_cast<float>(sign | (exponent << 23u) | (fraction << 13u));
    }

    /** Stores IEEE 754 binary16 values in generated host buffer payloads without widening their layout. */
    struct ShaderHalf
    {
        uint16_t bits;

        /** Preserves trivial default construction for GLM unions; value-initialize with {} for zero. */
        constexpr ShaderHalf() = default;
        /** Creates a half value from its exact binary16 representation, including NaN payloads. */
        static constexpr ShaderHalf fromBits(uint16_t value) { ShaderHalf result; result.bits = value; return result; }
        /** Rounds a host float to the shader's binary16 storage representation. */
        constexpr ShaderHalf(float value) : bits(encodeShaderHalf(value)) {}
        /** Decodes binary16 storage for host arithmetic or inspection. */
        constexpr operator float() const { return decodeShaderHalf(bits); }
        /** Converts the decoded value to a signed integer using ordinary C++ truncation. */
        explicit operator int32_t() const { return static_cast<int32_t>(float(*this)); }
        /** Converts the decoded value to an unsigned integer using ordinary C++ truncation. */
        explicit operator uint32_t() const { return static_cast<uint32_t>(float(*this)); }
        /** Adds a half operand and rounds the result back to binary16. */
        ShaderHalf &operator+=(ShaderHalf rhs) { return *this = ShaderHalf(float(*this) + float(rhs)); }
        /** Subtracts a half operand and rounds the result back to binary16. */
        ShaderHalf &operator-=(ShaderHalf rhs) { return *this = ShaderHalf(float(*this) - float(rhs)); }
        /** Multiplies by a half operand and rounds the result back to binary16. */
        ShaderHalf &operator*=(ShaderHalf rhs) { return *this = ShaderHalf(float(*this) * float(rhs)); }
        /** Divides by a half operand and rounds the result back to binary16. */
        ShaderHalf &operator/=(ShaderHalf rhs) { return *this = ShaderHalf(float(*this) / float(rhs)); }
        /** Negates a half value, preserving the floating-point sign semantics. */
        ShaderHalf operator-() const { return ShaderHalf(-float(*this)); }
    };

    /** Adds two binary16 values and rounds to binary16. */
    inline ShaderHalf operator+(ShaderHalf lhs, ShaderHalf rhs) { return lhs += rhs; }
    /** Subtracts two binary16 values and rounds to binary16. */
    inline ShaderHalf operator-(ShaderHalf lhs, ShaderHalf rhs) { return lhs -= rhs; }
    /** Multiplies two binary16 values and rounds to binary16. */
    inline ShaderHalf operator*(ShaderHalf lhs, ShaderHalf rhs) { return lhs *= rhs; }
    /** Divides two binary16 values and rounds to binary16. */
    inline ShaderHalf operator/(ShaderHalf lhs, ShaderHalf rhs) { return lhs /= rhs; }
    /** Compares decoded half values, including IEEE NaN and signed-zero semantics. */
    inline bool operator==(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) == float(rhs); }
    /** Compares decoded half values for inequality. */
    inline bool operator!=(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) != float(rhs); }
    /** Compares decoded half values for strict ascending order. */
    inline bool operator<(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) < float(rhs); }
    /** Compares decoded half values for strict descending order. */
    inline bool operator>(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) > float(rhs); }
    /** Compares decoded half values for non-strict ascending order. */
    inline bool operator<=(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) <= float(rhs); }
    /** Compares decoded half values for non-strict descending order. */
    inline bool operator>=(ShaderHalf lhs, ShaderHalf rhs) { return float(lhs) >= float(rhs); }

    static_assert(sizeof(ShaderHalf) == 2 && alignof(ShaderHalf) == 2);
}

namespace std
{
    /** Exposes binary16 numeric properties to GLM's host-side scalar constraints. */
    template <> class numeric_limits<GVM::Core::Math::ShaderHalf>
    {
        using Value = GVM::Core::Math::ShaderHalf;
    public:
        static constexpr bool is_specialized = true;
        static constexpr bool is_signed = true;
        static constexpr bool is_integer = false;
        static constexpr bool is_exact = false;
        static constexpr bool is_iec559 = true;
        static constexpr bool is_bounded = true;
        static constexpr bool is_modulo = false;
        static constexpr bool has_infinity = true;
        static constexpr bool has_quiet_NaN = true;
        static constexpr bool has_signaling_NaN = true;
        static constexpr bool has_denorm_loss = false;
        static constexpr bool traps = false;
        static constexpr bool tinyness_before = false;
        static constexpr float_denorm_style has_denorm = denorm_present;
        static constexpr float_round_style round_style = round_to_nearest;
        static constexpr int radix = 2;
        static constexpr int digits = 11;
        static constexpr int digits10 = 3;
        static constexpr int max_digits10 = 5;
        static constexpr int min_exponent = -13;
        static constexpr int min_exponent10 = -4;
        static constexpr int max_exponent = 16;
        static constexpr int max_exponent10 = 4;
        /** Returns the smallest positive normal binary16 value. */
        static constexpr Value min() noexcept { return Value::fromBits(0x0400); }
        /** Returns the most negative finite binary16 value. */
        static constexpr Value lowest() noexcept { return Value::fromBits(0xfbff); }
        /** Returns the largest finite binary16 value. */
        static constexpr Value max() noexcept { return Value::fromBits(0x7bff); }
        /** Returns the binary16 spacing immediately above one. */
        static constexpr Value epsilon() noexcept { return Value::fromBits(0x1400); }
        /** Returns the maximum rounding error under round-to-nearest. */
        static constexpr Value round_error() noexcept { return Value::fromBits(0x3800); }
        /** Returns positive infinity in binary16 representation. */
        static constexpr Value infinity() noexcept { return Value::fromBits(0x7c00); }
        /** Returns a quiet binary16 NaN. */
        static constexpr Value quiet_NaN() noexcept { return Value::fromBits(0x7e00); }
        /** Returns a signaling binary16 NaN. */
        static constexpr Value signaling_NaN() noexcept { return Value::fromBits(0x7d00); }
        /** Returns the smallest positive binary16 subnormal. */
        static constexpr Value denorm_min() noexcept { return Value::fromBits(0x0001); }
    };
}
