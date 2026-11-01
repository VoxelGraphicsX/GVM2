#include "Execution.hpp"
#include "../Private/TaskExecutionState.hpp"

namespace GVM::Core::Private
{
    ExecutionStatus executionStatus(const eastl::shared_ptr<TaskExecutionState> &state)
    {
        return state->status();
    }

    eastl::string executionError(const eastl::shared_ptr<TaskExecutionState> &state)
    {
        return state->error();
    }

    bool waitExecution(const eastl::shared_ptr<TaskExecutionState> &state, uint64_t timeoutNanoseconds)
    {
        return state->wait(timeoutNanoseconds);
    }

    void cancelExecution(const eastl::shared_ptr<TaskExecutionState> &state)
    {
        state->cancel();
    }

    eastl::any executionResult(const eastl::shared_ptr<TaskExecutionState> &state)
    {
        return state->result();
    }
}

template class GVM::Core::Execution<void>;
