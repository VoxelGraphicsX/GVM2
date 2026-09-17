#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GVM::Tests
{
    inline bool readEnvBool(const char *name, bool defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }

        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
        {
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
        {
            return false;
        }
        return defaultValue;
    }

    inline uint64_t readEnvUint64(const char *name, uint64_t defaultValue)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }
        return static_cast<uint64_t>(std::strtoull(value, nullptr, 10));
    }

    inline std::string readEnvString(const char *name, std::string defaultValue = {})
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return defaultValue;
        }
        return std::string(value);
    }

    inline std::filesystem::path requireEnvPath(const char *name)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            throw std::runtime_error(std::string("Required environment variable is missing: ") + name);
        }
        return std::filesystem::path(value);
    }

    inline std::string readTextFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path.string());
        }
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    inline GVM::RHI::LoggerOption makeStringLoggerOption(const char *key, std::string_view value)
    {
        GVM::RHI::LoggerOption option = {};
        option.key = key;
        option.value.type = GVM::RHI::LoggerOptionValueType::String;
        option.value.stringValue.assign(value.data(), value.size());
        return option;
    }

    inline GVM::RHI::LoggerOption makeBoolLoggerOption(const char *key, bool value)
    {
        GVM::RHI::LoggerOption option = {};
        option.key = key;
        option.value.type = GVM::RHI::LoggerOptionValueType::Bool;
        option.value.boolValue = value ? GVM::RHI::True : GVM::RHI::False;
        return option;
    }

    inline GVM::RHI::InstanceDescriptor makeTestInstanceDescriptor()
    {
        GVM::RHI::InstanceDescriptor descriptor = {};
        const bool enableCpuProbes = readEnvBool("GVM_TEST_ENABLE_CPU_PROBES", false);
        const bool enableGeneralLogs = readEnvBool("GVM_TEST_ENABLE_GENERAL_LOGS", false);
        if (!enableCpuProbes && !enableGeneralLogs)
        {
            return descriptor;
        }

        const std::string backendName = readEnvString("GVM_TEST_RHI_BACKEND", "default");
        const std::string outputDirectory = readEnvString(
            "GVM_TEST_CPU_PROBE_OUTPUT_DIR",
            (std::filesystem::temp_directory_path() / "gvm_test_probes").string());
        const std::string fileName = readEnvString(
            "GVM_TEST_CPU_PROBE_FILE_NAME",
            std::string("gvm_test_probe_") + backendName + ".log");

        descriptor.hasLoggingConfig = GVM::RHI::True;
        descriptor.logging.enabled = GVM::RHI::True;
        descriptor.logging.level = enableGeneralLogs ? GVM::RHI::LogLevel::Trace : GVM::RHI::LogLevel::Off;
        descriptor.logging.flushLevel = GVM::RHI::LogLevel::Off;
        descriptor.logging.mode = GVM::RHI::LoggerMode::RegisteredProvider;
        descriptor.logging.registeredProvider.providerName = "spdlog";
        descriptor.logging.registeredProvider.options.push_back(makeStringLoggerOption("sinks", "file"));
        descriptor.logging.registeredProvider.options.push_back(makeStringLoggerOption("output_directory", outputDirectory));
        descriptor.logging.registeredProvider.options.push_back(makeStringLoggerOption("file_name", fileName));
        descriptor.logging.registeredProvider.options.push_back(makeBoolLoggerOption("append", true));
        descriptor.logging.registeredProvider.options.push_back(makeBoolLoggerOption("create_parent_directories", true));
        descriptor.logging.registeredProvider.options.push_back(makeBoolLoggerOption("force_fsync", false));

        descriptor.logging.cpuProbe.enabled = enableCpuProbes ? GVM::RHI::True : GVM::RHI::False;
        descriptor.logging.cpuProbe.level = enableCpuProbes ? GVM::RHI::LogLevel::Trace : GVM::RHI::LogLevel::Off;
        descriptor.logging.cpuProbe.flushLevel = GVM::RHI::LogLevel::Off;
        descriptor.logging.cpuProbe.scopeEmitMode =
            readEnvBool("GVM_TEST_CPU_PROBE_BEGIN_END", false)
                ? GVM::RHI::CpuProbeScopeEmitMode::BeginEnd
                : GVM::RHI::CpuProbeScopeEmitMode::EndOnly;
        descriptor.logging.cpuProbe.minDurationUs = readEnvUint64("GVM_TEST_CPU_PROBE_MIN_DURATION_US", 0u);
        descriptor.logging.cpuProbe.emitInstant =
            readEnvBool("GVM_TEST_CPU_PROBE_EMIT_INSTANT", true) ? GVM::RHI::True : GVM::RHI::False;
        descriptor.logging.cpuProbe.emitValue =
            readEnvBool("GVM_TEST_CPU_PROBE_EMIT_VALUE", true) ? GVM::RHI::True : GVM::RHI::False;
        return descriptor;
    }

    inline GVM::RHI::Instance createTestInstance()
    {
        return GVM::RHI::createInstance(makeTestInstanceDescriptor());
    }
} // namespace GVM::Tests
