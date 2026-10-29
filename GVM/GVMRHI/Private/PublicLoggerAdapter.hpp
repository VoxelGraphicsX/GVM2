#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include "LoggingCore.hpp"

namespace GVM::RHI::Internal
{
    [[nodiscard]] Logger createPublicLogger(const eastl::shared_ptr<LogContext> &logContext);
} // namespace GVM::RHI::Internal
