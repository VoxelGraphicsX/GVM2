#include "TaskRuntime.hpp"
#include "../Private/PassWeightProfiler.hpp"
#include "../Private/TaskExecutionState.hpp"
#include "../Private/TaskRuntimeState.hpp"

namespace GVM::Core
{
    TaskRuntime::TaskRuntime(const TaskRuntimeDescriptor &descriptor)
        : mOwnedExecutor(eastl::make_unique<ThreadExecutor>(descriptor.workerCount)),
          mState(eastl::make_shared<Private::TaskRuntimeState>(*mOwnedExecutor, descriptor))
    {
    }

    TaskRuntime::TaskRuntime(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor)
        : mState(eastl::make_shared<Private::TaskRuntimeState>(executor, descriptor))
    {
    }

    TaskRuntime::TaskRuntime(RHI::DeviceImpl &device, const TaskRuntimeDescriptor &descriptor)
        : mOwnedExecutor(eastl::make_unique<ThreadExecutor>(descriptor.workerCount)),
          mState(eastl::make_shared<Private::TaskRuntimeState>(*mOwnedExecutor, descriptor, &device))
    {
    }

    TaskRuntime::TaskRuntime(RHI::DeviceImpl &device, ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor)
        : mState(eastl::make_shared<Private::TaskRuntimeState>(executor, descriptor, &device))
    {
    }

    TaskRuntime::~TaskRuntime()
    {
        mState->closeAndWait();
    }

    eastl::vector<PassProfileFeedbackEntry> TaskRuntime::passProfileSnapshot() const
    {
        if (mState == nullptr || mState->passWeightProfiler() == nullptr)
        {
            return {};
        }
        return mState->passWeightProfiler()->snapshot();
    }

    eastl::shared_ptr<Private::TaskExecutionState> TaskRuntime::submitEntry(eastl::function<Private::TaskGraphOutput()> prepare)
    {
        auto execution = eastl::make_shared<Private::TaskExecutionState>(mState);
        if (!mState->tryAdmit())
        {
            execution->reject();
            return execution;
        }
        execution->start(eastl::move(prepare));
        return execution;
    }
}
