#include "boundary.hpp"
#include <cstdio>

static_assert(EASTL_DEBUG == 0 && EASTL_NAME_ENABLED == 0,
              "Application debug flags must not alter the SDK's EASTL configuration.");

/** Exercises include order, public layouts, and cross-translation-unit container ownership. */
int main()
{
    GVM::RHI::BufferDescriptor descriptor = {};
    descriptor.label = "shared-dependency-buffer";
    descriptor.size = 123456789;
    if (dependencyDescriptorSize() != sizeof(descriptor) || !dependencyDescriptorMatches(descriptor))
        return 1;
    auto labels = dependencyLabels();
    for (unsigned index = 0; index < 257; ++index)
        labels.push_back("a consumer string long enough to require allocated storage");
    if (labels.size() != 259 || labels.front() != descriptor.label)
        return 2;
    if (!glmFirstProjectionMatches() || !gvmFirstProjectionMatches())
        return 3;
    std::puts("Shared GLM conventions, EASTL layouts and container ownership passed.");
    return 0;
}
