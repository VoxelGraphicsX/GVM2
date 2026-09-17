#pragma once // Use #pragma once for header guards, common in C++.

#include "Diagnostics.hpp"
#include "UGLC.Constants.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// Define a namespace to match the C# version.
namespace UGLC::CodeGen
{


    /**
     * @brief A class to combine multiple source files into a single file by processing #include directives.
     * This is a C++20 translation of the original C# SingleFileGenerator.
     */
    class SingleHeaderGenerator
    {
    private:
        // Member variables use the mPascalCase naming convention as requested.
        std::filesystem::path mSourceFolderPath;
        std::filesystem::path mSourceFilePath;
        std::string mResult;

        std::vector<std::string> mExternalIncludeStmts;
        std::vector<std::filesystem::path> mIncludePaths;
        std::vector<std::filesystem::path> mDslSourceRoots;
        // std::unordered_map is the equivalent of Dictionary for fast lookups.
        std::unordered_map<std::string, std::string> mCacheddFiles;
        std::unordered_set<std::string> mLoadedFiles;
        bool mIsInRawString = false;
        bool mIsInExportFile = false;
        bool mIsInBlockComment = false;
        std::string mExportFileContent;
        std::vector<std::filesystem::path> mIncludeStack;

        /**
         * @brief Normalizes a path to a preferred, absolute format.
         * @param path The input path.
         * @return An absolute, canonical path string.
         */
        static std::filesystem::path getPerfectPath(const std::filesystem::path &path)
        {
            // std::filesystem::weakly_canonical handles non-existent paths gracefully.
            return std::filesystem::weakly_canonical(path);
        }

        /**
         * @brief Produces a canonical string key for hash-based path lookups.
         * @param path The input path.
         * @return A stable canonical key suitable for unordered containers.
         */
        static std::string getPerfectPathKey(const std::filesystem::path &path)
        {
            return getPerfectPath(path).string();
        }

        static bool isPathInsideRoot(const std::filesystem::path &candidatePath, const std::filesystem::path &rootPath)
        {
            const auto canonicalCandidate = getPerfectPath(candidatePath).lexically_normal();
            const auto canonicalRoot = getPerfectPath(rootPath).lexically_normal();
            auto [candidateIt, rootIt] = std::mismatch(canonicalCandidate.begin(),
                                                       canonicalCandidate.end(),
                                                       canonicalRoot.begin(),
                                                       canonicalRoot.end());
            return rootIt == canonicalRoot.end();
        }

        [[nodiscard]] bool shouldInlineResolvedInclude(const std::filesystem::path &resolvedPath) const
        {
            return std::any_of(mDslSourceRoots.begin(),
                               mDslSourceRoots.end(),
                               [&](const std::filesystem::path &rootPath) { return isPathInsideRoot(resolvedPath, rootPath); });
        }

        void appendPreservedInclude(std::stringstream &fileContentStream, const std::string &includeLine)
        {
            if (std::find(mExternalIncludeStmts.begin(), mExternalIncludeStmts.end(), includeLine) == mExternalIncludeStmts.end())
            {
                mExternalIncludeStmts.push_back(includeLine);
                fileContentStream << includeLine << "\n";
            }
            else
            {
                fileContentStream << "\n";
            }
        }

        [[nodiscard]] std::string buildIncludeChainMessage(const std::filesystem::path &includingFile,
                                                           const std::filesystem::path &requestedInclude,
                                                           unsigned lineNumber) const
        {
            std::vector<std::string> chain;
            chain.reserve(mIncludeStack.size() + 1);
            for (const auto &entry : mIncludeStack)
            {
                chain.emplace_back(getPerfectPath(entry).string());
            }
            const std::string includingFilePath = getPerfectPath(includingFile).string();
            if (chain.empty() || chain.back() != includingFilePath)
            {
                chain.emplace_back(includingFilePath);
            }
            chain.emplace_back(requestedInclude.string());

            std::stringstream chainStream;
            for (size_t i = 0; i < chain.size(); ++i)
            {
                if (i > 0)
                {
                    chainStream << " -> ";
                }
                chainStream << chain[i];
            }

            return "\n"
                   + formatClangStyleDiagnostic(includingFilePath,
                                                lineNumber,
                                                1,
                                                "Include chain: " + chainStream.str(),
                                                "note");
        }

        /**
         * @brief Resolves paths containing '..' segments relative to a base path.
         * This function mimics the original C# implementation.
         * @param basePath The path of the file containing the include.
         * @param includePath The path from the include directive (e.g., "../common/utils.h").
         * @return A resolved, lexically normal path.
         */
        static std::filesystem::path resolveDotDotPath(const std::filesystem::path &basePath, const std::filesystem::path &includePath)
        {
            auto combinedPath = basePath.parent_path() / includePath;
            // lexically_normal cleans up ".." and "." segments.
            return combinedPath.lexically_normal();
        }

