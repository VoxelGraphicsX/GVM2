#include "CPPRendererEmitter.hpp"

#include "CPPVisitor.hpp"

#include <CodeGen/UGLC.Constants.hpp>
#include <llvm/Support/Casting.h>

#include <cctype>
#include <unordered_set>

namespace UGLC::CodeGen::CPP
{
    CPPRendererEmitter::CPPRendererEmitter(CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::string CPPRendererEmitter::replaceRendererClass(const clang::CXXRecordDecl *decl)
    {
        std::string result;
        std::string keyword = decl->isClass() ? "class" : "struct";
        std::string name = decl->getNameAsString();
        std::string ImplName = decl->getNameAsString() + "Impl";
        std::vector<std::pair<std::string, clang::FieldDecl *>> rendererExportedVariables;
        std::vector<std::string> exportedRenderSets;
        std::vector<std::string> exportedRenderSetTypes;
        std::unordered_set<std::string> seenExportedRenderSetTypes;
        std::string rendererNamespaceName = decl->getQualifiedNameAsString();
        (void)mVisitor.requireRendererRenderMethodOrThrow(decl);

        if (auto *templateDecl = decl->getDescribedClassTemplate())
        {
            result += mVisitor.getLineDirective(templateDecl->getBeginLoc());
            result += mVisitor.generateTemplateParameters(templateDecl->getTemplateParameters());
        }

        if (rendererNamespaceName == name)
        {
            rendererNamespaceName.clear();
        }
        else
        {
            rendererNamespaceName = rendererNamespaceName.substr(0, rendererNamespaceName.size() - 2 - name.size());
        }

        result += mVisitor.mSpaceManager.getSpace() + keyword + " " + ImplName + " : public GVM::Core::AbstractRendererImpl " + mVisitor.NewLine();
        result += mVisitor.enterScope();

        for (auto *innerDecl : decl->decls())
        {
            if (auto *nestedRecord = llvm::dyn_cast<clang::CXXRecordDecl>(innerDecl))
            {
                if (mVisitor.shouldEmitNestedRecordDefinition(decl, nestedRecord))
                {
                    result += mVisitor.getLineDirective(nestedRecord->getBeginLoc());
                    result += mVisitor.generateRecordDefinition(nestedRecord);
                }
            }
        }
        {
            auto fields = mVisitor.getAllFieldFromRecord(decl);
            for (auto &f : fields)
            {
                if (mVisitor.checkAttibuteByName(f, mUGLAttributeExportName))
                {
                    if (auto recordDecl = mVisitor.getUnqualifiedType(f->getType())->getAsCXXRecordDecl(); recordDecl && mVisitor.getClassCanonicalName(recordDecl) == mUGLRenderSetName)
                    {
                        exportedRenderSets.emplace_back(f->getNameAsString());
                        auto templateArgs = mVisitor.getTemplateArgumentsFromType(f->getType());
                        if (!templateArgs.empty() && templateArgs.front().getKind() == clang::TemplateArgument::Type)
                        {
                            if (auto *renderSetTypeDecl = templateArgs.front().getAsType()->getAsCXXRecordDecl())
                            {
                                const std::string renderSetTypeName = mVisitor.getClassCanonicalName(renderSetTypeDecl);
                                if (seenExportedRenderSetTypes.emplace(renderSetTypeName).second)
                                {
                                    exportedRenderSetTypes.emplace_back(renderSetTypeName);
                                }
                            }
                        }
                    }
                    else
                    {
                        rendererExportedVariables.emplace_back(f->getNameAsString(), f);
                        result += mVisitor.mSpaceManager.getSpace() + "eastl::array<" + mVisitor.generateTypeCanonicalName(f->getType()) + ",3> " + generateExportArrayVariableName(f->getNameAsString()) + mVisitor.EOS();
                    }
                }
            }
        }

        result += generateExportVariableEnum(rendererExportedVariables);
        result += mVisitor.generateRecordDataMembers(decl, &mVisitor.mTypeConvertor);
        result += makeRenderSetExecuteCommand(exportedRenderSets);
        {
            result += mVisitor.mSpaceManager.getSpace() + "private: eastl::vector<ExportVariables> mUpdatedVariables" + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + "private: uint64_t " + mVisitor.mUGLFrameCounterVariableName + " = 0" + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + "private: uint64_t " + mVisitor.mUGLFrameRingBufferVariableName + " = 1" + mVisitor.EOS();
        }

        for (auto *method : decl->methods())
        {
            if (!method->isCopyAssignmentOperator() && !method->isMoveAssignmentOperator() && !llvm::isa<clang::CXXConstructorDecl>(method) && !llvm::isa<clang::CXXDestructorDecl>(method))
            {
                if (method->getNameAsString() == "render")
                {
                    if (mVisitor.functionRequiresDeletedHostDefinition(method))
                    {
                        result += mVisitor.generateDeletedFunctionDeclaration(method, "renderImpl");
                    }
                    else
                    {
                        result += mVisitor.generateFunctionSignature(method, "renderImpl");
                        result += mVisitor.generateFunctionBody(method);
                    }
                }
                else
                {
                    result += mVisitor.generateFunctionDefinition(method);
                }
            }
        }
        {
            result += mVisitor.mSpaceManager.getSpace() + "private: uint64_t getNextFrameCounter() const" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "return (" + mVisitor.mUGLFrameCounterVariableName + " + 2) % this->" + mVisitor.mUGLFrameRingBufferVariableName + mVisitor.EOS();
            result += mVisitor.quitScope();

            result += mVisitor.mSpaceManager.getSpace() + "private: uint64_t getCurrentFrameCounter() const" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "return " + mVisitor.mUGLFrameCounterVariableName + mVisitor.EOS();
            result += mVisitor.quitScope();

            result += mVisitor.mSpaceManager.getSpace() + "private: void assignExportVariables()" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "for(const auto& var : mUpdatedVariables)" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "switch (var)" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            for (const auto &[name, evar] : rendererExportedVariables)
            {
                (void)evar;
                result += mVisitor.mSpaceManager.getSpace() + "case ExportVariables::" + name + ": " + name + " = " + generateExportArrayVariableName(name) + "[getCurrentFrameCounter()]; break" + mVisitor.EOS();
            }
            result += mVisitor.quitScope();
            result += mVisitor.quitScope();
            result += mVisitor.quitScope();

            result += mVisitor.mSpaceManager.getSpace() + "public:" + mVisitor.NewLine();
            result += mVisitor.mSpaceManager.getSpace() + "void render()" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "if(" + mVisitor.mUGLFrameRingBufferVariableName + " != 1)" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + "assignExportVariables()" + mVisitor.EOS();
            result += mVisitor.quitScope();
            result += mVisitor.mSpaceManager.getSpace() + "renderImpl()" + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + mVisitor.mUGLFrameCounterVariableName + " = (" + mVisitor.mUGLFrameCounterVariableName + " + 1) % this->" + mVisitor.mUGLFrameRingBufferVariableName + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + "mUpdatedVariables.clear()" + mVisitor.EOS();
            result += mVisitor.quitScope();

            result += mVisitor.mSpaceManager.getSpace() + "void setFrameRingBufferCountOne()" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + mVisitor.mUGLFrameRingBufferVariableName + " = 1" + mVisitor.EOS();
            result += mVisitor.quitScope();

            result += mVisitor.mSpaceManager.getSpace() + "void setFrameRingBufferCountThree()" + mVisitor.NewLine();
            result += mVisitor.enterScope();
            result += mVisitor.mSpaceManager.getSpace() + mVisitor.mUGLFrameRingBufferVariableName + " = 3" + mVisitor.EOS();
            result += mVisitor.quitScope();
        }
        {
            for (const auto &[name, varDecl] : rendererExportedVariables)
            {
                std::string varTypeName = mVisitor.generateTypeCanonicalName(varDecl->getType(), &mVisitor.mTypeConvertor);
                std::string varFunctionName = name;
                if (!varFunctionName.empty())
                {
                    varFunctionName[0] = static_cast<char>(std::toupper(varFunctionName[0]));
                }

                result += mVisitor.mSpaceManager.getSpace() + "const " + varTypeName + "& get" + varFunctionName + "()" + mVisitor.NewLine();
                result += mVisitor.enterScope();
                result += mVisitor.mSpaceManager.getSpace() + "return " + name + mVisitor.EOS();
                result += mVisitor.quitScope();

                result += mVisitor.mSpaceManager.getSpace() + "void set" + varFunctionName + "(const " + varTypeName + "& var)" + mVisitor.NewLine();
                result += mVisitor.enterScope();
                result += mVisitor.mSpaceManager.getSpace() + "if(" + mVisitor.mUGLFrameRingBufferVariableName + " == 1)" + mVisitor.NewLine();
                result += mVisitor.enterScope();
                result += mVisitor.mSpaceManager.getSpace() + "this->" + name + " = var" + mVisitor.EOS();
                result += mVisitor.quitScope();
                result += mVisitor.mSpaceManager.getSpace() + "else" + mVisitor.NewLine();
                result += mVisitor.enterScope();
                result += mVisitor.mSpaceManager.getSpace() + "mUpdatedVariables.emplace_back(ExportVariables::" + name + ")" + mVisitor.EOS();
                result += mVisitor.mSpaceManager.getSpace() + "this->" + generateExportArrayVariableName(name) + "[getNextFrameCounter()] = var" + mVisitor.EOS();
                result += mVisitor.quitScope();
                result += mVisitor.quitScope();
            }
        }
        result += mVisitor.endClass();
        result += mVisitor.mSpaceManager.getSpace() + mVisitor.NewLine() + "using " + name + " = eastl::intrusive_ptr<" + ImplName + ">" + mVisitor.EOS();
        result += mVisitor.mSpaceManager.getSpace() + name + " make" + name + "(){return new " + ImplName + ";}" + mVisitor.NewLine();
        result += makeExportHeader(rendererNamespaceName, exportedRenderSets, exportedRenderSetTypes);

        return result;
    }

    std::string CPPRendererEmitter::generateExportArrayVariableName(const std::string &name) const
    {
        return name + "__UGL__ARRAY";
    }

    std::string CPPRendererEmitter::generateExportVariableEnum(const std::vector<std::pair<std::string, clang::FieldDecl *>> &rendererExportedVariables)
    {
        std::string result;
        result += mVisitor.mSpaceManager.getSpace() + "private: enum class ExportVariables : uint64_t\n";
        result += mVisitor.enterScope();
        for (const auto &pair : rendererExportedVariables)
        {
            result += mVisitor.mSpaceManager.getSpace() + pair.first + "," + mVisitor.NewLine();
        }
        if (!rendererExportedVariables.empty())
        {
            result.erase(result.size() - 2, 2);
            result += mVisitor.NewLine();
        }
        result += mVisitor.endClass();
        return result;
    }

    std::string CPPRendererEmitter::makeRenderSetExecuteCommand(const std::vector<std::string> &exportedRenderSets)
    {
        std::string result;

        result += mVisitor.mSpaceManager.getSpace() + "public: virtual void executeRenderSetCommand(GVM::Core::RenderSetHandle renderSetHandle, const GVM::Core::AbstractRenderSetCommandEncoder &command) override" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        result += mVisitor.mSpaceManager.getSpace() + "switch (renderSetHandle)" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        for (const auto &renderSet : exportedRenderSets)
        {
            result += mVisitor.mSpaceManager.getSpace() + "case ExportedRenderSet::" + renderSet + ":" + mVisitor.NewLine();
            result += mVisitor.mSpaceManager.getSpace() + "this->" + renderSet + "->executeCommand(command);" + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + "break;" + mVisitor.EOS();
        }
        result += mVisitor.mSpaceManager.getSpace() + "default: throw std::runtime_error(\" can not find RenderSet \");" + mVisitor.EOS();
        result += mVisitor.quitScope();
        result += mVisitor.quitScope();

        result += mVisitor.mSpaceManager.getSpace() + "public: virtual GVM::Core::AbstractRenderSetCommandEncoder createRenderSetCommandEncoder(GVM::Core::RenderSetHandle renderSetHandle) override" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        result += mVisitor.mSpaceManager.getSpace() + "switch (renderSetHandle)" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        for (const auto &renderSet : exportedRenderSets)
        {
            result += mVisitor.mSpaceManager.getSpace() + "case ExportedRenderSet::" + renderSet + ":" + mVisitor.NewLine();
            result += mVisitor.mSpaceManager.getSpace() + "return this->" + renderSet + "->createEncoder();" + mVisitor.EOS();
            result += mVisitor.mSpaceManager.getSpace() + "break;" + mVisitor.EOS();
        }
        result += mVisitor.mSpaceManager.getSpace() + "default: throw std::runtime_error(\" can not find RenderSet \");" + mVisitor.EOS();
        result += mVisitor.quitScope();
        result += mVisitor.mSpaceManager.getSpace() + "return nullptr;" + mVisitor.EOS();
        result += mVisitor.quitScope();

        result += mVisitor.mSpaceManager.getSpace() + "public: virtual bool checkRenderSetComponentResourceName(GVM::Core::RenderSetHandle renderSetHandle, GVM::Core::RenderComponentHandle renderComponentHandle, const eastl::string &name) override" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        result += mVisitor.mSpaceManager.getSpace() + "switch (renderSetHandle)" + mVisitor.NewLine();
        result += mVisitor.enterScope();

        for (const auto &renderSet : exportedRenderSets)
        {
            result += mVisitor.mSpaceManager.getSpace() + "case ExportedRenderSet::" + renderSet + ":" + mVisitor.NewLine();
            result += mVisitor.mSpaceManager.getSpace() + "return this->" + renderSet + "->checkComponentResourceName(renderComponentHandle, name);" + mVisitor.NewLine();
        }
        result += mVisitor.mSpaceManager.getSpace() + "default: throw std::runtime_error(\" can not find RenderSet \");" + mVisitor.EOS();
        result += mVisitor.quitScope();
        result += mVisitor.mSpaceManager.getSpace() + "return false;" + mVisitor.EOS();
        result += mVisitor.quitScope();

        return result;
    }

    std::string CPPRendererEmitter::makeExportHeader(const std::string &rendererNamespaceName, const std::vector<std::string> &exportedRenderSets, const std::vector<std::string> &exportedRenderSetTypes)
    {
        std::string result;
        result += "#if defined(" + mUGLExportFileMacro + ")\n";
        result += "#include <inttypes.h>\n";
        result += "#include <GVMCore/GVMCore.Public.hpp>\n";
        if (!rendererNamespaceName.empty())
        {
            result += "namespace " + rendererNamespaceName + "\n";
            result += mVisitor.enterScope();
        }

        result += mVisitor.mSpaceManager.getSpace() + "namespace ExportedRenderSet\n";
        result += mVisitor.enterScope();
        int renderSetIndex = 0;
        for (const auto &name : exportedRenderSets)
        {
            result += mVisitor.mSpaceManager.getSpace() + "static constexpr uint64_t " + name + " = " + std::to_string(renderSetIndex + 1) + mVisitor.EOS();
            renderSetIndex++;
        }
        result += mVisitor.endClass();
        result += "\n";

        if (!rendererNamespaceName.empty())
        {
            result += mVisitor.quitScope();
        }

        for (const auto &name : exportedRenderSetTypes)
        {
            auto componentInfoIt = mVisitor.mRenderSetComponentInfos.find(name);
            if (componentInfoIt == mVisitor.mRenderSetComponentInfos.end())
            {
                continue;
            }
            const auto &componentInfos = componentInfoIt->second;
            result += mVisitor.mSpaceManager.getSpace() + "namespace " + name + "Components\n";
            result += mVisitor.enterScope();
            for (size_t i = 0; i < componentInfos.size(); ++i)
            {
                const auto &componentInfo = componentInfos[i];
                result += mVisitor.mSpaceManager.getSpace() + "static constexpr GVM::Core::RenderComponentHandle " + componentInfo.varName + " = " + std::to_string(i + 1) + mVisitor.EOS();
            }
            result += mVisitor.quitScope();
        }

        result += "#endif //" + mUGLExportFileMacro + "\n";

        return result;
    }
} // namespace UGLC::CodeGen::CPP
