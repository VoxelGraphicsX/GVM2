#include "DxcShaderBinaryCompiler.hpp"

#include <CodeGen/ShaderCompilerRegistry.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifndef UGLC_ENABLE_LEGACY
#define UGLC_ENABLE_LEGACY 0
#endif

#if UGLC_ENABLE_LEGACY
#include <dxc/WinAdapter.h>
#include <dxc/dxcapi.h>
#endif

namespace UGLC::CodeGen::HLSL
{
#if UGLC_ENABLE_LEGACY
    namespace
    {
        namespace Spirv
        {
            constexpr uint32_t kMagicNumber = 0x07230203u;
            constexpr uint16_t kOpEntryPoint = 15u;
            constexpr uint16_t kOpCapability = 17u;
            constexpr uint16_t kOpDecorate = 71u;

            constexpr uint32_t kExecutionModelTessellationControl = 1u;
            constexpr uint32_t kExecutionModelTessellationEvaluation = 2u;
            constexpr uint32_t kExecutionModelGeometry = 3u;
            constexpr uint32_t kExecutionModelFragment = 4u;

            constexpr uint32_t kCapabilityGeometry = 2u;
            constexpr uint32_t kCapabilityTessellation = 3u;

            constexpr uint32_t kDecorationBuiltIn = 11u;
            constexpr uint32_t kBuiltInPrimitiveId = 7u;
        } // namespace Spirv

        template <typename T>
        struct ComReleaser
        {
            void operator()(T *value) const
            {
                if (value != nullptr)
                {
                    value->Release();
                }
            }
        };

        template <typename T>
        using ComPtr = std::unique_ptr<T, ComReleaser<T>>;

        std::wstring widenUtf8(const std::string &input)
        {
            return std::wstring(input.begin(), input.end());
        }

        std::wstring getTargetProfile(ShaderStageKind stage)
        {
            switch (stage)
            {
            case ShaderStageKind::Vertex:
                return L"vs_6_2";
            case ShaderStageKind::Fragment:
                return L"ps_6_2";
            case ShaderStageKind::Compute:
                return L"cs_6_2";
            case ShaderStageKind::Hull:
            case ShaderStageKind::Domain:
                break;
            }
            throw std::runtime_error("DXC compiler service does not support shader stage \"" + std::string(getShaderStageDisplayName(stage)) + "\" yet.");
        }

        std::wstring getTargetEnvironment()
        {
            // DXC's SPIR-V backend defaults to vulkan1.0. Wave intrinsics require
            // Vulkan 1.1 / SPIR-V 1.3, so UGLC needs to opt in explicitly.
            return L"vulkan1.1";
        }

        std::wstring getHlslLanguageVersion()
        {
            // DXC v1.8.2505.1 accepts 202x as the newest legal -HV value. It is
            // still the compiler's forward-looking placeholder track, but it is
            // the latest language mode currently accepted by the pinned toolchain.
            return L"202x";
        }

        void appendUtf8CodePoint(std::string &result, uint32_t codePoint)
        {
            if ((codePoint >= 0xD800u && codePoint <= 0xDFFFu) || codePoint > 0x10FFFFu)
            {
                result.push_back('?');
                return;
            }

            if (codePoint <= 0x7Fu)
            {
                result.push_back(static_cast<char>(codePoint));
                return;
            }
            if (codePoint <= 0x7FFu)
            {
                result.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
                result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
                return;
            }
            if (codePoint <= 0xFFFFu)
            {
                result.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
                result.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
                result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
                return;
            }

            result.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
            result.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        }

