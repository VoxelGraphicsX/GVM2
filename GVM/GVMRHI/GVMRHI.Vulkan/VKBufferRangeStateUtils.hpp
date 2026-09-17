#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>

namespace GVM::RHI::Vulkan
{
    [[nodiscard]]
    uint64_t resolveBufferRangeSize(const char *context, const VKBuffer *buffer, uint64_t offset, uint64_t size);

    [[nodiscard]]
    bool bufferRangesOverlap(uint64_t lhsOffset, uint64_t lhsSize, uint64_t rhsOffset, uint64_t rhsSize);

    template <typename StateList>
    void splitBufferRangeStates(StateList &states, uint64_t splitOffset)
    {
        if (splitOffset == 0u)
        {
            return;
        }

        for (size_t index = 0u; index < states.size(); ++index)
        {
            auto &state = states[index];
            const uint64_t stateEnd = state.offset + state.size;
            if (splitOffset <= state.offset || splitOffset >= stateEnd)
            {
                continue;
            }

            auto tail = state;
            tail.offset = splitOffset;
            tail.size = stateEnd - splitOffset;
            state.size = splitOffset - state.offset;
            states.insert(states.begin() + (index + 1u), tail);
            return;
        }
    }

    template <typename StateList, typename PayloadMatches>
    void mergeAdjacentBufferRangeStates(StateList &states, const PayloadMatches &payloadMatches)
    {
        if (states.empty())
        {
            return;
        }

        eastl::sort(
            states.begin(),
            states.end(),
            [](const auto &lhs, const auto &rhs)
            {
                return lhs.offset < rhs.offset;
            });

        StateList mergedStates;
        mergedStates.reserve(states.size());
        for (const auto &state : states)
        {
            if (state.size == 0u)
            {
                continue;
            }

            if (!mergedStates.empty())
            {
                auto &last = mergedStates.back();
                if (payloadMatches(last, state) && last.offset + last.size == state.offset)
                {
                    last.size += state.size;
                    continue;
                }
            }

            mergedStates.push_back(state);
        }

        states = eastl::move(mergedStates);
    }

    template <typename StateList, typename PayloadMatches>
    void overlayBufferRangeState(
        StateList &states,
        typename StateList::value_type state,
        const PayloadMatches &payloadMatches)
    {
        if (state.size == 0u)
        {
            return;
        }

        splitBufferRangeStates(states, state.offset);
        splitBufferRangeStates(states, state.offset + state.size);
        const auto newEnd = eastl::remove_if(
            states.begin(),
            states.end(),
            [&state](const auto &existing)
            {
                return bufferRangesOverlap(existing.offset, existing.size, state.offset, state.size);
            });
        if (newEnd != states.end())
        {
            states.erase(newEnd, states.end());
        }

        const auto insertPosition = eastl::find_if(
            states.begin(),
            states.end(),
            [&state](const auto &existing)
            {
                return existing.offset > state.offset;
            });
        states.insert(insertPosition, state);
        mergeAdjacentBufferRangeStates(states, payloadMatches);
    }
} // namespace GVM::RHI::Vulkan
