#include "VKLogging.hpp"
#include <EASTL/atomic.h>

namespace GVM::RHI::Vulkan
{
    const char *safeLogLabel(const eastl::string &label)
    {
        return label.empty() ? "<unlabeled>" : label.c_str();
    }
} // namespace GVM::RHI::Vulkan

#if GVM_LOGGING_ENABLED

#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    void appendUniqueLogLabel(eastl::vector<eastl::string> &labels, const eastl::string &label)
    {
        const eastl::string resolvedLabel = label.empty() ? eastl::string("<unlabeled>") : label;
        if (eastl::find(labels.begin(), labels.end(), resolvedLabel) != labels.end())
        {
            return;
        }
        labels.push_back(resolvedLabel);
    }

    eastl::string buildLogLabelList(const eastl::vector<eastl::string> &labels, size_t limit)
    {
        if (labels.empty())
        {
            return "<none>";
        }

        eastl::string text;
        const size_t resolvedLimit = eastl::min(limit, labels.size());
        for (size_t index = 0; index < resolvedLimit; ++index)
        {
            if (index != 0u)
            {
                text += "|";
            }
            text += labels[index];
        }
        if (labels.size() > resolvedLimit)
        {
            text += "|...";
        }
        return text;
    }
} // namespace GVM::RHI::Vulkan

#endif
