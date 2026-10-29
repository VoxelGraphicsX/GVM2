#pragma once

#include <string>
#include <string_view>

namespace UGLC::CodeGen
{
    /**
     * @brief Small structured writer for generated source fragments.
     *
     * This class gives new backend-facing refactors one place to manage raw
     * text, lines, statements, and emitted buffers without hand-assembling every
     * newline and terminator inside visitor methods.
     */
    class CodeWriter
    {
        std::string mBuffer;

    public:
        /**
         * @brief Appends text without adding formatting.
         */
        void appendRaw(std::string_view text);

        /**
         * @brief Appends one line and terminates it with a newline.
         */
        void appendLine(std::string_view line = {});

        /**
         * @brief Appends an indented statement and terminates it with `;`.
         *
         * Example DSL-to-code emitters use this for fragments such as
         * `layoutEntry[0].binding = 1;` where the caller owns the semantic rule
         * and CodeWriter owns only the spelling and terminator.
         */
        void appendStatement(std::string_view indent, std::string_view statement);

        /**
         * @brief Returns the accumulated text without transferring ownership.
         */
        [[nodiscard]] const std::string &str() const;

        /**
         * @brief Moves the accumulated text out of the writer.
         */
        [[nodiscard]] std::string take();
    };
} // namespace UGLC::CodeGen
