#include "LoggingCore.hpp"

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace GVM::RHI::Internal
{
    namespace
    {
        constexpr size_t kLogFileBufferSize = 256u * 1024u;

        class DurableFileSink final : public spdlog::sinks::base_sink<std::mutex>
        {
        public:
            DurableFileSink(std::string filePath, bool append, bool forceFsync)
                : mFilePath(std::move(filePath)),
                  mForceFsync(forceFsync),
                  mBuffer(kLogFileBufferSize, '\0')
            {
                mFile = std::fopen(mFilePath.c_str(), append ? "ab" : "wb");
                if (mFile == nullptr)
                {
                    throw std::runtime_error("Failed to open the configured log file.");
                }
                std::setvbuf(mFile, mBuffer.data(), _IOFBF, mBuffer.size());
            }

            ~DurableFileSink() override
            {
                if (mFile != nullptr)
                {
                    flush_();
                    std::fclose(mFile);
                    mFile = nullptr;
                }
            }

        protected:
            void sink_it_(const spdlog::details::log_msg &msg) override
            {
                if (mFile == nullptr)
                {
                    return;
                }

                spdlog::memory_buf_t formatted;
                this->formatter_->format(msg, formatted);
                const size_t bytesWritten = std::fwrite(formatted.data(), sizeof(char), formatted.size(), mFile);
                if (bytesWritten != formatted.size())
                {
                    throw std::runtime_error("Failed to write the full log message to disk.");
                }
            }

            void flush_() override
            {
                if (mFile == nullptr)
                {
                    return;
                }

                std::fflush(mFile);
                if (!mForceFsync)
                {
                    return;
                }

#if defined(_WIN32)
                _commit(_fileno(mFile));
#else
                fsync(fileno(mFile));
#endif
            }

        private:
            std::string mFilePath;
            std::FILE *mFile = nullptr;
            bool mForceFsync = false;
            std::vector<char> mBuffer;
        };

        [[nodiscard]]
        const LoggerOption *findOption(const eastl::vector<LoggerOption> &options, eastl::string_view key)
        {
            for (const LoggerOption &option : options)
            {
                if (option.key == key)
                {
                    return &option;
                }
            }
            return nullptr;
        }

        [[nodiscard]]
        bool readBoolOption(const eastl::vector<LoggerOption> &options, eastl::string_view key, bool defaultValue)
        {
            if (const LoggerOption *option = findOption(options, key))
            {
                if (option->value.type != LoggerOptionValueType::Bool)
                {
                    throw std::runtime_error("Logger option type mismatch: expected Bool.");
                }
                return option->value.boolValue != False;
            }
            return defaultValue;
        }

        [[nodiscard]]
        eastl::string readStringOption(const eastl::vector<LoggerOption> &options, eastl::string_view key, eastl::string defaultValue)
        {
            if (const LoggerOption *option = findOption(options, key))
            {
                if (option->value.type != LoggerOptionValueType::String)
                {
                    throw std::runtime_error("Logger option type mismatch: expected String.");
                }
                return option->value.stringValue;
            }
            return defaultValue;
        }

        [[nodiscard]]
        const char *toBackendName(GraphicsBackend backend)
        {
            switch (backend)
            {
            case GraphicsBackend::Vulkan:
                return "vulkan";
            case GraphicsBackend::Metal:
                return "metal";
            default:
                return "unknown";
            }
        }

        [[nodiscard]]
        spdlog::level::level_enum toSpdlogLevel(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warn:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Off:
            default:
                return spdlog::level::off;
            }
        }

        [[nodiscard]]
        const char *toChannelName(LogChannel channel)
        {
            switch (channel)
            {
            case LogChannel::CpuProbe:
                return "cpu-probe";
            case LogChannel::General:
            default:
                return "log";
            }
        }

        class SpdlogLoggerSink final : public ILogSink
        {
        public:
            explicit SpdlogLoggerSink(std::shared_ptr<spdlog::logger> logger)
                : mLogger(std::move(logger))
            {
            }

            void log(const LogRecord &record) override
            {
                if (!mLogger)
                {
                    return;
                }

                const auto level = toSpdlogLevel(record.level);
                if (!mLogger->should_log(level))
                {
                    return;
                }

                if (record.channel == LogChannel::General)
                {
                    if (record.category.empty())
                    {
                        mLogger->log(level, "{}", record.message.c_str());
                    }
                    else
                    {
                        mLogger->log(level, "[{}] {}", record.category.c_str(), record.message.c_str());
                    }
                    return;
                }

                if (record.category.empty())
                {
                    mLogger->log(level, "[{}] {}", toChannelName(record.channel), record.message.c_str());
                    return;
                }

                mLogger->log(level, "[{}] [{}] {}", toChannelName(record.channel), record.category.c_str(), record.message.c_str());
            }

            void flush() override
            {
                if (mLogger)
                {
                    mLogger->flush();
                }
            }

        private:
            std::shared_ptr<spdlog::logger> mLogger;
        };

        class SpdlogLoggerFactory final : public ILoggerFactory
        {
        public:
            eastl::string_view getName() const override
            {
                return "spdlog";
            }

            eastl::shared_ptr<ILogSink> create(
                const LoggerCreateInfo &createInfo,
                const eastl::vector<LoggerOption> &options) override
            {
                eastl::string sinks = readStringOption(options, "sinks", "console");
                const bool enableConsole = sinks.find("console") != eastl::string::npos;
                const bool enableFile = sinks.find("file") != eastl::string::npos;
                const bool append = readBoolOption(options, "append", true);
                const bool createParentDirectories = readBoolOption(options, "create_parent_directories", true);
                const bool forceFsync = readBoolOption(options, "force_fsync", false);
                const bool colorConsole = readBoolOption(options, "color_console", true);
                eastl::string outputDirectory = readStringOption(options, "output_directory", ".");
                eastl::string fileName = readStringOption(options, "file_name", eastl::string("gvm_") + toBackendName(createInfo.backend) + "_backend.log");

                std::vector<spdlog::sink_ptr> resolvedSinks;
                if (enableConsole)
                {
                    if (colorConsole)
                    {
                        resolvedSinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
                    }
                    else
                    {
                        resolvedSinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
                    }
                }

                if (enableFile)
                {
                    std::filesystem::path outputPath = std::filesystem::path(outputDirectory.c_str()) / fileName.c_str();
                    if (createParentDirectories)
                    {
                        const std::filesystem::path parentPath = outputPath.parent_path();
                        if (!parentPath.empty())
                        {
                            std::filesystem::create_directories(parentPath);
                        }
                    }
                    resolvedSinks.push_back(std::make_shared<DurableFileSink>(outputPath.string(), append, forceFsync));
                }

                if (resolvedSinks.empty())
                {
                    throw std::runtime_error("The spdlog logger provider requires at least one sink.");
                }

                const std::string loggerName = std::string("gvm.") + toBackendName(createInfo.backend);
                auto logger = std::make_shared<spdlog::logger>(
                    loggerName,
                    resolvedSinks.begin(),
                    resolvedSinks.end());
                logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P:%t] [%n] [%^%l%$] %v");
                logger->set_level(toSpdlogLevel(createInfo.level));
                logger->flush_on(toSpdlogLevel(createInfo.flushLevel));
                return eastl::make_shared<SpdlogLoggerSink>(eastl::move(logger));
            }
        };
    } // namespace

    void registerBuiltInLoggerProviders(LoggerRegistry &registry)
    {
        registry.registerFactory(eastl::make_shared<SpdlogLoggerFactory>());
        registry.setDefaultFactory("spdlog");
    }
} // namespace GVM::RHI::Internal
