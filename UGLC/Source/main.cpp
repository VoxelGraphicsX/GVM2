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
#include <clang/Basic/Diagnostic.h>
#include <llvm/Support/WithColor.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/thread.h>
#include <llvm/TargetParser/Host.h>

#include <CompilerOutputWriter.hpp>
#include <CodeGen/DiagnosticError.hpp>
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
            for (const char *name : {"generate_result.hpp", "exports.hpp", "shader-compilation.json", "experimental-compilation.json", "constructor-arguments.json", "uglir", "msl", "hlsl", "spv"})
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
    explicit ShaderSemanticConsumer(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result,
                                    bool argumentContractsOnly)
        : Result(result), ArgumentContractsOnly(argumentContractsOnly) {}

    /** Attaches the Sema owned by this shader frontend. */
    void InitializeSema(clang::Sema &sema) override { Sema = &sema; }

    /** Clears the borrowed Sema reference when Clang detaches it. */
    void ForgetSema() override { Sema = nullptr; }

    /** Instantiates shader entries before source verification and lowering. */
    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        try
        {
            if (Sema == nullptr || context.getDiagnostics().hasErrorOccurred())
                throw std::runtime_error("UGLIR Sema: shader frontend parsing failed.");
            // ParseAST has already exited the parser's TU scope. Lazy builtins
            // created by implicit special members still require a live TUScope.
            clang::Scope translationUnitScope(nullptr, clang::Scope::DeclScope, context.getDiagnostics());
            translationUnitScope.setEntity(context.getTranslationUnitDecl());
            llvm::SaveAndRestore<clang::Scope *> scopeBinding(Sema->TUScope, &translationUnitScope);
            Result = UGLC::CodeGen::ShaderEmitter::prepareShaderTranslationUnit(context, *Sema, ArgumentContractsOnly);
        }
        catch (const UGLC::CodeGen::DiagnosticError &error)
        {
            UGLC::CodeGen::reportDiagnosticRecords(context, error.getRecords());
        }
        catch (const std::exception &error)
        {
            if (UGLC::CodeGen::isFormattedClangStyleDiagnostic(error.what()))
            {
                llvm::errs() << error.what() << '\n';
            }
            else
            {
                llvm::WithColor::error(llvm::errs(), "uglc") << error.what() << '\n';
            }
        }
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    bool ArgumentContractsOnly;
    clang::Sema *Sema = nullptr;
};

/** Parses original shader input without attaching host code generation. */
class ShaderSemanticFrontendAction final : public clang::ASTFrontendAction
{
public:
    /** Creates an isolated shader action with an invocation-wide preprocessing time. */
    ShaderSemanticFrontendAction(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result, uint64_t timestamp,
                                 bool argumentContractsOnly)
        : Result(result), Timestamp(timestamp), ArgumentContractsOnly(argumentContractsOnly) {}

    /** Configures deterministic preprocessing and readonly Args only for shader semantic parsing. */
    bool BeginInvocation(clang::CompilerInstance &compiler) override
    {
        compiler.getPreprocessorOpts().SourceDateEpoch = Timestamp;
        compiler.getPreprocessorOpts().addMacroDef(UGLC::CodeGen::mUGLShaderArgsReadonlyMacro);
        return true;
    }

    /** Creates the Sema-aware consumer for this shader AST. */
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<ShaderSemanticConsumer>(Result, ArgumentContractsOnly);
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    uint64_t Timestamp;
    bool ArgumentContractsOnly;
};

/** Creates shader actions that export owned modules into one TU result. */
class ShaderSemanticActionFactory final : public clang::tooling::FrontendActionFactory
{
public:
    /** Retains only the output result and the common preprocessing timestamp. */
    ShaderSemanticActionFactory(UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &result, uint64_t timestamp,
                                bool argumentContractsOnly)
        : Result(result), Timestamp(timestamp), ArgumentContractsOnly(argumentContractsOnly) {}

    /** Creates one independent shader frontend action. */
    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<ShaderSemanticFrontendAction>(Result, Timestamp, ArgumentContractsOnly);
    }

