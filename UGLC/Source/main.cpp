// UGLC – Multithreaded version (LLVM thread pool)
// -----------------------------------------------------------------------------
// 使用 llvm::thread 实现一个极简线程池，并为每个 worker 配置 8 MiB 栈，
// 并行处理翻译单元 (TU)。
// -----------------------------------------------------------------------------

#include <atomic>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cxxopts.hpp>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/thread.h>
#include <llvm/TargetParser/Host.h>

#include <CompilerOutputWriter.hpp>
#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/CPP/CPPVisitor.hpp>
#include <CodeGen/GeneratedShaderArtifactSupport.hpp>
#include <CodeGen/ShaderBackendRegistry.hpp>
#include <CodeGen/SingleHeaderGenerator.hpp>
#include <CodeGen/ShaderEmitter/PreparedShaderTranslationUnit.hpp>
#include <clang/Basic/Version.h>
#include <clang/Sema/SemaConsumer.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/Scope.h>
#include <llvm/Support/SaveAndRestore.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/Support/SHA256.h>
#include <nlohmann/json.hpp>
#include <spirv-tools/libspirv.h>


int main(int argc, const char **argv);

namespace
{
    constexpr char DebugPathSeparator =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif

    std::string trimAsciiWhitespace(std::string value)
    {
        const auto isNotWhitespace = [](unsigned char ch) {
            return !std::isspace(ch);
        };

        auto beginIt = std::find_if(value.begin(), value.end(), isNotWhitespace);
        if (beginIt == value.end())
        {
            return {};
        }
        auto endIt = std::find_if(value.rbegin(), value.rend(), isNotWhitespace).base();
        return std::string(beginIt, endIt);
    }

    void appendUniquePath(std::vector<std::string> &paths, const std::string &path)
    {
        const std::string normalizedPath = trimAsciiWhitespace(path);
        if (normalizedPath.empty())
        {
            return;
        }

        const auto alreadyPresent = std::find_if(paths.begin(), paths.end(), [&](const std::string &existingPath) {
            return std::filesystem::path(existingPath).lexically_normal() == std::filesystem::path(normalizedPath).lexically_normal();
        });
        if (alreadyPresent == paths.end())
        {
            paths.emplace_back(normalizedPath);
        }
    }

    /** Computes a deterministic content digest for shader source and delivered artifact provenance. */
    std::string shaderContentSha256(const std::string &content)
    {
        llvm::SHA256 digest;
        digest.update(llvm::StringRef(content));
        constexpr char HexDigits[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const uint8_t byte : digest.final())
        {
            result += HexDigits[byte >> 4u];
            result += HexDigits[byte & 15u];
        }
        return result;
    }

    /** Invalidates owned UGLIR shader outputs until the entire invocation succeeds. */
    class ArtifactPublication
    {
    public:
        /** Removes previous shader artifacts from an explicitly supplied output directory. */
        explicit ArtifactPublication(const std::filesystem::path &outputDir) : mOutputDir(outputDir)
        {
            if (outputDir.empty()) { throw std::runtime_error("The UGLIR pipeline requires an output directory."); }
            invalidate();
        }

        /** Removes partial outputs on any failed compilation or failed artifact write. */
        ~ArtifactPublication()
        {
            if (!mPublished)
            {
                try { invalidate(); }
                catch (const std::exception &error) { llvm::errs() << error.what() << '\n'; }
            }
        }

        /** Marks all shader artifacts as published only after every output write succeeds. */
        void publish() { mPublished = true; }

    private:
        std::filesystem::path mOutputDir;
        bool mPublished = false;

        /** Removes only compiler-owned shader outputs and preserves unrelated output-directory files. */
        void invalidate()
        {
            for (const char *name : {"generate_result.hpp", "exports.hpp", "shader-compilation.json", "experimental-compilation.json", "uglir", "msl", "hlsl", "spv"})
            {
                const std::filesystem::path artifact = mOutputDir / name;
                std::error_code error;
                std::filesystem::remove_all(artifact, error);
                if (error) { throw std::runtime_error("Failed to invalidate shader artifact \"" + artifact.string() + "\": " + error.message()); }
            }
        }
    };


    std::optional<std::string> readEnvironmentPath(const char *name)
    {
        const char *value = std::getenv(name);
        if (value == nullptr)
        {
            return std::nullopt;
        }

        const std::string normalizedValue = trimAsciiWhitespace(value);
        if (normalizedValue.empty())
        {
            return std::nullopt;
        }
        return normalizedValue;
    }

    std::vector<std::string> readEnvironmentPathList(const char *name)
    {
        std::vector<std::string> paths;
        const auto value = readEnvironmentPath(name);
        if (!value)
        {
            return paths;
        }

        std::stringstream stream(*value);
        std::string segment;
        while (std::getline(stream, segment, DebugPathSeparator))
        {
            appendUniquePath(paths, segment);
        }
        return paths;
    }

