#include "MQueueHistory.hpp"

namespace GVM::RHI::Metal
{
    /// Tests native byte intervals for RAW, WAR or WAW hazards without overflowing their end offsets.
    static bool conflicts(const MQueueAccess &access, const MQueueAccess &other)
    {
        return access.allocation == other.allocation && (access.writes || other.writes) && access.size != 0 && other.size != 0 &&
            (access.offset <= other.offset ? other.offset - access.offset < access.size : access.offset - other.offset < other.size);
    }

    std::recursive_mutex &MQueueHistory::mutex()
    {
        return mMutex;
    }

    void MQueueHistory::prepare(Queue queue, const eastl::vector<MQueueAccess> &uses, bool opaque, eastl::vector<Submission> &waits)
    {
        for (auto it = mAccepted.begin(); it != mAccepted.end();)
        {
            if (it->completion.isComplete())
            {
                it = mAccepted.erase(it);
                continue;
            }
            if (it->queue != queue)
            {
                bool conflict = opaque || it->opaque;
                for (const auto &access : uses)
                {
                    for (const auto &prior : it->uses)
                    {
                        conflict = conflict || conflicts(access, prior);
                    }
                    if (conflict)
                    {
                        break;
                    }
                }
                if (conflict)
                {
                    waits.push_back(it->completion);
                }
            }
            ++it;
        }
        mAccepted.reserve(mAccepted.size() + 1);
    }

    void MQueueHistory::accept(Queue queue, eastl::vector<MQueueAccess> uses, bool opaque, Submission completion)
    {
        mAccepted.push_back({queue, eastl::move(uses), opaque, eastl::move(completion)});
    }

    void MQueueHistory::clear()
    {
        mAccepted.clear();
    }
}
