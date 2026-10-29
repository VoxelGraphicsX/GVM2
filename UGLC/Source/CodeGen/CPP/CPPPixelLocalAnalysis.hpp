#pragma once

#include <CodeGen/PixelLocalFieldAnalysis.hpp>

#include "clang/AST/Type.h"

#include <cstdint>
#include <optional>
#include <string>

namespace clang
{
    class CXXRecordDecl;
    class Expr;
    class FieldDecl;
    class FunctionDecl;
    class VarDecl;
}

namespace UGLC::CodeGen
{
    class BaseASTVisitor;
}

namespace UGLC::CodeGen::CPP
{
    struct PixelLocalAttachmentAccessMask
    {
        std::uint64_t colorReadMask = 0u;
        std::uint64_t colorWriteMask = 0u;
        bool depthWrite = false;
    };

    class CPPPixelLocalAnalysis final
    {
    public:
        explicit CPPPixelLocalAnalysis(BaseASTVisitor &visitor);

        bool renderTargetHasPixelLocalAttachment(const clang::CXXRecordDecl *recordDecl) const;
        bool isPixelLocalAttachmentType(const clang::QualType &type) const;
        void validateAttachmentValueObjectVarOrThrow(const clang::VarDecl *decl) const;
        void validateInputAndReadUsage(const clang::CXXRecordDecl *decl, const clang::FunctionDecl *allowedPixelFunction) const;

        void accumulateReadAccess(const clang::FunctionDecl *shaderFunc, PixelLocalAttachmentAccessMask &access) const;
        void accumulateWriteAccess(
            const clang::CXXRecordDecl *renderTargetRecord,
            const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis,
            bool isPixelOnlyOperation,
            PixelLocalAttachmentAccessMask &access) const;
        void validateEntryAccessOrThrow(
            const clang::FunctionDecl *shaderFunc,
            const PixelLocalAttachmentAccessMask &access,
            const PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis) const;
        std::optional<PixelLocalAttachmentAccessMask> resolveRenderClassAccess(const clang::CXXRecordDecl *renderClassDecl) const;
        void validatePassExpressionOrThrow(const clang::Expr *expr) const;

        std::string resolveLoadOp(const clang::FieldDecl *fieldDecl) const;
        std::string resolveStoreOp(const clang::FieldDecl *fieldDecl) const;

    private:
        BaseASTVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::CPP
