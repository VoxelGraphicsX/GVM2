#pragma once

#include <EASTL/string.h>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace GVM::Tests
{
    /** Configures render-test timing and optional image output from explicit CLI arguments. */
    struct RenderTestOptions
    {
        std::filesystem::path artifactDirectory;
        eastl::string backendName = "default";
        bool writeImages = true;
        int simpleFrames = 120;
        int complexFrames = 180;
        int frameDelayMs = 16;
        int warmupMs = 350;
        int casePauseMs = 250;
    };

    inline RenderTestOptions renderTestOptions;

    /** Reads known render options before GoogleTest starts; unrelated test/backend arguments remain intact. */
    inline void configureRenderTestOptions(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            const char *name = argv[index];
            int *number = nullptr;
            if (std::strcmp(name, "--render-simple-frames") == 0) number = &renderTestOptions.simpleFrames;
            else if (std::strcmp(name, "--render-complex-frames") == 0) number = &renderTestOptions.complexFrames;
            else if (std::strcmp(name, "--render-frame-delay-ms") == 0) number = &renderTestOptions.frameDelayMs;
            else if (std::strcmp(name, "--render-warmup-ms") == 0) number = &renderTestOptions.warmupMs;
            else if (std::strcmp(name, "--render-case-pause-ms") == 0) number = &renderTestOptions.casePauseMs;
            const bool artifact = std::strcmp(name, "--artifact-dir") == 0;
            const bool images = std::strcmp(name, "--write-images") == 0;
            const bool backend = std::strcmp(name, "--gvm-backend") == 0;
            if (number == nullptr && !artifact && !images && !backend) continue;
            if (++index == argc) throw std::runtime_error("Missing render test option value.");
            const char *value = argv[index];
            if (number != nullptr)
            {
                const auto parsed = std::from_chars(value, value + std::strlen(value), *number);
                if (parsed.ec != std::errc{} || *parsed.ptr != '\0' || *number < 1 || *number > 60000)
                    throw std::runtime_error("Render timing/frame options require an integer between 1 and 60000.");
            }
            else if (artifact) renderTestOptions.artifactDirectory = value;
            else if (backend) renderTestOptions.backendName = value;
            else
            {
                if (std::strcmp(value, "true") != 0 && std::strcmp(value, "false") != 0)
                    throw std::runtime_error("--write-images must be true or false.");
                renderTestOptions.writeImages = std::strcmp(value, "true") == 0;
            }
        }
    }
}
