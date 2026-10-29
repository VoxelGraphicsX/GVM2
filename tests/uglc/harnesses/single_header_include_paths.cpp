#include <CodeGen/SingleHeaderGenerator.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

/** Writes one include-resolution fixture, reporting any filesystem failure. */
static void writeSource(const fs::path &path, const std::string &content)
{
    std::ofstream stream(path);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << content;
}

/** Verifies that a declared DSL dependency is expanded exactly once. */
static void verifyExpandedInclude(const fs::path &source,
                                  const std::string &includes,
                                  const std::vector<fs::path> &dslRoots,
                                  const std::string &marker)
{
    writeSource(source, includes);
    UGLC::CodeGen::SingleHeaderGenerator generator;
    generator.init(source, {}, dslRoots);
    generator.create({});
    const auto &result = generator.getResult();
    const auto position = result.find(marker);
    if (position == std::string::npos || result.find(marker, position + marker.size()) != std::string::npos)
    {
        throw std::runtime_error("Expected exactly one dependency definition for: " + includes);
    }
}

/** Checks canonical include identity using fixtures beside this test executable. */
int main(int argc, char **argv)
{
    if (argc != 1)
        throw std::runtime_error("This harness takes no arguments.");

    const auto root = fs::absolute(argv[0]).parent_path() / "single-header-path-fixtures";
    const auto entry = root / "entry";
    const auto shared = root / "shared";
    const auto other = root / "other";
    fs::create_directories(entry);
    fs::create_directories(shared);
    fs::create_directories(other);
    const auto source = entry / "main.hpp";
    writeSource(entry / "dep.hpp", "struct LocalDependency {};\n");
    writeSource(shared / "dep.hpp", "struct SharedDependency {};\n");

    verifyExpandedInclude(source, "#include \"dep.hpp\"\n", {}, "struct LocalDependency");
    std::cout << "plain_include_ok\n";
    verifyExpandedInclude(source, "#include \"./dep.hpp\"\n", {}, "struct LocalDependency");
    std::cout << "dot_include_ok\n";
    verifyExpandedInclude(source, "#include \"../shared/dep.hpp\"\n", {shared}, "struct SharedDependency");
    std::cout << "parent_dsl_include_ok\n";

    const std::string externalInclude = "#include \"../shared/dep.hpp\"";
    writeSource(source, externalInclude + "\n");
    UGLC::CodeGen::SingleHeaderGenerator externalGenerator;
    externalGenerator.init(source, {});
    externalGenerator.create({});
    const auto &externalResult = externalGenerator.getResult();
    if (externalResult.find(externalInclude) == std::string::npos ||
        externalResult.find("struct SharedDependency") != std::string::npos)
    {
        throw std::runtime_error("An include outside the declared DSL roots must remain external.");
    }
    std::cout << "external_include_preserved_ok\n";

    const auto canonicalDependency = fs::weakly_canonical(shared / "dep.hpp");
    verifyExpandedInclude(source, "#include \"" + canonicalDependency.generic_string() + "\"\n",
                          {shared}, "struct SharedDependency");
    std::cout << "canonical_absolute_include_ok\n";
    const auto absoluteAlias = entry / ".." / "shared" / "dep.hpp";
    verifyExpandedInclude(source, "#include \"" + absoluteAlias.generic_string() + "\"\n",
                          {shared}, "struct SharedDependency");
    std::cout << "absolute_alias_include_ok\n";

    const auto symlink = entry / "alias.hpp";
    fs::remove(symlink);
    fs::create_symlink(canonicalDependency, symlink);
    verifyExpandedInclude(source, "#include \"alias.hpp\"\n", {shared}, "struct SharedDependency");
    std::cout << "symlink_include_ok\n";
    verifyExpandedInclude(source,
                          "#include \"../shared/dep.hpp\"\n#include \"alias.hpp\"\n",
                          {shared}, "struct SharedDependency");
    std::cout << "duplicate_alias_expanded_once_ok\n";

    writeSource(shared / "collision.hpp", "struct FirstCandidate {};\n");
    writeSource(other / "collision.hpp", "struct SecondCandidate {};\n");
    writeSource(source, "#include \"collision.hpp\"\n");
    UGLC::CodeGen::SingleHeaderGenerator ambiguousGenerator;
    ambiguousGenerator.init(source, {shared, other}, {shared, other});
    bool rejected = false;
    try
    {
        ambiguousGenerator.create({});
    }
    catch (const std::runtime_error &error)
    {
        if (std::string(error.what()).find("Multiple include paths found for: collision.hpp") == std::string::npos)
            throw;
        rejected = true;
    }
    if (!rejected)
        throw std::runtime_error("Distinct files with the same include name must remain ambiguous.");
    std::cout << "distinct_files_rejected_ok\n";
    return 0;
}
