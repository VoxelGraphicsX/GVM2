#include <CodeGen/SingleHeaderGenerator.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

int main()
{
    namespace fs = std::filesystem;

    fs::create_directories("/tmp/uglc_single_header_case_one");
    fs::create_directories("/tmp/uglc_single_header_case_two");

    {
        std::ofstream("/tmp/uglc_single_header_case_one/shared_target.hpp") << "struct FromCaseOne {};\n";
        std::ofstream("/tmp/uglc_single_header_case_one/main.hpp") << "#include \"shared_target.hpp\"\nstruct RunOneMarker {};\n";
    }
    {
        std::ofstream("/tmp/uglc_single_header_case_two/shared_target.hpp") << "struct FromCaseTwo {};\n";
        std::ofstream("/tmp/uglc_single_header_case_two/main.hpp") << "#include \"shared_target.hpp\"\nstruct RunTwoMarker {};\n";
    }

    UGLC::CodeGen::SingleHeaderGenerator generator;

    generator.init("/tmp/uglc_single_header_case_one/main.hpp", {fs::path("/tmp/uglc_single_header_case_one")});
    generator.create({});
    std::string first = generator.getResult();
    if (first.find("FromCaseOne") == std::string::npos || first.find("RunOneMarker") == std::string::npos)
    {
        throw std::runtime_error("first run did not include expected case-one content");
    }

    generator.init("/tmp/uglc_single_header_case_two/main.hpp", {fs::path("/tmp/uglc_single_header_case_two")});
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

    fs::create_directories("/tmp/uglc_single_header_cache_case/root");
    {
        std::ofstream("/tmp/uglc_single_header_cache_case/root/shared_target.hpp") << "struct FromDiskCacheCase {};\n";
        std::ofstream("/tmp/uglc_single_header_cache_case/root/main.hpp") << "#include \"shared_target.hpp\"\nstruct CacheCaseMarker {};\n";
    }

    UGLC::CodeGen::SingleHeaderGenerator cachedGenerator;
    std::unordered_map<std::string, std::string> loadedFiles;
    loadedFiles.emplace(
        fs::weakly_canonical("/tmp/uglc_single_header_cache_case/root/shared_target.hpp").string(),
        "struct FromMemoryCacheCase {};\n"
    );
    cachedGenerator.init("/tmp/uglc_single_header_cache_case/root/main.hpp", {});
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

    fs::create_directories("/tmp/uglc_single_header_raw_string_case");
    {
        std::ofstream("/tmp/uglc_single_header_raw_string_case/after_raw.hpp") << "struct IncludedAfterShaderArtifact {};\n";
        std::ofstream("/tmp/uglc_single_header_raw_string_case/main.hpp")
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
    rawStringGenerator.init("/tmp/uglc_single_header_raw_string_case/main.hpp", {fs::path("/tmp/uglc_single_header_raw_string_case")});
    rawStringGenerator.create({});
    std::string rawStringResult = rawStringGenerator.getResult();
    if (rawStringResult.find("IncludedAfterShaderArtifact") == std::string::npos)
    {
        throw std::runtime_error("single-header merge stopped expanding includes after a raw string closed with a trailing comma");
    }

    std::cout << "single_header_cache_lookup_ok\n";
    std::cout << "single_header_reuse_ok\n";
    std::cout << "single_header_raw_string_close_ok\n";
    return 0;
}
