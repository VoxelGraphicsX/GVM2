#include <GVMRHI/GVMCpuProbe.hpp>

#include <atomic>

namespace GVM::RHI::Logging
{
#if GVM_CPU_PROBE_ENABLED
    namespace
    {
        std::atomic<uint64_t> gNextCpuProbeId = 1u;

        [[nodiscard]]
        CpuProbeConfig getCpuProbeConfig(const Logger &logger)
        {
            return logger ? logger->getConfig().cpuProbe : CpuProbeConfig{};
        }

        [[nodiscard]]
        uint64_t nextCpuProbeId()
        {
            return gNextCpuProbeId.fetch_add(1u, std::memory_order_acq_rel);
        }

        [[nodiscard]]
        uint64_t durationMicroseconds(const std::chrono::steady_clock::time_point startTime)
        {
            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
        }

        void emitScopeMessage(
            const Logger &logger,
            eastl::string_view category,
            const char *eventName,
            uint64_t probeId,
            eastl::string_view probeName,
            uint64_t durationUs,
            eastl::string_view detail)
        {
            eastl::string message;
            if (durationUs == 0u)
            {
                if (detail.empty())
                {
                    message = formatLogMessage(
                        "event={} probe_id={} name={}",
                        eventName,
                        probeId,
                        probeName);
                }
                else
                {
                    message = formatLogMessage(
                        "event={} probe_id={} name={} detail={}",
                        eventName,
                        probeId,
                        probeName,
                        detail);
                }
            }
            else
            {
                const double durationMs = static_cast<double>(durationUs) / 1000.0;
                if (detail.empty())
                {
                    message = formatLogMessage(
                        "event={} probe_id={} name={} duration_us={} duration_ms={:.3f}",
                        eventName,
                        probeId,
                        probeName,
                        durationUs,
                        durationMs);
                }
                else
                {
                    message = formatLogMessage(
                        "event={} probe_id={} name={} duration_us={} duration_ms={:.3f} detail={}",
                        eventName,
                        probeId,
                        probeName,
                        durationUs,
                        durationMs,
                        detail);
                }
            }

            logChannelMessage(logger, LogChannel::CpuProbe, LogLevel::Trace, category, message);
        }
    } // namespace

    bool shouldEmitCpuProbe(const Logger &logger)
    {
        return shouldLogChannel(logger, LogChannel::CpuProbe, LogLevel::Trace);
    }

    bool shouldEmitCpuProbeInstant(const Logger &logger)
    {
        if (!shouldEmitCpuProbe(logger))
        {
            return false;
        }
        return getCpuProbeConfig(logger).emitInstant != False;
    }

    bool shouldEmitCpuProbeValue(const Logger &logger)
    {
        if (!shouldEmitCpuProbe(logger))
        {
            return false;
        }
        return getCpuProbeConfig(logger).emitValue != False;
    }

    CpuProbeScope::CpuProbeScope(
        const Logger &logger,
        eastl::string_view category,
        eastl::string_view probeName,
        eastl::string detail)
        : mLogger(logger)
    {
        if (!shouldEmitCpuProbe(mLogger))
        {
            return;
        }

        const CpuProbeConfig config = getCpuProbeConfig(mLogger);
        mProbeId = nextCpuProbeId();
        mMinDurationUs = config.minDurationUs;
        mEmitMode = config.scopeEmitMode;
        mCategory.assign(category.data(), category.size());
        mProbeName.assign(probeName.data(), probeName.size());
        mDetail = eastl::move(detail);
        mStartTime = std::chrono::steady_clock::now();
        mActive = true;

        if (mEmitMode == CpuProbeScopeEmitMode::BeginEnd)
        {
            emitScopeMessage(mLogger, mCategory, "cpu_probe_scope_begin", mProbeId, mProbeName, 0u, mDetail);
        }
    }

    CpuProbeScope::~CpuProbeScope()
    {
        release();
    }

    CpuProbeScope::CpuProbeScope(CpuProbeScope &&other) noexcept
        : mLogger(eastl::move(other.mLogger)),
          mCategory(eastl::move(other.mCategory)),
          mProbeName(eastl::move(other.mProbeName)),
          mDetail(eastl::move(other.mDetail)),
          mProbeId(other.mProbeId),
          mMinDurationUs(other.mMinDurationUs),
          mEmitMode(other.mEmitMode),
          mStartTime(other.mStartTime),
          mActive(other.mActive)
    {
        other.mProbeId = 0u;
        other.mMinDurationUs = 0u;
        other.mActive = false;
    }

    CpuProbeScope &CpuProbeScope::operator=(CpuProbeScope &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        release();
        mLogger = eastl::move(other.mLogger);
        mCategory = eastl::move(other.mCategory);
        mProbeName = eastl::move(other.mProbeName);
        mDetail = eastl::move(other.mDetail);
        mProbeId = other.mProbeId;
        mMinDurationUs = other.mMinDurationUs;
        mEmitMode = other.mEmitMode;
        mStartTime = other.mStartTime;
        mActive = other.mActive;
        other.mProbeId = 0u;
        other.mMinDurationUs = 0u;
        other.mActive = false;
        return *this;
    }

    void CpuProbeScope::release() noexcept
    {
        if (!mActive || !shouldEmitCpuProbe(mLogger))
        {
            return;
        }

        const uint64_t durationUs = durationMicroseconds(mStartTime);
        if (mEmitMode == CpuProbeScopeEmitMode::EndOnly && durationUs < mMinDurationUs)
        {
            mActive = false;
            return;
        }

        emitScopeMessage(mLogger, mCategory, "cpu_probe_scope_end", mProbeId, mProbeName, durationUs, mDetail);
        mActive = false;
    }

    void emitCpuProbeInstant(const Logger &logger, eastl::string_view category, eastl::string_view eventName, const eastl::string &detail)
    {
        if (!shouldEmitCpuProbeInstant(logger))
        {
            return;
        }

        eastl::string message;
        if (detail.empty())
        {
            message = formatLogMessage("event=cpu_probe_instant name={}", eventName);
        }
        else
        {
            message = formatLogMessage("event=cpu_probe_instant name={} detail={}", eventName, detail);
        }
        logChannelMessage(logger, LogChannel::CpuProbe, LogLevel::Trace, category, message);
    }

    void emitCpuProbeValueU64(const Logger &logger, eastl::string_view category, eastl::string_view metricName, uint64_t value)
    {
        if (!shouldEmitCpuProbeValue(logger))
        {
            return;
        }

        logChannelMessage(
            logger,
            LogChannel::CpuProbe,
            LogLevel::Trace,
            category,
            formatLogMessage("event=cpu_probe_value name={} value_u64={}", metricName, value));
    }

    void emitCpuProbeValueF64(const Logger &logger, eastl::string_view category, eastl::string_view metricName, double value, eastl::string_view unit)
    {
        if (!shouldEmitCpuProbeValue(logger))
        {
            return;
        }

        if (unit.empty())
        {
            logChannelMessage(
                logger,
                LogChannel::CpuProbe,
                LogLevel::Trace,
                category,
                formatLogMessage("event=cpu_probe_value name={} value_f64={:.6f}", metricName, value));
            return;
        }

        logChannelMessage(
            logger,
            LogChannel::CpuProbe,
            LogLevel::Trace,
            category,
            formatLogMessage("event=cpu_probe_value name={} value_f64={:.6f} unit={}", metricName, value, unit));
    }
#endif
} // namespace GVM::RHI::Logging