private:
    UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit &Result;
    uint64_t Timestamp;
    bool ArgumentContractsOnly;
};

/** Generates host code after any isolated shader frontend has been destroyed. */
class BaseGeneratorASTConsumer : public clang::ASTConsumer
{
public:
    /** Creates a host generator with optional prepared shader modules. */
    BaseGeneratorASTConsumer(clang::Rewriter *rewriter, clang::ASTContext *context,
                             UGLC::CodeGen::ShaderSourcePipelineOptions options = {})
        : Visitor(rewriter, context, options), Options(options) {}

    /** Rewrites the independent host AST using the prepared shader interface. */
    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        try
        {
            Visitor.collectStaticShaderVariants(context.getTranslationUnitDecl());
            Visitor.TraverseDecl(context.getTranslationUnitDecl());
        }
        catch (const UGLC::CodeGen::DiagnosticError &error)
        {
            UGLC::CodeGen::reportDiagnosticRecords(context, error.getRecords());
        }
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

            llvm::outs() << "generated file: " << path.str() << '\n';
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
// -----------------------------------------------------------------------------
// main – 命令行接口保持不变，但改用 StdThreadPool
// -----------------------------------------------------------------------------

/// Holds the parsed and normalized command-line values shared by every compile phase.
struct CompilerCommandLine
{
    std::string sourcePath;
    std::vector<std::string> includePathsStr;
    std::vector<std::string> dslSourceRootsStr;
    std::vector<std::filesystem::path> includePaths;
    std::vector<std::filesystem::path> dslSourceRoots;
    std::string outputDir;
    bool emitDslSingleHeader = true;
    bool useUGLIRShaderPipeline = true;
    bool argumentContractsOnly = false;
    bool optimizeSPIRV = true;
    std::string emitMetallib;
    std::string metalCompilerExecutable;
    std::string metalCompilerTarget;
    std::optional<std::string> resourceDirOverride;
    std::optional<std::string> sdkRootOverride;
    std::optional<std::string> targetTripleOverride;
    std::unique_ptr<ArtifactPublication> publication;
};

/// Holds the translation-unit discovery result used by the Clang parser and the final merge.
struct TranslationUnitDiscovery
{
    std::string dslSingleHeader;
    std::string canonicalSourcePath;
};

/// Holds the resolved compiler arguments that every translation-unit invocation shares.
struct CompilerEnvironment
{
    std::string clangResourceDir;
    std::optional<std::string> appleSdkRoot;
    std::string targetTriple;
    std::vector<std::string> commonArgs;
};

/// Holds the shared result stores that survive from the compile phase to the merge phase.
struct CompilerOutputs
{
    UGLC::App::SharedResults globalStore;
    UGLC::App::SharedResults uglirShaderDebugStore;
};

/// Builds the configured cxxopts option set shared by the command-line parse.
static cxxopts::Options makeCommandLineOptions()
{
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
        ("emit-argument-contracts-only", "validate constructor Args and execution calls, then export logical contracts without native shader artifacts")
        ("emit-metallib", "embed a precompiled Metal library (on/off)", cxxopts::value<std::string>()->default_value("on"))
        ("metal-compiler", "Apple Metal compiler executable", cxxopts::value<std::string>()->default_value(UGLC_DEFAULT_METAL_COMPILER))
        ("metal-target", "Metal library target triple", cxxopts::value<std::string>()->default_value("air64-apple-macosx15.0.0"))
        ("no-spirv-optimization", "validate and deliver unoptimized direct SPIR-V for explicit comparisons");
    options.add_options()
        ("h,help", "print help");
    return options;
}

/// Deduplicates the path list in place and returns the matching filesystem paths.
static std::vector<std::filesystem::path> normalizePathList(std::vector<std::string> &pathsStr)
{
    std::vector<std::string> normalized;
    normalized.reserve(pathsStr.size());
    for (const auto &path : pathsStr)
    {
        appendUniquePath(normalized, path);
    }
    pathsStr = std::move(normalized);
    std::vector<std::filesystem::path> paths;
    paths.reserve(pathsStr.size());
    for (const auto &path : pathsStr)
    {
        paths.emplace_back(path);
    }
    return paths;
}

/// Parses the command line, validates pipeline flags, and returns the normalized compiler inputs.
static std::optional<CompilerCommandLine> parseCommandLine(int argc, const char **argv)
{
    cxxopts::Options options = makeCommandLineOptions();
    auto argResult = options.parse(argc, argv);

    if (argResult.count("h") != 0)
    {
        llvm::outs() << options.help() << '\n';
        return std::nullopt;
    }

    if (argResult.count("shader-pipeline") > 1)
    {
        throw std::runtime_error("Specify --shader-pipeline only once.");
    }

    const std::string selectedPipeline = argResult.count("shader-pipeline") != 0
        ? argResult["shader-pipeline"].as<std::string>()
        : "uglir";
    if (selectedPipeline != "uglir" && selectedPipeline != "legacy")
    {
        throw std::runtime_error("--shader-pipeline must be uglir or legacy.");
    }
    CompilerCommandLine cmd;
    cmd.useUGLIRShaderPipeline = selectedPipeline == "uglir";
    cmd.argumentContractsOnly = argResult.count("emit-argument-contracts-only") != 0;
    if (cmd.argumentContractsOnly && !cmd.useUGLIRShaderPipeline)
    {
        throw std::runtime_error("--emit-argument-contracts-only requires --shader-pipeline=uglir.");
    }
    if (!cmd.useUGLIRShaderPipeline && !UGLC_ENABLE_LEGACY)
    {
        throw std::runtime_error("Legacy shader pipeline is not built; enable UGLC_ENABLE_LEGACY.");
    }
    if (argResult.count("no-spirv-optimization") != 0 && !cmd.useUGLIRShaderPipeline)
    {
        throw std::runtime_error("--no-spirv-optimization requires --shader-pipeline=uglir.");
    }
    llvm::outs() << "UGLC shader pipeline: " << (cmd.useUGLIRShaderPipeline ? "UGLIR" : "LegacyAST") << '\n';

    cmd.optimizeSPIRV = argResult.count("no-spirv-optimization") == 0;
    cmd.emitMetallib = argResult["emit-metallib"].as<std::string>();
    cmd.metalCompilerExecutable = argResult["metal-compiler"].as<std::string>();
    cmd.metalCompilerTarget = argResult["metal-target"].as<std::string>();
    if (argResult.count("resource-dir") != 0) { cmd.resourceDirOverride = argResult["resource-dir"].as<std::string>(); }
    if (argResult.count("sdkroot") != 0) { cmd.sdkRootOverride = argResult["sdkroot"].as<std::string>(); }
    if (argResult.count("target") != 0) { cmd.targetTripleOverride = argResult["target"].as<std::string>(); }

    cmd.sourcePath = argResult.count("s") != 0 ? argResult["s"].as<std::string>() : std::string {};
    cmd.includePathsStr = argResult.count("I") != 0 ? argResult["I"].as<std::vector<std::string>>() : std::vector<std::string> {};
    cmd.dslSourceRootsStr = argResult.count("dsl-dir") != 0 ? argResult["dsl-dir"].as<std::vector<std::string>>() : std::vector<std::string> {};
    cmd.outputDir = argResult.count("o") != 0 ? argResult["o"].as<std::string>() : std::string {};
    const std::string emitDslSingleHeaderValue = argResult.count("emit-dsl-single-header") != 0 ? argResult["emit-dsl-single-header"].as<std::string>() : std::string("on");
    cmd.emitDslSingleHeader = parseEmitDslSingleHeaderOption(emitDslSingleHeaderValue);
    applyDebugDefaultsFromEnvironment(cmd.sourcePath, cmd.includePathsStr, cmd.dslSourceRootsStr, cmd.outputDir);

    if (cmd.sourcePath.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Missing required option --source/-s. Pass the DSL entry file explicitly, or set UGLC_DEBUG_SOURCE for a local debug shell profile."));
    }
    if (cmd.outputDir.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Missing required option --output/-o. Pass the generated output directory explicitly, or set UGLC_DEBUG_OUTPUT_DIR for a local debug shell profile."));
    }

    cmd.includePaths = normalizePathList(cmd.includePathsStr);
    cmd.dslSourceRoots = normalizePathList(cmd.dslSourceRootsStr);

    if (cmd.useUGLIRShaderPipeline) { cmd.publication = std::make_unique<ArtifactPublication>(cmd.outputDir); }
    return cmd;
}

/// Validates the resolved input paths and creates the output directory; returns false on a directory failure.
static bool validateInputPaths(const CompilerCommandLine &cmd)
{
    if (!std::filesystem::exists(cmd.sourcePath))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Source file does not exist: " + cmd.sourcePath));
    }
    if (!std::filesystem::is_regular_file(cmd.sourcePath))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Source path must be a file: " + cmd.sourcePath));
    }
    for (const auto &includePath : cmd.includePaths)
    {
        if (!std::filesystem::exists(includePath))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Include path does not exist: " + includePath.string()));
        }
        if (!std::filesystem::is_directory(includePath))
        {
            throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Include path must be a directory: " + includePath.string()));
        }
    }
    for (const auto &rootPath : cmd.dslSourceRoots)
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

    for (const auto &includePath : cmd.includePaths)
        llvm::outs() << "UGLC: found include path: " << includePath << '\n';
    for (const auto &rootPath : cmd.dslSourceRoots)
        llvm::outs() << "UGLC: found DSL source root: " << rootPath << '\n';
    llvm::outs() << "UGLC: MSL PixelLocal implementation: framebuffer_fetch" << '\n';
    llvm::outs() << "UGLC: found output path: " << cmd.outputDir << '\n';
    return UGLC::App::ensureOutputDirectory(cmd.outputDir);
}