    /**
     * Parses the command-line switch that controls whether UGLC writes the pre-codegen merged DSL single-header artifact.
     *
     * Accepts only "on" and "off" case-insensitively so invalid command-line input fails explicitly instead of silently
     * changing which generated artifacts are produced.
     */
    bool parseEmitDslSingleHeaderOption(std::string value)
    {
        value = trimAsciiWhitespace(std::move(value));
        for (char &character : value)
        {
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }

        if (value == "on")
        {
            return true;
        }
        if (value == "off")
        {
            return false;
        }

        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Invalid value for --emit-dsl-single-header: \"" + value + "\". Expected on/off."));
    }

    std::optional<std::string> captureCommandOutput(const char *command)
    {
        std::array<char, 256> buffer = {};
        std::string output;

#if defined(_WIN32)
        FILE *pipe = _popen(command, "r");
#else
        FILE *pipe = popen(command, "r");
#endif
        if (pipe == nullptr)
        {
            return std::nullopt;
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            output += buffer.data();
        }

        int exitCode = 0;
#if defined(_WIN32)
        exitCode = _pclose(pipe);
#else
        exitCode = pclose(pipe);
#endif
        if (exitCode != 0)
        {
            return std::nullopt;
        }

        output = trimAsciiWhitespace(output);
        if (output.empty())
        {
            return std::nullopt;
        }
        return output;
    }

    std::optional<std::string> detectAppleSdkRoot()
    {
#if defined(__APPLE__)
        if (auto environmentSdkRoot = readEnvironmentPath("SDKROOT"))
        {
            return environmentSdkRoot;
        }
        return captureCommandOutput("xcrun --show-sdk-path 2>/dev/null");
#else
        return std::nullopt;
#endif
    }

    void *getExecutableAddressAnchor()
    {
        return reinterpret_cast<void *>(&getExecutableAddressAnchor);
    }

    std::optional<std::string> findBundledClangResourceDirNearExecutable(const char *argv0)
    {
        if (argv0 == nullptr || std::string_view(argv0).empty())
        {
            return std::nullopt;
        }

        std::filesystem::path executablePath = std::filesystem::absolute(argv0);
        executablePath = executablePath.lexically_normal();

        std::filesystem::path searchRoot = executablePath.parent_path();
        while (!searchRoot.empty())
        {
            const std::filesystem::path clangVersionRoot = searchRoot / "lib" / "clang";
            if (std::filesystem::exists(clangVersionRoot) && std::filesystem::is_directory(clangVersionRoot))
            {
                for (const auto &entry : std::filesystem::directory_iterator(clangVersionRoot))
                {
                    if (!entry.is_directory())
                    {
                        continue;
                    }

                    const std::filesystem::path resourceCandidate = entry.path();
                    if (std::filesystem::exists(resourceCandidate / "include")
                        && std::filesystem::is_directory(resourceCandidate / "include"))
                    {
                        return resourceCandidate.string();
                    }
                }
            }

            const std::filesystem::path parent = searchRoot.parent_path();
            if (parent == searchRoot)
            {
                break;
            }
            searchRoot = parent;
        }

        return std::nullopt;
    }

    std::string resolveClangResourceDir(const char *argv0)
    {
        if (auto configuredResourceDir = readEnvironmentPath("UGLC_LLVM_RESOURCE_DIR"))
        {
            return *configuredResourceDir;
        }

        const std::string resourceDir = clang::CompilerInvocation::GetResourcesPath(argv0, getExecutableAddressAnchor());
        if (!resourceDir.empty()
            && std::filesystem::exists(resourceDir)
            && std::filesystem::is_directory(resourceDir))
        {
            return resourceDir;
        }

        if (auto bundledResourceDir = findBundledClangResourceDirNearExecutable(argv0))
        {
            return *bundledResourceDir;
        }

        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Unable to resolve clang resource directory automatically. Pass --resource-dir or set UGLC_LLVM_RESOURCE_DIR explicitly."));
    }

    void applyDebugDefaultsFromEnvironment(std::string &sourcePath,
                                           std::vector<std::string> &includePaths,
                                           std::vector<std::string> &dslSourceRoots,
                                           std::string &outputDir)
    {
        if (sourcePath.empty())
        {
            if (auto debugSource = readEnvironmentPath("UGLC_DEBUG_SOURCE"))
            {
                sourcePath = *debugSource;
            }
        }
        if (outputDir.empty())
        {
            if (auto debugOutputDir = readEnvironmentPath("UGLC_DEBUG_OUTPUT_DIR"))
            {
                outputDir = *debugOutputDir;
            }
        }
        for (const auto &includePath : readEnvironmentPathList("UGLC_DEBUG_INCLUDE_PATHS"))
        {
            appendUniquePath(includePaths, includePath);
        }
        for (const auto &dslSourceRoot : readEnvironmentPathList("UGLC_DEBUG_DSL_DIRS"))
        {
            appendUniquePath(dslSourceRoots, dslSourceRoot);
        }
    }
} // namespace

