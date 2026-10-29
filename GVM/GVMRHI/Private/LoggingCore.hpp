#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include "GVMRHIDefines.hpp"

#include <EASTL/shared_ptr.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <mutex>

namespace GVM::RHI::Internal
{
    class ILogSink
    {
    public:
        virtual ~ILogSink();
        virtual void log(const LogRecord &record) = 0;
        virtual void flush() = 0;
    };

    struct LoggerCreateInfo
    {
        GraphicsBackend backend = GraphicsBackend::Undefined;
        LoggingConfig config = {};
        LogLevel level = LogLevel::Info;
        LogLevel flushLevel = LogLevel::Warn;
    };

    class ILoggerFactory
    {
    public:
        virtual ~ILoggerFactory();
        virtual eastl::string_view getName() const = 0;
        virtual eastl::shared_ptr<ILogSink> create(
            const LoggerCreateInfo &createInfo,
            const eastl::vector<LoggerOption> &options) = 0;
    };

    class LoggerRegistry
    {
    public:
        LoggerRegistry();
        ~LoggerRegistry();

        void registerFactory(const eastl::shared_ptr<ILoggerFactory> &factory);
        void unregisterFactory(eastl::string_view name);
        eastl::shared_ptr<ILoggerFactory> findFactory(eastl::string_view name) const;
        void setDefaultFactory(eastl::string_view name);
        eastl::string getDefaultFactoryName() const;

    private:
        mutable std::mutex mMutex;
        eastl::unordered_map<eastl::string, eastl::shared_ptr<ILoggerFactory>> mFactories;
        eastl::string mDefaultFactoryName;
    };

    class LogContext
    {
    public:
        explicit LogContext(GraphicsBackend backend);
        ~LogContext();

        void configure(const LoggingConfig &config);
        void emit(LogChannel channel, LogLevel level, eastl::string_view category, const eastl::string &message);
        void emit(LogLevel level, eastl::string_view category, const eastl::string &message);
        void flush();
        bool shouldLog(LogChannel channel, LogLevel level) const;
        bool shouldLog(LogLevel level) const;
        LoggingConfig getConfig() const;
        GraphicsBackend getBackend() const;

    private:
        GraphicsBackend mBackend = GraphicsBackend::Undefined;
        mutable std::mutex mMutex;
        LoggingConfig mConfig = {};
        eastl::shared_ptr<ILogSink> mActiveLogger;
    };

    LoggerRegistry &getLoggerRegistry();
    void registerBuiltInLoggerProviders(LoggerRegistry &registry);
    bool isLogLevelEnabled(LogLevel requestedLevel, LogLevel threshold);
} // namespace GVM::RHI::Internal
