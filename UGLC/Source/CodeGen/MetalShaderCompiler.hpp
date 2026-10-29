#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    /** Configures offline Metal compilation independently of the host or runtime backend. */
    struct MetalCompilerOptions
    {
        bool enabled = false;
        std::string executable;
        std::string target;
    };

    /** Rejects unavailable tools or missing target information when binary emission is requested. */
    void validateMetalCompilerOptions(const MetalCompilerOptions &options);

    /** Compiles complete MSL into a metallib, removing intermediate files on success or failure. */
    std::vector<uint8_t> compileMetalLibrary(const std::string &source, const MetalCompilerOptions &options);
}
