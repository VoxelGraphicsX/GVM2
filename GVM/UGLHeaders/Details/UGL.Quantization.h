
#pragma once
#include "UGL.Types.h"
// ===================================================================================
// Part 1: Platform & Environment Detection
// ===================================================================================

// --- C++ Mode ---
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#ifdef __cplusplus
#define UGL_CPP_MODE
#endif


#define UGL_ATTRIBUTE(x) x


// ===================================================================================
// Part 2: Math Abstraction Layer (Cross-Language)
// ===================================================================================

namespace UGL
{


    // ===================================================================================
    // Part 3: Codec Policies (Shared Logic)
    // ===================================================================================

    // 1. Unsigned Normalized (0.0 .. 1.0)
    struct Codec_UNorm
    {
        using Type = float;
        static inline uint Encode(float v, int bits)
        {
            float maxVal = float((1u << bits) - 1);
            return round(saturate(v) * maxVal);
        }
        static inline float Decode(uint v, int bits)
        {
            float maxVal = float((1u << bits) - 1);
            return float(v) / maxVal;
        }
    };

    // 2. Unsigned Integer (Raw Bits)
    struct Codec_UInt
    {
        using Type = uint;
        static inline uint Encode(uint v, int /*bits*/)
        {
            return v;
        }
        static inline uint Decode(uint v, int /*bits*/)
        {
            return v;
        }
    };

    // 3. Boolean (1 Bit)
    struct Codec_Bool
    {
        using Type = bool;
        static inline uint Encode(bool v, int /*bits*/)
        {
            return v ? 1u : 0u;
        }
        static inline bool Decode(uint v, int /*bits*/)
        {
            return v != 0;
        }
    };

    // 4. Octahedron (External Implementation Placeholder)
    // Assumption: User provides `InternalPackOcta` and `InternalUnpackOcta`
    // compatible with both C++ (via helpers) and HLSL (via functions).
    struct Codec_Octa
    {
        using Type = float3;
        static inline uint Encode(float3 v, int bits)
        {
            // In a real scenario, call your shared math lib here
            return 0;
        }
        static inline float3 Decode(uint v, int bits)
        {
            return float3(0, 0, 1);
        }
    };

} // namespace UGL

// ===================================================================================
// Part 4: Accessor Logic (Bitwise Operations)
// ===================================================================================

namespace UGL
{

    template <typename StorageType, int GlobalOffset, int Width, typename Codec>
    struct Accessor
    {
        static inline typename Codec::Type Get(StorageType s)
        { // Passed by value in HLSL usually fine for uint4
            // Calculate index and shift constants
            static const int ArrayIdx = GlobalOffset / 32;
            static const int Shift = GlobalOffset % 32;
            static const uint Mask = (Width == 32) ? 0xFFFFFFFFu : ((1u << Width) - 1);

            // In HLSL `s.data[i]` assumes `s` is struct with `uint data[4]` or `uint4` accessed via array?
            // To be generic: We assume StorageType is `uint4` (vector) which allows array access in HLSL via `[]`
            // OR C++ struct wrapper.
            // For Vector types (uint4) in HLSL, array indexing `val[i]` works.
            uint raw = (s[ArrayIdx] >> Shift) & Mask;
            return Codec::Decode(raw, Width);
        }

        static inline void Set(
#ifdef UGL_CPP_MODE
            StorageType &s,
#else
            inout StorageType s,
#endif
            typename Codec::Type val)
        {
            static const int ArrayIdx = GlobalOffset / 32;
            static const int Shift = GlobalOffset % 32;
            static const uint Mask = (Width == 32) ? 0xFFFFFFFFu : ((1u << Width) - 1);

            uint encoded = Codec::Encode(val, Width) & Mask;

            // Clear and Set
            s[ArrayIdx] = (s[ArrayIdx] & ~(Mask << Shift)) | (encoded << Shift);
        }
    };

} // namespace UGL

// ===================================================================================
// Part 5: Preprocessor Macros (DSL)
// ===================================================================================

// --- Helper: Argument Unpacking ---
#define UGL_GET_NAME(tuple) UGL_GET_NAME_I tuple
#define UGL_GET_NAME_I(n, c, b) n
#define UGL_GET_CODEC(tuple) UGL_GET_CODEC_I tuple
#define UGL_GET_CODEC_I(n, c, b) c
#define UGL_GET_BITS(tuple) UGL_GET_BITS_I tuple
#define UGL_GET_BITS_I(n, c, b) b

// --- Codec Macros ---
// Important: Use fully qualified names for C++, macro will strip/handle namespace logic via using or scope

#define UGL_CODEC_REF(x) UGL::x

#define UGL_UNORM(name, bits) (name, UGL_CODEC_REF(Codec_UNorm), bits)
#define UGL_UINT(name, bits) (name, UGL_CODEC_REF(Codec_UInt), bits)
#define UGL_BOOL(name) (name, UGL_CODEC_REF(Codec_Bool), 1)
#define UGL_OCTA(name, bits) (name, UGL_CODEC_REF(Codec_Octa), bits)

