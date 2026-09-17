#include "VKCommon.hpp"

namespace GVM::RHI::Vulkan
{
    std::runtime_error makeRuntimeError(const char *message)
    {
        return std::runtime_error(message);
    }

    std::runtime_error makeRuntimeError(const eastl::string &message)
    {
        return std::runtime_error(message.c_str());
    }

    std::invalid_argument makeInvalidArgument(const char *message)
    {
        return std::invalid_argument(message);
    }

    std::invalid_argument makeInvalidArgument(const eastl::string &message)
    {
        return std::invalid_argument(message.c_str());
    }

    std::logic_error makeLogicError(const char *message)
    {
        return std::logic_error(message);
    }

    std::logic_error makeLogicError(const eastl::string &message)
    {
        return std::logic_error(message.c_str());
    }

    void incrementAtomicReference(std::atomic<uint32_t> &counter)
    {
        counter.fetch_add(1u, std::memory_order_acq_rel);
    }

    void decrementAtomicReference(std::atomic<uint32_t> &counter, const char *apiName)
    {
        uint32_t expected = counter.load(std::memory_order_acquire);
        while (true)
        {
            if (expected == 0u)
            {
                throw makeLogicError(eastl::string(apiName) + " observed an underflowing lifetime reference count.");
            }
            if (counter.compare_exchange_weak(expected, expected - 1u, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return;
            }
        }
    }

    std::out_of_range makeOutOfRange(const char *message)
    {
        return std::out_of_range(message);
    }

    std::out_of_range makeOutOfRange(const eastl::string &message)
    {
        return std::out_of_range(message.c_str());
    }
} // namespace GVM::RHI::Vulkan
