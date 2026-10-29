#include <CompilerOutputWriter.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace
{
    struct HarnessConfig
    {
        std::string UGLCExecutable;
        std::string UGLHeadersDir;
    };

    int fail(const std::string &message)
    {
        std::cerr << message << '\n';
        return 1;
    }

    std::optional<HarnessConfig> parseHarnessConfig(int argc, char **argv, std::string &error)
    {
        HarnessConfig config;

        for (int index = 1; index < argc; ++index)
        {
            const std::string key(argv[index]);
            if (key == "--uglc" || key == "--ugl-headers")
            {
                if (index + 1 >= argc)
                {
                    error = key + " requires a value.";
                    return std::nullopt;
                }

                const std::string value(argv[++index]);
                if (value.empty())
                {
                    error = key + " requires a non-empty value.";
                    return std::nullopt;
                }

                if (key == "--uglc")
                {
                    config.UGLCExecutable = value;
                }
                else
                {
                    config.UGLHeadersDir = value;
                }
                continue;
            }

            error = "Unknown harness argument: " + key;
            return std::nullopt;
        }

        if (config.UGLCExecutable.empty())
        {
            error = "Missing required harness argument --uglc from UGLCExecutable.";
            return std::nullopt;
        }
        if (config.UGLHeadersDir.empty())
        {
            error = "Missing required harness argument --ugl-headers from UGLHeadersDir.";
            return std::nullopt;
        }
        return config;
    }

    std::string shellQuote(const std::string &value)
    {
        std::string quoted = "'";
        for (const char ch : value)
        {
            if (ch == '\'')
            {
                quoted += "'\\''";
            }
            else
            {
                quoted += ch;
            }
        }
        quoted += "'";
        return quoted;
    }
}

int main(int argc, char **argv)
{
    namespace fs = std::filesystem;

    std::string configError;
    const auto config = parseHarnessConfig(argc, argv, configError);
    if (!config)
    {
        return fail(configError);
    }

    const fs::path tempRoot = fs::temp_directory_path() / "uglc_app_support_regressions";
    std::error_code cleanupEc;
    fs::remove_all(tempRoot, cleanupEc);
    std::error_code tempRootEc;
    fs::create_directories(tempRoot, tempRootEc);
    if (tempRootEc)
    {
        return fail("failed to prepare temporary regression workspace: " + tempRootEc.message());
    }

    UGLC::App::SharedResults results;
    std::ostringstream duplicateWarning;
    std::streambuf *originalCerrBuffer = std::cerr.rdbuf(duplicateWarning.rdbuf());
    const bool firstStoreOverwrote = results.store("duplicate-key.hpp", std::string("first"));
    const bool secondStoreOverwrote = results.store("duplicate-key.hpp", std::string("second"));
    std::cerr.rdbuf(originalCerrBuffer);

    if (firstStoreOverwrote)
    {
        return fail("first SharedResults::store() unexpectedly reported an overwrite");
    }
    if (!secondStoreOverwrote)
    {
        return fail("second SharedResults::store() did not report an overwrite");
    }
    const auto stored = results.Map.find("duplicate-key.hpp");
    if (stored == results.Map.end() || stored->second != "second")
    {
        return fail("SharedResults did not keep the most recent generated content");
    }
    if (duplicateWarning.str().find("duplicate generated file key") == std::string::npos)
    {
        return fail("SharedResults overwrite warning was not emitted");
    }
    std::cout << "shared_results_overwrite_ok\n";

    const fs::path writableDir = tempRoot / "output" / "nested";
    if (!UGLC::App::ensureOutputDirectory(writableDir))
    {
        return fail("ensureOutputDirectory() unexpectedly failed for a writable temp path");
    }

    const fs::path writableFile = writableDir / "result.txt";
    if (!UGLC::App::writeResult(writableFile, "ok"))
    {
        return fail("writeResult() unexpectedly failed for a writable temp file");
    }

    std::ifstream input(writableFile, std::ios::binary);
    std::string writtenContent((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (writtenContent != "ok")
    {
        return fail("writeResult() did not persist the expected file content");
    }
    std::cout << "write_result_success_ok\n";

    const fs::path blockedParentFile = tempRoot / "blocked-parent-file";
    if (!UGLC::App::writeResult(blockedParentFile, "blocked-parent"))
    {
        return fail("failed to create the blocked parent file for writeResult() regression coverage");
    }
    if (UGLC::App::writeResult(blockedParentFile / "blocked.txt", "blocked"))
    {
        return fail("writeResult() unexpectedly succeeded for an invalid output path");
    }
    std::cout << "write_result_failure_ok\n";

    const fs::path sharedHeaderRoot = config->UGLHeadersDir;
    if (!fs::exists(sharedHeaderRoot) || !fs::is_directory(sharedHeaderRoot))
    {
        return fail("The configured UGLHeadersDir does not point to an existing directory");
    }
    const fs::path uglcExecutable = config->UGLCExecutable;
    if (!fs::exists(uglcExecutable) || !fs::is_regular_file(uglcExecutable))
    {
        return fail("The configured UGLCExecutable does not point to an existing file");
    }

    const fs::path blockedOutputParent = tempRoot / "blocked-output-parent";
    if (!UGLC::App::writeResult(blockedOutputParent, "blocked-output-parent"))
    {
        return fail("failed to create the blocked parent file for the UGLC output-directory regression coverage");
    }

    const fs::path repoRoot = fs::current_path();
    const fs::path failureLogPath = tempRoot / "uglc_output_failure.log";
    const fs::path sourcePath = repoRoot / "tests" / "uglc" / "fixtures" / "compute-basic" / "ComputeBasic.hpp";
    const fs::path fixtureIncludePath = repoRoot / "tests" / "uglc" / "fixtures" / "compute-basic";
    const fs::path invalidOutputDir = blockedOutputParent / "uglc_s2_output_failure";
    const std::string command =
        shellQuote(uglcExecutable.string())
        + " -s " + shellQuote(sourcePath.string())
        + " -I " + shellQuote(fixtureIncludePath.string())
        + " -I " + shellQuote(sharedHeaderRoot.string())
        + " -o " + shellQuote(invalidOutputDir.string())
        + " > " + shellQuote(failureLogPath.string())
        + " 2>&1";

    if (std::system(command.c_str()) == 0)
    {
        return fail("UGLC unexpectedly returned success for an invalid output directory");
    }

    std::ifstream logInput(failureLogPath, std::ios::binary);
    std::string logContent((std::istreambuf_iterator<char>(logInput)), std::istreambuf_iterator<char>());
    const bool creationFailed = logContent.find("Error creating output directory") != std::string::npos;
    const bool invalidationFailed = logContent.find("Failed to invalidate shader artifact") != std::string::npos;
    if ((!creationFailed && !invalidationFailed) || logContent.find(invalidOutputDir.string()) == std::string::npos)
    {
        return fail("UGLC did not surface the output-directory failure diagnostic");
    }
    std::cout << "uglc_output_failure_exit_ok\n";

    std::error_code finalCleanupEc;
    fs::remove_all(tempRoot, finalCleanupEc);
    return 0;
}