// --- Loop Expansion (Standard Boilerplate) ---
#define UGL_EXPAND(x) x
#define UGL_FOR_EACH_1(M, Ctx, Idx, Arg) M(Ctx, Idx, Arg)
#define UGL_FOR_EACH_2(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_1(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_3(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_2(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_4(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_3(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_5(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_4(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_6(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_5(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_7(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_6(M, Ctx, Idx + 1, __VA_ARGS__))
#define UGL_FOR_EACH_8(M, Ctx, Idx, Arg, ...) M(Ctx, Idx, Arg) UGL_EXPAND(UGL_FOR_EACH_7(M, Ctx, Idx + 1, __VA_ARGS__))
// ... Add more if needed ...

#define UGL_GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define UGL_FOR_EACH(Macro, Context, ...) UGL_EXPAND(UGL_GET_MACRO(__VA_ARGS__, UGL_FOR_EACH_8, UGL_FOR_EACH_7, UGL_FOR_EACH_6, UGL_FOR_EACH_5, UGL_FOR_EACH_4, UGL_FOR_EACH_3, UGL_FOR_EACH_2, UGL_FOR_EACH_1)(Macro, Context, 0, __VA_ARGS__))

// ===================================================================================
// Part 6: Core Generation Logic (The Magic)
// ===================================================================================

// Step 1: Chained Offset Calculation
// This is valid in both C++ and HLSL (DXC).
// We define `Offset_N` based on `Offset_N-1`.
// We handle the base case (Index 0) via ternary operator.

#define UGL_PREV_OFFSET(Idx) Offset_##Idx
#define UGL_PREV_WIDTH(Idx) Width_##Idx

// Note: To reference N-1, we need a decrement macro or just a simple textual substitution trick.
// Since macros don't do math, we rely on the linear expansion.
// The macro loop gives us current Idx.
// We actually need to generate the chain:
// static const int Off_0 = 0;
// static const int Off_1 = Off_0 + Width_0;

#define UGL_GEN_OFFSET_LOGIC(MemberName, Idx, Tuple)                                                                             \
    typedef UGL_GET_CODEC(Tuple) Codec_##Idx;                                                                                    \
    static const int Width_##Idx = UGL_GET_BITS(Tuple);                                                                          \
    /* Chain Logic: If Idx=0, Offset=0. Else Offset = PrevOffset + PrevWidth */                                                  \
    /* Since we can't easily do (Idx-1) in C Preprocessor for variable names, we assume the FOR_EACH expands in order */         \
    /* But we need to reference the previous variable name. */                                                                   \
    /* TRICK: We accumulate everything into the struct scope. */                                                                 \
    /* To calculate Offset_N, we simply sum widths? No, Accessor needs scalar offset. */                                         \
    /* Alternative: We define a Helper struct per field that inherits from previous? No, HLSL no inheritance. */                 \
    /* SOLUTION: Just generate the Getters/Setters. We can't generate static const int lines cleanly without knowing N-1 name.*/ \
    /* WAIT: We can use an Accumulator if we rely on C++ enum auto-increment? No, HLSL. */                                       \
    /* BEST APPROACH FOR DUAL LANG: Hardcoded linear dependency is hard in macros without recursion helper. */                   \
    /* Let's use a simpler approach: Each Getter calls an external `GetField<Offset, Width>` */                                  \
    /* But we need to KNOW the Offset. */                                                                                        \
    /* Backtrack: We can use `Sum<0, ...>::value` in C++. In HLSL? */                                                            \
    /* HLSL doesn't support variadic templates well enough for sum. */                                                           \
    /* NEW TRICK: Define `EndOffset_Idx` */

// Refined Logic:
// We will generate `static const int EndOffset_##Idx`.
// EndOffset_0 = Width_0;
// EndOffset_1 = EndOffset_0 + Width_1;
// StartOffset_N = EndOffset_N - Width_N;

// Helper to get previous index name is hard.
// Let's use the standard "Accumulator Struct" pattern which is valid in HLSL struct scope.
// But HLSL struct scope cannot contain other struct definitions easily in old compilers.
// Assuming Modern HLSL (DXC).

// If we strictly cannot do `Idx-1`, we can do this:
// Define an enum or static const sequence.
// C++: enum { O_0 = 0, O_1 = O_0 + W_0 };
// HLSL: static const int O_0 = 0; static const int O_1 = O_0 + W_0;
// BUT we need to know the string "0", "1". The Macro `Idx` provides this!
// We just need a macro `DEC_0 -> Error`, `DEC_1 -> 0`.