// -----------------------------------------------------------------------------
// ASTConsumer – 驱动 RecursiveASTVisitor
// -----------------------------------------------------------------------------
/** Completes shader semantics inside an isolated Clang frontend. */
class ShaderSemanticConsumer final : public clang::SemaConsumer
{
public:
    /** Writes owned shader modules into the translation-unit result. */
    explicit ShaderSemanticConsumer(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result)
        : Result(result) {}

    /** Attaches the Sema owned by this shader frontend. */
    void InitializeSema(clang::Sema &sema) override { Sema = &sema; }

    /** Clears the borrowed Sema reference when Clang detaches it. */
    void ForgetSema() override { Sema = nullptr; }

    /** Instantiates shader entries before source verification and lowering. */
    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        if (Sema == nullptr || context.getDiagnostics().hasErrorOccurred())
            throw std::runtime_error("UGLIR Sema: shader frontend parsing failed.");
        // ParseAST has already exited the parser's TU scope. Lazy builtins
        // created by implicit special members still require a live TUScope.
        clang::Scope translationUnitScope(nullptr, clang::Scope::DeclScope, context.getDiagnostics());
        translationUnitScope.setEntity(context.getTranslationUnitDecl());
        llvm::SaveAndRestore<clang::Scope *> scopeBinding(Sema->TUScope, &translationUnitScope);
        Result = UGLC::CodeGen::ShaderEmitter::prepareShaderTranslationUnit(context, *Sema);
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    clang::Sema *Sema = nullptr;
};

/** Parses original shader input without attaching host code generation. */
class ShaderSemanticFrontendAction final : public clang::ASTFrontendAction
{
public:
    /** Creates an isolated shader action with an invocation-wide preprocessing time. */
    ShaderSemanticFrontendAction(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result, uint64_t timestamp)
        : Result(result), Timestamp(timestamp) {}

    /** Fixes volatile preprocessing time before Clang creates its preprocessor. */
    bool BeginInvocation(clang::CompilerInstance &compiler) override
    {
        compiler.getPreprocessorOpts().SourceDateEpoch = Timestamp;
        return true;
    }

    /** Creates the Sema-aware consumer for this shader AST. */
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<ShaderSemanticConsumer>(Result);
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    uint64_t Timestamp;
};

/** Creates shader actions that export owned modules into one TU result. */
class ShaderSemanticActionFactory final : public clang::tooling::FrontendActionFactory
{
public:
    /** Retains only the output result and the common preprocessing timestamp. */
    ShaderSemanticActionFactory(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result, uint64_t timestamp)
        : Result(result), Timestamp(timestamp) {}

    /** Creates one independent shader frontend action. */
    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<ShaderSemanticFrontendAction>(Result, Timestamp);
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    uint64_t Timestamp;
};

/** Generates host code after any isolated shader frontend has been destroyed. */
class BaseGeneratorASTConsumer : public clang::ASTConsumer
{
public:
    /** Creates a host generator with optional prepared shader modules. */
    BaseGeneratorASTConsumer(clang::Rewriter *rewriter, clang::ASTContext *context,
                             UGLC::CodeGen::ShaderSourcePipelineOptions options = {})
        : Visitor(rewriter, context, options), Options(options) {}

    /** Checks original input consistency and rewrites only the independent host AST. */
    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        if (Options.preparedShaders != nullptr)
            UGLC::CodeGen::ShaderEmitter::verifyShaderFrontendInputs(*Options.preparedShaders, context.getSourceManager());
        Visitor.collectStaticShaderVariants(context.getTranslationUnitDecl());
        Visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    UGLC::CodeGen::CPP::CPPVisitor Visitor;
    UGLC::CodeGen::ShaderSourcePipelineOptions Options;
};

// -----------------------------------------------------------------------------
// FrontendAction – 绑定 Rewriter 并在 TU 结束时 flush
// -----------------------------------------------------------------------------
class BaseGeneratorFrontendAction : public clang::ASTFrontendAction
{
public:
    /** Creates a frontend action that stores rewritten files and uses the selected shader source pipeline. */
    BaseGeneratorFrontendAction(UGLC::App::SharedResults &S, UGLC::CodeGen::ShaderSourcePipelineOptions shaderSourcePipelineOptions = {})
        : Store(S)
        , ShaderSourcePipelineOptions(shaderSourcePipelineOptions)
    {
    }

    /** Uses the same preprocessing time as the isolated shader frontend. */
    bool BeginInvocation(clang::CompilerInstance &compiler) override
    {
        if (ShaderSourcePipelineOptions.pipelineKind == UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR)
            compiler.getPreprocessorOpts().SourceDateEpoch = ShaderSourcePipelineOptions.frontendTimestamp;
        return true;
    }

