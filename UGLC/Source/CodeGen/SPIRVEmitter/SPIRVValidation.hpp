#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Stores SPIRV-Tools validation and disassembly output for one generated module. */
    struct SPIRVValidationResult
    {
        bool valid = false;
        std::string diagnostics;
        std::string disassembly;
    };

    /** Stores SPIRV-Tools disassembly output for one generated module without running validation. */
    struct SPIRVDisassemblyResult
    {
        bool disassembled = false;
        std::string diagnostics;
        std::string disassembly;
    };

    /** Stores the optimized SPIR-V words selected for runtime artifact emission. */
    struct SPIRVOptimizationResult
    {
        bool optimized = false;
        std::vector<uint32_t> words;
        std::string diagnostics;
    };

    /** Optimizes direct-writer SPIR-V words after the raw module has already passed writer validation. */
    SPIRVOptimizationResult optimizeSPIRVWordsForRuntime(const std::vector<uint32_t> &words);

    /** Validates SPIR-V words with SPIRV-Tools and returns a human-readable disassembly for debug artifacts. */
    SPIRVValidationResult validateAndDisassembleSPIRVWords(const std::vector<uint32_t> &words);

    /** Disassembles SPIR-V words without validation so optimized runtime artifacts can be inspected directly. */
    SPIRVDisassemblyResult disassembleSPIRVWords(const std::vector<uint32_t> &words);

    /** Formats SPIR-V words as a deterministic hexadecimal text dump suitable for repository test artifacts. */
    std::string dumpSPIRVWordsAsHexText(const std::vector<uint32_t> &words);
} // namespace UGLC::CodeGen::SPIRVEmitter
