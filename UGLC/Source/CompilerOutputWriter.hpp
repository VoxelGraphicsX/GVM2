#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace UGLC::App
{
    struct SharedResults
    {
        std::unordered_map<std::string, std::string> Map;
        std::mutex Mtx;

        // Returns true when a later translation unit overwrites an existing key.
        // We intentionally keep the most recent result because silently preserving
        // stale content is worse than surfacing a duplicate-write warning.
        bool store(const std::string &key, std::string &&value)
        {
            std::lock_guard<std::mutex> lock(Mtx);
            const bool overwroteExisting = Map.contains(key);
            if (overwroteExisting)
            {
                std::cerr << "uglc: warning: duplicate generated file key \"" << key << "\" was overwritten by a later translation unit result.\n";
            }
            Map.insert_or_assign(key, std::move(value));
            return overwroteExisting;
        }

        /** Stores a result only when the key is new, leaving existing content untouched on duplicates. */
        bool storeUnique(const std::string &key, std::string &&value)
        {
            std::lock_guard<std::mutex> lock(Mtx);
            if (Map.contains(key))
            {
                return false;
            }
            Map.emplace(key, std::move(value));
            return true;
        }
    };

    /** Converts long Windows paths into extended-length Win32 paths so file and directory operations work past MAX_PATH. */
    inline std::filesystem::path toExtendedFilesystemPath(const std::filesystem::path &path)
    {
#if defined(_WIN32)
        if (path.empty())
        {
            return path;
        }
        std::error_code absoluteError;
        std::filesystem::path normalized = std::filesystem::absolute(path, absoluteError);
        if (absoluteError)
        {
            normalized = path;
        }
        normalized = normalized.lexically_normal().make_preferred();
        const std::wstring nativePath = normalized.wstring();
        if (nativePath.size() >= 240 && nativePath.rfind(L"\\\\?\\", 0) != 0)
        {
            if (nativePath.rfind(L"\\\\", 0) == 0)
            {
                return std::filesystem::path(L"\\\\?\\UNC\\" + nativePath.substr(2));
            }
            return std::filesystem::path(L"\\\\?\\" + nativePath);
        }
#endif
        return path;
    }

    // Creates the requested output directory tree up front so the driver can
    // fail fast before spending time on translation work that cannot be written.
    inline bool ensureOutputDirectory(const std::filesystem::path &outputDir)
    {
        std::error_code ec;
        std::filesystem::create_directories(toExtendedFilesystemPath(outputDir), ec);
        if (ec)
        {
            std::cerr << "uglc: error: creating output directory " << outputDir << ": " << ec.message() << '\n';
            return false;
        }
        return true;
    }

    // Returns a status instead of throwing so `main()` can aggregate all writes
    // and translate any failure into a clear non-zero process exit code.
    inline bool writeResult(const std::filesystem::path &path, const std::string &content)
    {
        const std::filesystem::path parentDir = path.parent_path();
        if (!parentDir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(toExtendedFilesystemPath(parentDir), ec);
            if (ec)
            {
                std::cerr << "uglc: error: creating parent directory for " << path << ": " << ec.message() << '\n';
                return false;
            }
        }

        const std::filesystem::path openPath = toExtendedFilesystemPath(path);
        std::ofstream outFile(openPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open())
        {
            std::cerr << "uglc: error: opening file " << path << " for writing.\n";
            return false;
        }

        outFile.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!outFile.good())
        {
            std::cerr << "uglc: error: writing file " << path << ".\n";
            return false;
        }

        outFile.close();
        if (!outFile)
        {
            std::cerr << "uglc: error: finalizing file " << path << ".\n";
            return false;
        }

        std::cout << "Successfully wrote result to " << path << '\n';
        return true;
    }
} // namespace UGLC::App