/// Runs the DSL single-header discovery pass and reports the canonical source path.
static TranslationUnitDiscovery runDiscoveryPass(const CompilerCommandLine &cmd)
{
    UGLC::CodeGen::SingleHeaderGenerator discoverPass;
    discoverPass.init(cmd.sourcePath, cmd.includePaths, cmd.dslSourceRoots);
    discoverPass.create({});
    const std::filesystem::path canonicalSourcePath = std::filesystem::weakly_canonical(cmd.sourcePath);
    llvm::outs() << "found translation unit: " << canonicalSourcePath << '\n';
    return {discoverPass.getResult(), canonicalSourcePath.string()};
}

/// Resolves the Clang resource directory, SDK root, and target triple into shared compile arguments.
static CompilerEnvironment resolveCompilerArguments(const CompilerCommandLine &cmd, const char *argv0)
{
    CompilerEnvironment env;
    env.clangResourceDir = cmd.resourceDirOverride.has_value()
                               ? *cmd.resourceDirOverride
                               : resolveClangResourceDir(argv0);
    if (!std::filesystem::exists(env.clangResourceDir) || !std::filesystem::is_directory(env.clangResourceDir))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Clang resource directory does not exist or is not a directory: " + env.clangResourceDir));
    }

    env.appleSdkRoot = cmd.sdkRootOverride.has_value() ? cmd.sdkRootOverride : detectAppleSdkRoot();
    if (env.appleSdkRoot.has_value()
        && (!std::filesystem::exists(*env.appleSdkRoot) || !std::filesystem::is_directory(*env.appleSdkRoot)))
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("SDK root does not exist or is not a directory: " + *env.appleSdkRoot));
    }

    env.targetTriple = cmd.targetTripleOverride.has_value()
                           ? *cmd.targetTripleOverride
                           : llvm::sys::getDefaultTargetTriple();
    if (env.targetTriple.empty())
    {
        throw std::runtime_error(UGLC::CodeGen::formatUnlocatedDiagnostic("Unable to determine a target triple automatically. Pass --target explicitly."));
    }

    env.commonArgs = {
        "-std=c++20",
        "-Werror=unknown-attributes",
        "-D__STDC_CONSTANT_MACROS",
        "-D__STDC_FORMAT_MACROS",
        "-D__STDC_LIMIT_MACROS",
        ("-resource-dir=" + env.clangResourceDir),
        "-target",
        env.targetTriple,
    };
