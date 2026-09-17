#include "CPPShaderArtifactEmitter.hpp"

#include "CPPVisitor.hpp"

#include <CodeGen/CodeWriter.hpp>
#include <CodeGen/DSLReservedIdentifierValidator.hpp>
#if UGLC_ENABLE_LEGACY
#include <CodeGen/Legacy/HLSL/HLSLTypeConvertor.hpp>
#endif
#include <CodeGen/ShaderBackendRegistry.hpp>
#include <CodeGen/ShaderBufferLayoutValidator.hpp>
#include <CodeGen/ShaderCompilerRegistry.hpp>
#include <CodeGen/ShaderReferenceVisitor.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>

#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

#ifndef UGLC_ENABLE_LEGACY
#define UGLC_ENABLE_LEGACY 0
#endif

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        std::string formatSpirvWordLiteral(uint32_t word)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << word;
            return stream.str();
        }

#if UGLC_ENABLE_LEGACY
        std::string getHlslBindGroupResourceGlobalName(const std::string &bindGroupName, const std::string &resourceName)
        {
            return bindGroupName + "_" + resourceName;
        }

        void validateGeneratedHlslStorageTextureDeclarations(BaseASTVisitor &visitor,
                                                             const BindGroupInfoMap &bindGroupInfoMap,
                                                             const EmittedShaderSource &hlslShader)
        {
            UGLC::CodeGen::HLSL::HLSLTypeConvertor hlslTypeConvertor;

            for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
            {
                (void)slotIndex;
                for (const auto &resourceBinding : bindGroupInfo.resourceBindings)
                {
                    if (resourceBinding.kind != BaseShaderResourceKind::StorageTexture)
                    {
                        continue;
                    }

                    if (resourceBinding.fieldDecl == nullptr)
                    {
                        throw std::runtime_error("Storage texture binding metadata is missing its field declaration while validating generated HLSL.");
                    }

                    const std::string formatName = resourceBinding.elementTypeName.empty()
                                                       ? visitor.generateTypeCanonicalName(resourceBinding.elementType)
                                                       : resourceBinding.elementTypeName;
                    const std::string imageFormat = UGLC::CodeGen::HLSL::MakeVulkanImageFormatForStorageTexture(formatName);
                    const std::string typeName = visitor.generateTypeCanonicalName(resourceBinding.fieldDecl->getType(), &hlslTypeConvertor);
                    const std::string expectedDeclaration = "[[vk::image_format(\"" + imageFormat + "\")]] "
                                                            + typeName + " "
                                                            + getHlslBindGroupResourceGlobalName(bindGroupInfo.name, resourceBinding.fieldDecl->getNameAsString())
                                                            + " : register(u" + std::to_string(resourceBinding.bindingIndex)
                                                            + ", space" + std::to_string(bindGroupInfo.bindingIndex) + ");";

                    if (hlslShader.sourceText.find(expectedDeclaration) != std::string::npos)
                    {
                        continue;
                    }

                    throw std::runtime_error("Generated HLSL is missing the expected Vulkan storage texture declaration for BindGroup \""
                                             + bindGroupInfo.name + "\" field \"" + resourceBinding.fieldDecl->getNameAsString()
                                             + "\". Expected declaration: " + expectedDeclaration);
                }
            }
        }

        std::string formatShaderCompileFailureMessage(const ShaderCompileDiagnostics &diagnostics)
        {
            std::ostringstream stream;
            stream << diagnostics.backendName << " compilation failed";
            if (!diagnostics.stageName.empty())
            {
                stream << " for " << diagnostics.stageName;
            }
            if (!diagnostics.entryPoint.empty())
            {
                stream << " entry \"" << diagnostics.entryPoint << "\"";
            }
            if (!diagnostics.sourceName.empty())
            {
                stream << " in \"" << diagnostics.sourceName << "\"";
            }
            stream << ".";
            if (!diagnostics.compilerOutput.empty())
            {
                stream << "\n" << diagnostics.compilerOutput;
            }
            return stream.str();
        }