        std::string stripCommentsForDirectiveScan(const std::string &line)
        {
            std::string sanitizedLine;
            sanitizedLine.reserve(line.size());

            for (size_t index = 0; index < line.size(); ++index)
            {
                const char currentChar = line[index];
                const char nextChar = index + 1 < line.size() ? line[index + 1] : '\0';

                if (mIsInBlockComment)
                {
                    if (currentChar == '*' && nextChar == '/')
                    {
                        mIsInBlockComment = false;
                        ++index;
                    }
                    continue;
                }

                if (currentChar == '/' && nextChar == '/')
                {
                    break;
                }
                if (currentChar == '/' && nextChar == '*')
                {
                    mIsInBlockComment = true;
                    ++index;
                    continue;
                }

                sanitizedLine.push_back(currentChar);
            }

            return sanitizedLine;
        }

        static bool checkFileLoaded(const std::unordered_map<std::string, std::string> &loadedFiles, const std::filesystem::path &testPath)
        {
            return loadedFiles.contains(getPerfectPathKey(testPath));
        }

        static std::string getFileLoaded(const std::unordered_map<std::string, std::string> &loadedFiles, const std::filesystem::path &testPath)
        {
            const auto it = loadedFiles.find(getPerfectPathKey(testPath));
            return it == loadedFiles.end() ? "" : it->second;
        }

        /**
         * @brief Reads content from memory cache or falls back to reading from disk.
         * @param path The full path to the file.
         * @return The content of the file as a string.
         */
        std::string reuseFromMemOrReadFromDisk(const std::filesystem::path &path)
        {
            /* if (checkFileLoaded(this->mCacheddFiles, path))
            {
                return getFileLoaded(mCacheddFiles, path);
            }
            return "file not found: " + path.string(); // readFileByLines(fullPathStr); */
            return processFileByLines(path);
        }

        /**
         * @brief Checks if an included file exists in any of the specified include directories.
         * @param path The relative path from the #include directive.
         * @param thisFilePath The path of the file being currently processed.
         * @param perfectIncludeFilePath [out] The full, canonical path of the found file.
         * @return True if the file was found, false otherwise.
         */
        bool checkIncludeFileExists(const std::filesystem::path &path, const std::filesystem::path &thisFilePath, std::filesystem::path &perfectIncludeFilePath)
        {
            bool exists = false;
            std::filesystem::path existingPath;

            // Create a temporary list of paths to check, including the current file's directory.
            auto tempIncludePaths = mIncludePaths;
            tempIncludePaths.push_back(thisFilePath.parent_path());

            for (const auto &includePath : tempIncludePaths)
            {
                auto tempPath = includePath / path;
                if (std::filesystem::exists(tempPath))
                {
                    const auto resolvedPath = getPerfectPath(tempPath);
                    if (exists && existingPath != resolvedPath)
                    {
                        // Mimics the original C# exception for ambiguous includes.
                        throw std::runtime_error("Multiple include paths found for: " + path.string());
                    }
                    exists = true;
                    perfectIncludeFilePath = resolvedPath;
                    existingPath = perfectIncludeFilePath;
                }
            }
            return exists;
        }

        std::stringstream readFileByLines(const std::filesystem::path &path)
        {
            std::stringstream sourceStream;
            if (auto cachedContent = getFileLoaded(mCacheddFiles, path); cachedContent.empty() == false)
            {
                sourceStream.str(cachedContent);
            }
            else
            {
                std::ifstream fileStream(path);
                if (!fileStream.is_open())
                {
                    std::cerr << "Unable to read file: " << path << std::endl;
                    throw std::runtime_error("Unable to read file: " + path.string());
                }

                std::stringstream buffer;
                buffer << fileStream.rdbuf();
                std::string content = buffer.str();

                sourceStream.str(content);
            }
            return sourceStream;
        }

        /**
         * @brief Recursively reads a file, processes its #include directives, and returns its content.
         * @param path The path of the file to read.
         * @return The processed content of the file as a string.
         */
        std::string processFileByLines(const std::filesystem::path &path)
        {
            struct IncludeStackScope
            {
                std::vector<std::filesystem::path> &stack;

                explicit IncludeStackScope(std::vector<std::filesystem::path> &includeStack, const std::filesystem::path &currentPath)
                    : stack(includeStack)
                {
                    stack.push_back(currentPath);
                }

                ~IncludeStackScope()
                {
                    stack.pop_back();
                }
            };

            IncludeStackScope includeStackScope(mIncludeStack, path);
            const std::string pathKey = getPerfectPathKey(path);
            if (mLoadedFiles.contains(pathKey))
            {
                return {};
            }
            mLoadedFiles.emplace(pathKey);

            std::stringstream fileStream = readFileByLines(path);

            std::stringstream fileContentStream;
            std::string line;
            std::regex includePattern(R"(#include\s*(["<])([^"">]+)[">])");
            size_t lineNumber = 0;

