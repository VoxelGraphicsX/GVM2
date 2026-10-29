#pragma once

#include "VKCommon.hpp"

#include <GVMRHI/GVMLogging.hpp>

namespace GVM::RHI::Vulkan
{
    const char *safeLogLabel(const eastl::string &label);
} // namespace GVM::RHI::Vulkan

#if GVM_LOGGING_ENABLED

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /** Appends label to labels when it is not already present; empty labels log as "<unlabeled>". */
    void appendUniqueLogLabel(eastl::vector<eastl::string> &labels, const eastl::string &label);
    /** Formats up to limit labels as a pipe-separated list followed by a truncation marker; empty lists log as "<none>". */
    eastl::string buildLogLabelList(const eastl::vector<eastl::string> &labels, size_t limit = 8u);
} // namespace GVM::RHI::Vulkan

#endif
