#include "CodeWriter.hpp"

#include <utility>

namespace UGLC::CodeGen
{
    void CodeWriter::appendRaw(std::string_view text)
    {
        mBuffer.append(text.data(), text.size());
    }

    void CodeWriter::appendLine(std::string_view line)
    {
        appendRaw(line);
        mBuffer.push_back('\n');
    }

    void CodeWriter::appendStatement(std::string_view indent, std::string_view statement)
    {
        appendRaw(indent);
        appendRaw(statement);
        mBuffer += ";\n";
    }

    const std::string &CodeWriter::str() const
    {
        return mBuffer;
    }

    std::string CodeWriter::take()
    {
        return std::move(mBuffer);
    }
} // namespace UGLC::CodeGen
