#pragma once

#include <GVMRHI/GVMLogging.hpp>

#include <chrono>
#include <cstdint>
#include <utility>

namespace GVM::RHI::Logging
{
#if GVM_CPU_PROBE_ENABLED
    class CpuProbeScope final
    {
    public:
        CpuProbeScope() = default;
        CpuProbeScope(
            const Logger &logger,
            eastl::string_view category,
            eastl::string_view probeName,
            eastl::string detail = {});
        ~CpuProbeScope();

        CpuProbeScope(const CpuProbeScope &) = delete;
        CpuProbeScope &operator=(const CpuProbeScope &) = delete;
        CpuProbeScope(CpuProbeScope &&other) noexcept;
        CpuProbeScope &operator=(CpuProbeScope &&other) noexcept;

    private:
        void release() noexcept;

        Logger mLogger;
        eastl::string mCategory;
        eastl::string mProbeName;
        eastl::string mDetail;
        uint64_t mProbeId = 0u;
        uint64_t mMinDurationUs = 0u;
        CpuProbeScopeEmitMode mEmitMode = CpuProbeScopeEmitMode::EndOnly;
        std::chrono::steady_clock::time_point mStartTime = {};
        bool mActive = false;
    };

    [[nodiscard]] bool shouldEmitCpuProbe(const Logger &logger);
    [[nodiscard]] bool shouldEmitCpuProbeInstant(const Logger &logger);
    [[nodiscard]] bool shouldEmitCpuProbeValue(const Logger &logger);
    void emitCpuProbeInstant(const Logger &logger, eastl::string_view category, eastl::string_view eventName, const eastl::string &detail = {});
    void emitCpuProbeValueU64(const Logger &logger, eastl::string_view category, eastl::string_view metricName, uint64_t value);
    void emitCpuProbeValueF64(const Logger &logger, eastl::string_view category, eastl::string_view metricName, double value, eastl::string_view unit);

    [[nodiscard]]
    inline CpuProbeScope makeCpuProbeScope(const Logger &logger, eastl::string_view category, eastl::string_view probeName)
    {
        if (!shouldEmitCpuProbe(logger))
        {
            return CpuProbeScope{};
        }
        return CpuProbeScope(logger, category, probeName);
    }

    template <typename DetailFactory>
    [[nodiscard]]
    inline CpuProbeScope makeCpuProbeScope(
        const Logger &logger,
        eastl::string_view category,
        eastl::string_view probeName,
        DetailFactory &&detailFactory)
    {
        if (!shouldEmitCpuProbe(logger))
        {
            return CpuProbeScope{};
        }
        return CpuProbeScope(logger, category, probeName, std::forward<DetailFactory>(detailFactory)());
    }

    template <typename DetailFactory>
    inline void emitCpuProbeInstant(
        const Logger &logger,
        eastl::string_view category,
        eastl::string_view eventName,
        DetailFactory &&detailFactory)
    {
        if (!shouldEmitCpuProbeInstant(logger))
        {
            return;
        }
        const eastl::string detail = std::forward<DetailFactory>(detailFactory)();
        emitCpuProbeInstant(logger, category, eventName, detail);
    }
#else
    class CpuProbeScope final
    {
    public:
        CpuProbeScope() = default;
    };
#endif
} // namespace GVM::RHI::Logging

#define GVM_INTERNAL_CPU_PROBE_JOIN_IMPL(a, b) a##b
#define GVM_INTERNAL_CPU_PROBE_JOIN(a, b) GVM_INTERNAL_CPU_PROBE_JOIN_IMPL(a, b)

#if GVM_CPU_PROBE_ENABLED
#define GVM_INTERNAL_CPU_PROBE_SCOPE_IMPL(uniqueName, subjectExpr, categoryExpr, probeNameExpr)                 \
    [[maybe_unused]] auto uniqueName = [&]()                                                                    \
    {                                                                                                           \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);         \
        return ::GVM::RHI::Logging::makeCpuProbeScope(_gvmCpuProbeLogger, categoryExpr, probeNameExpr);        \
    }()

#define GVM_INTERNAL_CPU_PROBE_SCOPE_DETAIL_IMPL(uniqueName, subjectExpr, categoryExpr, probeNameExpr, detailFmt, ...) \
    [[maybe_unused]] auto uniqueName = [&]()                                                                        \
    {                                                                                                               \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);             \
        return ::GVM::RHI::Logging::makeCpuProbeScope(                                                              \
            _gvmCpuProbeLogger,                                                                                     \
            categoryExpr,                                                                                           \
            probeNameExpr,                                                                                          \
            [&]()                                                                                                   \
            {                                                                                                       \
                return ::GVM::RHI::Logging::formatLogMessage(detailFmt __VA_OPT__(,) __VA_ARGS__);                \
            });                                                                                                     \
    }()

