#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/SourceManager.h>

#include <cctype>
#include <string>
#include <string_view>

namespace UGLC::CodeGen
{
    inline std::string formatUnlocatedDiagnostic(std::string_view message, std::string_view severity = "error")
    {
        return "UGLC: " + std::string(severity) + ": " + std::string(message);
    }

    inline bool isFormattedClangStyleDiagnostic(std::string_view message)
    {
        const size_t firstLineEnd = message.find('\n');
        const std::string_view firstLine = message.substr(0, firstLineEnd);
        if (firstLine.rfind("UGLC: error:", 0) == 0)
        {
            return true;
        }

        const size_t firstColon = firstLine.find(':');
        if (firstColon == std::string_view::npos)
        {
            return false;
        }
        const size_t secondColon = firstLine.find(':', firstColon + 1);
        if (secondColon == std::string_view::npos || secondColon == firstColon + 1)
        {
            return false;
        }
        for (size_t i = firstColon + 1; i < secondColon; ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(firstLine[i])))
            {
                return false;
            }
        }

        const size_t thirdColon = firstLine.find(':', secondColon + 1);
        if (thirdColon == std::string_view::npos || thirdColon == secondColon + 1)
        {
            return false;
        }
        for (size_t i = secondColon + 1; i < thirdColon; ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(firstLine[i])))
            {
                return false;
            }
        }

        return firstLine.find(" error: ", thirdColon) != std::string_view::npos;
    }

    inline clang::SourceLocation normalizeDiagnosticLocation(const clang::SourceManager &sourceManager, clang::SourceLocation location)
    {
        if (location.isInvalid())
        {
            return location;
        }
        return sourceManager.getExpansionLoc(location);
    }

    inline std::string formatClangStyleDiagnostic(std::string_view filePath,
                                                  unsigned line,
                                                  unsigned column,
                                                  std::string_view message,
                                                  std::string_view severity = "error")
    {
        return std::string(filePath)
               + ":" + std::to_string(line)
               + ":" + std::to_string(column)
               + ": " + std::string(severity)
               + ": " + std::string(message);
    }

    inline std::string formatClangStyleDiagnostic(const clang::SourceManager &sourceManager,
                                                  clang::SourceLocation location,
                                                  std::string_view message,
                                                  std::string_view severity = "error")
    {
        location = normalizeDiagnosticLocation(sourceManager, location);
        if (location.isInvalid())
        {
            return formatUnlocatedDiagnostic(message, severity);
        }

        const clang::PresumedLoc presumedLoc = sourceManager.getPresumedLoc(location);
        if (presumedLoc.isInvalid())
        {
            return formatUnlocatedDiagnostic(message, severity);
        }

        return formatClangStyleDiagnostic(presumedLoc.getFilename(),
                                          presumedLoc.getLine(),
                                          presumedLoc.getColumn(),
                                          message,
                                          severity);
    }

    inline std::string formatClangStyleDiagnostic(const clang::ASTContext &context,
                                                  const clang::Stmt *stmt,
                                                  std::string_view message,
                                                  std::string_view severity = "error")
    {
        if (stmt == nullptr)
        {
            return formatUnlocatedDiagnostic(message, severity);
        }
        return formatClangStyleDiagnostic(context.getSourceManager(), stmt->getBeginLoc(), message, severity);
    }

    inline std::string formatClangStyleDiagnostic(const clang::Decl *decl,
                                                  std::string_view message,
                                                  std::string_view severity = "error")
    {
        if (decl == nullptr)
        {
            return formatUnlocatedDiagnostic(message, severity);
        }
        return formatClangStyleDiagnostic(decl->getASTContext().getSourceManager(), decl->getLocation(), message, severity);
    }
} // namespace UGLC::CodeGen