        std::string decodeUtf16BlobToUtf8(const void *bufferPointer, size_t bufferSize)
        {
            if (bufferPointer == nullptr || bufferSize == 0)
            {
                return {};
            }

            const size_t unitCount = bufferSize / sizeof(char16_t);
            const auto *bytes = static_cast<const std::byte *>(bufferPointer);
            std::string result;
            result.reserve(unitCount);

            for (size_t i = 0; i < unitCount; ++i)
            {
                char16_t unit = 0;
                std::memcpy(&unit, bytes + (i * sizeof(char16_t)), sizeof(char16_t));
                if (unit == 0)
                {
                    break;
                }

                if (unit >= 0xD800u && unit <= 0xDBFFu && (i + 1) < unitCount)
                {
                    char16_t lowSurrogate = 0;
                    std::memcpy(&lowSurrogate, bytes + ((i + 1) * sizeof(char16_t)), sizeof(char16_t));
                    if (lowSurrogate >= 0xDC00u && lowSurrogate <= 0xDFFFu)
                    {
                        const uint32_t codePoint = 0x10000u + (((static_cast<uint32_t>(unit) - 0xD800u) << 10)
                                                                | (static_cast<uint32_t>(lowSurrogate) - 0xDC00u));
                        appendUtf8CodePoint(result, codePoint);
                        ++i;
                        continue;
                    }
                }

                appendUtf8CodePoint(result, static_cast<uint32_t>(unit));
            }

            return result;
        }

        std::string decodeUtf32BlobToUtf8(const void *bufferPointer, size_t bufferSize)
        {
            if (bufferPointer == nullptr || bufferSize == 0)
            {
                return {};
            }

            const size_t unitCount = bufferSize / sizeof(char32_t);
            const auto *bytes = static_cast<const std::byte *>(bufferPointer);
            std::string result;
            result.reserve(unitCount);

            for (size_t i = 0; i < unitCount; ++i)
            {
                char32_t unit = 0;
                std::memcpy(&unit, bytes + (i * sizeof(char32_t)), sizeof(char32_t));
                if (unit == 0)
                {
                    break;
                }
                appendUtf8CodePoint(result, static_cast<uint32_t>(unit));
            }

            return result;
        }

        std::string decodeTextBlobToUtf8(IDxcBlobEncoding *blob)
        {
            if (blob == nullptr)
            {
                return {};
            }

            const void *bufferPointer = blob->GetBufferPointer();
            const size_t bufferSize = blob->GetBufferSize();
            if (bufferPointer == nullptr || bufferSize == 0)
            {
                return {};
            }

            BOOL encodingKnown = FALSE;
            UINT32 codePage = DXC_CP_ACP;
            if (FAILED(blob->GetEncoding(&encodingKnown, &codePage)))
            {
                encodingKnown = FALSE;
                codePage = DXC_CP_ACP;
            }

            if (!encodingKnown || codePage == DXC_CP_UTF8 || codePage == DXC_CP_ACP)
            {
                const auto *chars = static_cast<const char *>(bufferPointer);
                size_t length = bufferSize;
                while (length > 0 && chars[length - 1] == '\0')
                {
                    --length;
                }
                return std::string(chars, length);
            }

            if (codePage == DXC_CP_UTF16)
            {
                return decodeUtf16BlobToUtf8(bufferPointer, bufferSize);
            }

            if (codePage == DXC_CP_UTF32 || codePage == DXC_CP_WIDE)
            {
                return decodeUtf32BlobToUtf8(bufferPointer, bufferSize);
            }

            const auto *chars = static_cast<const char *>(bufferPointer);
            size_t length = bufferSize;
            while (length > 0 && chars[length - 1] == '\0')
            {
                --length;
            }
            return std::string(chars, length);
        }