#define GVMCpuProbeFunc(subjectExpr, categoryExpr) \
    GVM_INTERNAL_CPU_PROBE_SCOPE_IMPL(GVM_INTERNAL_CPU_PROBE_JOIN(_gvmCpuProbeScope_, __LINE__), subjectExpr, categoryExpr, __func__)

#define GVMCpuProbeScope(subjectExpr, categoryExpr, probeNameLiteral) \
    GVM_INTERNAL_CPU_PROBE_SCOPE_IMPL(GVM_INTERNAL_CPU_PROBE_JOIN(_gvmCpuProbeScope_, __LINE__), subjectExpr, categoryExpr, probeNameLiteral)

#define GVMCpuProbeScopeDetail(subjectExpr, categoryExpr, probeNameLiteral, detailFmt, ...) \
    GVM_INTERNAL_CPU_PROBE_SCOPE_DETAIL_IMPL(GVM_INTERNAL_CPU_PROBE_JOIN(_gvmCpuProbeScope_, __LINE__), subjectExpr, categoryExpr, probeNameLiteral, detailFmt __VA_OPT__(,) __VA_ARGS__)

#define GVMCpuProbeInstant(subjectExpr, categoryExpr, eventNameLiteral)                                          \
    do                                                                                                           \
    {                                                                                                            \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);          \
        ::GVM::RHI::Logging::emitCpuProbeInstant(_gvmCpuProbeLogger, categoryExpr, eventNameLiteral);           \
    } while (0)

#define GVMCpuProbeInstantDetail(subjectExpr, categoryExpr, eventNameLiteral, detailFmt, ...)                    \
    do                                                                                                            \
    {                                                                                                             \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);           \
        ::GVM::RHI::Logging::emitCpuProbeInstant(                                                                 \
            _gvmCpuProbeLogger,                                                                                   \
            categoryExpr,                                                                                         \
            eventNameLiteral,                                                                                     \
            [&]()                                                                                                 \
            {                                                                                                     \
                return ::GVM::RHI::Logging::formatLogMessage(detailFmt __VA_OPT__(,) __VA_ARGS__);              \
            });                                                                                                   \
    } while (0)

#define GVMCpuProbeValueU64(subjectExpr, categoryExpr, metricNameLiteral, valueExpr)                             \
    do                                                                                                            \
    {                                                                                                             \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);           \
        if (::GVM::RHI::Logging::shouldEmitCpuProbeValue(_gvmCpuProbeLogger))                                     \
        {                                                                                                         \
            ::GVM::RHI::Logging::emitCpuProbeValueU64(                                                            \
                _gvmCpuProbeLogger,                                                                               \
                categoryExpr,                                                                                     \
                metricNameLiteral,                                                                                \
                static_cast<uint64_t>(valueExpr));                                                                \
        }                                                                                                         \
    } while (0)

#define GVMCpuProbeValueF64(subjectExpr, categoryExpr, metricNameLiteral, valueExpr, unitLiteral)                \
    do                                                                                                            \
    {                                                                                                             \
        const ::GVM::RHI::Logger _gvmCpuProbeLogger = ::GVM::RHI::Logging::resolveLogger(subjectExpr);           \
        if (::GVM::RHI::Logging::shouldEmitCpuProbeValue(_gvmCpuProbeLogger))                                     \
        {                                                                                                         \
            ::GVM::RHI::Logging::emitCpuProbeValueF64(                                                            \
                _gvmCpuProbeLogger,                                                                               \
                categoryExpr,                                                                                     \
                metricNameLiteral,                                                                                \
                static_cast<double>(valueExpr),                                                                   \
                unitLiteral);                                                                                     \
        }                                                                                                         \
    } while (0)
#else
#define GVMCpuProbeFunc(subjectExpr, categoryExpr) ((void)0)
#define GVMCpuProbeScope(subjectExpr, categoryExpr, probeNameLiteral) ((void)0)
#define GVMCpuProbeScopeDetail(subjectExpr, categoryExpr, probeNameLiteral, detailFmt, ...) ((void)0)
#define GVMCpuProbeInstant(subjectExpr, categoryExpr, eventNameLiteral) ((void)0)
#define GVMCpuProbeInstantDetail(subjectExpr, categoryExpr, eventNameLiteral, detailFmt, ...) ((void)0)
#define GVMCpuProbeValueU64(subjectExpr, categoryExpr, metricNameLiteral, valueExpr) ((void)0)
#define GVMCpuProbeValueF64(subjectExpr, categoryExpr, metricNameLiteral, valueExpr, unitLiteral) ((void)0)
#endif
