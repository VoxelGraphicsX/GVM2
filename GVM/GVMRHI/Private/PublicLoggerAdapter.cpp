#include "PublicLoggerAdapter.hpp"

namespace GVM::RHI::Internal
{
    namespace
    {
        class PublicLoggerAdapter final : public LoggerImpl
        {
        public:
            explicit PublicLoggerAdapter(eastl::shared_ptr<LogContext> logContext)
                : mLogContext(logContext)
            {
            }

            GraphicsBackend getBackend() const override
            {
                return mLogContext ? mLogContext->getBackend() : GraphicsBackend::Undefined;
            }

            LoggingConfig getConfig() const override
            {
                return mLogContext ? mLogContext->getConfig() : LoggingConfig{};
            }

            bool shouldLogChannel(LogChannel channel, LogLevel level) const override
            {
                return mLogContext && mLogContext->shouldLog(channel, level);
            }

            void logChannel(LogChannel channel, LogLevel level, eastl::string_view category, eastl::string_view message) override
            {
                if (!mLogContext)
                {
                    return;
                }

                eastl::string ownedMessage;
                ownedMessage.assign(message.data(), message.size());
                mLogContext->emit(channel, level, category, ownedMessage);
            }

            void flush() override
            {
                if (mLogContext)
                {
                    mLogContext->flush();
                }
            }

        private:
            eastl::shared_ptr<LogContext> mLogContext;
        };
    } // namespace

    Logger createPublicLogger(const eastl::shared_ptr<LogContext> &logContext)
    {
        if (!logContext)
        {
            return Logger{};
        }

        return Logger(new PublicLoggerAdapter(logContext));
    }
} // namespace GVM::RHI::Internal