        std::string collectDxcErrors(IDxcResult *result)
        {
            if (result == nullptr)
            {
                return {};
            }

            IDxcBlobEncoding *rawErrors = nullptr;
            if (FAILED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&rawErrors), nullptr)) || rawErrors == nullptr)
            {
                return {};
            }

            ComPtr<IDxcBlobEncoding> errors(rawErrors);
            return decodeTextBlobToUtf8(errors.get());
        }

        struct PrimitiveIdCapabilityPatchInfo
        {
            bool validModule = false;
            bool sawFragmentEntryPoint = false;
            bool sawNonFragmentEntryPoint = false;
            bool usesPrimitiveIdBuiltIn = false;
            bool hasGeometryExecutionModel = false;
            bool hasTessellationExecutionModel = false;
            bool hasTessellationCapability = false;
            std::vector<size_t> geometryCapabilityInstructionOffsets;
        };

        PrimitiveIdCapabilityPatchInfo analyzePrimitiveIdCapabilityPatchEligibility(const std::vector<uint32_t> &spirvWords)
        {
            PrimitiveIdCapabilityPatchInfo info;
            if (spirvWords.size() < 5 || spirvWords.front() != Spirv::kMagicNumber)
            {
                return info;
            }

            size_t wordOffset = 5;
            while (wordOffset < spirvWords.size())
            {
                const uint32_t instructionHeader = spirvWords[wordOffset];
                const uint16_t wordCount = static_cast<uint16_t>(instructionHeader >> 16u);
                const uint16_t opcode = static_cast<uint16_t>(instructionHeader & 0xFFFFu);
                if (wordCount == 0u || (wordOffset + wordCount) > spirvWords.size())
                {
                    return info;
                }

                switch (opcode)
                {
                case Spirv::kOpEntryPoint:
                    if (wordCount >= 2u)
                    {
                        const uint32_t executionModel = spirvWords[wordOffset + 1];
                        if (executionModel == Spirv::kExecutionModelFragment)
                        {
                            info.sawFragmentEntryPoint = true;
                        }
                        else
                        {
                            info.sawNonFragmentEntryPoint = true;
                        }

                        if (executionModel == Spirv::kExecutionModelGeometry)
                        {
                            info.hasGeometryExecutionModel = true;
                        }
                        if (executionModel == Spirv::kExecutionModelTessellationControl ||
                            executionModel == Spirv::kExecutionModelTessellationEvaluation)
                        {
                            info.hasTessellationExecutionModel = true;
                        }
                    }
                    break;
                case Spirv::kOpCapability:
                    if (wordCount >= 2u)
                    {
                        const uint32_t capability = spirvWords[wordOffset + 1];
                        if (capability == Spirv::kCapabilityGeometry)
                        {
                            info.geometryCapabilityInstructionOffsets.push_back(wordOffset);
                        }
                        else if (capability == Spirv::kCapabilityTessellation)
                        {
                            info.hasTessellationCapability = true;
                        }
                    }
                    break;
                case Spirv::kOpDecorate:
                    if (wordCount >= 4u)
                    {
                        const uint32_t decoration = spirvWords[wordOffset + 2];
                        if (decoration == Spirv::kDecorationBuiltIn &&
                            spirvWords[wordOffset + 3] == Spirv::kBuiltInPrimitiveId)
                        {
                            info.usesPrimitiveIdBuiltIn = true;
                        }
                    }
                    break;
                default:
                    break;
                }

                wordOffset += wordCount;
            }

            info.validModule = true;
            return info;
        }

        bool patchFragmentPrimitiveIdCapabilityToTessellation(const EmittedShaderSource &source,
                                                              std::vector<uint32_t> &spirvWords)
        {
            if (source.stage != ShaderStageKind::Fragment)
            {
                return false;
            }

            PrimitiveIdCapabilityPatchInfo info = analyzePrimitiveIdCapabilityPatchEligibility(spirvWords);
            if (!info.validModule ||
                !info.sawFragmentEntryPoint ||
                info.sawNonFragmentEntryPoint ||
                !info.usesPrimitiveIdBuiltIn ||
                info.geometryCapabilityInstructionOffsets.empty() ||
                info.hasGeometryExecutionModel ||
                info.hasTessellationExecutionModel)
            {
                return false;
            }

            // Vulkan allows fragment PrimitiveId to satisfy SPIR-V's capability
            // requirement through Tessellation. DXC currently emits Geometry for
            // standalone PS+SV_PrimitiveID modules, which over-constrains devices
            // that do not expose geometryShader. Re-target the capability while
            // keeping the rest of the module byte-for-byte stable.
            const size_t firstGeometryCapabilityOffset = info.geometryCapabilityInstructionOffsets.front();
            if (!info.hasTessellationCapability)
            {
                spirvWords[firstGeometryCapabilityOffset + 1] = Spirv::kCapabilityTessellation;
            }

            if (info.geometryCapabilityInstructionOffsets.size() > 1u || info.hasTessellationCapability)
            {
                for (size_t i = info.geometryCapabilityInstructionOffsets.size(); i > 0; --i)
                {
                    const size_t instructionOffset = info.geometryCapabilityInstructionOffsets[i - 1];
                    if (instructionOffset == firstGeometryCapabilityOffset && !info.hasTessellationCapability)
                    {
                        continue;
                    }
                    spirvWords.erase(spirvWords.begin() + static_cast<std::ptrdiff_t>(instructionOffset),
                                     spirvWords.begin() + static_cast<std::ptrdiff_t>(instructionOffset + 2));
                }
            }

            return true;
        }
    } // namespace
