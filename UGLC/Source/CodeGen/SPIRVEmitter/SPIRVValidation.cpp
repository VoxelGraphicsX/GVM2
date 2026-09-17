#include "SPIRVValidation.hpp"

#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>

#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace UGLC::CodeGen::SPIRVEmitter
{
    namespace
    {
        /** Returns a compact label for a SPIRV-Tools message level. */
        const char *getSPIRVToolsMessageLevelName(spv_message_level_t level)
        {
            switch (level)
            {
            case SPV_MSG_FATAL:
                return "fatal";
            case SPV_MSG_INTERNAL_ERROR:
                return "internal";
            case SPV_MSG_ERROR:
                return "error";
            case SPV_MSG_WARNING:
                return "warning";
            case SPV_MSG_INFO:
                return "info";
            case SPV_MSG_DEBUG:
                return "debug";
            }
            return "message";
        }

        /** Returns the Vulkan SPIR-V environment used by the direct writer and optimizer. */
        spv_target_env getSPIRVTargetEnvironment()
        {
            return SPV_ENV_VULKAN_1_2;
        }
    } // namespace

    SPIRVOptimizationResult optimizeSPIRVWordsForRuntime(const std::vector<uint32_t> &words)
    {
        SPIRVOptimizationResult result;
        spvtools::Optimizer optimizer(getSPIRVTargetEnvironment());
        std::ostringstream diagnostics;
        optimizer.SetMessageConsumer([&diagnostics](spv_message_level_t level, const char *source, const spv_position_t &position, const char *message) {
            diagnostics << getSPIRVToolsMessageLevelName(level) << ": ";
            if (source != nullptr && source[0] != '\0')
            {
                diagnostics << source << ':';
            }
            diagnostics << position.line << ':' << position.column << ": " << (message == nullptr ? "" : message) << '\n';
        });

        const std::vector<std::string> optimizerFlags = {
            "--inline-entry-points-exhaustive",
            "--convert-local-access-chains",
            "--eliminate-local-multi-store",
            "--eliminate-local-single-block",
            "--eliminate-local-single-store",
            // Keep aggregate stores: scalar replacement reconstructs RowMajor matrix records
            // in a form that macOS Vulkan translates with an extra transpose. The uniform
            // matrix aggregate GPU regression covers this call-boundary failure.
            "--ssa-rewrite",
            "--ccp",
            "--redundancy-elimination",
            "--eliminate-dead-branches",
            "--eliminate-dead-inserts",
            // Fold remaining composite insert/extract chains before publishing the module.
            // This also avoids SPIRV-Cross emitting an unconverted bool-vector struct store.
            "--simplify-instructions",
            "--eliminate-dead-code-aggressive",
            "--eliminate-dead-functions",
            "--eliminate-dead-const",
            "--compact-ids",
            // Keep names in raw diagnostics, but omit debug instructions from optimized runtime modules.
            "--strip-debug",
        };
        if (!optimizer.RegisterPassesFromFlags(optimizerFlags, true))
        {
            result.diagnostics = diagnostics.str();
            return result;
        }

        spvtools::ValidatorOptions validatorOptions;
        result.optimized = optimizer.Run(words.data(), words.size(), &result.words, validatorOptions, false);
        result.diagnostics = diagnostics.str();
        return result;
    }

    SPIRVValidationResult validateAndDisassembleSPIRVWords(const std::vector<uint32_t> &words)
    {
        SPIRVValidationResult result;
        spvtools::SpirvTools tools(getSPIRVTargetEnvironment());
        std::ostringstream diagnostics;
        tools.SetMessageConsumer([&diagnostics](spv_message_level_t level, const char *source, const spv_position_t &position, const char *message) {
            diagnostics << getSPIRVToolsMessageLevelName(level) << ": ";
            if (source != nullptr && source[0] != '\0')
            {
                diagnostics << source << ':';
            }
            diagnostics << position.line << ':' << position.column << ": " << (message == nullptr ? "" : message) << '\n';
        });

        result.valid = tools.Validate(words);
        result.diagnostics = diagnostics.str();
        std::string disassembly;
        if (tools.Disassemble(words, &disassembly, SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_INDENT))
        {
            result.disassembly = std::move(disassembly);
        }
        else
        {
            result.valid = false;
            result.diagnostics = diagnostics.str();
            if (result.diagnostics.empty())
            {
                result.diagnostics = "SPIRV-Tools failed to disassemble the generated module.\n";
            }
        }
        return result;
    }

    SPIRVDisassemblyResult disassembleSPIRVWords(const std::vector<uint32_t> &words)
    {
        SPIRVDisassemblyResult result;
        spvtools::SpirvTools tools(getSPIRVTargetEnvironment());
        std::ostringstream diagnostics;
        tools.SetMessageConsumer([&diagnostics](spv_message_level_t level, const char *source, const spv_position_t &position, const char *message) {
            diagnostics << getSPIRVToolsMessageLevelName(level) << ": ";
            if (source != nullptr && source[0] != '\0')
            {
                diagnostics << source << ':';
            }
            diagnostics << position.line << ':' << position.column << ": " << (message == nullptr ? "" : message) << '\n';
        });

        result.disassembled = tools.Disassemble(words,
                                                &result.disassembly,
                                                SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_INDENT);
        result.diagnostics = diagnostics.str();
        if (!result.disassembled && result.diagnostics.empty())
        {
            result.diagnostics = "SPIRV-Tools failed to disassemble the generated module.\n";
        }
        return result;
    }

    std::string dumpSPIRVWordsAsHexText(const std::vector<uint32_t> &words)
    {
        std::ostringstream stream;
        stream << "word_count " << words.size() << '\n';
        for (size_t index = 0; index < words.size(); ++index)
        {
            if ((index % 8u) == 0u)
            {
                stream << std::setw(6) << std::setfill('0') << std::dec << index << ": ";
            }
            stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << words[index];
            if ((index % 8u) == 7u || index + 1u == words.size())
            {
                stream << '\n';
            }
            else
            {
                stream << ' ';
            }
        }
        return stream.str();
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
