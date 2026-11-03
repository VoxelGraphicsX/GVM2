#pragma once
#include <GVMRHI/GVMRHI.hpp>
#include <mutex>

namespace GVM::RHI::Metal
{
    /// Names a native allocation's byte range; texture views use the identity of their parent allocation.
    struct MQueueAccess
    {
        const void *allocation = nullptr;
        uint64_t offset = 0;
        uint64_t size = UINT64_MAX;
        bool writes = false;
    };

    /// Holds outstanding submissions across both device queues; native command buffers retain their referenced allocations.
    class MQueueHistory final
    {
    public:
        /// Orders native acceptance and publication across queues without serializing command recording.
        std::recursive_mutex &mutex();
        /// Reserves publication storage and adds foreign waits for outstanding conflicting submissions.
        void prepare(Queue queue, const eastl::vector<MQueueAccess> &uses, bool opaque, eastl::vector<Submission> &waits);
        /// Publishes an accepted submission using storage reserved before any native commit.
        void accept(Queue queue, eastl::vector<MQueueAccess> uses, bool opaque, Submission completion);
        /// Releases completion handles after both device queues have drained.
        void clear();
    private:
        /// Keeps the exact accesses of one accepted batch until all its native command buffers complete.
        struct Accepted
        {
            Queue queue;
            eastl::vector<MQueueAccess> uses;
            bool opaque;
            Submission completion;
        };
        std::recursive_mutex mMutex;
        eastl::vector<Accepted> mAccepted;
    };
}