#if defined(__APPLE__)
    if (env.appleSdkRoot.has_value())
    {
        env.commonArgs.emplace_back("-isysroot");
        env.commonArgs.emplace_back(*env.appleSdkRoot);
    }
    env.commonArgs.emplace_back("-stdlib=libc++");
#endif
    return env;
}

/// Builds the shader-source pipeline options shared by every translation-unit invocation.
static UGLC::CodeGen::ShaderSourcePipelineOptions makeShaderSourcePipelineOptions(const CompilerCommandLine &cmd)
{
    UGLC::CodeGen::ShaderSourcePipelineOptions options;
    if (cmd.emitMetallib != "on" && cmd.emitMetallib != "off")
    {
        throw std::runtime_error("--emit-metallib must be on or off.");
    }
    options.metalCompiler.enabled = cmd.emitMetallib == "on" && !cmd.argumentContractsOnly;
    options.metalCompiler.executable = cmd.metalCompilerExecutable;
    options.metalCompiler.target = cmd.metalCompilerTarget;
    UGLC::CodeGen::validateMetalCompilerOptions(options.metalCompiler);
    options.pipelineKind = cmd.useUGLIRShaderPipeline ? UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR : UGLC::CodeGen::ShaderSourcePipelineKind::LegacyAST;
    if (cmd.useUGLIRShaderPipeline)
    {
        options.optimizeSPIRV = cmd.optimizeSPIRV;
    }
    options.frontendTimestamp = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    return options;
}