            while (std::getline(fileStream, line))
            {
                ++lineNumber;

                std::smatch match;
                if (line.find("R\"(") != line.npos)
                {
                    mIsInRawString = true;
                }
                // Generated shader artifact wrappers may close a raw string with
                // `)",` before passing more arguments, so the old `)";` check
                // was too narrow and could leave the merger stuck in raw-string
                // mode for the rest of the file.
                if (mIsInRawString && line.find(")\"") != line.npos)
                {
                    mIsInRawString = false;
                }

                if (line.find(mUGLExportFileMacro) != line.npos)
                {
                    mIsInExportFile = !mIsInExportFile;
                }
                else if (mIsInExportFile)
                {
                    mExportFileContent += line + "\n";
                }

                const std::string directiveScanLine = stripCommentsForDirectiveScan(line);
                if (std::regex_search(directiveScanLine, match, includePattern) && mIsInRawString == false)
                {
                    const bool isQuotedInclude = match[1].str() == "\"";
                    std::filesystem::path includeFilePath = match[2].str();

                    if (!isQuotedInclude)
                    {
                        // System/third-party headers are preserved as-is. Only
                        // explicitly configured DSL source roots are eligible
                        // for recursive flattening.
                        appendPreservedInclude(fileContentStream, line);
                        continue;
                    }

                    std::filesystem::path perfectIncludePath;
                    if (!checkIncludeFileExists(includeFilePath, path, perfectIncludePath))
                    {
                        throw std::runtime_error(formatClangStyleDiagnostic(path.string(),
                                                                            static_cast<unsigned>(lineNumber),
                                                                            1,
                                                                            "Unable to resolve local include \"" + includeFilePath.string()
                                                                                + "\". Add the containing directory to -I or fix the include path.")
                                                 + buildIncludeChainMessage(path, includeFilePath, static_cast<unsigned>(lineNumber)));
                    }

                    if (!shouldInlineResolvedInclude(perfectIncludePath))
                    {
                        appendPreservedInclude(fileContentStream, line);
                        continue;
                    }

                    std::string includeContent;
                    // Check if the file has already been processed to avoid infinite loops.
                    if (mLoadedFiles.contains(getPerfectPathKey(perfectIncludePath)) == false)
                    {
                        includeContent = reuseFromMemOrReadFromDisk(perfectIncludePath);
                    }

                    fileContentStream << includeContent << "\n";
                }
                else
                {

                    fileContentStream << line << "\n";
                }
            }

            std::string result = fileContentStream.str();
            return result;
        }

    public:
        /**
         * @brief Initializes the generator with the source file and include paths.
         * @param sourcePath The path to the main source file to process.
         * @param includePaths A list of directories to search for included files.
         */
        void init(const std::filesystem::path &sourcePath,
                  const std::vector<std::filesystem::path> &includePaths,
                  const std::vector<std::filesystem::path> &dslSourceRoots = {})
        {
            mSourceFilePath = getPerfectPath(sourcePath);

            if (!mSourceFilePath.has_parent_path())
            {
                throw std::invalid_argument("The source path must be a file in a directory.");
            }
            mSourceFolderPath = mSourceFilePath.parent_path();

            mIncludePaths.clear();
            for (const auto &p : includePaths)
            {
                mIncludePaths.push_back(getPerfectPath(p));
            }
            // Always include the source file's own directory as an include path.
            mIncludePaths.push_back(mSourceFolderPath);

            mDslSourceRoots.clear();
            mDslSourceRoots.push_back(mSourceFolderPath);
            for (const auto &rootPath : dslSourceRoots)
            {
                mDslSourceRoots.push_back(getPerfectPath(rootPath));
            }
            std::sort(mDslSourceRoots.begin(), mDslSourceRoots.end());
            mDslSourceRoots.erase(std::unique(mDslSourceRoots.begin(), mDslSourceRoots.end()), mDslSourceRoots.end());
        }

        /**
         * @brief Starts the file generation process.
         */
        void create(const std::unordered_map<std::string, std::string> &loadedFiles)
        {
            // Reset state for a new creation
            mResult.clear();
            mExternalIncludeStmts.clear();
            mCacheddFiles.clear();
            mLoadedFiles.clear();
            mIsInRawString = false;
            mIsInExportFile = false;
            mIsInBlockComment = false;
            mExportFileContent.clear();
            mIncludeStack.clear();
            // mLoadedFiles = loadedFiles;
            for (const auto &[filepath, content] : loadedFiles)
            {
                mCacheddFiles.emplace(getPerfectPathKey(filepath), content);
            }
            mResult = processFileByLines(mSourceFilePath);
        }

        /**
         * @brief Writes the combined result to a specified output file.
         * @param outputPath The path to write the output file to.
         */
        void writeResult(const std::filesystem::path &outputPath)
        {
            std::ofstream outFile(outputPath);
            if (!outFile)
            {
                std::cerr << "Unable to open file for writing: " << outputPath << std::endl;
                return;
            }
            outFile << mResult;
        }

        /**
         * @brief Gets the generated single-file content as a string.
         * @return The result string.
         */
        [[nodiscard]] std::string getResult() const
        {
            return mResult;
        }
        [[nodiscard]] std::string getExportResult() const
        {
            return mExportFileContent;
        }

        [[nodiscard]] std::vector<std::string> queryLoadedFileNames() const
        {
            std::vector<std::string> result;
            for (const auto &filename : mLoadedFiles)
            {
                result.emplace_back(filename);
            }
            return result;
        }
    };
} // namespace UGLC::CodeGen