#endif

    ShaderCompileResult DxcShaderBinaryCompiler::compileToSpirv(const EmittedShaderSource &source)
    {
        ShaderCompileDiagnostics diagnostics;
        diagnostics.success = false;
        diagnostics.backend = source.backend;
        diagnostics.stage = source.stage;
        diagnostics.backendName = source.backendName;
        diagnostics.stageName = source.stageName.empty() ? getShaderStageDisplayName(source.stage) : source.stageName;
        diagnostics.entryPoint = source.entryPoint;
        diagnostics.sourceName = source.sourceName;

#if !UGLC_ENABLE_LEGACY
        diagnostics.compilerOutput = "DXC compiler service was compiled out of this UGLC binary.";
        return {
            .binary = std::nullopt,
            .diagnostics = std::move(diagnostics),
        };
#else
        const std::string fullSource = source.preludeText.empty() ? source.sourceText : (source.preludeText + "\n" + source.sourceText);
        const std::wstring sourceNameWide = widenUtf8(source.sourceName.empty() ? source.debugName : source.sourceName);
        const std::wstring entryPointWide = widenUtf8(source.entryPoint);
        const std::wstring targetProfileWide = getTargetProfile(source.stage);
        const std::wstring targetEnvironmentWide = getTargetEnvironment();
        const std::wstring hlslLanguageVersionWide = getHlslLanguageVersion();

        IDxcUtils *rawUtils = nullptr;
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&rawUtils))) || rawUtils == nullptr)
        {
            diagnostics.compilerOutput = "Failed to create IDxcUtils.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }
        ComPtr<IDxcUtils> utils(rawUtils);

        IDxcCompiler3 *rawCompiler = nullptr;
        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&rawCompiler))) || rawCompiler == nullptr)
        {
            diagnostics.compilerOutput = "Failed to create IDxcCompiler3.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }
        ComPtr<IDxcCompiler3> compiler(rawCompiler);

        DxcBuffer sourceBuffer{};
        sourceBuffer.Ptr = fullSource.data();
        sourceBuffer.Size = fullSource.size();
        sourceBuffer.Encoding = DXC_CP_UTF8;

        std::vector<std::wstring> argumentsStorage;
        argumentsStorage.emplace_back(L"-spirv");
        argumentsStorage.emplace_back(L"-E");
        argumentsStorage.emplace_back(entryPointWide);
        argumentsStorage.emplace_back(L"-T");
        argumentsStorage.emplace_back(targetProfileWide);
        argumentsStorage.emplace_back(L"-fspv-target-env=");
        argumentsStorage.back() += targetEnvironmentWide;
        argumentsStorage.emplace_back(L"-encoding");
        argumentsStorage.emplace_back(L"utf8");
        argumentsStorage.emplace_back(L"-HV");
        argumentsStorage.emplace_back(hlslLanguageVersionWide);
        argumentsStorage.emplace_back(L"-enable-16bit-types");
        argumentsStorage.emplace_back(L"-Zi");
        argumentsStorage.emplace_back(L"-Qembed_debug");

        std::vector<LPCWSTR> arguments;
        arguments.reserve(argumentsStorage.size());
        for (const auto &argument : argumentsStorage)
        {
            arguments.push_back(argument.c_str());
        }

        IDxcResult *rawResult = nullptr;
        if (FAILED(compiler->Compile(&sourceBuffer,
                                     arguments.data(),
                                     static_cast<uint32_t>(arguments.size()),
                                     nullptr,
                                     IID_PPV_ARGS(&rawResult))) ||
            rawResult == nullptr)
        {
            diagnostics.compilerOutput = "IDxcCompiler3::Compile failed before producing a result object.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }
        ComPtr<IDxcResult> result(rawResult);

        diagnostics.compilerOutput = collectDxcErrors(result.get());

        HRESULT compileStatus = S_OK;
        if (FAILED(result->GetStatus(&compileStatus)))
        {
            diagnostics.compilerOutput += diagnostics.compilerOutput.empty() ? "" : "\n";
            diagnostics.compilerOutput += "DXC result did not expose a compile status.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }

        if (FAILED(compileStatus))
        {
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }

        IDxcBlob *rawObject = nullptr;
        if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&rawObject), nullptr)) || rawObject == nullptr)
        {
            diagnostics.compilerOutput += diagnostics.compilerOutput.empty() ? "" : "\n";
            diagnostics.compilerOutput += "DXC succeeded but did not return a SPIR-V object blob.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }
        ComPtr<IDxcBlob> objectBlob(rawObject);

        const size_t objectSize = objectBlob->GetBufferSize();
        if (objectSize == 0)
        {
            diagnostics.compilerOutput += diagnostics.compilerOutput.empty() ? "" : "\n";
            diagnostics.compilerOutput += "DXC returned an empty SPIR-V object blob.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }
        if ((objectSize % sizeof(uint32_t)) != 0)
        {
            diagnostics.compilerOutput += diagnostics.compilerOutput.empty() ? "" : "\n";
            diagnostics.compilerOutput += "DXC returned a SPIR-V blob whose byte size is not divisible by 4.";
            return {
                .binary = std::nullopt,
                .diagnostics = std::move(diagnostics),
            };
        }

        CompiledShaderBinary binary;
        binary.backend = source.backend;
        binary.stage = source.stage;
        binary.backendName = source.backendName;
        binary.entryPoint = source.entryPoint;
        binary.sourceName = source.sourceName;
        binary.spirvWords.resize(objectSize / sizeof(uint32_t));
        std::memcpy(binary.spirvWords.data(), objectBlob->GetBufferPointer(), objectSize);
        const bool patchedPrimitiveIdCapability = patchFragmentPrimitiveIdCapabilityToTessellation(source, binary.spirvWords);

        diagnostics.success = true;
        if (diagnostics.compilerOutput.empty())
        {
            diagnostics.compilerOutput = "DXC compiled HLSL to SPIR-V successfully.";
        }
        if (patchedPrimitiveIdCapability)
        {
            diagnostics.compilerOutput += diagnostics.compilerOutput.empty() ? "" : "\n";
            diagnostics.compilerOutput += "UGLC patched fragment PrimitiveId capability from Geometry to Tessellation for Vulkan compatibility.";
        }

        return {
            .binary = std::move(binary),
            .diagnostics = std::move(diagnostics),
        };
#endif
    }
} // namespace UGLC::CodeGen::HLSL