#define UGL_DEC_0 X
#define UGL_DEC_1 0
#define UGL_DEC_2 1
#define UGL_DEC_3 2
#define UGL_DEC_4 3
#define UGL_DEC_5 4
#define UGL_DEC_6 5
#define UGL_DEC_7 6
#define UGL_DEC_8 7
// ... define up to max args

#define UGL_GET_PREV(i) UGL_DEC_##i

#define UGL_GEN_OFFSETS(MemberName, Idx, Tuple)         \
    static const int Width_##Idx = UGL_GET_BITS(Tuple); \
    static const int Offset_##Idx = (Idx == 0) ? 0 : (Offset_##UGL_GET_PREV(Idx) + Width_##UGL_GET_PREV(Idx));

#define UGL_GEN_FUNCS(MemberName, Idx, Tuple)                                                                                     \
    inline UGL_GET_CODEC(Tuple)::Type get_##UGL_GET_NAME(Tuple)() const                                                           \
    {                                                                                                                             \
        /* Look up the constants generated by GEN_OFFSETS in the same scope */                                                    \
        /* Namespace fix: In HLSL Accessor is global, in C++ it is UGL::Internal::Accessor */                                     \
        /* We use UGL_CODEC_REF macro to handle namespace */                                                                      \
        return UGL_NAMESPACE_BEGIN Accessor<StorageType, Offset_##Idx, Width_##Idx, UGL_GET_CODEC(Tuple)>::Get(this->MemberName); \
    }                                                                                                                             \
    inline void set_##UGL_GET_NAME(Tuple)(UGL_GET_CODEC(Tuple)::Type v)                                                           \
    {                                                                                                                             \
        UGL_NAMESPACE_BEGIN Accessor<StorageType, Offset_##Idx, Width_##Idx, UGL_GET_CODEC(Tuple)>::Set(this->MemberName, v);     \
    }


// --- Main Macro ---
// Note: In C++, we might put this in a struct. In HLSL, also a struct.
// The constants Offset_0, Offset_1 will be member constants of the struct.
// This is perfectly valid in C++ and HLSL.

#define UGL_PACKED_LAYOUT(MemberName, StorageType, Attribute, ...)                                        \
    /* 1. Storage */                                                                                      \
    StorageType MemberName UGL_ATTRIBUTE(Attribute);                                                      \
                                                                                                          \
    /* 2. Type/Storage Alias for Accessor */                                                              \
    using StorageType_##MemberName = StorageType;                                                         \
                                                                                                          \
    /* 3. Scope Wrapper to prevent name collisions of Offsets if multiple Layouts exist */                \
    /* C++: struct Wrapper { ... } */                                                                     \
    /* HLSL: struct methods can access static consts defined inside. */                                   \
    /* ISSUE: If we have multiple UGL_PACKED_LAYOUT, "Offset_0" will collide inside the parent struct. */ \
    /* SOLUTION: Prefix constants with MemberName. */                                                     \
                                                                                                          \
    /* 4. Generate Offsets (Linear Chain) */                                                              \
    /* We redefine the macros to include MemberName prefix */                                             \
    /* Defines: MemberName_Width_0, MemberName_Offset_0 ... */                                            \
    UGL_FOR_EACH(UGL_INTERNAL_GEN_OFFSETS_W_PREFIX, MemberName, __VA_ARGS__)                              \
                                                                                                          \
    /* 5. Generate Functions */                                                                           \
    UGL_FOR_EACH(UGL_INTERNAL_GEN_FUNCS_W_PREFIX, MemberName, __VA_ARGS__)

// --- Internal Macros for Prefixing ---
#define UGL_INTERNAL_GEN_OFFSETS_W_PREFIX(MemberName, Idx, Tuple)    \
    static const int MemberName##_Width_##Idx = UGL_GET_BITS(Tuple); \
    static const int MemberName##_Offset_##Idx = (Idx == 0) ? 0 : (MemberName##_Offset_##UGL_GET_PREV(Idx) + MemberName##_Width_##UGL_GET_PREV(Idx));

#define UGL_INTERNAL_GEN_FUNCS_W_PREFIX(MemberName, Idx, Tuple)                                                                                             \
    inline UGL_GET_CODEC(Tuple)::Type get_##UGL_GET_NAME(Tuple)() const                                                                                     \
    {                                                                                                                                                       \
        return UGL_NAMESPACE_BEGIN Accessor<StorageType, MemberName##_Offset_##Idx, MemberName##_Width_##Idx, UGL_GET_CODEC(Tuple)>::Get(this->MemberName); \
    }                                                                                                                                                       \
    inline void set_##UGL_GET_NAME(Tuple)(UGL_GET_CODEC(Tuple)::Type v)                                                                                     \
    {                                                                                                                                                       \
        UGL_NAMESPACE_BEGIN Accessor<StorageType, MemberName##_Offset_##Idx, MemberName##_Width_##Idx, UGL_GET_CODEC(Tuple)>::Set(this->MemberName, v);     \
    }
