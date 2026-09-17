#include "PreparedShaderTranslationUnit.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <CodeGen/UGLIR/UGLIRLowering.hpp>
#include <CodeGen/UGLIR/UGLIRSourceVerifier.hpp>
#include <CodeGen/UGLIR/UGLIRVerifier.hpp>
#include <CodeGen/UGLIR/UGLIRDump.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/SHA256.h>
#include <stdexcept>

namespace UGLC::CodeGen::ShaderEmitter
{
    namespace
    {
        /** Completes record and array interface types without instantiating unrelated methods. */
        void requireCompleteInterfaceType(clang::Sema &sema, clang::QualType type,
                                          clang::SourceLocation location,
                                          llvm::SmallPtrSetImpl<const clang::Type *> &visited)
        {
            type = type.getNonReferenceType();
            if (type.isNull() || type->isVoidType() || type->isDependentType() ||
                !visited.insert(type.getCanonicalType().getTypePtr()).second)
                return;
            if (!type->isRecordType() && !type->isArrayType())
                return;
            if (sema.RequireCompleteType(location, type, clang::diag::err_incomplete_type))
                return;
            if (const auto *array = sema.Context.getAsArrayType(type))
                requireCompleteInterfaceType(sema, array->getElementType(), location, visited);
            else if (const auto *record = type->getAsCXXRecordDecl())
                for (const auto *field : record->fields())
                    requireCompleteInterfaceType(sema, field->getType(), field->getLocation(), visited);
        }

        /** Derives Metal argument-buffer mappings solely from the prepared logical interface. */
        MSLEmitter::MSLResourceLayout buildMetalResourceLayout(const UGLIR::Reflection &reflection)
        {
            MSLEmitter::MSLResourceLayout result;
            for (const auto &group : reflection.bindGroups)
            {
                MSLEmitter::MSLBindGroupLayout layout;
                layout.bindGroupName = group.name;
                layout.bindGroupTypeName = group.typeName;
                layout.metalBufferIndex = group.bindGroupIndex + reflection.metalBindGroupBufferOffset;
                for (const auto &resource : reflection.resources)
                {
                    if (resource.bindGroupIndex != group.bindGroupIndex)
                        continue;
                    const auto prefix = group.name + ".";
                    if (resource.name.rfind(prefix, 0) != 0)
                        throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR interface: resource does not belong to its declared bind group: " + resource.name));
                    layout.resources.push_back({resource.name, resource.name.substr(prefix.size()), resource.bindingIndex});
                }
                result.bindGroups.push_back(std::move(layout));
            }
            return result;
        }

        /** Captures the bytes actually loaded by Clang, including included headers. */
        llvm::StringMap<std::array<uint8_t, 32>> collectInputDigests(const clang::SourceManager &sources)
        {
            llvm::StringMap<std::array<uint8_t, 32>> result;
            for (auto entry = sources.fileinfo_begin(); entry != sources.fileinfo_end(); ++entry)
            {
                const auto bytes = entry->second->getBufferDataIfLoaded();
                if (!bytes)
                    continue;
                llvm::SHA256 digest;
                digest.update(*bytes);
                result[entry->first.getName()] = digest.final();
            }
            return result;
        }
    }

    PreparedShaderTranslationUnit prepareShaderTranslationUnit(clang::ASTContext &context, clang::Sema &sema)
    {
        const auto roots = UGLIR::discoverShaderRoots(context);
        llvm::SmallPtrSet<const clang::Type *, 32> completedTypes;
        for (const auto &root : roots)
        {
            for (auto *entry : root.recordDecl->methods())
            {
                const auto name = entry->getNameAsString();
                const bool isEntry =
                    (root.kind == UGLIR::ShaderClassKind::Compute && name == mUGLComputeShaderFunctionName) ||
                    (root.kind == UGLIR::ShaderClassKind::Render &&
                        (name == mUGLVertexShaderFunctionName || name == mUGLFragmentShaderFunctionName)) ||
                    (root.kind == UGLIR::ShaderClassKind::PixelLocal && name == mUGLPixelShaderFunctionName);
                if (!isEntry)
                    continue;
                if (entry->isDependentContext())
                    throw std::runtime_error(formatClangStyleDiagnostic(entry, "UGLIR Sema: shader entry is not concrete."));
                if (!entry->hasBody() && entry->getTemplateInstantiationPattern() != nullptr)
                    sema.InstantiateFunctionDefinition(entry->getLocation(), entry, true, true);
                if (context.getDiagnostics().hasErrorOccurred())
                    throw std::runtime_error(formatClangStyleDiagnostic(entry, "UGLIR Sema: shader entry instantiation failed."));
                if (entry->isInvalidDecl() || !entry->hasBody())
                    throw std::runtime_error(formatClangStyleDiagnostic(entry, "UGLIR Sema: shader entry has no valid concrete definition."));
                requireCompleteInterfaceType(sema, entry->getReturnType(), entry->getLocation(), completedTypes);
                for (const auto *parameter : entry->parameters())
                    requireCompleteInterfaceType(sema, parameter->getType(), parameter->getLocation(), completedTypes);
            }
        }
        if (context.getDiagnostics().hasErrorOccurred())
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR Sema: shader interface completion failed."));

        const auto verification = UGLIR::verifyTranslationUnitForUGLIR(context, roots);
        if (!verification.diagnostics.empty())
            throw std::runtime_error(UGLIR::formatUGLIRVerificationDiagnostics(verification));
        auto lowering = UGLIR::lowerTranslationUnitToUGLIR(context, roots);
        if (!lowering.diagnostics.empty())
            throw std::runtime_error(UGLIR::formatUGLIRLoweringDiagnostics(lowering));
        if (lowering.modules.empty())
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR lowering: no shader modules were produced by the UGLIR shader pipeline."));
        for (const auto &module : lowering.modules)
            for (const auto &diagnostic : UGLIR::validateModule(module))
                throw std::runtime_error(formatClangStyleDiagnostic(diagnostic.sourceLocation.file,
                    diagnostic.sourceLocation.line, diagnostic.sourceLocation.column, diagnostic.message));

        PreparedShaderTranslationUnit result;
        for (auto &module : lowering.modules)
        {
            PreparedShader shader;
            shader.metalLayout = buildMetalResourceLayout(module.reflection);
            shader.module = std::move(module);
            result.shaders.push_back(std::move(shader));
        }
        result.inputDigests = collectInputDigests(context.getSourceManager());
        return result;
    }

    void verifyShaderFrontendInputs(const PreparedShaderTranslationUnit &shaders, const clang::SourceManager &sources)
    {
        const auto hostInputs = collectInputDigests(sources);
        if (hostInputs.size() != shaders.inputDigests.size())
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR frontend: shader and host inputs differ."));
        for (const auto &input : shaders.inputDigests)
        {
            const auto host = hostInputs.find(input.getKey());
            if (host == hostInputs.end() || host->second != input.second)
                throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR frontend: input changed between shader and host parsing: " + input.getKey().str()));
        }
    }

    void storePreparedShaderDebugOutputs(const PreparedShaderTranslationUnit &shaders, IShaderDebugOutputSink &sink)
    {
        for (const auto &shader : shaders.shaders)
        {
            const auto &module = shader.module;
            const auto stem = UGLIR::makeUGLIRDebugArtifactStem(module.name);
            sink.storeShaderDebugOutput("uglir/" + stem + ".uglir.txt", UGLIR::dumpModuleAsText(module));
            sink.storeShaderDebugOutput("uglir/" + stem + ".uglir.json", UGLIR::dumpModuleAsJson(module));
        }
    }
}
