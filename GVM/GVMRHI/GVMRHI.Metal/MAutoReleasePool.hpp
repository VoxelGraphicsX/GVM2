#pragma once
#include <defer/defer.hpp>
#define MAutoReleasePool                              \
	auto pool = NS::AutoreleasePool::alloc()->init(); \
	DEFER({ pool->release(); });
namespace GVM::RHI
{

} // namespace GVM::RHI