#endif
    } // namespace

    CPPShaderArtifactEmitter::CPPShaderArtifactEmitter(CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::string CPPShaderArtifactEmitter::buildEmbeddedShaderArtifactMember(const std::string &memberName,
                                                                            const std::string &shaderHeaderVariableName,
                                                                            const clang::CXXRecordDecl *shaderClassDecl,
                                                                            const clang::FunctionDecl *entryFunction,
                                                                            const BindGroupInfoMap &bindGroupInfoMap,
                                                                            const std::vector<clang::Decl *> &extraDecls,
                                                                            UGLC::CodeGen::IShaderSourceEmitter &shaderSourceEmitter,
                                                                            const clang::ClassTemplateSpecializationDecl *templateSpecialization)
    {
        const bool usesDirectSpirv = mVisitor.mShaderSourcePipelineOptions.pipelineKind == ShaderSourcePipelineKind::UGLIR;
        std::vector<const clang::Decl *> shaderDeclarations;
        if (!usesDirectSpirv)
        {
            ShaderReferenceVisitor refVisitor(mVisitor.Context);
            refVisitor.solveReference(entryFunction, bindGroupInfoMap, extraDecls, templateSpecialization);
            shaderDeclarations = refVisitor.getRefResult();
        }
        validateShaderDSLReservedBindingIdentifiersOrThrow(mVisitor, shaderClassDecl, bindGroupInfoMap);
        if (!usesDirectSpirv)
        {
            validateShaderDSLReservedIdentifiersOrThrow(mVisitor, shaderDeclarations);
        }

        if (usesDirectSpirv)
        {
            // The direct emitter must preserve the layout rejection performed by the legacy MSL visitor.
            validateMSLShaderBufferLayouts(mVisitor, bindGroupInfoMap);
        }

        CodeWriter writer;
        // Keep one shader stage's generated artifacts bundled together so the
        // current MSL runtime path and future SPIR-V-carrying paths both hang
        // off the same generated-class contract.
        const EmittedShaderSource emittedShader = shaderSourceEmitter.generateShader(shaderDeclarations, bindGroupInfoMap, shaderClassDecl, entryFunction, templateSpecialization);

#if UGLC_ENABLE_LEGACY
        std::optional<EmittedShaderSource> hlslShader;
#endif
        std::optional<CompiledShaderBinary> spirvBinary;
        if (usesDirectSpirv)
        {
            auto spirvEmitter = UGLC::CodeGen::createShaderBinaryEmitterForBackend(ShaderBackendKind::HLSLSPIRV, mVisitor.Context, mVisitor.mShaderSourcePipelineOptions);
            if (!spirvEmitter)
            {
                mVisitor.throwCodegenError("UGLIR SPIR-V emitter: UGLIR shader pipeline requires the direct SPIR-V writer.");
            }
            spirvBinary = spirvEmitter->generateShaderBinary(shaderDeclarations, bindGroupInfoMap, shaderClassDecl, entryFunction, templateSpecialization);
            if (!spirvBinary.has_value() || spirvBinary->spirvWords.empty())
            {
                mVisitor.throwCodegenError("UGLIR SPIR-V emitter: direct SPIR-V writer produced an empty SPIR-V module.");
            }
        }
#if UGLC_ENABLE_LEGACY
        if (!usesDirectSpirv)
        {
            if (auto hlslEmitter = UGLC::CodeGen::createShaderEmitterForBackend(ShaderBackendKind::HLSLSPIRV, mVisitor.Context, mVisitor.mShaderSourcePipelineOptions))
            {
                hlslShader = hlslEmitter->generateShader(shaderDeclarations, bindGroupInfoMap, shaderClassDecl, entryFunction, templateSpecialization);
                validateGeneratedHlslStorageTextureDeclarations(mVisitor, bindGroupInfoMap, *hlslShader);
                if (auto spirvEmitter = UGLC::CodeGen::createShaderBinaryEmitterForBackend(ShaderBackendKind::HLSLSPIRV, mVisitor.Context, mVisitor.mShaderSourcePipelineOptions))
                {
                    spirvBinary = spirvEmitter->generateShaderBinary(shaderDeclarations, bindGroupInfoMap, shaderClassDecl, entryFunction, templateSpecialization);
                }
                else if (auto spirvCompiler = createSpirvCompilerForBackend(ShaderBackendKind::HLSLSPIRV))
                {
                    ShaderCompileResult compileResult = spirvCompiler->compileToSpirv(*hlslShader);
                    if (!compileResult.succeeded())
                    {
                        const ShaderCompilerFeatureFlags compilerFeatures = getShaderCompilerFeatureFlags();
                        if (compilerFeatures.hasDxcCompilerService)
                        {
                            mVisitor.throwCodegenError(formatShaderCompileFailureMessage(compileResult.diagnostics));
                        }
                    }
                    else
                    {
                        spirvBinary = std::move(compileResult.binary);
                    }
                }
            }
        }
#endif

        std::optional<std::string> spirvArrayName;
        if (spirvBinary.has_value() && !spirvBinary->spirvWords.empty())
        {
            spirvArrayName = memberName + "_SpirvWords";
            writer.appendLine(mVisitor.mSpaceManager.getSpace() + "static constexpr uint32_t " + *spirvArrayName + "[] = {");
            std::string currentLine = mVisitor.mSpaceManager.getSpace() + "    ";
            for (size_t i = 0; i < spirvBinary->spirvWords.size(); ++i)
            {
                currentLine += formatSpirvWordLiteral(spirvBinary->spirvWords[i]);
                if (i + 1 < spirvBinary->spirvWords.size())
                {
                    currentLine += ", ";
                }
                if (((i + 1) % 8) == 0 || i + 1 == spirvBinary->spirvWords.size())
                {
                    writer.appendLine(currentLine);
                    currentLine = mVisitor.mSpaceManager.getSpace() + "    ";
                }
            }
            writer.appendLine(mVisitor.mSpaceManager.getSpace() + "};");
        }

        writer.appendLine(mVisitor.mSpaceManager.getSpace() + "const UGLC::Generated::ShaderArtifact " + memberName + " = UGLC::Generated::MakeShaderArtifact(");
        writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    " + shaderHeaderVariableName + " + R\"(");
        writer.appendRaw(emittedShader.sourceText);
        writer.appendLine(")\",");
        if (usesDirectSpirv)
        {
            writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    eastl::string{},");
        }
#if UGLC_ENABLE_LEGACY
        else
        {
            writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    R\"(");
            if (hlslShader.has_value())
            {
                if (!hlslShader->preludeText.empty())
                {
                    writer.appendRaw(hlslShader->preludeText);
                    writer.appendRaw("\n");
                }
                writer.appendRaw(hlslShader->sourceText);
            }
            writer.appendLine(")\",");
        }
#else
        else
        {
            writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    eastl::string{},");
        }
#endif
        writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    " + (spirvArrayName.has_value() ? *spirvArrayName : std::string("nullptr")) + ",");
        writer.appendLine(mVisitor.mSpaceManager.getSpace() + "    " + (spirvBinary.has_value() ? std::to_string(spirvBinary->spirvWords.size()) : std::string("0")));
        writer.appendStatement(mVisitor.mSpaceManager.getSpace(), ")");
        return writer.take();
    }
} // namespace UGLC::CodeGen::CPP
