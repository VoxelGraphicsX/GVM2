#pragma once
#include <GVMRHI/GVMRHI.hpp>

/** Returns the public descriptor layout seen by the separately compiled provider. */
size_t dependencyDescriptorSize();
/** Checks descriptor fields across translation units with different debug flags. */
bool dependencyDescriptorMatches(const GVM::RHI::BufferDescriptor &descriptor);
/** Creates owned strings that the consumer grows and releases using the shared allocator. */
eastl::vector<eastl::string> dependencyLabels();
/** Checks projection semantics after including GLM before the actual GVM host header. */
bool glmFirstProjectionMatches();
/** Checks projection semantics after including the actual GVM host header before GLM. */
bool gvmFirstProjectionMatches();