    /** Creates the AST consumer that performs host rewriting for one translation unit. */
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI, llvm::StringRef InFile) override
    {
        (void)InFile;
        TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<BaseGeneratorASTConsumer>(&TheRewriter, &CI.getASTContext(), ShaderSourcePipelineOptions);
    }

    /** Flushes all rewritten buffers into the shared result map at the end of a translation unit. */
    void EndSourceFileAction() override
    {
        auto &SM = TheRewriter.getSourceMgr();
        auto storeBuffer = [&](clang::FileID fileId) {
            const clang::FileEntry *entry = SM.getFileEntryForID(fileId);
            if (entry == nullptr)
            {
                return;
            }

            llvm::StringRef path = entry->tryGetRealPathName();
            if (path.empty())
            {
                return;
            }

            std::string rewritten;
            llvm::raw_string_ostream rewrittenStream(rewritten);
            TheRewriter.getEditBuffer(fileId).write(rewrittenStream);
            rewrittenStream.flush();

            std::cout << "generated file: " << path.str() << '\n';
            Store.store(path.str(), std::move(rewritten));
        };

        const clang::FileID mainFileId = SM.getMainFileID();
        storeBuffer(mainFileId);

        for (auto it = TheRewriter.buffer_begin(); it != TheRewriter.buffer_end(); ++it)
        {
            if (it->first == mainFileId)
            {
                continue;
            }
            storeBuffer(it->first);
        }
    }

private:
    clang::Rewriter TheRewriter;
    UGLC::App::SharedResults &Store;
    UGLC::CodeGen::ShaderSourcePipelineOptions ShaderSourcePipelineOptions;
};

/** Adapts UGLIR shader debug output writes to the app-level shared result store. */
class SharedResultsShaderDebugOutputSink final : public UGLC::CodeGen::IShaderDebugOutputSink
{
public:
    /** Creates a sink that writes debug artifacts into the provided shared result store. */
    explicit SharedResultsShaderDebugOutputSink(UGLC::App::SharedResults &store)
        : Store(store)
    {
    }

    /** Stores one UGLIR shader debug artifact at a path relative to the output directory. */
    void storeShaderDebugOutput(const std::string &relativePath, std::string content) override
    {
        if (!Store.storeUnique(relativePath, std::move(content)))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("UGLIR debug output: duplicate artifact path \"" + relativePath + "\"."));
        }
    }

private:
    UGLC::App::SharedResults &Store;
};

// -----------------------------------------------------------------------------
// 帮助函数：把公共编译参数注入 Tool
// -----------------------------------------------------------------------------
static void addCommonAdjusters(clang::tooling::ClangTool &Tool, const std::vector<std::string> &CommonArgs, const std::vector<std::filesystem::path> &IncludePaths)
{
    using clang::tooling::ArgumentInsertPosition;
    Tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster("-std=c++20", ArgumentInsertPosition::BEGIN));
    Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(CommonArgs, ArgumentInsertPosition::END));

    // 动态 include
    for (const auto &IP : IncludePaths)
    {
        Tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(("-I" + IP.string()).c_str(), ArgumentInsertPosition::BEGIN));
    }
}

// -----------------------------------------------------------------------------
// 简单线程池：固定 worker 数，循环领取任务索引
// -----------------------------------------------------------------------------
class StdThreadPool
{
public:
    explicit StdThreadPool(unsigned ThreadCount = llvm::thread::hardware_concurrency())
        : NextIndex(0)
        , Concurrency(std::max(1u, ThreadCount))
    {
    }

    template <typename Task>
    void run(const std::vector<std::string> &Tasks, Task &&Fn)
    {
        std::exception_ptr FirstException;
        std::mutex ExceptionMtx;
        std::atomic_bool StopRequested = false;
        Workers.reserve(std::min<size_t>(Concurrency, Tasks.size()));
        for (unsigned i = 0; i < Concurrency && i < Tasks.size(); ++i)
        {
            Workers.emplace_back(std::optional<unsigned>(8u * 1024u * 1024u), [&, Fn]() {
                try
                {
                    size_t Idx;
                    while (!StopRequested.load() && (Idx = NextIndex.fetch_add(1)) < Tasks.size())
                    {
                        Fn(Tasks[Idx]);
                    }
                }
                catch (...)
                {
                    StopRequested.store(true);
                    std::lock_guard<std::mutex> Lock(ExceptionMtx);
                    if (!FirstException)
                    {
                        FirstException = std::current_exception();
                    }
                }
            });
        }
        for (auto &T : Workers)
            T.join();
        Workers.clear();
        if (FirstException)
        {
            std::rethrow_exception(FirstException);
        }
    }

private:
    std::atomic_size_t NextIndex;
    unsigned Concurrency;
    std::vector<llvm::thread> Workers;
};

