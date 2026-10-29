#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>

#include <string>

namespace UGLC::CodeGen::UGLIR
{
    /** Returns a deterministic human-readable dump for a complete UGLIR module. */
    [[nodiscard]] std::string dumpModuleAsText(const Module &module);

    /** Returns a deterministic JSON dump for a complete UGLIR module. */
    [[nodiscard]] std::string dumpModuleAsJson(const Module &module);
} // namespace UGLC::CodeGen::UGLIR
