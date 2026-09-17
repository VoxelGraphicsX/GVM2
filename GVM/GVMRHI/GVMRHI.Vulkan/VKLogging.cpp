#include "VKLogging.hpp"

namespace GVM::RHI::Vulkan
{
    uint64_t submissionDiagnosticId(const VKSubmissionCompletion &completion)
    {
        return completion ? completion->submissionId.load(std::memory_order_acquire) : 0u;
    }

    const char *safeLogLabel(const eastl::string &label)
    {
        return label.empty() ? "<unlabeled>" : label.c_str();
    }
} // namespace GVM::RHI::Vulkan