static std::string MakeHostShaderOnlyStubs()
{
    // The generated single-header output does not include the external DSL headers.
    // Keep shader-only helper names available in host builds, but model them as
    // deleted declarations so merely including the header stays valid. Any real
    // host-side use still fails at compile time at the exact call site.
    return R"UGL(
#ifndef __UGLC_HOST_SHADER_ONLY_STUBS_HPP
#define __UGLC_HOST_SHADER_ONLY_STUBS_HPP
namespace UGL
{
// Keep generated host headers self-contained: ballot-style APIs need a visible
// return type even when the external DSL vector aliases are not included.
struct UGLC_HostWaveMask
{
    unsigned int x = 0;
    unsigned int y = 0;
    unsigned int z = 0;
    unsigned int w = 0;
};

template <class T>
T WaveReadLaneAt(T value, unsigned int laneIndex) = delete;

template <class T>
T WaveReadLaneFirst(T value) = delete;

template <class T>
T WavePrefixSum(T value) = delete;

inline unsigned int WaveActiveCountBits(bool predicate) = delete;

inline unsigned int WavePrefixCountBits(bool predicate) = delete;

inline UGLC_HostWaveMask WaveActiveBallot(bool predicate) = delete;

template <class T>
UGLC_HostWaveMask WaveMatch(T value) = delete;

template <class T>
T QuadReadLaneAt(T value, unsigned int laneIndex) = delete;

template <class T>
T QuadReadAcrossX(T value) = delete;

template <class T>
T QuadReadAcrossY(T value) = delete;

template <class T>
T QuadReadAcrossDiagonal(T value) = delete;

template <class T>
T WaveReadAcrossX(T value) = delete;

template <class T>
T WaveReadAcrossY(T value) = delete;

template <class T>
T WaveReadAcrossDiagonal(T value) = delete;

inline unsigned int WaveGetLaneCount() = delete;

inline unsigned int WaveGetLaneIndex() = delete;
} // namespace UGL
#endif
)UGL";
}

