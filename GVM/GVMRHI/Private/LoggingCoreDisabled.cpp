#include "LoggingCore.hpp"

#include <stdexcept>

namespace GVM::RHI::Internal
{
    namespace
    {
        [[nodiscard]]
        CpuProbeConfig disabledCpuProbeConfig()
        {
            CpuProbeConfig config = {};
            config.enabled = False;
            config.level = LogLevel::Off;
            config.flushLevel = LogLevel::Off;
            return config;
        }

        [[nodiscard]]
        LoggingConfig disabledLoggingConfig()
        {
            LoggingConfig config = {};
            config.enabled = False;
            config.level = LogLevel::Off;
            config.flushLevel = LogLevel::Off;
            config.mode = LoggerMode::DefaultProvider;
            config.cpuProbe = disabledCpuProbeConfig();
            return config;
        }

        [[nodiscard]]
        bool requestsLogging(const LoggingConfig &config)
        {
            return config.enabled != False &&
                   (config.level != LogLevel::Off ||
                    (config.cpuProbe.enabled != False && config.cpuProbe.level != LogLevel::Off));
        }
    } // namespace

    ILogSink::~ILogSink() = default;
    ILoggerFactory::~ILoggerFactory() = default;

    LoggerRegistry::LoggerRegistry() = default;
    LoggerRegistry::~LoggerRegistry() = default;

    void LoggerRegistry::registerFactory(const eastl::shared_ptr<ILoggerFactory> &)
    {
    }

    void LoggerRegistry::unregisterFactory(eastl::string_view)
    {
    }

    eastl::shared_ptr<ILoggerFactory> LoggerRegistry::findFactory(eastl::string_view) const
    {
        return {};
    }

    void LoggerRegistry::setDefaultFactory(eastl::string_view)
    {
    }

    eastl::string LoggerRegistry::getDefaultFactoryName() const
    {
        return {};
    }

    LogContext::LogContext(GraphicsBackend backend)
        : mBackend(backend),
          mConfig(disabledLoggingConfig())
    {
    }

    LogContext::~LogContext() = default;

    void LogContext::configure(const LoggingConfig &config)
    {
        if (requestsLogging(config))
        {
            throw std::runtime_error("Logging was requested, but this build was compiled with GVM_LOGGING_ENABLED=0.");
        }

        std::lock_guard<std::mutex> lock(mMutex);
        mConfig = disabledLoggingConfig();
        mActiveLogger.reset();
    }

    void LogContext::emit(LogChannel, LogLevel, eastl::string_view, const eastl::string &)
    {
    }

    void LogContext::emit(LogLevel, eastl::string_view, const eastl::string &)
    {
    }

    void LogContext::flush()
    {
    }

    bool LogContext::shouldLog(LogChannel, LogLevel) const
    {
        return false;
    }

    bool LogContext::shouldLog(LogLevel) const
    {
        return false;
    }

    LoggingConfig LogContext::getConfig() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mConfig;
    }

    GraphicsBackend LogContext::getBackend() const
    {
        return mBackend;
    }

    bool isLogLevelEnabled(LogLevel, LogLevel)
    {
        return false;
    }

    LoggerRegistry &getLoggerRegistry()
    {
        static LoggerRegistry registry;
        return registry;
    }
} // namespace GVM::RHI::Internal
