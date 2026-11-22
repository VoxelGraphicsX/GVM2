#include <CodeGen/SingleHeaderGenerator.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

int main()
{
    namespace fs = std::filesystem;

    const fs::path tempRoot = fs::temp_directory_path() / "uglc_single_header_state_reset";
    std::error_code cleanupEc;
    fs::remove_all(tempRoot, cleanupEc);
    const fs::path caseOneDir = tempRoot / "case_one";
    const fs::path caseTwoDir = tempRoot / "case_two";
    const fs::path cacheCaseRoot = tempRoot / "cache_case" / "root";
    const fs::path rawStringDir = tempRoot / "raw_string_case";

    fs::create_directories(caseOneDir);
    fs::create_directories(caseTwoDir);

    {
        std::ofstream(caseOneDir / "shared_target.hpp") << "struct FromCaseOne {};\n";
        std::ofstream(caseOneDir / "main.hpp") << "#include \"shared_target.hpp\"\nstruct RunOneMarker {};\n";
    }
    {
        std::ofstream(caseTwoDir / "shared_target.hpp") << "struct FromCaseTwo {};\n";
        std::ofstream(caseTwoDir / "main.hpp") << "#include \"shared_target.hpp\"\nstruct RunTwoMarker {};\n";
    }

    UGLC::CodeGen::SingleHeaderGenerator generator;

    generator.init((caseOneDir / "main.hpp").string(), {caseOneDir});
    generator.create({});
    std::string first = generator.getResult();
    if (first.find("FromCaseOne") == std::string::npos || first.find("RunOneMarker") == std::string::npos)
    {
        throw std::runtime_error("first run did not include expected case-one content");
    }

    generator.init((caseTwoDir / "main.hpp").string(), {caseTwoDir});
    generator.create({});
    std::string second = generator.getResult();
    if (second.find("FromCaseTwo") == std::string::npos || second.find("RunTwoMarker") == std::string::npos)
    {
        throw std::runtime_error("second run did not include expected case-two content");
    }
    if (second.find("FromCaseOne") != std::string::npos || second.find("RunOneMarker") != std::string::npos)
    {
        throw std::runtime_error("second run leaked state from the first run");
    }

    fs::create_directories(cacheCaseRoot);
    {
        std::ofstream(cacheCaseRoot / "shared_target.hpp") << "struct FromDiskCacheCase {};\n";
        std::ofstream(cacheCaseRoot / "main.hpp") << "#include \"shared_target.hpp\"\nstruct CacheCaseMarker {};\n";
    }

    UGLC::CodeGen::SingleHeaderGenerator cachedGenerator;
    std::unordered_map<std::string, std::string> loadedFiles;
    loadedFiles.emplace(
        fs::weakly_canonical(cacheCaseRoot / "shared_target.hpp").string(),
        "struct FromMemoryCacheCase {};\n"
    );
    cachedGenerator.init((cacheCaseRoot / "main.hpp").string(), {});
    cachedGenerator.create(loadedFiles);
    std::string cachedResult = cachedGenerator.getResult();
    if (cachedResult.find("FromMemoryCacheCase") == std::string::npos)
    {
        throw std::runtime_error("canonical cache lookup did not reuse the in-memory file content");
    }
    if (cachedResult.find("FromDiskCacheCase") != std::string::npos)
    {
        throw std::runtime_error("canonical cache lookup fell back to stale on-disk content");
    }

    fs::create_directories(rawStringDir);
    {
        std::ofstream(rawStringDir / "after_raw.hpp") << "struct IncludedAfterShaderArtifact {};\n";
        std::ofstream(rawStringDir / "main.hpp")
            << "struct Holder\n"
            << "{\n"
            << "    const auto shaderArtifact = UGLC::Generated::MakeShaderArtifact(\n"
            << "        __UGL__Global__MSLHeader + R\"(\n"
            << "kernel void computeMain() {}\n"
            << ")\",\n"
            << "        nullptr,\n"
            << "        0\n"
            << "    );\n"
            << "};\n"
            << "#include \"after_raw.hpp\"\n";
    }

    UGLC::CodeGen::SingleHeaderGenerator rawStringGenerator;
    rawStringGenerator.init((rawStringDir / "main.hpp").string(), {rawStringDir});
    rawStringGenerator.create({});
    std::string rawStringResult = rawStringGenerator.getResult();
    if (rawStringResult.find("IncludedAfterShaderArtifact") == std::string::npos)
    {
        throw std::runtime_error("single-header merge stopped expanding includes after a raw string closed with a trailing comma");
    }

    fs::remove_all(tempRoot, cleanupEc);
    std::cout << "single_header_cache_lookup_ok\n";
    std::cout << "single_header_reuse_ok\n";
    std::cout << "single_header_raw_string_close_ok\n";
    return 0;
}
