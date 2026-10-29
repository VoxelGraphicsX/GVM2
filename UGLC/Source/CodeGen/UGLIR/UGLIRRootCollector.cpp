#include "UGLIRRootCollector.hpp"

#include "UGLIRNameUtils.hpp"
#include "UGLIRTypeSpelling.hpp"

#include <CodeGen/DiagnosticError.hpp>

namespace UGLC::CodeGen::UGLIR
{
    using namespace TypeSpelling;

    UGLIRRootCollector::UGLIRRootCollector(clang::ASTContext &context)
        : mContext(context)
        , mSourceManager(context.getSourceManager())
    {
    }

        std::vector<ShaderClassRoot> UGLIRRootCollector::collectRoots()
        {
            TraverseDecl(mContext.getTranslationUnitDecl());
            return std::move(mShaderRoots);
        }

        bool UGLIRRootCollector::VisitCXXRecordDecl(const clang::CXXRecordDecl *decl)
        {
            if (decl == nullptr || !decl->isCompleteDefinition() || !isUserAuthoredRecordDecl(mSourceManager, decl) || isUninstantiatedTemplatePattern(decl))
            {
                return true;
            }

            ShaderClassKind kind = ShaderClassKind::Compute;
            if (tryClassifyShaderClass(decl, kind))
            {
                addShaderRoot(decl, kind);
            }
            return true;
        }

        bool UGLIRRootCollector::VisitCallExpr(const clang::CallExpr *expression)
        {
            collectShaderWrapperType(expression->getType());
            return true;
        }

        bool UGLIRRootCollector::VisitVarDecl(const clang::VarDecl *decl)
        {
            if (decl != nullptr)
            {
                collectShaderWrapperType(decl->getType());
            }
            return true;
        }

        bool UGLIRRootCollector::VisitFieldDecl(const clang::FieldDecl *decl)
        {
            if (decl != nullptr)
            {
                collectShaderWrapperType(decl->getType());
            }
            return true;
        }

        void UGLIRRootCollector::collectShaderWrapperType(const clang::QualType &type)
        {
            if (type.isNull())
            {
                return;
            }
            const std::string wrapperName = canonicalRecordName(type);
            ShaderClassKind expectedKind = ShaderClassKind::Compute;
            if (wrapperName == "UGL::ComputeClass")
            {
                expectedKind = ShaderClassKind::Compute;
            }
            else if (wrapperName == "UGL::RenderClass")
            {
                expectedKind = ShaderClassKind::Render;
            }
            else
            {
                return;
            }
            const clang::QualType shaderType = getTemplateTypeArgument(type, 0);
            const clang::CXXRecordDecl *shaderRecordDecl = recordDefinition(shaderType.getNonReferenceType()->getAsCXXRecordDecl());
            if (shaderRecordDecl == nullptr)
            {
                return;
            }
            ShaderClassKind kind = ShaderClassKind::Compute;
            if (tryClassifyShaderClass(shaderRecordDecl, kind) &&
                (kind == expectedKind || (expectedKind == ShaderClassKind::Render && kind == ShaderClassKind::PixelLocal)))
            {
                addShaderRoot(shaderRecordDecl, kind);
            }
        }

        bool UGLIRRootCollector::tryClassifyShaderClass(const clang::CXXRecordDecl *decl, ShaderClassKind &kind) const
        {
            if (isDerivedFromUGLBase(mSourceManager, decl, UGLC::CodeGen::mUGLPixelLocalRenderClassBaseName))
            {
                kind = ShaderClassKind::PixelLocal;
                return true;
            }
            if (isDerivedFromUGLBase(mSourceManager, decl, UGLC::CodeGen::mUGLRenderClassBaseName))
            {
                kind = ShaderClassKind::Render;
                return true;
            }
            if (isDerivedFromUGLBase(mSourceManager, decl, UGLC::CodeGen::mUGLComputeClassBaseName))
            {
                kind = ShaderClassKind::Compute;
                return true;
            }
            return false;
        }

        void UGLIRRootCollector::addShaderRoot(const clang::CXXRecordDecl *recordDecl, ShaderClassKind kind)
        {
            recordDecl = recordDefinition(recordDecl);
            if (recordDecl == nullptr)
            {
                return;
            }
            const std::string symbolName = makeUGLIRRecordSymbolName(*recordDecl);
            if (mShaderRootNames.find(symbolName) != mShaderRootNames.end())
            {
                for (const auto &root : mShaderRoots)
                {
                    if (makeUGLIRRecordSymbolName(*root.recordDecl) == symbolName &&
                        root.recordDecl->getCanonicalDecl() != recordDecl->getCanonicalDecl())
                        throw UGLC::CodeGen::DiagnosticError(recordDecl != nullptr ? (recordDecl)->getLocation() : clang::SourceLocation(), clang::DiagnosticsEngine::Error, "UGLIR: distinct shader declarations have the same symbol: " + symbolName);
                }
                return;
            }
            mShaderRootNames.insert(symbolName);
            mShaderRoots.push_back(ShaderClassRoot{recordDecl, kind});
        }

        bool UGLIRRootCollector::isUninstantiatedTemplatePattern(const clang::CXXRecordDecl *recordDecl) const
        {
            if (recordDecl == nullptr)
            {
                return true;
            }
            if (recordDecl->isDependentContext())
            {
                return true;
            }
            return recordDecl->getDescribedClassTemplate() != nullptr;
        }

} // namespace UGLC::CodeGen::UGLIR
