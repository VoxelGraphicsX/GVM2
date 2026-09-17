#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <spirv/unified1/spirv.hpp>

namespace UGLC::CodeGen::SPIRVEmitter
{
    /** Builds raw SPIR-V instruction words while keeping opcode and string encoding centralized. */
    class SPIRVInstructionBuilder
    {
    public:
        /** Appends one instruction whose operands are already encoded as SPIR-V words. */
        void appendInstruction(spv::Op opcode, const std::vector<uint32_t> &operands);

        /** Appends one instruction from a small initializer-list operand set. */
        void appendInstruction(spv::Op opcode, std::initializer_list<uint32_t> operands);

        /** Appends one instruction that contains one literal string operand followed by optional word operands. */
        void appendInstructionWithString(spv::Op opcode, const std::vector<uint32_t> &prefixOperands, const std::string &literal, const std::vector<uint32_t> &suffixOperands = {});

        /** Appends every instruction from another builder without changing either builder's state. */
        void appendBuilder(const SPIRVInstructionBuilder &other);

        /** Returns the encoded words accumulated by this builder. */
        const std::vector<uint32_t> &words() const;

        /** Moves the encoded words out of this builder. */
        std::vector<uint32_t> takeWords();

        /** Encodes a UTF-8 literal string as null-terminated SPIR-V words. */
        static std::vector<uint32_t> encodeStringLiteral(const std::string &literal);

    private:
        std::vector<uint32_t> mWords;
    };
} // namespace UGLC::CodeGen::SPIRVEmitter
