#pragma once

#include "VKCommon.hpp"

#include <GVMRHI/GVMLogging.hpp>

namespace GVM::RHI::Vulkan
{
    uint64_t submissionDiagnosticId(const VKSubmissionCompletion &completion);
    const char *safeLogLabel(const eastl::string &label);
} // namespace GVM::RHI::Vulkan
