#include "SPIRVInstructionBuilder.hpp"

#include <algorithm>
#include <utility>

namespace UGLC::CodeGen::SPIRVEmitter
{
    void SPIRVInstructionBuilder::appendInstruction(spv::Op opcode, const std::vector<uint32_t> &operands)
    {
        const uint32_t wordCount = static_cast<uint32_t>(operands.size() + 1u);
        mWords.push_back((wordCount << spv::WordCountShift) | static_cast<uint32_t>(opcode));
        mWords.insert(mWords.end(), operands.begin(), operands.end());
    }

    void SPIRVInstructionBuilder::appendInstruction(spv::Op opcode, std::initializer_list<uint32_t> operands)
    {
        appendInstruction(opcode, std::vector<uint32_t>(operands));
    }

    void SPIRVInstructionBuilder::appendInstructionWithString(spv::Op opcode, const std::vector<uint32_t> &prefixOperands, const std::string &literal, const std::vector<uint32_t> &suffixOperands)
    {
        std::vector<uint32_t> operands;
        operands.reserve(prefixOperands.size() + suffixOperands.size() + ((literal.size() + 4u) / 4u));
        operands.insert(operands.end(), prefixOperands.begin(), prefixOperands.end());
        std::vector<uint32_t> encodedLiteral = encodeStringLiteral(literal);
        operands.insert(operands.end(), encodedLiteral.begin(), encodedLiteral.end());
        operands.insert(operands.end(), suffixOperands.begin(), suffixOperands.end());
        appendInstruction(opcode, operands);
    }

    void SPIRVInstructionBuilder::appendBuilder(const SPIRVInstructionBuilder &other)
    {
        const std::vector<uint32_t> &otherWords = other.words();
        mWords.insert(mWords.end(), otherWords.begin(), otherWords.end());
    }

    const std::vector<uint32_t> &SPIRVInstructionBuilder::words() const
    {
        return mWords;
    }

    std::vector<uint32_t> SPIRVInstructionBuilder::takeWords()
    {
        return std::move(mWords);
    }

    std::vector<uint32_t> SPIRVInstructionBuilder::encodeStringLiteral(const std::string &literal)
    {
        std::vector<uint32_t> words((literal.size() + 4u) / 4u, 0u);
        for (size_t index = 0; index < literal.size(); ++index)
        {
            const size_t wordIndex = index / 4u;
            const size_t byteShift = (index % 4u) * 8u;
            words[wordIndex] |= static_cast<uint32_t>(static_cast<unsigned char>(literal[index])) << byteShift;
        }
        return words;
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
