#pragma once

#include <chrono>

namespace GVM::RHI::Internal
{
    [[nodiscard]]
    inline double elapsedMilliseconds(std::chrono::steady_clock::time_point startTime)
    {
        const auto elapsed = std::chrono::steady_clock::now() - startTime;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }
} // namespace GVM::RHI::Internal
