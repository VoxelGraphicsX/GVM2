#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

#if GVM_LOGGING_ENABLED
#include <fmt/format.h>

#include <iterator>
#endif

namespace GVM::RHI::Logging
{
    inline Logger resolveLogger(const Logger &logger)
    {
        return logger;
    }

    inline Logger resolveLogger(Instance instance)
    {
        return getLogger(instance);
    }

    inline Logger resolveLogger(Device device)
    {
        return getDeviceLogger(device);
    }

    inline Logger resolveLogger(std::nullptr_t)
    {
        return Logger{};
    }

    template <typename T>
    concept LoggerProviderReference =
        requires(const T &value)
        {
            { value.getLogger() };
        } && std::is_convertible_v<decltype(std::declval<const T &>().getLogger()), Logger>;

    template <LoggerProviderReference T>
    inline Logger resolveLogger(const T &value)
    {
        return value.getLogger();
    }

    template <typename T>
    concept LoggerProviderPointer =
        requires(const T *value)
        {
            { value->getLogger() };
        } && std::is_convertible_v<decltype(std::declval<const T *>()->getLogger()), Logger>;

    template <LoggerProviderPointer T>
    inline Logger resolveLogger(const T *value)
    {
        return value != nullptr ? value->getLogger() : Logger{};
    }

#if GVM_LOGGING_ENABLED
    template <typename... Args>
    [[nodiscard]]
    inline eastl::string formatLogMessage(const char *formatString, Args &&...args)
    {
        fmt::memory_buffer buffer;
        fmt::format_to(
            std::back_inserter(buffer),
            fmt::runtime(formatString),
            std::forward<Args>(args)...);
        eastl::string message;
        message.assign(buffer.data(), buffer.size());
        return message;
    }
#endif
} // namespace GVM::RHI::Logging

#if GVM_LOGGING_ENABLED
#define GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, levelEnum, formatString, ...)                 \
    do                                                                                                \
    {                                                                                                 \
        const ::GVM::RHI::Logger _gvmLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);       \
        if (::GVM::RHI::shouldLog(_gvmLogger, levelEnum))                                             \
        {                                                                                             \
            ::GVM::RHI::logMessage(                                                                   \
                _gvmLogger,                                                                           \
                levelEnum,                                                                            \
                categoryExpr,                                                                         \
                ::GVM::RHI::Logging::formatLogMessage(formatString __VA_OPT__(,) __VA_ARGS__));      \
        }                                                                                             \
    } while (0)

#define GVMLogTrace(subjectExpr, categoryExpr, formatString, ...) \
    GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, ::GVM::RHI::LogLevel::Trace, formatString __VA_OPT__(,) __VA_ARGS__)
#define GVMLogDebug(subjectExpr, categoryExpr, formatString, ...) \
    GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, ::GVM::RHI::LogLevel::Debug, formatString __VA_OPT__(,) __VA_ARGS__)
#define GVMLogInfo(subjectExpr, categoryExpr, formatString, ...) \
    GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, ::GVM::RHI::LogLevel::Info, formatString __VA_OPT__(,) __VA_ARGS__)
#define GVMLogWarn(subjectExpr, categoryExpr, formatString, ...) \
    GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, ::GVM::RHI::LogLevel::Warn, formatString __VA_OPT__(,) __VA_ARGS__)
#define GVMLogError(subjectExpr, categoryExpr, formatString, ...) \
    GVM_INTERNAL_LOG_IMPL(subjectExpr, categoryExpr, ::GVM::RHI::LogLevel::Error, formatString __VA_OPT__(,) __VA_ARGS__)
#else
#define GVMLogTrace(subjectExpr, categoryExpr, formatString, ...) ((void)0)
#define GVMLogDebug(subjectExpr, categoryExpr, formatString, ...) ((void)0)
#define GVMLogInfo(subjectExpr, categoryExpr, formatString, ...) ((void)0)
#define GVMLogWarn(subjectExpr, categoryExpr, formatString, ...) ((void)0)
#define GVMLogError(subjectExpr, categoryExpr, formatString, ...) ((void)0)
#endif
