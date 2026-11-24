#pragma once

#if defined(__OHOS__) && defined(__cplusplus)
#include <cerrno>
#include <pthread.h>
#include <sched.h>

/** Queries the CPU affinity mask of the calling OpenHarmony thread via sched_getaffinity. */
inline int gvmOhosPthreadGetAffinityNp(pthread_t thread, size_t cpusetSize, cpu_set_t *cpuset)
{
    if (!pthread_equal(thread, pthread_self()))
    {
        return ENOTSUP;
    }
    if (sched_getaffinity(0, cpusetSize, cpuset) == 0)
    {
        return 0;
    }
    return errno;
}

/** Applies the CPU affinity mask to the calling OpenHarmony thread via sched_setaffinity. */
inline int gvmOhosPthreadSetAffinityNp(pthread_t thread, size_t cpusetSize, const cpu_set_t *cpuset)
{
    if (!pthread_equal(thread, pthread_self()))
    {
        return ENOTSUP;
    }
    if (sched_setaffinity(0, cpusetSize, cpuset) == 0)
    {
        return 0;
    }
    return errno;
}

#define pthread_getaffinity_np gvmOhosPthreadGetAffinityNp
#define pthread_setaffinity_np gvmOhosPthreadSetAffinityNp
#endif