// 定义自制 FrontendActionFactory，能够把 GlobalStore 传入 Action
/// Creates host-code frontend actions that share output storage and shader source pipeline selection.
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

/// Runs the thread-pool translation-unit compiles that fill the shared result stores.
static void compileTranslationUnits(
    const CompilerCommandLine &cmd,
    clang::tooling::CommonOptionsParser &optionsParser,
    const CompilerEnvironment &env,
    const UGLC::CodeGen::ShaderSourcePipelineOptions &pipelineOptions,
    CompilerOutputs &outputs)
{
    SharedResultsShaderDebugOutputSink uglirShaderDebugSink(outputs.uglirShaderDebugStore);
    UGLC::CodeGen::ShaderSourcePipelineOptions baseOptions = pipelineOptions;
    if (cmd.useUGLIRShaderPipeline)
    {
        baseOptions.debugOutputSink = &uglirShaderDebugSink;
    }

    const std::vector<std::string> &tuList = optionsParser.getSourcePathList();
    StdThreadPool pool; // 默认硬件并发
    pool.run(tuList, [&](const std::string &TU) {
            auto tuOptions = baseOptions;
            UGLC::CodeGen::ShaderEmitter::PreparedShaderTranslationUnit preparedShaders;
            if (tuOptions.pipelineKind == UGLC::CodeGen::ShaderSourcePipelineKind::UGLIR)
            {
                {
                    clang::tooling::ClangTool shaderTool(optionsParser.getCompilations(), {TU});
                    addCommonAdjusters(shaderTool, env.commonArgs, cmd.includePaths);
                    ShaderSemanticActionFactory shaderFactory(preparedShaders, tuOptions.frontendTimestamp, cmd.argumentContractsOnly);
                    if (shaderTool.run(&shaderFactory) != 0)
                        throw std::runtime_error("Failed to prepare shader translation unit \"" + TU + "\".");
                }
                if (cmd.argumentContractsOnly)
                {
                    outputs.globalStore.store("constructor-arguments.json", UGLC::CodeGen::dumpConstructorArgumentContracts(preparedShaders.argumentContracts));
                    return;
                }
                tuOptions.preparedShaders = &preparedShaders;
                if (tuOptions.debugOutputSink != nullptr)
                {
                    UGLC::CodeGen::ShaderEmitter::storePreparedShaderDebugOutputs(preparedShaders, *tuOptions.debugOutputSink);
                }
            }
            clang::tooling::ClangTool tool(optionsParser.getCompilations(), {TU});
            addCommonAdjusters(tool, env.commonArgs, cmd.includePaths);

            UGLCActionFactory factory(outputs.globalStore, tuOptions);
            if (tool.run(&factory) != 0)
            {
                throw std::runtime_error("Failed to process translation unit \"" + TU + "\".");
            }
        });
}

