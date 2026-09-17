#include "LoggingCore.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace GVM::RHI::Internal
{
    namespace
    {
        [[nodiscard]]
        bool shouldFlush(LogLevel level, LogLevel flushLevel)
        {
            return flushLevel != LogLevel::Off &&
                   level != LogLevel::Off &&
                   static_cast<uint32_t>(level) >= static_cast<uint32_t>(flushLevel);
        }

        class CallbackLoggerSink final : public ILogSink
        {
        public:
            explicit CallbackLoggerSink(const CallbackLoggerConfig &config)
                : mConfig(config)
            {
            }

            void log(const LogRecord &record) override
            {
                if (mConfig.callback != nullptr)
                {
                    mConfig.callback(record, mConfig.userData);
                }
            }

            void flush() override
            {
            }

        private:
            CallbackLoggerConfig mConfig = {};
        };

        [[nodiscard]]
        uint32_t currentProcessId()
        {
#if defined(_WIN32)
            return static_cast<uint32_t>(_getpid());
#else
            return static_cast<uint32_t>(getpid());
#endif
        }

        [[nodiscard]]
        uint64_t currentThreadId()
        {
            return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }

        [[nodiscard]]
        uint64_t currentTimestampNs()
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        [[nodiscard]]
        CpuProbeConfig disabledCpuProbeConfig()
        {
            CpuProbeConfig config = {};
            config.enabled = False;
            config.level = LogLevel::Off;
            config.flushLevel = LogLevel::Off;
            config.scopeEmitMode = CpuProbeScopeEmitMode::EndOnly;
            config.minDurationUs = 0u;
            config.emitInstant = True;
            config.emitValue = True;
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
        LogLevel normalizeFlushLevel(LogLevel level, LogLevel flushLevel)
        {
            if (level == LogLevel::Off || flushLevel == LogLevel::Off)
            {
                return flushLevel;
            }
            if (static_cast<uint32_t>(flushLevel) < static_cast<uint32_t>(level))
            {
                return level;
            }
            return flushLevel;
        }

        [[nodiscard]]
        bool hasGeneralLoggingRequested(const LoggingConfig &config)
        {
            return config.enabled != False && config.level != LogLevel::Off;
        }

        [[nodiscard]]
        bool hasCpuProbeRequested(const LoggingConfig &config)
        {
            return config.enabled != False &&
                   config.cpuProbe.enabled != False &&
                   config.cpuProbe.level != LogLevel::Off;
        }

        [[nodiscard]]
        bool hasAnyLoggingRequested(const LoggingConfig &config)
        {
            return hasGeneralLoggingRequested(config) || hasCpuProbeRequested(config);
        }

        [[nodiscard]]
        CpuProbeConfig normalizeCpuProbeConfig(const CpuProbeConfig &config)
        {
            CpuProbeConfig normalized = config;
            if (normalized.enabled == False || normalized.level == LogLevel::Off)
            {
                return disabledCpuProbeConfig();
            }

            normalized.flushLevel = normalizeFlushLevel(normalized.level, normalized.flushLevel);
            return normalized;
        }

        [[nodiscard]]
        LoggingConfig normalizeLoggingConfig(const LoggingConfig &config)
        {
            LoggingConfig normalized = config;
            if (normalized.enabled == False)
            {
                return disabledLoggingConfig();
            }

            normalized.flushLevel = normalizeFlushLevel(normalized.level, normalized.flushLevel);
            normalized.cpuProbe = normalizeCpuProbeConfig(normalized.cpuProbe);
            if (!hasAnyLoggingRequested(normalized))
            {
                return disabledLoggingConfig();
            }
            return normalized;
        }

        [[nodiscard]]
        LogLevel mostVerboseEnabledLevel(LogLevel current, LogLevel candidate)
        {
            if (candidate == LogLevel::Off)
            {
                return current;
            }
            if (current == LogLevel::Off)
            {
                return candidate;
            }
            return static_cast<uint32_t>(candidate) < static_cast<uint32_t>(current) ? candidate : current;
        }

        [[nodiscard]]
        LogLevel effectiveProviderLevel(const LoggingConfig &config)
        {
            LogLevel level = LogLevel::Off;
            if (hasGeneralLoggingRequested(config))
            {
                level = mostVerboseEnabledLevel(level, config.level);
            }
            if (hasCpuProbeRequested(config))
            {
                level = mostVerboseEnabledLevel(level, config.cpuProbe.level);
            }
            return level;
        }

        [[nodiscard]]
        LogLevel effectiveProviderFlushLevel(const LoggingConfig &config)
        {
            LogLevel level = LogLevel::Off;
            if (hasGeneralLoggingRequested(config))
            {
                level = mostVerboseEnabledLevel(level, config.flushLevel);
            }
            if (hasCpuProbeRequested(config))
            {
                level = mostVerboseEnabledLevel(level, config.cpuProbe.flushLevel);
            }
            return level;
        }

        [[nodiscard]]
        LogLevel getChannelLevel(const LoggingConfig &config, LogChannel channel)
        {
            switch (channel)
            {
            case LogChannel::CpuProbe:
                return config.cpuProbe.level;
            case LogChannel::General:
            default:
                return config.level;
            }
        }

        [[nodiscard]]
        LogLevel getChannelFlushLevel(const LoggingConfig &config, LogChannel channel)
        {
            switch (channel)
            {
            case LogChannel::CpuProbe:
                return config.cpuProbe.flushLevel;
            case LogChannel::General:
            default:
                return config.flushLevel;
            }
        }

        [[nodiscard]]
        bool isChannelRequested(const LoggingConfig &config, LogChannel channel)
        {
            switch (channel)
            {
            case LogChannel::CpuProbe:
                return hasCpuProbeRequested(config);
            case LogChannel::General:
            default:
                return hasGeneralLoggingRequested(config);
            }
        }

        [[nodiscard]]
        eastl::shared_ptr<ILogSink> createLoggerForConfig(
            GraphicsBackend backend,
            const LoggingConfig &config)
        {
            if (!hasAnyLoggingRequested(config))
            {
                return {};
            }

            if (config.mode == LoggerMode::Callback)
            {
                if (config.callback.callback == nullptr)
                {
                    throw std::runtime_error("Logging mode=Callback requires callback.callback to be non-null.");
                }
                return eastl::make_shared<CallbackLoggerSink>(config.callback);
            }

            LoggerRegistry &registry = getLoggerRegistry();
            eastl::string providerName;
            if (config.mode == LoggerMode::DefaultProvider)
            {
                providerName = registry.getDefaultFactoryName();
                if (providerName.empty())
                {
                    throw std::runtime_error("Logging was enabled, but no default logger provider is registered.");
                }
            }
            else
            {
                providerName = config.registeredProvider.providerName;
                if (providerName.empty())
                {
                    throw std::runtime_error("Logging mode=RegisteredProvider requires providerName to be non-empty.");
                }
            }

            const eastl::shared_ptr<ILoggerFactory> factory = registry.findFactory(providerName);
            if (!factory)
            {
                throw std::runtime_error("The requested logger provider is not registered.");
            }

            LoggerCreateInfo createInfo = {};
            createInfo.backend = backend;
            createInfo.config = config;
            createInfo.level = effectiveProviderLevel(config);
            createInfo.flushLevel = effectiveProviderFlushLevel(config);
            const eastl::vector<LoggerOption> &options =
                config.mode == LoggerMode::RegisteredProvider ? config.registeredProvider.options : eastl::vector<LoggerOption>{};
            return factory->create(createInfo, options);
        }
    } // namespace

    ILogSink::~ILogSink() = default;
    ILoggerFactory::~ILoggerFactory() = default;

    LoggerRegistry::LoggerRegistry() = default;
    LoggerRegistry::~LoggerRegistry() = default;

    void LoggerRegistry::registerFactory(const eastl::shared_ptr<ILoggerFactory> &factory)
    {
        if (!factory)
        {
            return;
        }

        const eastl::string_view name = factory->getName();
        if (name.empty())
        {
            throw std::runtime_error("Logger factory name cannot be empty.");
        }

        std::lock_guard<std::mutex> lock(mMutex);
        mFactories[eastl::string(name)] = factory;
    }

    void LoggerRegistry::unregisterFactory(eastl::string_view name)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mFactories.erase(eastl::string(name));
        if (mDefaultFactoryName == name)
        {
            mDefaultFactoryName.clear();
        }
    }

    eastl::shared_ptr<ILoggerFactory> LoggerRegistry::findFactory(eastl::string_view name) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto iterator = mFactories.find(eastl::string(name));
        return iterator != mFactories.end() ? iterator->second : eastl::shared_ptr<ILoggerFactory>{};
    }

    void LoggerRegistry::setDefaultFactory(eastl::string_view name)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!name.empty())
        {
            const auto iterator = mFactories.find(eastl::string(name));
            if (iterator == mFactories.end())
            {
                throw std::runtime_error("Cannot set the default logger provider to an unregistered factory.");
            }
        }
        mDefaultFactoryName = eastl::string(name);
    }

    eastl::string LoggerRegistry::getDefaultFactoryName() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mDefaultFactoryName;
    }

    LogContext::LogContext(GraphicsBackend backend)
        : mBackend(backend),
          mConfig(disabledLoggingConfig())
    {
    }

    LogContext::~LogContext() = default;

    void LogContext::configure(const LoggingConfig &config)
    {
        const LoggingConfig normalized = normalizeLoggingConfig(config);
        eastl::shared_ptr<ILogSink> nextLogger;
        if (normalized.enabled != False)
        {
            nextLogger = createLoggerForConfig(mBackend, normalized);
        }

        eastl::shared_ptr<ILogSink> previousLogger;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            previousLogger = mActiveLogger;
            mConfig = normalized;
            mActiveLogger = nextLogger;
        }

        if (previousLogger)
        {
            previousLogger->flush();
        }
    }

    void LogContext::emit(LogChannel channel, LogLevel level, eastl::string_view category, const eastl::string &message)
    {
        eastl::shared_ptr<ILogSink> logger;
        LogLevel flushLevel = LogLevel::Off;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mConfig.enabled == False || !isChannelRequested(mConfig, channel))
            {
                return;
            }

            const LogLevel channelLevel = getChannelLevel(mConfig, channel);
            if (!isLogLevelEnabled(level, channelLevel))
            {
                return;
            }

            logger = mActiveLogger;
            flushLevel = getChannelFlushLevel(mConfig, channel);
        }

        if (!logger)
        {
            return;
        }

        LogRecord record = {};
        record.backend = mBackend;
        record.level = level;
        record.channel = channel;
        record.category.assign(category.data(), category.size());
        record.message = message;
        record.timestampNs = currentTimestampNs();
        record.threadId = currentThreadId();
        record.processId = currentProcessId();
        logger->log(record);
        if (shouldFlush(level, flushLevel))
        {
            logger->flush();
        }
    }

    void LogContext::emit(LogLevel level, eastl::string_view category, const eastl::string &message)
    {
        emit(LogChannel::General, level, category, message);
    }

    void LogContext::flush()
    {
        eastl::shared_ptr<ILogSink> logger;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            logger = mActiveLogger;
        }
        if (logger)
        {
            logger->flush();
        }
    }

    bool LogContext::shouldLog(LogChannel channel, LogLevel level) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mConfig.enabled == False || !static_cast<bool>(mActiveLogger) || !isChannelRequested(mConfig, channel))
        {
            return false;
        }
        return isLogLevelEnabled(level, getChannelLevel(mConfig, channel));
    }

    bool LogContext::shouldLog(LogLevel level) const
    {
        return shouldLog(LogChannel::General, level);
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

    bool isLogLevelEnabled(LogLevel requestedLevel, LogLevel threshold)
    {
        return threshold != LogLevel::Off &&
               requestedLevel != LogLevel::Off &&
               static_cast<uint32_t>(requestedLevel) >= static_cast<uint32_t>(threshold);
    }

    LoggerRegistry &getLoggerRegistry()
    {
        static LoggerRegistry registry;
        static const bool initialized = []()
        {
            registerBuiltInLoggerProviders(registry);
            return true;
        }();
        (void)initialized;
        return registry;
    }
} // namespace GVM::RHI::Internal
