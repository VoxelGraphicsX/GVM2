#include "MetalShaderCompiler.hpp"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace UGLC::CodeGen
{
    namespace
    {
        /** Owns one compiler invocation's temporary directory; no compiled shader cache is retained. */
        class MetalCompilationDirectory
        {
        public:
            /** Creates a unique directory suitable for concurrent translation units. */
            MetalCompilationDirectory()
            {
                llvm::SmallString<256> directory;
                if (const auto error = llvm::sys::fs::createUniqueDirectory("uglc-metal", directory))
                    throw std::runtime_error("Cannot create Metal compilation directory: " + error.message());
                path = directory.str().str();
            }

            /** Removes all intermediate source, AIR and diagnostic files. */
            ~MetalCompilationDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }

            std::filesystem::path path;
        };

        /** Executes the Apple compiler without a shell, with a timeout and bounded reported diagnostics. */
        void executeMetalCompiler(const MetalCompilerOptions &options,
                                  const std::vector<std::string> &arguments,
                                  const std::filesystem::path &diagnostics)
        {
            std::vector<llvm::StringRef> command;
            command.emplace_back(options.executable);
            for (const auto &argument : arguments)
                command.emplace_back(argument);
            const std::string diagnosticPath = diagnostics.string();
            const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), llvm::StringRef(diagnosticPath), llvm::StringRef(diagnosticPath)};
            std::string error;
            const int status = llvm::sys::ExecuteAndWait(options.executable, command, std::nullopt, redirects, 120, 0, &error);
            if (status != 0)
            {
                std::ifstream stream(diagnostics, std::ios::binary);
                std::string message(64 * 1024, '\0');
                stream.read(message.data(), static_cast<std::streamsize>(message.size()));
                message.resize(static_cast<size_t>(stream.gcount()));
                throw std::runtime_error("Metal compilation failed (" + std::to_string(status) + ", target " + options.target + "): " + error + "\n" + message);
            }
        }
    }

    void validateMetalCompilerOptions(const MetalCompilerOptions &options)
    {
        if (!options.enabled)
            return;
        if (options.executable.empty() || !llvm::sys::fs::can_execute(options.executable))
            throw std::runtime_error("Embedded metallib requires Apple's Metal compiler. Set --metal-compiler to its executable or explicitly use --emit-metallib off for source-only tooling.");
        if (options.target.empty())
            throw std::runtime_error("Embedded metallib requires an explicit --metal-target.");
    }

    std::vector<uint8_t> compileMetalLibrary(const std::string &source, const MetalCompilerOptions &options)
    {
        validateMetalCompilerOptions(options);
        MetalCompilationDirectory directory;
        const auto input = directory.path / "shader.metal";
        const auto air = directory.path / "shader.air";
        const auto library = directory.path / "shader.metallib";
        {
            std::ofstream stream(input, std::ios::binary);
            stream.write(source.data(), static_cast<std::streamsize>(source.size()));
            stream.close();
            if (!stream)
                throw std::runtime_error("Cannot write MSL compiler input.");
        }
        executeMetalCompiler(options, {"-target", options.target, "-std=metal3.2", "-ffast-math", "-c", input.string(), "-o", air.string()}, directory.path / "compile.log");
        executeMetalCompiler(options, {"-target", options.target, air.string(), "-o", library.string()}, directory.path / "link.log");
        std::ifstream stream(library, std::ios::binary | std::ios::ate);
        const auto size = stream.tellg();
        if (size < 4 || size > 64 * 1024 * 1024)
            throw std::runtime_error("Metal compiler produced an empty or oversized library.");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'L' || bytes[3] != 'B')
            throw std::runtime_error("Metal compiler did not produce a valid metallib container.");
        return bytes;
    }
}
