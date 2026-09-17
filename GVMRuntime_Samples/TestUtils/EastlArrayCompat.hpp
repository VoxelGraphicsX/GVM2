#pragma once

#if __has_include(<EASTL/array.h>)
#include <EASTL/array.h>
#else
#include <array>

namespace eastl
{
    template <class T, std::size_t N>
    using array = std::array<T, N>;
}
#endif