/// Writes the argument-contract artifact and publishes it; returns whether the write succeeded.
static bool writeArgumentContracts(const CompilerCommandLine &cmd, CompilerOutputs &outputs)
{
    if (!UGLC::App::writeResult(cmd.outputDir + "/constructor-arguments.json", outputs.globalStore.Map.at("constructor-arguments.json")))
    {
        throw std::runtime_error("Failed to write constructor argument contracts.");
    }
    cmd.publication->publish();
    llvm::outs() << "UGLC: constructor argument contracts validated and exported.\n";
    return true;
}

/// Writes the UGLIR debug artifacts and the shader-compilation manifest, then publishes the outputs.
static bool writeShaderCompilationArtifacts(
    const CompilerCommandLine &cmd,
    const CompilerEnvironment &env,
    CompilerOutputs &outputs,
    const UGLC::CodeGen::ShaderSourcePipelineOptions &pipelineOptions,
    nlohmann::json &artifacts,
    int argc, const char **argv)
{
    bool writeSucceeded = true;
    for (const auto &[relativePath, content] : outputs.uglirShaderDebugStore.Map)
    {
        writeSucceeded = UGLC::App::writeResult(std::filesystem::path(cmd.outputDir) / relativePath, content) && writeSucceeded;
        artifacts.push_back(relativePath);
    }
    const nlohmann::json compilation = {
        {"schemaVersion", 1}, {"pipeline", "UGLIR"},
        {"defaultPipeline", "UGLIR"}, {"fallbackUsed", false},
        {"source", cmd.sourcePath},
        {"clangVersion", clang::getClangFullVersion()}, {"spirvToolsVersion", spvSoftwareVersionDetailsString()},
        {"targetTriple", env.targetTriple}, {"spirvTargetEnvironment", "vulkan1.2"},
        {"spirvOptimization", pipelineOptions.optimizeSPIRV ? "runtime" : "disabled"},
        {"embeddedMetallib", pipelineOptions.metalCompiler.enabled},
        {"metalCompiler", pipelineOptions.metalCompiler.executable},
        {"metalTarget", pipelineOptions.metalCompiler.target},
        {"arguments", std::vector<std::string>(argv + 1, argv + argc)},
        {"artifacts", artifacts},
    };
    writeSucceeded = UGLC::App::writeResult(std::filesystem::path(cmd.outputDir) / "shader-compilation.json", compilation.dump(2) + "\n") && writeSucceeded;
    if (writeSucceeded)
    {
        cmd.publication->publish();
    }
    return writeSucceeded;
}

