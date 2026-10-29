#include "PreparedShaderTranslationUnit.hpp"
#include <CodeGen/DiagnosticError.hpp>

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <CodeGen/UGLIR/UGLIRLowering.hpp>
#include <CodeGen/UGLIR/UGLIRSourceVerifier.hpp>
#include <CodeGen/UGLIR/UGLIRVerifier.hpp>
#include <CodeGen/UGLIR/UGLIRDump.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/DiagnosticSema.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
            {
                return;
            }
            if (!type->isRecordType() && !type->isArrayType())
            {
                return;
            }
            if (sema.RequireCompleteType(location, type, clang::diag::err_incomplete_type))
            {
                return;
            }
            if (const auto *array = sema.Context.getAsArrayType(type))
            {
                requireCompleteInterfaceType(sema, array->getElementType(), location, visited);
            }
            else if (const auto *record = type->getAsCXXRecordDecl())
            {
                for (const auto *field : record->fields())
                {
                    requireCompleteInterfaceType(sema, field->getType(), field->getLocation(), visited);
                }
            }
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
                layout.metalBufferIndex = group.bindGroupIndex;
                for (const auto &resource : reflection.resources)
                {
                    if (resource.bindGroupIndex != group.bindGroupIndex)
                    {
                        continue;
                    }
                    const auto prefix = group.name + ".";
                    if (resource.name.rfind(prefix, 0) != 0)
                    {
                        throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR interface: resource does not belong to its declared bind group: " + resource.name));
                    }
                    layout.resources.push_back({resource.name, resource.name.substr(prefix.size()), resource.bindingIndex});
                }
                result.bindGroups.push_back(std::move(layout));
            }
            return result;
        }

        /** Classifies the lvalue root one resolved expression chain reaches during Args write analysis. */
        enum class ArgsLvalueRoot
        {
            None,    ///< the chain does not designate an Args resource or an unclassifiable handle
            Argument, ///< the chain is rooted at a constructor Args parameter
            Unknown, ///< the chain may alias an Args resource but cannot be resolved precisely
        };

        /** Carries one resolved lvalue chain for Args write analysis; segments retain "[index]" tokens. */
        struct ArgsLvalueResolution
        {
            ArgsLvalueRoot root = ArgsLvalueRoot::None;
            std::vector<std::string> segments;
        };

        /** Splits a dotted or indexed Args resource path into member and "[index]" segments. */
        std::vector<std::string> splitArgsResourcePath(const std::string &path)
        {
            std::vector<std::string> segments;
            size_t begin = 0;
            while (begin < path.size())
            {
                const size_t end = path.find_first_of(".[", begin);
                if (end == std::string::npos)
                {
                    segments.push_back(path.substr(begin));
                    break;
                }
                if (end > begin)
                {
                    segments.push_back(path.substr(begin, end - begin));
                }
                if (path[end] == '.')
                {
                    begin = end + 1;
                    continue;
                }
                const size_t close = path.find(']', end);
                if (close == std::string::npos)
                {
                    segments.push_back(path.substr(end));
                    break;
                }
                segments.push_back(path.substr(end, close - end + 1));
                begin = close + 1;
            }
            return segments;
        }

        /** Returns whether one lowered type name refers to a shader resource handle. */
        bool isArgsResourceHandleType(const UGLIR::Module &module, const std::string &typeName)
        {
            const auto *type = UGLIR::findTypeByName(module, typeName);
            if (type == nullptr)
            {
                return false;
            }
            return type->kind == UGLIR::TypeKind::Buffer || type->kind == UGLIR::TypeKind::Resource ||
                type->kind == UGLIR::TypeKind::Texture || type->kind == UGLIR::TypeKind::Sampler;
        }

        /** Recursive worker for argsTypeContainsHandles with a cycle guard for reference aliases. */
        bool argsTypeContainsHandlesImpl(const UGLIR::Module &module, const std::string &typeName,
            std::unordered_set<std::string> &visiting)
        {
            if (isArgsResourceHandleType(module, typeName) || !visiting.insert(typeName).second)
            {
                return true;
            }
            const auto *type = UGLIR::findTypeByName(module, typeName);
            bool contains = true;
            if (type != nullptr)
            {
                switch (type->kind)
                {
                case UGLIR::TypeKind::Workgroup:
                    break;
                case UGLIR::TypeKind::Array:
                case UGLIR::TypeKind::ReferenceAlias:
                    contains = !type->elementType.empty() &&
                        argsTypeContainsHandlesImpl(module, type->elementType, visiting);
                    break;
                case UGLIR::TypeKind::Struct:
                    contains = false;
                    for (const auto &field : type->fields)
                    {
                        if (argsTypeContainsHandlesImpl(module, field.type, visiting))
                        {
                            contains = true;
                            break;
                        }
                    }
                    break;
                default:
                    contains = false;
                    break;
                }
            }
            visiting.erase(typeName);
            return contains;
        }

        /** Returns whether one lowered type is or transitively can hold an Args resource handle; unknown types assume yes. */
        bool argsTypeContainsHandles(const UGLIR::Module &module, const std::string &typeName)
        {
            std::unordered_set<std::string> visiting;
            return argsTypeContainsHandlesImpl(module, typeName, visiting);
        }

        /** Resolves the lvalue chain one expression designates, following recorded local handle aliases. */
        ArgsLvalueResolution resolveArgsLvalue(const UGLIR::Module &module, const UGLIR::Expression &expression,
            const std::unordered_set<std::string> &argumentRoots,
            const std::unordered_map<std::string, ArgsLvalueResolution> &aliases)
        {
            switch (expression.kind)
            {
            case UGLIR::ExpressionKind::DeclRef:
            {
                if (const auto alias = aliases.find(expression.name); alias != aliases.end())
                {
                    return alias->second;
                }
                auto segments = splitArgsResourcePath(expression.name);
                if (!segments.empty() && argumentRoots.count(segments.front()) != 0)
                {
                    return {ArgsLvalueRoot::Argument, std::move(segments)};
                }
                // Parameters and globals never declared in this function may still alias Args resources.
                return argsTypeContainsHandles(module, expression.type)
                    ? ArgsLvalueResolution{ArgsLvalueRoot::Unknown, {}} : ArgsLvalueResolution{};
            }
            case UGLIR::ExpressionKind::MemberRef:
            case UGLIR::ExpressionKind::Subscript:
            {
                if (expression.operands.empty())
                {
                    return {ArgsLvalueRoot::Unknown, {}};
                }
                auto base = resolveArgsLvalue(module, expression.operands.front(), argumentRoots, aliases);
                if (base.root == ArgsLvalueRoot::Argument)
                {
                    if (expression.kind == UGLIR::ExpressionKind::MemberRef)
                    {
                        base.segments.push_back(expression.name);
                    }
                    else
                    {
                        const auto &index = expression.operands.size() > 1 ? expression.operands[1] : expression.operands.front();
                        base.segments.push_back(index.kind == UGLIR::ExpressionKind::Literal ? "[" + index.value + "]" : "[*]");
                    }
                    return base;
                }
                if (base.root == ArgsLvalueRoot::Unknown)
                {
                    return base;
                }
                // A declared local that resolved to no Args path stays unmarked; any other
                // unresolvable handle-capable base may still alias Args resource storage.
                if (expression.operands.front().kind != UGLIR::ExpressionKind::DeclRef &&
                    argsTypeContainsHandles(module, expression.operands.front().type))
                {
                    return {ArgsLvalueRoot::Unknown, {}};
                }
                return {};
            }
            case UGLIR::ExpressionKind::ThisRef:
                return {ArgsLvalueRoot::Unknown, {}};
            default:
            {
                // Value, wrapper, and call expressions forward the first resolvable handle operand; a
                // resource-typed call result is unclassifiable and conservatively treated as an alias.
                if (expression.kind == UGLIR::ExpressionKind::Call && isArgsResourceHandleType(module, expression.type))
                {
                    return {ArgsLvalueRoot::Unknown, {}};
                }
                // A scalar result such as half4(texture->read(...)) cannot alias a handle, so its
                // operands' resource paths must not leak into alias tracking for the result.
                if (!argsTypeContainsHandles(module, expression.type))
                {
                    return {};
                }
                ArgsLvalueResolution result{};
                for (const auto &operand : expression.operands)
                {
                    auto resolved = resolveArgsLvalue(module, operand, argumentRoots, aliases);
                    if (resolved.root == ArgsLvalueRoot::Argument)
                    {
                        return resolved;
                    }
                    if (resolved.root == ArgsLvalueRoot::Unknown)
                    {
                        result = resolved;
                    }
                }
                return result;
            }
            }
        }

        /** Returns true when the two Args paths share a prefix; a write through either reaches the resource. */
        bool argsResourcePathsOverlap(const std::vector<std::string> &writePath, const std::vector<std::string> &resourcePath)
        {
            const size_t count = std::min(writePath.size(), resourcePath.size());
            for (size_t index = 0; index < count; ++index)
            {
                if (writePath[index] != "[*]" && writePath[index] != resourcePath[index])
                {
                    return false;
                }
            }
            return true;
        }

        /** Returns whether a structured intrinsic writes through its resource operand. */
        bool isArgsWriteIntrinsic(UGLIR::IntrinsicCallKind kind)
        {
            switch (kind)
            {
            case UGLIR::IntrinsicCallKind::AtomicAdd:
            case UGLIR::IntrinsicCallKind::AtomicAnd:
            case UGLIR::IntrinsicCallKind::AtomicOr:
            case UGLIR::IntrinsicCallKind::AtomicMin:
            case UGLIR::IntrinsicCallKind::AtomicMax:
            case UGLIR::IntrinsicCallKind::AtomicLoad:
            case UGLIR::IntrinsicCallKind::AtomicStore:
            case UGLIR::IntrinsicCallKind::AtomicCompareExchange:
            case UGLIR::IntrinsicCallKind::TextureWrite:
                return true;
            default:
                return false;
            }
        }

        /** Tracks resolved Args resources and the candidates proven written by the scanned module. */
        struct ArgsWriteScan
        {
            const UGLIR::Module &module;
            std::unordered_set<std::string> argumentRoots;
            std::vector<std::vector<std::string>> resourcePaths;
            std::unordered_map<std::string, ArgsLvalueResolution> aliases;
            std::unordered_set<size_t> written;
            bool allWritten = false;
        };

        /** Marks every writable Args resource one resolved lvalue chain can reach. */
        void markArgsWriteTarget(ArgsWriteScan &scan, const ArgsLvalueResolution &target)
        {
            if (target.root == ArgsLvalueRoot::Unknown)
            {
                scan.allWritten = true;
                return;
            }
            if (target.root != ArgsLvalueRoot::Argument)
            {
                return;
            }
            for (size_t index = 0; index < scan.resourcePaths.size(); ++index)
            {
                if (argsResourcePathsOverlap(target.segments, scan.resourcePaths[index]))
                {
                    scan.written.insert(index);
                }
            }
        }

        /** Scans one expression tree for writes and unclassifiable uses that reach Args resources. */
        void scanArgsWriteExpression(ArgsWriteScan &scan, const UGLIR::Expression &expression)
        {
            switch (expression.kind)
            {
            case UGLIR::ExpressionKind::Store:
                if (expression.operands.size() >= 2 && expression.operands.front().kind == UGLIR::ExpressionKind::DeclRef &&
                    argsTypeContainsHandles(scan.module, expression.operands.front().type))
                {
                    // Rebinding a whole variable updates the alias instead of writing a resource.
                    auto aliased = resolveArgsLvalue(scan.module, expression.operands[1], scan.argumentRoots, scan.aliases);
                    if (aliased.root == ArgsLvalueRoot::None)
                    {
                        aliased.root = ArgsLvalueRoot::Unknown;
                    }
                    scan.aliases[expression.operands.front().name] = aliased;
                }
                else if (!expression.operands.empty())
                {
                    markArgsWriteTarget(scan, resolveArgsLvalue(scan.module, expression.operands.front(), scan.argumentRoots, scan.aliases));
                }
                break;
            case UGLIR::ExpressionKind::Unary:
                if ((expression.operatorName == "++" || expression.operatorName == "--") && !expression.operands.empty())
                {
                    markArgsWriteTarget(scan, resolveArgsLvalue(scan.module, expression.operands.front(), scan.argumentRoots, scan.aliases));
                }
                break;
            case UGLIR::ExpressionKind::Call:
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::None || isArgsWriteIntrinsic(expression.intrinsicCallKind))
                {
                    for (const auto &operand : expression.operands)
                    {
                        markArgsWriteTarget(scan, resolveArgsLvalue(scan.module, operand, scan.argumentRoots, scan.aliases));
                    }
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathModf ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSincos)
                {
                    // modf and sincos write through their trailing out-parameter operands.
                    for (size_t index = 1; index < expression.operands.size(); ++index)
                    {
                        markArgsWriteTarget(scan, resolveArgsLvalue(scan.module, expression.operands[index], scan.argumentRoots, scan.aliases));
                    }
                }
                break;
            default:
                break;
            }
            for (const auto &operand : expression.operands)
            {
                scanArgsWriteExpression(scan, operand);
            }
        }

        /** Scans one statement tree, recording local resource-handle aliases in evaluation order. */
        void scanArgsWriteStatement(ArgsWriteScan &scan, const UGLIR::Statement &statement)
        {
            for (const auto &expression : statement.expressions)
            {
                scanArgsWriteExpression(scan, expression);
            }
            if (statement.kind == UGLIR::StatementKind::VariableDeclaration && !statement.name.empty())
            {
                // Track which Args path a local variable or aggregate copy was initialized from so
                // later member/subscript chains resolve back to the same resource.
                ArgsLvalueResolution aliased{};
                if (!statement.expressions.empty())
                {
                    aliased = resolveArgsLvalue(scan.module, statement.expressions.front(), scan.argumentRoots, scan.aliases);
                    if (aliased.root == ArgsLvalueRoot::None && argsTypeContainsHandles(scan.module, statement.type))
                    {
                        aliased.root = ArgsLvalueRoot::Unknown;
                    }
                }
                scan.aliases[statement.name] = aliased;
            }
            for (const auto &child : statement.children)
            {
                scanArgsWriteStatement(scan, child);
            }
            for (const auto &child : statement.elseChildren)
            {
                scanArgsWriteStatement(scan, child);
            }
            for (const auto &arm : statement.switchCases)
            {
                for (const auto &label : arm.labels)
                {
                    scanArgsWriteExpression(scan, label);
                }
                for (const auto &child : arm.body)
                {
                    scanArgsWriteStatement(scan, child);
                }
            }
        }

        /** Pairs one shader class's Args resources and writable candidates with the names proven written. */
        struct ArgsAccessVerdicts
        {
            std::unordered_set<std::string> argumentRoots;
            std::unordered_set<std::string> candidates;
            std::unordered_set<std::string> written;
        };

        /** Scans one module's functions for writes to the class's writable Args resources. */
        void scanArgumentResourceWrites(const UGLIR::Module &module, ArgsAccessVerdicts &verdicts)
        {
            ArgsWriteScan scan{module, {}, {}, {}, {}, false};
            scan.argumentRoots = verdicts.argumentRoots;
            std::vector<std::string> candidateNames;
            for (const auto &name : verdicts.candidates)
            {
                candidateNames.push_back(name);
                scan.resourcePaths.push_back(splitArgsResourcePath(name));
            }
            if (scan.resourcePaths.empty())
            {
                return;
            }
            for (const auto &function : module.functions)
            {
                scan.aliases.clear();
                for (const auto &statement : function.body)
                {
                    scanArgsWriteStatement(scan, statement);
                }
            }
            for (size_t index = 0; index < scan.resourcePaths.size(); ++index)
            {
                if (scan.allWritten || scan.written.count(index) != 0)
                {
                    verdicts.written.insert(candidateNames[index]);
                }
            }
        }

        /** Marks each Args resource a shader class never writes so host layouts and SPIR-V narrow it. */
        void applyNarrowedArgumentResources(UGLIR::Module &module, const ArgsAccessVerdicts &verdicts)
        {
            for (auto &resource : module.reflection.resources)
            {
                if (resource.accessMode == UGLIR::AccessMode::ReadWrite && resource.kind != UGLIR::ResourceKind::InputAttachment &&
                    verdicts.candidates.count(resource.name) != 0 && verdicts.written.count(resource.name) == 0)
                {
                    resource.provenReadOnly = true;
                }
            }
        }

    } // namespace

    /** Completes shader interfaces and prepares verified IR, argument contracts, and native resource mappings. */
    PreparedShaderTranslationUnit prepareShaderTranslationUnit(clang::ASTContext &context, clang::Sema &sema,
                                                              bool argumentContractsOnly)
    {
        const auto roots = UGLIR::discoverShaderRoots(context);
        llvm::SmallPtrSet<const clang::Type *, 32> completedTypes;
        for (const auto &root : roots)
        {
            for (auto *entry : root.recordDecl->methods())
            {
                const auto name = entry->getNameAsString();
                if (name == mUGLCTORFunctionName)
                {
                    for (const auto *parameter : entry->parameters())
                    {
                        requireCompleteInterfaceType(sema, parameter->getType(), parameter->getLocation(), completedTypes);
                    }
                }
                const bool isEntry =
                    (root.kind == UGLIR::ShaderClassKind::Compute && name == mUGLComputeShaderFunctionName) ||
                    (root.kind == UGLIR::ShaderClassKind::Render &&
                        (name == mUGLVertexShaderFunctionName || name == mUGLFragmentShaderFunctionName)) ||
                    (root.kind == UGLIR::ShaderClassKind::PixelLocal && name == mUGLPixelShaderFunctionName);
                if (!isEntry)
                {
                    continue;
                }
                if (entry->isDependentContext())
                {
                    throw UGLC::CodeGen::DiagnosticError(entry != nullptr ? (entry)->getLocation() : clang::SourceLocation(), clang::DiagnosticsEngine::Error, "UGLIR Sema: shader entry is not concrete.");
                }
                if (!entry->hasBody() && entry->getTemplateInstantiationPattern() != nullptr)
                {
                    sema.InstantiateFunctionDefinition(entry->getLocation(), entry, true, true);
                }
                if (context.getDiagnostics().hasErrorOccurred())
                {
                    throw UGLC::CodeGen::DiagnosticError(entry != nullptr ? (entry)->getLocation() : clang::SourceLocation(), clang::DiagnosticsEngine::Error, "UGLIR Sema: shader entry instantiation failed.");
                }
                if (entry->isInvalidDecl() || !entry->hasBody())
                {
                    throw UGLC::CodeGen::DiagnosticError(entry != nullptr ? (entry)->getLocation() : clang::SourceLocation(), clang::DiagnosticsEngine::Error, "UGLIR Sema: shader entry has no valid concrete definition.");
                }
                requireCompleteInterfaceType(sema, entry->getReturnType(), entry->getLocation(), completedTypes);
                for (const auto *parameter : entry->parameters())
                {
                    requireCompleteInterfaceType(sema, parameter->getType(), parameter->getLocation(), completedTypes);
                }
            }
        }
        if (context.getDiagnostics().hasErrorOccurred())
        {
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR Sema: shader interface completion failed."));
        }

        PreparedShaderTranslationUnit result;
        result.argumentContracts = prepareConstructorArgumentContracts(context, sema, roots, argumentContractsOnly);
        if (context.getDiagnostics().hasErrorOccurred())
        {
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR Sema: shader argument contract validation failed."));
        }

        const auto verification = UGLIR::verifyTranslationUnitForUGLIR(context, roots);
        if (!verification.diagnostics.empty())
        {
            throw UGLC::CodeGen::DiagnosticError(UGLIR::collectUGLIRVerificationDiagnosticRecords(verification));
        }
        if (argumentContractsOnly)
        {
            return result;
        }
        auto lowering = UGLIR::lowerTranslationUnitToUGLIR(context, roots);
        if (!lowering.diagnostics.empty())
        {
            throw UGLC::CodeGen::DiagnosticError(UGLIR::collectUGLIRLoweringDiagnosticRecords(lowering));
        }
        if (lowering.modules.empty())
        {
            throw std::runtime_error(formatUnlocatedDiagnostic("UGLIR lowering: no shader modules were produced by the UGLIR shader pipeline."));
        }
        for (const auto &module : lowering.modules)
        {
            for (const auto &diagnostic : UGLIR::validateModule(module))
            {
                DiagnosticRecord record = {};
                record.file = diagnostic.sourceLocation.file;
                record.line = diagnostic.sourceLocation.line;
                record.column = diagnostic.sourceLocation.column;
                record.message = diagnostic.message;
                record.level = clang::DiagnosticsEngine::Error;
                throw UGLC::CodeGen::DiagnosticError({record});
            }
        }

        // A write in any entry of a shader class keeps its Args resource writable, so write
        // verdicts are collected across all modules of the class before any access is narrowed.
        // Candidate roots are collected first because stage-specific modules only tag the Args
        // resources their stage uses while writes can live in any sibling module.
        std::unordered_map<std::string, ArgsAccessVerdicts> argumentAccessVerdicts;
        for (const auto &module : lowering.modules)
        {
            if (!module.reflection.arguments.enabled)
            {
                continue;
            }
            auto &verdicts = argumentAccessVerdicts[module.reflection.shaderClassName];
            for (const auto &resource : module.reflection.resources)
            {
                if (resource.argumentType.empty())
                {
                    continue;
                }
                const auto segments = splitArgsResourcePath(resource.name);
                if (!segments.empty())
                {
                    verdicts.argumentRoots.insert(segments.front());
                }
                if (resource.accessMode == UGLIR::AccessMode::ReadWrite && resource.kind != UGLIR::ResourceKind::InputAttachment)
                {
                    verdicts.candidates.insert(resource.name);
                }
            }
        }
        for (const auto &module : lowering.modules)
        {
            if (module.reflection.arguments.enabled)
            {
                scanArgumentResourceWrites(module, argumentAccessVerdicts[module.reflection.shaderClassName]);
            }
        }

        BaseASTVisitor queries(&context, nullptr, nullptr, nullptr);
        for (auto &module : lowering.modules)
        {
            PreparedShader shader;
            shader.metalLayout = buildMetalResourceLayout(module.reflection);
            std::string contractClassName;
            if (module.reflection.arguments.enabled)
            {
                for (const auto &root : roots)
                {
                    if (UGLIR::makeUGLIRRecordSymbolName(*root.recordDecl) == module.reflection.shaderClassName)
                    {
                        contractClassName = queries.generateTypeCanonicalName(context.getRecordType(root.recordDecl));
                        break;
                    }
                }
            }
            if (module.reflection.arguments.enabled)
            {
                for (const auto &contract : result.argumentContracts)
                {
                    if (contract.className != contractClassName)
                    {
                        continue;
                    }
                    for (auto &resource : module.reflection.resources)
                    {
                        if (resource.kind == UGLIR::ResourceKind::InputAttachment)
                        {
                            continue;
                        }
                        resource.visibleStages.clear();
                        for (uint32_t index = 0; index < contract.parameters.size(); ++index)
                        {
                            const auto &parameterName = contract.parameters[index].name;
                            if (resource.name != parameterName && resource.name.rfind(parameterName + ".", 0) != 0 &&
                                resource.name.rfind(parameterName + "[", 0) != 0)
                            {
                                continue;
                            }
                            for (const auto &entry : contract.entries)
                            {
                                if (std::find(entry.argumentIndices.begin(), entry.argumentIndices.end(), index) != entry.argumentIndices.end())
                                {
                                    resource.visibleStages.push_back(entry.entryKind == UGLIR::ShaderEntryKind::Compute ? UGLIR::ShaderStage::Compute :
                                        entry.entryKind == UGLIR::ShaderEntryKind::Vertex ? UGLIR::ShaderStage::Vertex : UGLIR::ShaderStage::Fragment);
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                applyNarrowedArgumentResources(module, argumentAccessVerdicts[module.reflection.shaderClassName]);
            }
            shader.module = std::move(module);
            result.shaders.push_back(std::move(shader));
        }
        return result;
    }

    /** Writes the prepared IR text and reflection JSON to the selected diagnostic sink. */
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