// -----------------------------------------------------------------------------
// main – 命令行接口保持不变，但改用 StdThreadPool
// -----------------------------------------------------------------------------
int main(int argc, const char **argv)
{
    try
    {
        std::cout << "====================UGLC executes started=====================================\n";

    // ---------- CLI 解析 -------------------------------------------------------
    cxxopts::Options options("UGLC", "Compiler for C++ Rendering System Development");
    options.add_options()
        ("s,source", "source file", cxxopts::value<std::string>())
        ("I,include", "include path", cxxopts::value<std::vector<std::string>>())
        ("dsl-dir", "directory roots that contain DSL-authored headers UGLC is allowed to inline into the merged output", cxxopts::value<std::vector<std::string>>())
        ("o,output", "output directory", cxxopts::value<std::string>())
        ("emit-dsl-single-header", "write the pre-codegen merged DSL single-header artifact to the output directory (on/off, default: on)", cxxopts::value<std::string>()->default_value("on"))
        ("resource-dir", "explicit clang resource directory", cxxopts::value<std::string>())
        ("sdkroot", "explicit SDK root for Apple platforms", cxxopts::value<std::string>())
        ("target", "explicit target triple", cxxopts::value<std::string>());
    options.add_options()
        ("shader-pipeline", "shader pipeline: uglir or legacy (default: uglir)", cxxopts::value<std::string>())
        ("no-spirv-optimization", "validate and deliver unoptimized direct SPIR-V for explicit comparisons");
    options.add_options()
        ("h,help", "print help");
    auto ArgResult = options.parse(argc, argv);

    if (ArgResult.count("h") != 0)
    {
        std::cout << options.help() << '\n';
        return 0;
    }

    if (ArgResult.count("shader-pipeline") > 1)
        throw std::runtime_error("Specify --shader-pipeline only once.");

    const std::string SelectedPipeline = ArgResult.count("shader-pipeline") != 0
        ? ArgResult["shader-pipeline"].as<std::string>()
        : "uglir";
    if (SelectedPipeline != "uglir" && SelectedPipeline != "legacy")
        throw std::runtime_error("--shader-pipeline must be uglir or legacy.");
    const bool UseUGLIRShaderPipeline = SelectedPipeline == "uglir";
    if (!UseUGLIRShaderPipeline && !UGLC_ENABLE_LEGACY)
        throw std::runtime_error("Legacy shader pipeline is not built; enable UGLC_ENABLE_LEGACY.");
    if (ArgResult.count("no-spirv-optimization") != 0 && !UseUGLIRShaderPipeline)
        throw std::runtime_error("--no-spirv-optimization requires --shader-pipeline=uglir.");
    std::cout << "UGLC shader pipeline: " << (UseUGLIRShaderPipeline ? "UGLIR" : "LegacyAST") << '\n';

    std::string SourcePath = ArgResult.count("s") != 0 ? ArgResult["s"].as<std::string>() : std::string {};
    std::vector<std::string> IncludePathsStr = ArgResult.count("I") != 0 ? ArgResult["I"].as<std::vector<std::string>>() : std::vector<std::string> {};
    std::vector<std::string> DslSourceRootsStr = ArgResult.count("dsl-dir") != 0 ? ArgResult["dsl-dir"].as<std::vector<std::string>>() : std::vector<std::string> {};
    std::string OutputDir = ArgResult.count("o") != 0 ? ArgResult["o"].as<std::string>() : std::string {};
    const std::string EmitDslSingleHeaderValue = ArgResult.count("emit-dsl-single-header") != 0 ? ArgResult["emit-dsl-single-header"].as<std::string>() : std::string("on");
    const bool EmitDslSingleHeader = parseEmitDslSingleHeaderOption(EmitDslSingleHeaderValue);
    applyDebugDefaultsFromEnvironment(SourcePath, IncludePathsStr, DslSourceRootsStr, OutputDir);

    if (SourcePath.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Missing required option --source/-s. Pass the DSL entry file explicitly, or set UGLC_DEBUG_SOURCE for a local debug shell profile."));
    }
    if (OutputDir.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Missing required option --output/-o. Pass the generated output directory explicitly, or set UGLC_DEBUG_OUTPUT_DIR for a local debug shell profile."));
    }

    std::vector<std::string> NormalizedIncludePathsStr;
    NormalizedIncludePathsStr.reserve(IncludePathsStr.size());
    for (const auto &includePath : IncludePathsStr)
    {
        appendUniquePath(NormalizedIncludePathsStr, includePath);
    }
    IncludePathsStr = std::move(NormalizedIncludePathsStr);

    std::vector<std::filesystem::path> IncludePaths;
    IncludePaths.reserve(IncludePathsStr.size());
    for (const auto &includePath : IncludePathsStr)
    {
        IncludePaths.emplace_back(includePath);
    }

    std::vector<std::string> NormalizedDslSourceRootsStr;
    NormalizedDslSourceRootsStr.reserve(DslSourceRootsStr.size());
    for (const auto &dslSourceRoot : DslSourceRootsStr)
    {
        appendUniquePath(NormalizedDslSourceRootsStr, dslSourceRoot);
    }
    DslSourceRootsStr = std::move(NormalizedDslSourceRootsStr);

    std::vector<std::filesystem::path> DslSourceRoots;
    DslSourceRoots.reserve(DslSourceRootsStr.size());
    for (const auto &dslSourceRoot : DslSourceRootsStr)
    {
        DslSourceRoots.emplace_back(dslSourceRoot);
    }

    std::optional<ArtifactPublication> Publication;
    if (UseUGLIRShaderPipeline) { Publication.emplace(OutputDir); }
    if (!std::filesystem::exists(SourcePath))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Source file does not exist: " + SourcePath));
    }
    if (!std::filesystem::is_regular_file(SourcePath))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Source path must be a file: " + SourcePath));
    }
    for (const auto &IP : IncludePaths)
    {
        if (!std::filesystem::exists(IP))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Include path does not exist: " + IP.string()));
        }
        if (!std::filesystem::is_directory(IP))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Include path must be a directory: " + IP.string()));
        }
    }
    for (const auto &rootPath : DslSourceRoots)
    {
        if (!std::filesystem::exists(rootPath))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("DSL source root does not exist: " + rootPath.string()));
        }
        if (!std::filesystem::is_directory(rootPath))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("DSL source root must be a directory: " + rootPath.string()));
        }
    }

    for (const auto &IP : IncludePaths)
        std::cout << "UGLC: found include path: " << IP << '\n';
    for (const auto &rootPath : DslSourceRoots)
        std::cout << "UGLC: found DSL source root: " << rootPath << '\n';
    std::cout << "UGLC: MSL PixelLocal implementation: framebuffer_fetch" << '\n';
    std::cout << "UGLC: found output path: " << OutputDir << '\n';
    if (!UGLC::App::ensureOutputDirectory(OutputDir))
    {
        return 1;
    }

    // ---------- Pass 1: 发现 TU ----------------------------------------------
    std::vector<const char *> NewArgv;
    NewArgv.push_back("");

    UGLC::CodeGen::SingleHeaderGenerator DiscoverPass;
    DiscoverPass.init(SourcePath, IncludePaths, DslSourceRoots);
    DiscoverPass.create({});
    const std::string DslSingleHeader = DiscoverPass.getResult();
    const std::filesystem::path CanonicalSourcePath = std::filesystem::weakly_canonical(SourcePath);
    std::cout << "found translation unit: " << CanonicalSourcePath << '\n';
    std::vector<std::string> InputFiles = {CanonicalSourcePath.string()};
    NewArgv.push_back(InputFiles.front().c_str());

    int NewArgc = static_cast<int>(NewArgv.size());
    llvm::cl::OptionCategory Category("UGLC options");
    auto ExpectedParser = clang::tooling::CommonOptionsParser::create(NewArgc, NewArgv.data(), Category);
    if (!ExpectedParser)
    {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();

    // ---------- 公共编译参数 ----------------------------------------------------
    const std::string ClangResourceDir = ArgResult.count("resource-dir") != 0
                                             ? ArgResult["resource-dir"].as<std::string>()
                                             : resolveClangResourceDir(argv[0]);
    if (!std::filesystem::exists(ClangResourceDir) || !std::filesystem::is_directory(ClangResourceDir))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Clang resource directory does not exist or is not a directory: " + ClangResourceDir));
    }

    const std::optional<std::string> AppleSdkRoot = ArgResult.count("sdkroot") != 0
                                                        ? std::optional<std::string>(ArgResult["sdkroot"].as<std::string>())
                                                        : detectAppleSdkRoot();
    if (AppleSdkRoot.has_value()
        && (!std::filesystem::exists(*AppleSdkRoot) || !std::filesystem::is_directory(*AppleSdkRoot)))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("SDK root does not exist or is not a directory: " + *AppleSdkRoot));
    }

    const std::string TargetTriple = ArgResult.count("target") != 0
                                         ? ArgResult["target"].as<std::string>()
                                         : llvm::sys::getDefaultTargetTriple();
    if (TargetTriple.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Unable to determine a target triple automatically. Pass --target explicitly."));
    }

    std::vector<std::string> CommonArgs = {
        "-std=c++20",
        "-D__STDC_CONSTANT_MACROS",
        "-D__STDC_FORMAT_MACROS",
        "-D__STDC_LIMIT_MACROS",
        ("-resource-dir=" + ClangResourceDir),
        "-target",
        TargetTriple,
    };
