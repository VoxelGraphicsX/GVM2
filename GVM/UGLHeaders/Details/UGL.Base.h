#pragma once
#include <inttypes.h>
#include <limits>
namespace UGL
{
    using string = const char *;
    static constexpr uint64_t WholeSize = std::numeric_limits<uint64_t>::max();
    static constexpr uint64_t WholeMapSize = std::numeric_limits<uint64_t>::max();

} // namespace UGL