/// Merges the shared results and writes the generated headers, debug artifacts, and compilation manifest.
static bool writeGeneratedOutputs(
    const CompilerCommandLine &cmd,
    const TranslationUnitDiscovery &discovery,
    const CompilerEnvironment &env,
    CompilerOutputs &outputs,
    const UGLC::CodeGen::ShaderSourcePipelineOptions &pipelineOptions,
    int argc, const char **argv)
{
    UGLC::CodeGen::SingleHeaderGenerator generator;
    generator.init(cmd.sourcePath, cmd.includePaths, cmd.dslSourceRoots);
    generator.create(outputs.globalStore.Map);
    bool writeSucceeded = true;
    nlohmann::json artifacts = nlohmann::json::array();

    if (cmd.emitDslSingleHeader)
    {
        writeSucceeded = UGLC::App::writeResult(cmd.outputDir + "/dsl_single_header.hpp", discovery.dslSingleHeader) && writeSucceeded;
    }
    {

        std::string finalHeader;
        finalHeader += "#pragma once\n";
        finalHeader += "// clang-format off\n";
        finalHeader += "#include <cstdint>\n";
        finalHeader += "#include <GVMRHI/GVMRHI.hpp>\n";
        finalHeader += "#include <EASTL/array.h>\n";
        finalHeader += "#include <GVMCore/Private/GVMCore.Private.hpp>\n";
        finalHeader += "#include <GVMCore/GVMCore.Public.hpp>\n";
        finalHeader += "#include \"exports.hpp\"\n";

        finalHeader += "using namespace GVM::Core::Math;\n";
        finalHeader += UGLC::CodeGen::MakeGeneratedShaderArtifactSupport();
        finalHeader += MakeHostShaderOnlyStubs();
        for (const auto &shaderPrelude : UGLC::CodeGen::collectRegisteredShaderBackendPreludes())
        {
            // Keep backend shader preludes centralized in the registry so final
            // single-header assembly does not need to know which concrete
            // backends are currently participating in code generation.
            finalHeader += "static const eastl::string " + shaderPrelude.variableName + " = R\"(" + shaderPrelude.sourceText + ")\";\n";
        }
        finalHeader += generator.getResult();

        writeSucceeded = UGLC::App::writeResult(cmd.outputDir + "/generate_result.hpp", finalHeader) && writeSucceeded;
        if (cmd.useUGLIRShaderPipeline) { artifacts.push_back("generate_result.hpp"); }
    }
    {
        std::string finalHeader;
        finalHeader += "#ifndef UGL_EXPORT_HPP\n";
        finalHeader += "#define UGL_EXPORT_HPP\n";
        finalHeader += generator.getExportResult();
        finalHeader += "\n#endif\n";
        writeSucceeded = UGLC::App::writeResult(cmd.outputDir + "/exports.hpp", finalHeader) && writeSucceeded;
        if (cmd.useUGLIRShaderPipeline) { artifacts.push_back("exports.hpp"); }
    }
    if (cmd.useUGLIRShaderPipeline)
    {
        writeSucceeded = writeShaderCompilationArtifacts(cmd, env, outputs, pipelineOptions, artifacts, argc, argv) && writeSucceeded;
    }
    return writeSucceeded;
}

int main(int argc, const char **argv)
{
    try
    {
        llvm::outs() << "====================UGLC executes started=====================================\n";

        std::optional<CompilerCommandLine> parsedCommandLine = parseCommandLine(argc, argv);
        if (!parsedCommandLine.has_value())
        {
            return 0;
        }
        CompilerCommandLine &cmd = *parsedCommandLine;
        if (!validateInputPaths(cmd))
        {
            return 1;
        }

        const TranslationUnitDiscovery discovery = runDiscoveryPass(cmd);
        std::vector<std::string> inputFiles = {discovery.canonicalSourcePath};
        std::vector<const char *> newArgv = {"", inputFiles.front().c_str()};

        int newArgc = static_cast<int>(newArgv.size());
        llvm::cl::OptionCategory category("UGLC options");
        auto expectedParser = clang::tooling::CommonOptionsParser::create(newArgc, newArgv.data(), category);
        if (!expectedParser)
        {
            llvm::errs() << expectedParser.takeError();
            return 1;
        }
        clang::tooling::CommonOptionsParser &optionsParser = expectedParser.get();

        const CompilerEnvironment env = resolveCompilerArguments(cmd, argv[0]);
        const UGLC::CodeGen::ShaderSourcePipelineOptions pipelineOptions = makeShaderSourcePipelineOptions(cmd);
        CompilerOutputs outputs;
        compileTranslationUnits(cmd, optionsParser, env, pipelineOptions, outputs);

        if (cmd.argumentContractsOnly)
        {
            return writeArgumentContracts(cmd, outputs) ? 0 : 1;
        }
        const bool writeSucceeded = writeGeneratedOutputs(cmd, discovery, env, outputs, pipelineOptions, argc, argv);
        llvm::outs() << "====================UGLC executes ended=====================================\n";
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
