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
                std::cerr << "UGLC warning: duplicate generated file key \"" << key << "\" was overwritten by a later translation unit result.\n";
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

    // Creates the requested output directory tree up front so the driver can
    // fail fast before spending time on translation work that cannot be written.
    inline bool ensureOutputDirectory(const std::filesystem::path &outputDir)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec)
        {
            std::cerr << "Error creating output directory " << outputDir << ": " << ec.message() << '\n';
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
            std::filesystem::create_directories(parentDir, ec);
            if (ec)
            {
                std::cerr << "Error creating parent directory for " << path << ": " << ec.message() << '\n';
                return false;
            }
        }

        std::ofstream outFile(path, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open())
        {
            std::cerr << "Error opening file " << path << " for writing.\n";
            return false;
        }

        outFile.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!outFile.good())
        {
            std::cerr << "Error writing file " << path << ".\n";
            return false;
        }

        outFile.close();
        if (!outFile)
        {
            std::cerr << "Error finalizing file " << path << ".\n";
            return false;
        }

        std::cout << "Successfully wrote result to " << path << '\n';
        return true;
    }
} // namespace UGLC::App
