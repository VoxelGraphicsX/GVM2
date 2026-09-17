#include "boundary.hpp"

/** Returns the provider's compiled public descriptor size. */
size_t dependencyDescriptorSize()
{
    return sizeof(GVM::RHI::BufferDescriptor);
}

/** Verifies both the container field and the trailing size field across the boundary. */
bool dependencyDescriptorMatches(const GVM::RHI::BufferDescriptor &descriptor)
{
    return descriptor.label == "shared-dependency-buffer" && descriptor.size == 123456789;
}

/** Returns a populated container whose storage is released by the consumer. */
eastl::vector<eastl::string> dependencyLabels()
{
    return {"shared-dependency-buffer", "a string long enough to require allocated storage"};
}
