#include <UGL.h>

#if defined(DISABLE_UGL)
#error The standalone UGLHeaders consumer must use the DSL declarations.
#endif
#if !defined(EASTL_USER_DEFINED_ALLOCATOR) || EASTL_USER_DEFINED_ALLOCATOR != 1
#error UGLHeaders must propagate the GVM allocator configuration.
#endif

/** Verifies the standalone UGL include and allocation for its vector-valued API. */
int main()
{
    UGL::GpuTimestampFrameProfiler profiler;
    auto scopes = profiler.getScopes();
    for (uint32_t index = 0; index < 257; ++index)
    {
        UGL::GpuTimestampFrameProfiler::Scope scope;
        scope.timestampWrites.beginningOfPassWriteIndex = index;
        scopes.push_back(scope);
    }
    return scopes.size() == 257 && scopes.back().timestampWrites.beginningOfPassWriteIndex == 256 ? 0 : 1;
}
