#include "GVMShaderReadbackTest.hpp"
#include <charconv>
#include <cstdio>
#include <cstring>

/** Parses sustained-validation duration without environment configuration, then starts GoogleTest. */
int main(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--measure-gpu-time") == 0) { GVM::Tests::shaderReadbackMeasureGpuTime = true; continue; }
        if (std::strcmp(argv[index], "--backend") == 0)
        {
            if (index + 1 >= argc) { std::fputs("Missing backend.\n", stderr); return 2; }
            const char *backend = argv[++index];
            if (std::strcmp(backend, "metal") == 0) { GVM::Tests::shaderReadbackBackend = GVM::RHI::GraphicsBackend::Metal; }
            else if (std::strcmp(backend, "vulkan") == 0) { GVM::Tests::shaderReadbackBackend = GVM::RHI::GraphicsBackend::Vulkan; }
            else { std::fputs("Backend must be metal or vulkan.\n", stderr); return 2; }
            continue;
        }
        if (std::strcmp(argv[index], "--readback-duration-seconds") != 0) { continue; }
        if (index + 1 >= argc) { std::fputs("Missing readback duration.\n", stderr); return 2; }
        const char *value = argv[++index];
        uint32_t seconds = 0;
        const auto parsed = std::from_chars(value, value + std::strlen(value), seconds);
        if (parsed.ec != std::errc{} || *parsed.ptr != '\0' || seconds > 86400u)
        {
            std::fputs("Readback duration must be an integer between 0 and 86400 seconds.\n", stderr);
            return 2;
        }
        GVM::Tests::shaderReadbackDurationSeconds = seconds;
    }
    if (GVM::Tests::shaderReadbackBackend == GVM::RHI::GraphicsBackend::Undefined)
    {
        std::fputs("Shader readback tests require --backend metal or --backend vulkan.\n", stderr);
        return 2;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