#if defined(__APPLE__)
    if (AppleSdkRoot.has_value())
    {
        CommonArgs.emplace_back("-isysroot");
        CommonArgs.emplace_back(*AppleSdkRoot);
    }
    CommonArgs.emplace_back("-stdlib=libc++");
#endif

    const std::vector<std::string> &TUList = OptionsParser.getSourcePathList();

    UGLC::CodeGen::ShaderSourcePipelineOptions ShaderSourcePipelineOptions;
    UGLC::App::SharedResults UGLIRShaderDebugStore;
    SharedResultsShaderDebugOutputSink UGLIRShaderDebugSink(UGLIRShaderDebugStore);
    ShaderSourcePipelineOptions.pipelineKind = UseUGLIRShaderPipeline ? UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR : UGLC::CodeGen::ShaderSourcePipelineKind::LegacyAST;
    if (UseUGLIRShaderPipeline)
    {
        ShaderSourcePipelineOptions.pipelineKind = UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR;
        ShaderSourcePipelineOptions.debugOutputSink = &UGLIRShaderDebugSink;
        ShaderSourcePipelineOptions.optimizeSPIRV = ArgResult.count("no-spirv-optimization") == 0;
    }

    ShaderSourcePipelineOptions.frontendTimestamp = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    // ---------- 多线程执行 ------------------------------------------------------
    UGLC::App::SharedResults GlobalStore;
    StdThreadPool Pool; // 默认硬件并发

    // 定义自制 FrontendActionFactory，能够把 GlobalStore 传入 Action
    class UGLCActionFactory : public clang::tooling::FrontendActionFactory
    {
    public:
        /** Creates host-code frontend actions that share output storage and shader source pipeline selection. */
        UGLCActionFactory(UGLC::App::SharedResults &S, UGLC::CodeGen::ShaderSourcePipelineOptions shaderSourcePipelineOptions)
            : Store(S)
            , ShaderSourcePipelineOptions(shaderSourcePipelineOptions)
        {
        }
        /** Creates one frontend action for the current translation-unit invocation. */
        std::unique_ptr<clang::FrontendAction> create() override
        {
            return std::make_unique<BaseGeneratorFrontendAction>(Store, ShaderSourcePipelineOptions);
        }

    private:
        UGLC::App::SharedResults &Store;
        UGLC::CodeGen::ShaderSourcePipelineOptions ShaderSourcePipelineOptions;
    };

        Pool.run(TUList, [&](const std::string &TU) {
            auto tuOptions = ShaderSourcePipelineOptions;
            UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit preparedShaders;
            if (tuOptions.pipelineKind == UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR)
            {
                {
                    clang::tooling::ClangTool shaderTool(OptionsParser.getCompilations(), {TU});
                    addCommonAdjusters(shaderTool, CommonArgs, IncludePaths);
                    ShaderSemanticActionFactory shaderFactory(preparedShaders, tuOptions.frontendTimestamp);
                    if (shaderTool.run(&shaderFactory) != 0)
                        throw std::runtime_error("Failed to prepare shader translation unit \"" + TU + "\".");
                }
                tuOptions.preparedShaders = &preparedShaders;
                if (tuOptions.debugOutputSink != nullptr)
                    UGLC::CodeGen::ShaderEmitter::storePreparedShaderDebugOutputs(preparedShaders, *tuOptions.debugOutputSink);
            }
            clang::tooling::ClangTool Tool(OptionsParser.getCompilations(), {TU});
            addCommonAdjusters(Tool, CommonArgs, IncludePaths);

            UGLCActionFactory Factory(GlobalStore, tuOptions);
            if (Tool.run(&Factory) != 0)
            {
                throw std::runtime_error("Failed to process translation unit \"" + TU + "\".");
            }
        });


    // ---------- Pass 2: 合并 ---------------------------------------------------
    UGLC::CodeGen::SingleHeaderGenerator Generator;
    Generator.init(SourcePath, IncludePaths, DslSourceRoots);
    Generator.create(GlobalStore.Map);
    bool writeSucceeded = true;
    nlohmann::json ArtifactDigests = nlohmann::json::object();

    if (EmitDslSingleHeader)
    {
        writeSucceeded = UGLC::App::writeResult(OutputDir + "/dsl_single_header.hpp", DslSingleHeader) && writeSucceeded;
    }
    {

        std::string FinalHeader;
        FinalHeader += "#pragma once\n";
        FinalHeader += "// clang-format off\n";
        FinalHeader += "#include <cstdint>\n";
        FinalHeader += "#include <GVMRHI/GVMRHI.hpp>\n";
        FinalHeader += "#include <EASTL/array.h>\n";
        FinalHeader += "#include <GVMCore/Private/GVMCore.Private.hpp>\n";
        FinalHeader += "#include <GVMCore/GVMCore.Public.hpp>\n";
        FinalHeader += "#include \"exports.hpp\"\n";

        FinalHeader += "using namespace GVM::Core::Math;\n";
        FinalHeader += UGLC::CodeGen::MakeGeneratedShaderArtifactSupport();
        FinalHeader += MakeHostShaderOnlyStubs();
        for (const auto &shaderPrelude : UGLC::CodeGen::collectRegisteredShaderBackendPreludes())
        {
            // Keep backend shader preludes centralized in the registry so final
            // single-header assembly does not need to know which concrete
            // backends are currently participating in code generation.
            FinalHeader += "static const eastl::string " + shaderPrelude.variableName + " = R\"(" + shaderPrelude.sourceText + ")\";\n";
        }
        FinalHeader += Generator.getResult();

        writeSucceeded = UGLC::App::writeResult(OutputDir + "/generate_result.hpp", FinalHeader) && writeSucceeded;
        if (UseUGLIRShaderPipeline) { ArtifactDigests["generate_result.hpp"] = shaderContentSha256(FinalHeader); }
    }
    {
        std::string FinalHeader;
        FinalHeader += "#ifndef UGL_EXPORT_HPP\n";
        FinalHeader += "#define UGL_EXPORT_HPP\n";
        FinalHeader += Generator.getExportResult();
        FinalHeader += "\n#endif\n";
        writeSucceeded = UGLC::App::writeResult(OutputDir + "/exports.hpp", FinalHeader) && writeSucceeded;
        if (UseUGLIRShaderPipeline) { ArtifactDigests["exports.hpp"] = shaderContentSha256(FinalHeader); }
    }
    if (UseUGLIRShaderPipeline)
    {
        for (const auto &[relativePath, content] : UGLIRShaderDebugStore.Map)
        {
            writeSucceeded = UGLC::App::writeResult(std::filesystem::path(OutputDir) / relativePath, content) && writeSucceeded;
            ArtifactDigests[relativePath] = shaderContentSha256(content);
        }
        const nlohmann::json compilation = {
            {"schemaVersion", 1}, {"pipeline", "UGLIR"},
            {"defaultPipeline", "UGLIR"}, {"fallbackUsed", false},
            {"source", SourcePath}, {"mergedDslSha256", shaderContentSha256(DslSingleHeader)},
            {"clangVersion", clang::getClangFullVersion()}, {"spirvToolsVersion", spvSoftwareVersionDetailsString()},
            {"targetTriple", TargetTriple}, {"spirvTargetEnvironment", "vulkan1.2"},
            {"spirvOptimization", ShaderSourcePipelineOptions.optimizeSPIRV ? "runtime" : "disabled"},
            {"arguments", std::vector<std::string>(argv + 1, argv + argc)},
            {"artifacts", ArtifactDigests},
        };
        writeSucceeded = UGLC::App::writeResult(std::filesystem::path(OutputDir) / "shader-compilation.json", compilation.dump(2) + "\n") && writeSucceeded;
        if (writeSucceeded) { Publication->publish(); }
    }
        std::cout << "====================UGLC executes ended=====================================\n";
        return writeSucceeded ? 0 : 1;
    }
    catch (const std::exception &Ex)
    {
        if (UGLC::CodeGen::isFormattedClangStyleDiagnostic(Ex.what()))
        {
            llvm::errs() << Ex.what() << '\n';
        }
        else
        {
            llvm::errs() << UGLC::CodeGen::formatUnlocatedDiagnostic(Ex.what()) << '\n';
        }
        return 1;
    }
}
