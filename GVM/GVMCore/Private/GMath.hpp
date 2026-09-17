#pragma once

#include <cstdint>
#include "GShaderHalf.hpp"
namespace GVM::Core::Math
{

    /** Converts a host swizzle to a concrete vector, evaluating its source once, including repeated components. */
    template <typename Destination, typename Source>
    Destination convertShaderSwizzle(const Source &source)
    {
        Destination result{};
        for (int component = 0; component < Destination::length(); ++component)
        {
            result[component] = static_cast<typename Destination::value_type>(source[component]);
        }
        return result;
    }
} // namespace GVM::Core::Math
