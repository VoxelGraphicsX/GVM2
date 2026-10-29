#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace UGLC::CodeGen
{
    /** One structured shader diagnostic. Carries either a Clang source location (for AST-located errors) or a
     *  DSL text position (file/line/column, copied from the emitter's own UGLIR::SourceLocation); exactly one is
     *  set, and a valid `location` takes precedence when both are present. */
    struct DiagnosticRecord
    {
        clang::SourceLocation location;
        std::string file;
        unsigned line = 0;
        unsigned column = 0;
        clang::DiagnosticsEngine::Level level = clang::DiagnosticsEngine::Error;
        std::string message;
    };

    /** Carries one or more structured diagnostics from a generator to the nearest Clang action boundary, where
     *  each record is reported through clang::DiagnosticsEngine instead of a raw throw string. */
    class DiagnosticError final : public std::runtime_error
    {
    public:
        /** Captures a single Clang-located diagnostic for throw sites that already have a SourceLocation. */
        DiagnosticError(clang::SourceLocation location, clang::DiagnosticsEngine::Level level, std::string message)
            : std::runtime_error(message)
        {
            mRecords.push_back({location, {}, 0u, 0u, level, std::move(message)});
        }

        /** Captures a batch of diagnostics for emitters that accumulate them into a list. */
        explicit DiagnosticError(std::vector<DiagnosticRecord> records)
            : std::runtime_error(FormatWhat(records)), mRecords(std::move(records))
        {
        }

        /** Returns the diagnostics to report at the action boundary. */
        [[nodiscard]] const std::vector<DiagnosticRecord> &getRecords() const { return mRecords; }
        /** Returns the first record's location for single-record callers. */
        [[nodiscard]] clang::SourceLocation getLocation() const { return mRecords.empty() ? clang::SourceLocation() : mRecords.front().location; }
        /** Returns the first record's severity for single-record callers. */
        [[nodiscard]] clang::DiagnosticsEngine::Level getLevel() const { return mRecords.empty() ? clang::DiagnosticsEngine::Error : mRecords.front().level; }
        /** Returns the first record's plain message body for single-record callers. */
        [[nodiscard]] const std::string &getMessage() const { return mRecords.empty() ? EmptyMessage() : mRecords.front().message; }

    private:
        static const std::string &EmptyMessage()
        {
            static const std::string empty;
            return empty;
        }
        std::vector<DiagnosticRecord> mRecords;

        /** Maps a DiagnosticsEngine level to the severity word Clang prints. */
        static const char *SeverityWord(clang::DiagnosticsEngine::Level level)
        {
            switch (level)
            {
            case clang::DiagnosticsEngine::Ignored:
            case clang::DiagnosticsEngine::Note:
            {
                return "note";
            }
            case clang::DiagnosticsEngine::Remark:
            {
                return "remark";
            }
            case clang::DiagnosticsEngine::Warning:
            {
                return "warning";
            }
            case clang::DiagnosticsEngine::Error:
            {
                return "error";
            }
            case clang::DiagnosticsEngine::Fatal:
            {
                return "fatal error";
            }
            }
            return "error";
        }

        /** Formats the runtime_error text so ex.what() callers keep the clang-style location prefix. */
        static std::string FormatWhat(const std::vector<DiagnosticRecord> &records)
        {
            if (records.empty())
            {
                return "UGLC diagnostic";
            }
            const DiagnosticRecord &record = records.front();
            if (!record.file.empty() && record.line != 0u)
            {
                return record.file + ':' + std::to_string(record.line) + ':' + std::to_string(record.column) + ": " + SeverityWord(record.level) + ": " + record.message;
            }
            return record.message;
        }
    };
    /** Builds one DiagnosticRecord from a source location: file/line/column are filled from the presumed
     *  position of the expansion location when resolvable, and left empty otherwise. */
    inline DiagnosticRecord makeDiagnosticRecord(
        const clang::SourceManager &sourceManager,
        clang::SourceLocation location,
        clang::DiagnosticsEngine::Level level,
        std::string message)
    {
        DiagnosticRecord record = {};
        const clang::SourceLocation expansion = sourceManager.getExpansionLoc(location);
        const clang::PresumedLoc presumedLoc = sourceManager.getPresumedLoc(expansion);
        const auto fileEntry = sourceManager.getFileEntryRefForID(sourceManager.getFileID(expansion));
        if (presumedLoc.isValid() && fileEntry)
        {
            record.file = fileEntry->getName().str();
            record.line = presumedLoc.getLine();
            record.column = presumedLoc.getColumn();
        }
        record.level = level;
        record.message = std::move(message);
        return record;
    }

    /** Reports every diagnostic record through DiagnosticsEngine: AST-located records report at their own
     *  location, DSL text positions are translated through the source manager, and unlocatable records
     *  report without a position so no diagnostic is silently dropped. */
    inline void reportDiagnosticRecords(clang::ASTContext &context, const std::vector<DiagnosticRecord> &records)
    {
        auto &diags = context.getDiagnostics();
        auto &sourceManager = context.getSourceManager();
        for (const DiagnosticRecord &record : records)
        {
            clang::SourceLocation location = record.location;
            if (location.isInvalid() && !record.file.empty() && record.line != 0u)
            {
                auto fileEntry = sourceManager.getFileManager().getOptionalFileRef(record.file);
                if (fileEntry)
                {
                    location = sourceManager.translateFileLineCol(*fileEntry, record.line, record.column == 0u ? 1u : record.column);
                }
            }
            std::string message = record.message;
            if (location.isInvalid() && !record.file.empty())
            {
                message = record.file + ':' + std::to_string(record.line) + ':' + std::to_string(record.column) + ": " + record.message;
            }
            diags.Report(location, diags.getCustomDiagID(record.level, "%0")) << message;
        }
    }
} // namespace UGLC::CodeGen
